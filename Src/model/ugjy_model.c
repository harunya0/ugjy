#include "ugjy_model.h"
#include "ugjy_arena.h"
#include "ugjy_error.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define ORT_CHECK_RET(api, expr, err_code) do { \
    OrtStatus* _status = (expr); \
    if (_status != NULL) { \
        (api)->ReleaseStatus(_status); \
        return (err_code); \
    } \
} while (0)

static inline void get_phoneme_viseme(int64_t token, float *open_y, float *form) {
    switch (token) {
        case 1:  // A
        case 7:  // a
            *open_y = 1.0f; *form = 0.0f; break;
        case 3:  // I
        case 21: // i
            *open_y = 0.3f; *form = 1.0f; break;
        case 6:  // U
        case 40: // u
            *open_y = 0.25f; *form = -1.0f; break;
        case 2:  // E
        case 14: // e
            *open_y = 0.6f; *form = 0.5f; break;
        case 5:  // O
        case 30: // o
            *open_y = 0.8f; *form = -0.6f; break;
        case 4:  // N (ん)
            *open_y = 0.1f; *form = 0.0f; break;
        case 0:  // pau (ポーズ)
        case 11: // cl (っ)
            *open_y = 0.0f; *form = 0.0f; break;
        default: // その他の子音
            *open_y = 0.15f; *form = 0.0f; break;
    }
}

// 無声母音・ポーズ・促音判定 (SHAREVOX音素インデックス: 0:pau, 1:A, 2:E, 3:I, 5:O, 6:U, 11:cl)
static inline bool is_unvoiced_phoneme(int64_t ph) {
    return ph == 0 || ph == 1 || ph == 2 || ph == 3 || ph == 5 || ph == 6 || ph == 11;
}

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
    model->default_speaker = 4; // つくよみちゃん「おしとやかv3」

    return UGJY_OK;
}

void ugjy_model_destroy(ugjy_model_t *model) {
    if (!model) return;
    ugjy_onnx_destroy(&model->embedder);
    ugjy_onnx_destroy(&model->variance);
    ugjy_onnx_destroy(&model->decoder);
}

// ============================================================================
// 単一責任ヘルパー関数群 (6ステップ パイプライン)
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
    ORT_CHECK_RET(model->embedder.api,
                  model->embedder.api->GetTensorMutableData(emb_out_tensors[0], (void **)&feature_embedded),
                  UGJY_ERR_ONNX_DATA);

    *out_emb_tensor = emb_out_tensors[0];
    *out_phonemes_tensor = t_phonemes;
    *out_features = feature_embedded;
    return UGJY_OK;
}

// 2. Variance推論: ピッチ・音素長予測 & 無音ゼロクリア
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
    ORT_CHECK_RET(model->variance.api, model->variance.api->GetTensorMutableData(var_out_tensors[0], (void **)&pitches), UGJY_ERR_ONNX_DATA);
    ORT_CHECK_RET(model->variance.api, model->variance.api->GetTensorMutableData(var_out_tensors[1], (void **)&durations), UGJY_ERR_ONNX_DATA);

    // 無声音のピッチをゼロクリア
    for (size_t i = 0; i < num_tokens; i++) {
        if (is_unvoiced_phoneme(tokens[i])) {
            pitches[i] = 0.0f;
        }
    }

    *out_pitches_tensor = var_out_tensors[0];
    *out_durations_tensor = var_out_tensors[1];
    *out_pitches = pitches;
    *out_durations = durations;
    return UGJY_OK;
}

// 3. Length Regulator: フレーム伸張 & Viseme口形計算
static int step_length_regulator(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const int64_t *tokens,
    size_t num_tokens,
    const float *feature_embedded,
    const float *pitches,
    const float *durations,
    float speed,
    size_t *out_total_frames,
    float **out_lr_features,
    float **out_lr_pitches
) {
    const float regulation_base = 93.75f; // 48000Hz / 512hop = 93.75
    size_t total_frames = 4;
    int *frame_counts = (int *)ugjy_arena_alloc(arena, num_tokens * sizeof(int));
    if (!frame_counts) return UGJY_ERR_OUT_OF_MEMORY;

    for (size_t i = 0; i < num_tokens; i++) {
        float dur_sec = durations[i] / speed;
        // 句読点（、や！、。）のポーズを適切に確保
        if (tokens[i] == 0) {
            if (i > 0 && i + 1 < num_tokens) {
                if (dur_sec < 0.22f) dur_sec = 0.22f; // 中間の読点ポーズ
            } else if (i == 0) {
                if (dur_sec < 0.08f) dur_sec = 0.08f; // 文頭の微小ポーズ
            } else {
                if (dur_sec < 0.15f) dur_sec = 0.18f; // 文末の余韻ポーズ
            }
        }
        int frames = (int)roundf(dur_sec * regulation_base);
        if (frames < 1 && tokens[i] != 0) {
            frames = 1; // pause 以外は最低1フレーム保証
        }
        if (frames < 0) frames = 0;
        frame_counts[i] = frames;
        total_frames += frames;
    }

    if (total_frames == 0) total_frames = 1;

    float *lr_features = (float *)ugjy_arena_alloc(arena, total_frames * 192 * sizeof(float));
    float *lr_pitches  = (float *)ugjy_arena_alloc(arena, total_frames * sizeof(float));
    if (!lr_features || !lr_pitches) return UGJY_ERR_OUT_OF_MEMORY;

    size_t curr_frame = 0;
    float smooth_open = 0.0f;
    float smooth_form = 0.0f;
    for (size_t i = 0; i < num_tokens; i++) {
        int cnt = frame_counts[i];
        const float *src_feat = feature_embedded + (i * 192);
        float p = pitches[i];
        float target_open = 0.0f, target_form = 0.0f;
        get_phoneme_viseme(tokens[i], &target_open, &target_form);

        for (int f = 0; f < cnt; f++) {
            memcpy(lr_features + (curr_frame * 192), src_feat, 192 * sizeof(float));
            lr_pitches[curr_frame] = p;

            // 指数移動平均で滑らかに遷移（Live2Dのカクつき防止）
            smooth_open += 0.35f * (target_open - smooth_open);
            smooth_form += 0.35f * (target_form - smooth_form);
            if (curr_frame < 2048) {
                model->visemes[curr_frame].mouth_open = smooth_open;
                model->visemes[curr_frame].mouth_form = smooth_form;
            }
            curr_frame++;
        }
    }
    model->num_visemes = (curr_frame < 2048) ? curr_frame : 2048;

    *out_total_frames = total_frames;
    *out_lr_features = lr_features;
    *out_lr_pitches = lr_pitches;
    return UGJY_OK;
}

// 4. Pitch Dynamics: 声帯平滑化・感情抑揚ブースト・揺らぎ・スタイル(Whisper)制御
static int step_pitch_dynamics(
    ugjy_arena_t *arena,
    float *lr_pitches,
    size_t total_frames,
    uint8_t emotion,
    uint8_t style
) {
    // スタイルがささやき (Whisper/ASMR) の場合: 有声帯振動を全停止して完全息音化
    if (style == UGJY_STYLE_WHISPER) {
        for (size_t f = 0; f < total_frames; f++) {
            lr_pitches[f] = 0.0f;
        }
        return UGJY_OK;
    }

    // 1. 声帯の慣性平滑化（カクつき・詰まり音解消）
    float *temp_pitches = (float *)ugjy_arena_alloc(arena, total_frames * sizeof(float));
    if (temp_pitches) {
        for (int pass = 0; pass < 2; pass++) {
            const float *src = (pass == 0) ? lr_pitches : temp_pitches;
            float *dst = (pass == 0) ? temp_pitches : lr_pitches;
            for (size_t f = 0; f < total_frames; f++) {
                if (src[f] <= 0.0f) {
                    dst[f] = 0.0f;
                    continue;
                }
                float prev = (f > 0 && src[f - 1] > 0.0f) ? src[f - 1] : src[f];
                float next = (f + 1 < total_frames && src[f + 1] > 0.0f) ? src[f + 1] : src[f];
                dst[f] = 0.25f * prev + 0.50f * src[f] + 0.25f * next;
            }
        }
    }

    // 2. 対数F0マイクロダイナミクス（抑揚ブースト ＆ 1/f揺らぎ）
    float pitch_shift      = -0.08f;
    float intonation_scale = 1.05f;
    float flutter_depth    = 0.008f;

    switch (emotion) {
        case 1: // UGJY_MOOD_HAPPY: 嬉しい・上機嫌
            pitch_shift = -0.06f;
            intonation_scale = 1.10f;
            break;
        case 2: // UGJY_MOOD_ANGRY: 怒り・不機嫌
            pitch_shift = -0.10f;
            intonation_scale = 1.10f;
            break;
        case 3: // UGJY_MOOD_SAD: 悲しい・落ち込み
            pitch_shift = -0.10f;
            intonation_scale = 0.85f;
            break;
        case 4: // UGJY_MOOD_RELAXED: まったり
            pitch_shift = -0.09f;
            intonation_scale = 0.98f;
            break;
        default: // UGJY_MOOD_NORMAL
            break;
    }

    float f0_sum = 0.0f;
    size_t voiced_count = 0;
    for (size_t f = 0; f < total_frames; f++) {
        if (lr_pitches[f] > 0.0f) {
            f0_sum += lr_pitches[f];
            voiced_count++;
        }
    }

    if (voiced_count > 0) {
        float mean_log_f0 = f0_sum / (float)voiced_count;
        uint32_t rng = 123456789;
        float brownian = 0.0f;
        for (size_t f = 0; f < total_frames; f++) {
            rng = rng * 1664525u + 1013904223u;
            float white = ((float)(rng & 0xFFFF) / 32768.0f) - 1.0f;
            brownian = 0.85f * brownian + 0.15f * white;
            if (lr_pitches[f] > 0.0f) {
                float p = mean_log_f0 + (lr_pitches[f] - mean_log_f0) * intonation_scale;
                p += pitch_shift;
                p += brownian * flutter_depth;
                lr_pitches[f] = p;
            }
        }
    }

    return UGJY_OK;
}

// 5. Decoder推論: HiFi-GAN による 48kHz 波形生成
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
    ORT_CHECK_RET(model->decoder.api, model->decoder.api->GetTensorMutableData(dec_out_tensors[0], (void **)&wav_data), UGJY_ERR_ONNX_DATA);

    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    ORT_CHECK_RET(model->decoder.api, model->decoder.api->GetTensorTypeAndShape(dec_out_tensors[0], &shape_info), UGJY_ERR_ONNX_DATA);

    size_t num_wav_samples = 0;
    ORT_CHECK_RET(model->decoder.api, model->decoder.api->GetTensorShapeElementCount(shape_info, &num_wav_samples), UGJY_ERR_ONNX_DATA);
    model->decoder.api->ReleaseTensorTypeAndShapeInfo(shape_info);

    *out_wav_tensor = dec_out_tensors[0];
    *out_wav_data = wav_data;
    *out_wav_samples = num_wav_samples;
    return UGJY_OK;
}

// 6. DSP 後処理: HPF(50Hz), LPF(12kHz), ピーク正規化(0.95), コサインフェードアウト
static void step_dsp_postprocess(
    const float *wav_data,
    size_t num_wav_samples,
    float *out_pcm,
    size_t max_samples,
    size_t *out_samples
) {
    size_t copy_samples = (num_wav_samples > max_samples) ? max_samples : num_wav_samples;
    if (!wav_data || copy_samples == 0) {
        *out_samples = 0;
        return;
    }

    // 1. バッファへコピー
    for (size_t i = 0; i < copy_samples; i++) {
        out_pcm[i] = wav_data[i];
    }

    // 2. 超低音カット (50Hz 1次HPF: DCオフセット・ボコつき除去)
    const float alpha_hp = 0.9935f;
    float prev_x = out_pcm[0];
    float prev_y = out_pcm[0];
    for (size_t i = 0; i < copy_samples; i++) {
        float x = out_pcm[i];
        float y = alpha_hp * (prev_y + x - prev_x);
        prev_x = x;
        prev_y = y;
        out_pcm[i] = y;
    }

    // 3. 超高音カット (12kHz 2次バターワースLPF: チリチリ・がびがび高周波ノイズ除去)
    const float b0 = 0.292893f, b1 = 0.585786f, b2 = 0.292893f;
    const float a2 = 0.171573f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
    for (size_t i = 0; i < copy_samples; i++) {
        float x0 = out_pcm[i];
        float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a2 * y2;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        out_pcm[i] = y0;
    }

    // 4. ピーク正規化 (0.95f スケール)
    float max_abs = 0.001f;
    for (size_t i = 0; i < copy_samples; i++) {
        float a = fabsf(out_pcm[i]);
        if (a > max_abs) max_abs = a;
    }
    float scale = (max_abs > 0.95f) ? (0.95f / max_abs) : 1.0f;
    for (size_t i = 0; i < copy_samples; i++) {
        out_pcm[i] *= scale;
    }

    // 5. 末尾 10ms のコサインフェードアウト (ブツ切りノイズ防止)
    size_t fade_len = 480;
    if (copy_samples > fade_len) {
        for (size_t k = 0; k < fade_len; k++) {
            size_t idx = copy_samples - fade_len + k;
            float w = 0.5f * (1.0f + cosf(3.14159265f * (float)k / (float)fade_len));
            out_pcm[idx] *= w;
        }
    }

    *out_samples = copy_samples;
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
    float speed = (req->speed > 0.0f) ? req->speed : 1.0f;

    // Step 1: Embedder 推論
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

    // Step 3: Length Regulator (フレーム伸張 & Viseme計算)
    size_t total_frames = 0;
    float *lr_features = NULL, *lr_pitches = NULL;
    ret = step_length_regulator(model, arena, req->tokens, req->num_tokens, feature_embedded,
                                pitches, durations, speed, &total_frames, &lr_features, &lr_pitches);

    // Embedder と Variance のORTテンソル解放
    model->embedder.api->ReleaseValue(t_emb_out);
    model->embedder.api->ReleaseValue(t_phonemes);
    model->variance.api->ReleaseValue(t_var_pitches);
    model->variance.api->ReleaseValue(t_var_durations);

    if (ret != UGJY_OK) goto cleanup;

    // Step 4: Pitch Dynamics (声帯平滑化・抑揚ブースト・ささやき制御)
    ret = step_pitch_dynamics(arena, lr_pitches, total_frames, req->emotion, req->style);
    if (ret != UGJY_OK) goto cleanup;

    // Step 5: Decoder 推論 (HiFi-GAN 波形生成)
    OrtValue *t_wav = NULL;
    float *wav_data = NULL;
    size_t num_wav_samples = 0;
    ret = step_decoder(model, arena, lr_features, lr_pitches, speaker_id, total_frames,
                       &t_wav, &wav_data, &num_wav_samples);
    if (ret != UGJY_OK) goto cleanup;

    // Step 6: DSP 後処理 (フィルター・正規化・フェードアウト)
    step_dsp_postprocess(wav_data, num_wav_samples, out_pcm, max_samples, out_samples);
    model->decoder.api->ReleaseValue(t_wav);

cleanup:
    ugjy_arena_restore(arena, arena_marker);
    return ret;
}
