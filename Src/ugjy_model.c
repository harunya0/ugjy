#include "ugjy_model.h"
#include "ugjy_arena.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define ORT_CHECK(api, expr) do { \
    OrtStatus* status = (expr); \
    if (status != NULL) { \
        fprintf(stderr, "[ugjy_onnx error] %s\n", (api)->GetErrorMessage(status)); \
        (api)->ReleaseStatus(status); \
        return -1; \
    } \
} while (0)

int ugjy_model_load(
    ugjy_model_t *model,
    const char *model_dir,
    int num_threads
) {
    if (!model || !model_dir) return -1;
    memset(model, 0, sizeof(*model));

    char path[1024];

    // 1. embedder_model.onnx
    snprintf(path, sizeof(path), "%s/embedder_model.onnx", model_dir);
    int ret = ugjy_onnx_session_init(&model->embedder, path, num_threads);
    if (ret != 0) {
        fprintf(stderr, "[ugjy] Failed to load embedder: %s\n", path);
        return ret;
    }

    // 2. variance_model.onnx
    snprintf(path, sizeof(path), "%s/variance_model.onnx", model_dir);
    ret = ugjy_onnx_session_init(&model->variance, path, num_threads);
    if (ret != 0) {
        fprintf(stderr, "[ugjy] Failed to load variance: %s\n", path);
        ugjy_onnx_destroy(&model->embedder);
        return ret;
    }

    // 3. decoder_model.onnx
    snprintf(path, sizeof(path), "%s/decoder_model.onnx", model_dir);
    ret = ugjy_onnx_session_init(&model->decoder, path, num_threads);
    if (ret != 0) {
        fprintf(stderr, "[ugjy] Failed to load decoder: %s\n", path);
        ugjy_onnx_destroy(&model->embedder);
        ugjy_onnx_destroy(&model->variance);
        return ret;
    }

    model->sample_rate = 48000;
    model->default_speaker = 4; // つくよみちゃん「おしとやかv3」

    printf("[ugjy] 高性能ツクヨミちゃんモデル読み込み完了 (48000Hz, HiFi-GAN)\n");
    return 0;
}

void ugjy_model_destroy(ugjy_model_t *model) {
    if (!model) return;
    ugjy_onnx_destroy(&model->embedder);
    ugjy_onnx_destroy(&model->variance);
    ugjy_onnx_destroy(&model->decoder);
}

// 無声母音・ポーズ・促音判定 (SHAREVOX音素インデックス: 0:pau, 1:A, 2:E, 3:I, 5:O, 6:U, 11:cl)
static inline bool is_unvoiced_phoneme(int64_t ph) {
    return ph == 0 || ph == 1 || ph == 2 || ph == 3 || ph == 5 || ph == 6 || ph == 11;
}

int ugjy_model_infer(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const ugjy_model_request_t *req,
    float *out_pcm,
    size_t max_samples,
    size_t *out_samples
) {
    if (!model || !arena || !req || !out_pcm || !out_samples) return -1;
    if (req->num_tokens == 0) return -2;

    *out_samples = 0;
    size_t arena_marker = ugjy_arena_mark(arena);
    int ret_code = 0;

    int64_t speaker_id = (req->speaker_id > 0) ? (int64_t)req->speaker_id : (int64_t)model->default_speaker;
    float speed = (req->speed > 0.0f) ? req->speed : 1.0f;
    size_t L = req->num_tokens;

    // ----------------------------------------------------
    // Step 1: Embedder 推論
    // ----------------------------------------------------
    int64_t *phoneme_buf = (int64_t *)ugjy_arena_alloc(arena, L * sizeof(int64_t));
    if (!phoneme_buf) { ret_code = -10; goto cleanup; }
    memcpy(phoneme_buf, req->tokens, L * sizeof(int64_t));

    int64_t shape_L[2] = {1, (int64_t)L};
    OrtValue *t_phonemes = ugjy_onnx_create_tensor(&model->embedder, phoneme_buf, L * sizeof(int64_t), shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    if (!t_phonemes) { ret_code = -11; goto cleanup; }

    const char *emb_in_names[] = {"phonemes"};
    const OrtValue *emb_in_tensors[] = {t_phonemes};
    const char *emb_out_names[] = {"feature_embedded"};
    OrtValue *emb_out_tensors[1] = {NULL};

    if (ugjy_onnx_run(&model->embedder, emb_in_names, emb_in_tensors, 1, emb_out_names, 1, emb_out_tensors) != 0 || !emb_out_tensors[0]) {
        model->embedder.api->ReleaseValue(t_phonemes);
        ret_code = -12;
        goto cleanup;
    }

    float *feature_embedded = NULL;
    ORT_CHECK(model->embedder.api, model->embedder.api->GetTensorMutableData(emb_out_tensors[0], (void **)&feature_embedded));

    // ----------------------------------------------------
    // Step 2: Variance 推論 (ピッチ・音素長予測)
    // ----------------------------------------------------
    int64_t *accent_buf = (int64_t *)ugjy_arena_alloc(arena, L * sizeof(int64_t));
    if (!accent_buf) { ret_code = -20; goto cleanup; }
    if (req->prosody_features) {
        memcpy(accent_buf, req->prosody_features, L * sizeof(int64_t));
    } else {
        for (size_t i = 0; i < L; i++) accent_buf[i] = 4; // デフォルト: _ (変化なし)
    }

    int64_t *spk_buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    *spk_buf = speaker_id;
    int64_t shape_1[1] = {1};

    OrtValue *t_var_phonemes = ugjy_onnx_create_tensor(&model->variance, phoneme_buf, L * sizeof(int64_t), shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    OrtValue *t_var_accents  = ugjy_onnx_create_tensor(&model->variance, accent_buf, L * sizeof(int64_t), shape_L, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    OrtValue *t_var_speaker  = ugjy_onnx_create_tensor(&model->variance, spk_buf, sizeof(int64_t), shape_1, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);

    const char *var_in_names[] = {"phonemes", "accents", "speakers"};
    const OrtValue *var_in_tensors[] = {t_var_phonemes, t_var_accents, t_var_speaker};
    const char *var_out_names[] = {"pitches", "durations"};
    OrtValue *var_out_tensors[2] = {NULL, NULL};

    int var_ret = ugjy_onnx_run(&model->variance, var_in_names, var_in_tensors, 3, var_out_names, 2, var_out_tensors);
    model->variance.api->ReleaseValue(t_var_phonemes);
    model->variance.api->ReleaseValue(t_var_accents);
    model->variance.api->ReleaseValue(t_var_speaker);

    if (var_ret != 0 || !var_out_tensors[0] || !var_out_tensors[1]) {
        model->embedder.api->ReleaseValue(emb_out_tensors[0]);
        model->embedder.api->ReleaseValue(t_phonemes);
        ret_code = -21;
        goto cleanup;
    }

    float *pitches = NULL;
    float *durations = NULL;
    ORT_CHECK(model->variance.api, model->variance.api->GetTensorMutableData(var_out_tensors[0], (void **)&pitches));
    ORT_CHECK(model->variance.api, model->variance.api->GetTensorMutableData(var_out_tensors[1], (void **)&durations));

    // 無声音のピッチをゼロクリア
    for (size_t i = 0; i < L; i++) {
        if (is_unvoiced_phoneme(req->tokens[i])) {
            pitches[i] = 0.0f;
        }
    }

    // ----------------------------------------------------
    // Step 3: Length Regulator (ゼロアロケーション・フレーム伸張)
    // ----------------------------------------------------
    const float regulation_base = 93.75f; // 48000Hz / 512hop = 93.75
    size_t total_frames = 4;
    int *frame_counts = (int *)ugjy_arena_alloc(arena, L * sizeof(int));
    if (!frame_counts) { ret_code = -30; goto cleanup; }

    for (size_t i = 0; i < L; i++) {
        float dur_sec = durations[i] / speed;
        // 句読点（、や！、。）のポーズをしっかり確保
        if (req->tokens[i] == 0) {
            if (i > 0 && i + 1 < L) {
                if (dur_sec < 0.22f) dur_sec = 0.22f; // 中間の句読点ポーズ (約20フレーム = 0.22秒)
            } else if (i == 0) {
                if (dur_sec < 0.08f) dur_sec = 0.08f; // 文頭の微小ポーズ
            } else {
                if (dur_sec < 0.15f) dur_sec = 0.18f; // 文末の余韻ポーズ
            }
        }
        int frames = (int)roundf(dur_sec * regulation_base);
        if (frames < 1 && req->tokens[i] != 0) {
            frames = 1; // pause 以外は最低1フレーム保証
        }
        if (frames < 0) frames = 0;
        frame_counts[i] = frames;
        total_frames += frames;
    }

    if (total_frames == 0) total_frames = 1;

    // 伸張バッファをアリーナから確保
    float *lr_features = (float *)ugjy_arena_alloc(arena, total_frames * 192 * sizeof(float));
    float *lr_pitches  = (float *)ugjy_arena_alloc(arena, total_frames * sizeof(float));
    if (!lr_features || !lr_pitches) { ret_code = -31; goto cleanup; }

    size_t curr_frame = 0;
    for (size_t i = 0; i < L; i++) {
        int cnt = frame_counts[i];
        const float *src_feat = feature_embedded + (i * 192);
        float p = pitches[i];

        for (int f = 0; f < cnt; f++) {
            memcpy(lr_features + (curr_frame * 192), src_feat, 192 * sizeof(float));
            lr_pitches[curr_frame] = p;
            curr_frame++;
        }
    }

    // ----------------------------------------------------
    // 1. 声帯の慣性平滑化（カクつき・詰まり音の解消）
    // ----------------------------------------------------
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

    // ----------------------------------------------------
    // 2. 対数F0マイクロダイナミクス（過度平滑化の解消・抑揚ブースト）
    // ----------------------------------------------------
    const float pitch_shift      = -0.0f; // ピッチシフト (-0.058: 半音1つ下げ, -0.085: 落ち着いたお姉さん声)
    const float intonation_scale = 1.0f;   // 抑揚ブースト (1.10〜1.18)
    const float flutter_depth    = 0.0f;  // 揺らぎの深さ (0.010〜0.020: ほんのり自然な生っぽさ)

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
        // 1/f ピンクノイズ生成器
        uint32_t rng = 123456789;
        float brownian = 0.0f;
        for (size_t f = 0; f < total_frames; f++) {
            // 毎フレーム乱数を生成して、前回の値と滑らかにブレンド（遮断周波数 約5Hz）
            rng = rng * 1664525u + 1013904223u;
            float white = ((float)(rng & 0xFFFF) / 32768.0f) - 1.0f; // -1.0f 〜 +1.0f
            brownian = 0.85f * brownian + 0.15f * white;
            if (lr_pitches[f] > 0.0f) {
                // 抑揚ブースト
                float p = mean_log_f0 + (lr_pitches[f] - mean_log_f0) * intonation_scale;
                // ピッチシフト（落ち着いた声へ下げる）
                p += pitch_shift;
                // 1/f 微細ピッチ揺らぎ
                p += brownian * flutter_depth;
                lr_pitches[f] = p;
            }
        }
    }

    // Embedder と Variance のテンソルを解放
    model->embedder.api->ReleaseValue(emb_out_tensors[0]);
    model->embedder.api->ReleaseValue(t_phonemes);
    model->variance.api->ReleaseValue(var_out_tensors[0]);
    model->variance.api->ReleaseValue(var_out_tensors[1]);

    // ----------------------------------------------------
    // Step 4: Decoder 推論 (24kHz HiFi-GAN 波形生成)
    // ----------------------------------------------------
    int64_t shape_dec_feat[3] = {1, (int64_t)total_frames, 192};
    int64_t shape_dec_pitch[2] = {1, (int64_t)total_frames};

    OrtValue *t_dec_features = ugjy_onnx_create_tensor(&model->decoder, lr_features, total_frames * 192 * sizeof(float), shape_dec_feat, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    OrtValue *t_dec_pitches  = ugjy_onnx_create_tensor(&model->decoder, lr_pitches, total_frames * sizeof(float), shape_dec_pitch, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    OrtValue *t_dec_speaker  = ugjy_onnx_create_tensor(&model->decoder, spk_buf, sizeof(int64_t), shape_1, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);

    const char *dec_in_names[] = {"length_regulated_tensor", "pitches", "speakers"};
    const OrtValue *dec_in_tensors[] = {t_dec_features, t_dec_pitches, t_dec_speaker};
    const char *dec_out_names[] = {"wav"};
    OrtValue *dec_out_tensors[1] = {NULL};

    int dec_ret = ugjy_onnx_run(&model->decoder, dec_in_names, dec_in_tensors, 3, dec_out_names, 1, dec_out_tensors);
    model->decoder.api->ReleaseValue(t_dec_features);
    model->decoder.api->ReleaseValue(t_dec_pitches);
    model->decoder.api->ReleaseValue(t_dec_speaker);

    if (dec_ret != 0 || !dec_out_tensors[0]) {
        ret_code = -40;
        goto cleanup;
    }

    float *wav_data = NULL;
    ORT_CHECK(model->decoder.api, model->decoder.api->GetTensorMutableData(dec_out_tensors[0], (void **)&wav_data));
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    ORT_CHECK(model->decoder.api, model->decoder.api->GetTensorTypeAndShape(dec_out_tensors[0], &shape_info));
    size_t num_wav_samples = 0;
    ORT_CHECK(model->decoder.api, model->decoder.api->GetTensorShapeElementCount(shape_info, &num_wav_samples));
    model->decoder.api->ReleaseTensorTypeAndShapeInfo(shape_info);

    // ----------------------------------------------------
    // Step 5: ピーク正規化 (0.95) & 出力コピー
    // ----------------------------------------------------
   size_t copy_samples = (num_wav_samples > max_samples) ? max_samples : num_wav_samples;
    if (wav_data && copy_samples > 0) {
        // 1. まずバッファにコピー
        for (size_t i = 0; i < copy_samples; i++) {
            out_pcm[i] = wav_data[i];
        }
        // 2. 超低音カット (50Hz 1次ハイパスフィルター: DCオフセット・ボコつき除去)
        // 48000Hz における 50Hz カットオフの係数
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
        // 3. 超高音カット (12kHz 2次バターワース・ローパスフィルター: チリチリ・がびがび高周波ノイズ除去)
        // 48000Hz / 4 = 12000Hz (Q = 0.7071) の完全最適化係数
        const float b0 = 0.292893f, b1 = 0.585786f, b2 = 0.292893f;
        const float a2 = 0.171573f; // a1 は数学的にジャスト 0
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
        for (size_t i = 0; i < copy_samples; i++) {
            float x0 = out_pcm[i];
            float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a2 * y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            out_pcm[i] = y0;
        }
        // 4. ピーク正規化 (ノイズ除去後の綺麗な波形で 0.95 にスケール)
        float max_abs = 0.001f;
        for (size_t i = 0; i < copy_samples; i++) {
            float a = fabsf(out_pcm[i]);
            if (a > max_abs) max_abs = a;
        }
        float scale = (max_abs > 0.95f) ? (0.95f / max_abs) : 1.0f;
        for (size_t i = 0; i < copy_samples; i++) {
            out_pcm[i] *= scale;
        }
        // 5. 末尾 10ms のコサインフェードアウト（ブツ切り防止）
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

    model->decoder.api->ReleaseValue(dec_out_tensors[0]);

cleanup:
    ugjy_arena_restore(arena, arena_marker);
    return ret_code;
}
