#include "ugjy_model.h"
#include "ugjy_arena.h"
#include "ugjy_error.h"
#include "ugjy_onnx.h"
#include "ugjy_viseme.h"
#include "ugjy_prosody.h"
#include "ugjy_dsp.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

int ugjy_model_load(
    ugjy_model_t *model,
    const char *model_dir,
    int num_threads
) {
    if (!model || !model_dir) return UGJY_ERR_INVALID_ARG;
    memset(model, 0, sizeof(*model));

    char path[1024];

    // 1. embedder_model.onnx
    snprintf(path, sizeof(path), "%s/embedder_model.onnx", model_dir);
    int ret = ugjy_onnx_session_init(&model->embedder, path, num_threads);
    if (ret != UGJY_OK) return ret;

    // 2. variance_model.onnx
    snprintf(path, sizeof(path), "%s/variance_model.onnx", model_dir);
    ret = ugjy_onnx_session_init(&model->variance, path, num_threads);
    if (ret != UGJY_OK) {
        ugjy_onnx_destroy(&model->embedder);
        return ret;
    }

    // 3. decoder_model.onnx
    snprintf(path, sizeof(path), "%s/decoder_model.onnx", model_dir);
    ret = ugjy_onnx_session_init(&model->decoder, path, num_threads);
    if (ret != UGJY_OK) {
        ugjy_onnx_destroy(&model->embedder);
        ugjy_onnx_destroy(&model->variance);
        return ret;
    }

    model->sample_rate = 48000;
    model->default_speaker = 0; // スロット 0: ノーマル (つくよみ×アミ喜び)

    return UGJY_OK;
}

void ugjy_model_destroy(ugjy_model_t *model) {
    if (!model) return;
    ugjy_onnx_destroy(&model->embedder);
    ugjy_onnx_destroy(&model->variance);
    ugjy_onnx_destroy(&model->decoder);
}

// ============================================================================
// 純粋ニューラル推論ステップ群 (Embedder / Variance / LengthRegulator / Decoder)
// ============================================================================

// 1. Embedder推論: 音素ID列から音素埋め込み特徴量を算出
static int step_embedder(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const int64_t *tokens,
    size_t num_tokens,
    OrtValue **out_emb_tensor,
    OrtValue **out_phonemes_tensor,
    float **out_features
) {
    int64_t *phoneme_buf = (int64_t *)ugjy_arena_alloc(arena, num_tokens * sizeof(int64_t));
    if (!phoneme_buf) return UGJY_ERR_OUT_OF_MEMORY;
    memcpy(phoneme_buf, tokens, num_tokens * sizeof(int64_t));

    int64_t shape_L[2] = {1, (int64_t)num_tokens};
    OrtValue *t_phonemes = ugjy_onnx_create_tensor(
        &model->embedder, phoneme_buf, num_tokens * sizeof(int64_t),
        shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
    );
    if (!t_phonemes) return UGJY_ERR_ONNX_TENSOR;

    const char *emb_in_names[] = {"phonemes"};
    const OrtValue *emb_in_tensors[] = {t_phonemes};
    const char *emb_out_names[] = {"feature_embedded"};
    OrtValue *emb_out_tensors[1] = {NULL};

    if (ugjy_onnx_run(&model->embedder, emb_in_names, emb_in_tensors, 1,
                      emb_out_names, 1, emb_out_tensors) != UGJY_OK || !emb_out_tensors[0]) {
        model->embedder.api->ReleaseValue(t_phonemes);
        return UGJY_ERR_MODEL_EMBEDDER;
    }

    float *feature_embedded = NULL;
    OrtStatus *st = model->embedder.api->GetTensorMutableData(emb_out_tensors[0], (void **)&feature_embedded);
    if (st != NULL) {
        model->embedder.api->ReleaseStatus(st);
        model->embedder.api->ReleaseValue(emb_out_tensors[0]);
        model->embedder.api->ReleaseValue(t_phonemes);
        return UGJY_ERR_ONNX_DATA;
    }

    *out_emb_tensor = emb_out_tensors[0];
    *out_phonemes_tensor = t_phonemes;
    *out_features = feature_embedded;
    return UGJY_OK;
}

// 2. Variance推論: ピッチ・音素長予測
static int step_variance(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const int64_t *tokens,
    size_t num_tokens,
    const int64_t *prosody_features,
    int64_t speaker_id,
    OrtValue **out_pitches_tensor,
    OrtValue **out_durations_tensor,
    float **out_pitches,
    float **out_durations
) {
    int64_t *phoneme_buf = (int64_t *)ugjy_arena_alloc(arena, num_tokens * sizeof(int64_t));
    int64_t *accent_buf  = (int64_t *)ugjy_arena_alloc(arena, num_tokens * sizeof(int64_t));
    int64_t *spk_buf     = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!phoneme_buf || !accent_buf || !spk_buf) return UGJY_ERR_OUT_OF_MEMORY;

    memcpy(phoneme_buf, tokens, num_tokens * sizeof(int64_t));
    if (prosody_features) {
        memcpy(accent_buf, prosody_features, num_tokens * sizeof(int64_t));
    } else {
        for (size_t i = 0; i < num_tokens; i++) accent_buf[i] = 4; // デフォルト: _ (変化なし)
    }
    *spk_buf = speaker_id;

    int64_t shape_L[2] = {1, (int64_t)num_tokens};
    int64_t shape_1[1] = {1};

    OrtValue *t_var_phonemes = ugjy_onnx_create_tensor(&model->variance, phoneme_buf, num_tokens * sizeof(int64_t), shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    OrtValue *t_var_accents  = ugjy_onnx_create_tensor(&model->variance, accent_buf, num_tokens * sizeof(int64_t), shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    OrtValue *t_var_speaker  = ugjy_onnx_create_tensor(&model->variance, spk_buf, sizeof(int64_t), shape_1, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    if (!t_var_phonemes || !t_var_accents || !t_var_speaker) {
        if (t_var_phonemes) model->variance.api->ReleaseValue(t_var_phonemes);
        if (t_var_accents)  model->variance.api->ReleaseValue(t_var_accents);
        if (t_var_speaker)  model->variance.api->ReleaseValue(t_var_speaker);
        return UGJY_ERR_ONNX_TENSOR;
    }

    const char *var_in_names[] = {"phonemes", "accents", "speakers"};
    const OrtValue *var_in_tensors[] = {t_var_phonemes, t_var_accents, t_var_speaker};
    const char *var_out_names[] = {"pitches", "durations"};
    OrtValue *var_out_tensors[2] = {NULL, NULL};

    int var_ret = ugjy_onnx_run(&model->variance, var_in_names, var_in_tensors, 3, var_out_names, 2, var_out_tensors);
    model->variance.api->ReleaseValue(t_var_phonemes);
    model->variance.api->ReleaseValue(t_var_accents);
    model->variance.api->ReleaseValue(t_var_speaker);

    if (var_ret != UGJY_OK || !var_out_tensors[0] || !var_out_tensors[1]) {
        if (var_out_tensors[0]) model->variance.api->ReleaseValue(var_out_tensors[0]);
        if (var_out_tensors[1]) model->variance.api->ReleaseValue(var_out_tensors[1]);
        return UGJY_ERR_MODEL_VARIANCE;
    }

    float *pitches = NULL, *durations = NULL;
    OrtStatus *st1 = model->variance.api->GetTensorMutableData(var_out_tensors[0], (void **)&pitches);
    if (st1 != NULL) {
        model->variance.api->ReleaseStatus(st1);
        model->variance.api->ReleaseValue(var_out_tensors[0]);
        model->variance.api->ReleaseValue(var_out_tensors[1]);
        return UGJY_ERR_ONNX_DATA;
    }

    OrtStatus *st2 = model->variance.api->GetTensorMutableData(var_out_tensors[1], (void **)&durations);
    if (st2 != NULL) {
        model->variance.api->ReleaseStatus(st2);
        model->variance.api->ReleaseValue(var_out_tensors[0]);
        model->variance.api->ReleaseValue(var_out_tensors[1]);
        return UGJY_ERR_ONNX_DATA;
    }

    *out_pitches_tensor = var_out_tensors[0];
    *out_durations_tensor = var_out_tensors[1];
    *out_pitches = pitches;
    *out_durations = durations;
    return UGJY_OK;
}

// 3. Length Regulator: フレーム数配列に従った特徴量・ピッチの純粋な時間軸リピート展開
static int step_length_regulator(
    ugjy_arena_t *arena,
    size_t num_tokens,
    const float *feature_embedded,
    const float *pitches,
    const int *frame_counts,
    size_t total_frames,
    float **out_lr_features,
    float **out_lr_pitches
) {
    float *lr_features = (float *)ugjy_arena_alloc(arena, total_frames * 192 * sizeof(float));
    float *lr_pitches  = (float *)ugjy_arena_alloc(arena, total_frames * sizeof(float));
    if (!lr_features || !lr_pitches) return UGJY_ERR_OUT_OF_MEMORY;

    memset(lr_features, 0, total_frames * 192 * sizeof(float));
    memset(lr_pitches, 0, total_frames * sizeof(float));

    size_t curr_frame = 0;
    for (size_t i = 0; i < num_tokens; i++) {
        int cnt = frame_counts[i];
        const float *src_feat = feature_embedded + (i * 192);
        float p = pitches[i];

        for (int f = 0; f < cnt; f++) {
            memcpy(lr_features + (curr_frame * 192), src_feat, 192 * sizeof(float));
            lr_pitches[curr_frame] = p;
            curr_frame++;
        }
    }

    *out_lr_features = lr_features;
    *out_lr_pitches = lr_pitches;
    return UGJY_OK;
}

// 4. Decoder推論: HiFi-GAN による 48kHz 波形生成
static int step_decoder(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const float *lr_features,
    const float *lr_pitches,
    int64_t speaker_id,
    size_t total_frames,
    OrtValue **out_wav_tensor,
    float **out_wav_data,
    size_t *out_wav_samples
) {
    int64_t *spk_buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!spk_buf) return UGJY_ERR_OUT_OF_MEMORY;
    *spk_buf = speaker_id;

    int64_t shape_dec_feat[3] = {1, (int64_t)total_frames, 192};
    int64_t shape_dec_pitch[2] = {1, (int64_t)total_frames};
    int64_t shape_1[1] = {1};

    OrtValue *t_dec_features = ugjy_onnx_create_tensor(&model->decoder, (void *)lr_features, total_frames * 192 * sizeof(float), shape_dec_feat, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    OrtValue *t_dec_pitches  = ugjy_onnx_create_tensor(&model->decoder, (void *)lr_pitches, total_frames * sizeof(float), shape_dec_pitch, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    OrtValue *t_dec_speaker  = ugjy_onnx_create_tensor(&model->decoder, spk_buf, sizeof(int64_t), shape_1, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);

    if (!t_dec_features || !t_dec_pitches || !t_dec_speaker) {
        if (t_dec_features) model->decoder.api->ReleaseValue(t_dec_features);
        if (t_dec_pitches)  model->decoder.api->ReleaseValue(t_dec_pitches);
        if (t_dec_speaker)  model->decoder.api->ReleaseValue(t_dec_speaker);
        return UGJY_ERR_ONNX_TENSOR;
    }

    const char *dec_in_names[] = {"length_regulated_tensor", "pitches", "speakers"};
    const OrtValue *dec_in_tensors[] = {t_dec_features, t_dec_pitches, t_dec_speaker};
    const char *dec_out_names[] = {"wav"};
    OrtValue *dec_out_tensors[1] = {NULL};

    int dec_ret = ugjy_onnx_run(&model->decoder, dec_in_names, dec_in_tensors, 3, dec_out_names, 1, dec_out_tensors);
    model->decoder.api->ReleaseValue(t_dec_features);
    model->decoder.api->ReleaseValue(t_dec_pitches);
    model->decoder.api->ReleaseValue(t_dec_speaker);

    if (dec_ret != UGJY_OK || !dec_out_tensors[0]) {
        if (dec_out_tensors[0]) model->decoder.api->ReleaseValue(dec_out_tensors[0]);
        return UGJY_ERR_MODEL_DECODER;
    }

    float *wav_data = NULL;
    OrtStatus *st = model->decoder.api->GetTensorMutableData(dec_out_tensors[0], (void **)&wav_data);
    if (st != NULL) {
        model->decoder.api->ReleaseStatus(st);
        model->decoder.api->ReleaseValue(dec_out_tensors[0]);
        return UGJY_ERR_ONNX_DATA;
    }

    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    st = model->decoder.api->GetTensorTypeAndShape(dec_out_tensors[0], &shape_info);
    if (st != NULL) {
        model->decoder.api->ReleaseStatus(st);
        model->decoder.api->ReleaseValue(dec_out_tensors[0]);
        return UGJY_ERR_ONNX_DATA;
    }

    size_t num_wav_samples = 0;
    st = model->decoder.api->GetTensorShapeElementCount(shape_info, &num_wav_samples);
    model->decoder.api->ReleaseTensorTypeAndShapeInfo(shape_info);
    if (st != NULL) {
        model->decoder.api->ReleaseStatus(st);
        model->decoder.api->ReleaseValue(dec_out_tensors[0]);
        return UGJY_ERR_ONNX_DATA;
    }

    *out_wav_tensor = dec_out_tensors[0];
    *out_wav_data = wav_data;
    *out_wav_samples = num_wav_samples;
    return UGJY_OK;
}

// ============================================================================
// オーケストレーター (ugjy_model_infer)
// ============================================================================

int ugjy_model_infer(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const ugjy_model_request_t *req,
    float *out_pcm,
    size_t max_samples,
    size_t *out_samples
) {
    if (!model || !arena || !req || !out_pcm || !out_samples) return UGJY_ERR_INVALID_ARG;
    if (req->num_tokens == 0) return UGJY_ERR_MODEL_NO_TOKENS;

    *out_samples = 0;
    size_t arena_marker = ugjy_arena_mark(arena);
    int ret = UGJY_OK;

    int64_t speaker_id = (req->speaker_id > 0) ? (int64_t)req->speaker_id : (int64_t)model->default_speaker;
    if (req->style == UGJY_STYLE_WHISPER) {
        speaker_id = 1; // ベイクされたささやき (ASMR) 専用スロット
    }
    float speed = (req->speed > 0.0f) ? req->speed : 1.0f;

    // Step 1: Embedder 推論 (音素埋め込み)
    OrtValue *t_emb_out = NULL, *t_phonemes = NULL;
    float *feature_embedded = NULL;
    ret = step_embedder(model, arena, req->tokens, req->num_tokens, &t_emb_out, &t_phonemes, &feature_embedded);
    if (ret != UGJY_OK) goto cleanup;

    // Step 2: Variance 推論 (F0・Duration予測)
    OrtValue *t_var_pitches = NULL, *t_var_durations = NULL;
    float *pitches = NULL, *durations = NULL;
    ret = step_variance(model, arena, req->tokens, req->num_tokens, req->prosody_features, speaker_id,
                        &t_var_pitches, &t_var_durations, &pitches, &durations);
    if (ret != UGJY_OK) {
        model->embedder.api->ReleaseValue(t_emb_out);
        model->embedder.api->ReleaseValue(t_phonemes);
        goto cleanup;
    }

    // Step 3: 音声規則適用 (無声音のピッチ0化 & ポーズ補正・フレーム数計算)
    ugjy_prosody_zero_unvoiced(req->tokens, req->num_tokens, pitches);

    int *frame_counts = (int *)ugjy_arena_alloc(arena, req->num_tokens * sizeof(int));
    if (!frame_counts) {
        ret = UGJY_ERR_OUT_OF_MEMORY;
        model->embedder.api->ReleaseValue(t_emb_out);
        model->embedder.api->ReleaseValue(t_phonemes);
        model->variance.api->ReleaseValue(t_var_pitches);
        model->variance.api->ReleaseValue(t_var_durations);
        goto cleanup;
    }
    size_t total_frames = ugjy_prosody_compute_frames(req->tokens, durations, req->num_tokens, speed, frame_counts);

    // Step 4: Length Regulator (決定されたフレーム数に従って特徴量を時間軸展開)
    float *lr_features = NULL, *lr_pitches = NULL;
    ret = step_length_regulator(arena, req->num_tokens, feature_embedded, pitches,
                                frame_counts, total_frames, &lr_features, &lr_pitches);

    // Embedder と Variance のORTテンソル解放
    model->embedder.api->ReleaseValue(t_emb_out);
    model->embedder.api->ReleaseValue(t_phonemes);
    model->variance.api->ReleaseValue(t_var_pitches);
    model->variance.api->ReleaseValue(t_var_durations);

    if (ret != UGJY_OK) goto cleanup;

    // Step 5: 口パク Viseme 生成 (Live2D パラメータ算出)
    ugjy_viseme_generate(req->tokens, frame_counts, req->num_tokens, model->visemes, 2048, &model->num_visemes);

    // Step 6: 韻律・ピッチ加工 (平滑化・抑揚・1/f揺らぎ・ささやき)
    float *temp_pitches = (float *)ugjy_arena_alloc(arena, total_frames * sizeof(float));
    ret = ugjy_prosody_process(lr_pitches, total_frames, temp_pitches, req->emotion, req->style);
    if (ret != UGJY_OK) goto cleanup;

    // 囁きモード: prosody加工完了後に無声区間（0.0f）のみ極小ディザを注入
    // prosody_process 通過後なので mean_log_f0 計算には一切影響しない
    if (req->style == UGJY_STYLE_WHISPER) {
        uint32_t rng_uv = 0xDEADBEEFu;
        for (size_t f = 0; f < total_frames; f++) {
            if (lr_pitches[f] == 0.0f) {
                rng_uv = rng_uv * 1664525u + 1013904223u;
                // 極小値（HiFi-GANが無声として認識する範囲内、励起ノイズを抑えるため約1/4に低減）
                lr_pitches[f] = 0.0008f + ((float)(rng_uv & 0xFF) / 255.0f) * 0.0005f;
            }
        }
    }

    // ピッチ遷移スムージング: voiced↔unvoiced 境界の急峻な崖を3フレームかけて線形補間
    // HiFi-GAN転置畳み込みの崖によるリンギング（カサカサ・チリチリ）を低減
    if (req->style == UGJY_STYLE_WHISPER) {
        const size_t ramp = 3; // 前後3フレーム ≈ 32ms
        // Forward pass: voiced→unvoiced フェードアウト
        for (size_t f = 0; f + 1 < total_frames; f++) {
            if (lr_pitches[f] > 0.01f && lr_pitches[f + 1] <= 0.01f) {
                float base = lr_pitches[f];
                for (size_t r = 1; r <= ramp && f + r < total_frames; r++) {
                    float t = (float)r / (float)(ramp + 1);
                    lr_pitches[f + r] = base * (1.0f - t);
                }
            }
        }
        // Backward pass: unvoiced→voiced フェードイン
        for (size_t f = total_frames; f > 0; f--) {
            size_t fi = f - 1;
            if (lr_pitches[fi] > 0.01f && fi > 0 && lr_pitches[fi - 1] <= 0.01f) {
                float base = lr_pitches[fi];
                for (size_t r = 1; r <= ramp && fi >= r; r++) {
                    float t = (float)r / (float)(ramp + 1);
                    lr_pitches[fi - r] = base * (1.0f - t);
                }
            }
        }
    }



    // Step 7: Decoder 推論 (HiFi-GAN 波形生成)
    OrtValue *t_wav = NULL;
    float *wav_data = NULL;
    size_t num_wav_samples = 0;
    ret = step_decoder(model, arena, lr_features, lr_pitches, speaker_id, total_frames,
                       &t_wav, &wav_data, &num_wav_samples);
    if (ret != UGJY_OK) goto cleanup;

    // Step 8: 音響DSP後処理 (HPF, LPF, 正規化, フェード)
    size_t copy_samples = (num_wav_samples > max_samples) ? max_samples : num_wav_samples;
    memcpy(out_pcm, wav_data, copy_samples * sizeof(float));
    model->decoder.api->ReleaseValue(t_wav);

    ugjy_dsp_postprocess(out_pcm, copy_samples, model->sample_rate, req->style, &model->dsp_state, arena);
    *out_samples = copy_samples;

cleanup:
    ugjy_arena_restore(arena, arena_marker);
    return ret;
}

// SVS歌唱専用推論実行 (varianceモデルをスキップし、SVSピッチ・フレーム長でHiFi-GANを駆動)
int ugjy_model_infer_svs(
    ugjy_model_t            *model,
    ugjy_arena_t            *arena,
    const ugjy_svs_phrase_t *phrase,
    uint32_t                 speaker_id,
    float                   *out_pcm,
    size_t                   max_samples,
    size_t                  *out_samples
) {
    if (!model || !arena || !phrase || !out_pcm || !out_samples) return UGJY_ERR_INVALID_ARG;
    if (phrase->num_tokens == 0 || phrase->total_frames == 0) return UGJY_ERR_MODEL_NO_TOKENS;

    *out_samples = 0;
    size_t arena_marker = ugjy_arena_mark(arena);
    int ret = UGJY_OK;

    int64_t spk_id = (speaker_id > 0) ? (int64_t)speaker_id : (int64_t)model->default_speaker;

    // Step 1: Embedder推論 (音素埋め込み)
    OrtValue *t_emb_out = NULL, *t_phonemes = NULL;
    float *feature_embedded = NULL;
    ret = step_embedder(model, arena, phrase->tokens, phrase->num_tokens, &t_emb_out, &t_phonemes, &feature_embedded);
    if (ret != UGJY_OK) goto cleanup;

    // Step 2: Length Regulator (SVSのframe_countsに従って特徴量を展開)
    float *lr_features = (float *)ugjy_arena_alloc(arena, phrase->total_frames * 192 * sizeof(float));
    if (!lr_features) {
        ret = UGJY_ERR_OUT_OF_MEMORY;
        model->embedder.api->ReleaseValue(t_emb_out);
        model->embedder.api->ReleaseValue(t_phonemes);
        goto cleanup;
    }

    size_t curr_frame = 0;
    for (size_t i = 0; i < phrase->num_tokens; i++) {
        int cnt = phrase->frame_counts[i];
        const float *src_feat = feature_embedded + (i * 192);
        for (int f = 0; f < cnt; f++) {
            memcpy(lr_features + (curr_frame * 192), src_feat, 192 * sizeof(float));
            curr_frame++;
        }
    }
    model->embedder.api->ReleaseValue(t_emb_out);
    model->embedder.api->ReleaseValue(t_phonemes);

    // 特徴量の時間軸1段スムージング (3タップ対称FIR) ＆ マイルドクランプ (±4.0f)
    // 音素内部は100%完全維持したまま、境界の急峻なエッジだけをスッと溶かし、倍音裏の微細粒子感を消滅！
    float *feat_buf = (float *)ugjy_arena_alloc(arena, phrase->total_frames * 192 * sizeof(float));
    if (feat_buf) {
        memcpy(feat_buf, lr_features, phrase->total_frames * 192 * sizeof(float));
        for (size_t f = 0; f < phrase->total_frames; f++) {
            size_t prev_f = (f > 0) ? (f - 1) : f;
            size_t next_f = (f + 1 < phrase->total_frames) ? (f + 1) : f;
            const float *p_prev = feat_buf + (prev_f * 192);
            const float *p_curr = feat_buf + (f * 192);
            const float *p_next = feat_buf + (next_f * 192);
            float *p_dst = lr_features + (f * 192);

            for (size_t d = 0; d < 192; d++) {
                float val = 0.25f * p_prev[d] + 0.50f * p_curr[d] + 0.25f * p_next[d];
                if (val > 4.0f) val = 4.0f;
                else if (val < -4.0f) val = -4.0f;
                p_dst[d] = val;
            }
        }
    }

    // Step 3: 口パク Viseme 生成
    ugjy_viseme_generate(phrase->tokens, phrase->frame_counts, phrase->num_tokens, model->visemes, 2048, &model->num_visemes);



    // Step 4: Decoder推論 (VUV分離＆安全クランプされたSVSピッチ配列を直接投入)
    OrtValue *t_wav = NULL;
    float *wav_data = NULL;
    size_t num_wav_samples = 0;
    ret = step_decoder(model, arena, lr_features, phrase->pitches, spk_id, phrase->total_frames,
                       &t_wav, &wav_data, &num_wav_samples);
    if (ret != UGJY_OK) goto cleanup;

    // Step 5: 音響後処理 (歌唱はクリアなNORMALスタイルDSPを適用)
    size_t copy_samples = (num_wav_samples > max_samples) ? max_samples : num_wav_samples;
    memcpy(out_pcm, wav_data, copy_samples * sizeof(float));
    model->decoder.api->ReleaseValue(t_wav);

    ugjy_dsp_postprocess(out_pcm, copy_samples, model->sample_rate, UGJY_STYLE_SINGING, &model->dsp_state, arena);

    // フレーズ末尾のゼロ軟着陸コサインフェード (10ms、ブツッ音を完全排除)
    const size_t fade_samples = (copy_samples > 480) ? 480 : copy_samples;
    for (size_t k = 0; k < fade_samples; k++) {
        size_t idx = copy_samples - fade_samples + k;
        float r = 0.5f * (1.0f + cosf((float)k / (float)fade_samples * 3.14159265f));
        out_pcm[idx] *= r;
    }
    *out_samples = copy_samples;

cleanup:
    ugjy_arena_restore(arena, arena_marker);
    return ret;
}
