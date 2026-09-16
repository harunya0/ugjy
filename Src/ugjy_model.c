#include "ugjy_model.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

// エラーチェック用マクロ
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
    const char *model_path,
    int num_threads
) {
    if (!model || !model_path) return -1;
    memset(model, 0, sizeof(*model));

    // ONNXセッションを初期化
    int ret = ugjy_onnx_session_init(&model->onnx, model_path, num_threads);
    if (ret != 0) return ret;

    // デフォルト値の設定（後でモデルメタデータから取得も可能）
    model->sample_rate = UGJY_SAMPLE_RATE; // 16000Hz
    model->num_speakers = 1;
    model->has_speaker_id = false;
    model->has_f0_input = false;
    model->has_lid = false;
    model->has_prosody = false;
    model->has_speaker_embedding = false;
    model->has_speaker_embedding_mask = false;

    // モデルの入力ノードを走査してsidやf0の有無を確認
    size_t num_inputs = 0;
    ORT_CHECK(model->onnx.api, model->onnx.api->SessionGetInputCount(model->onnx.session, &num_inputs));
    OrtAllocator *allocator = NULL;
    ORT_CHECK(model->onnx.api, model->onnx.api->GetAllocatorWithDefaultOptions(&allocator));

    for (size_t i = 0; i < num_inputs; i++) {
        char *input_name = NULL;
        ORT_CHECK(model->onnx.api, model->onnx.api->SessionGetInputName(model->onnx.session, i, allocator, &input_name));
        if (input_name) {
            printf("  [ONNX Input %zu]: %s\n", i, input_name);
            if (strcmp(input_name, "sid") == 0 || strcmp(input_name, "speaker_id") == 0) {
                model->has_speaker_id = true;
                model->num_speakers = 512; // マルチ話者対応
            }
            if (strcmp(input_name, "f0") == 0 || strcmp(input_name, "pitch") == 0) {
                model->has_f0_input = true;
            } else if (strcmp(input_name, "lid") == 0) {
                model->has_lid = true;
            } else if (strcmp(input_name, "prosody_features") == 0) {
                model->has_prosody = true;
            } else if (strcmp(input_name, "speaker_embedding") == 0) {
                model->has_speaker_embedding = true;
            } else if (strcmp(input_name, "speaker_embedding_mask") == 0) {
                model->has_speaker_embedding_mask = true;
            }
            allocator->Free(allocator, input_name);
        }
    }
    return 0;
}

void ugjy_model_destroy(ugjy_model_t *model) {
    if (!model) return;
    ugjy_onnx_destroy(&model->onnx);
    memset(model, 0, sizeof(*model));
}

// tokens配列をONNXのテンソルに変換
static OrtValue *build_tokens_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    const int64_t *tokens,
    size_t num_tokens
) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, num_tokens * sizeof(int64_t));
    if (!buf) return NULL;
    memcpy(buf, tokens, num_tokens * sizeof(int64_t));

    int64_t shape[2] = {1, (int64_t)num_tokens};
    return ugjy_onnx_create_tensor(
        onnx,
        buf,
        num_tokens * sizeof(int64_t),
        shape,
        2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
    );
}

// tokens_lengthの長さを持つint64_t配列をONNXのテンソルに変換
static OrtValue *build_length_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    size_t num_tokens
) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!buf) return NULL;
    *buf = (int64_t)num_tokens;

    int64_t shape[1] = {1};
    return ugjy_onnx_create_tensor(
        onnx,
        buf,
        sizeof(int64_t),
        shape,
        1,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
    );
}

// scalesをONNXのテンソルに変換
static OrtValue *build_scales_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    float speed,
    float noise_scale,
    float noise_scale_w
) {
    float *buf = (float *)ugjy_arena_alloc(arena, 3 * sizeof(float));
    if (!buf) return NULL;
    buf[0] = (noise_scale > 0.0f) ? noise_scale : 0.667;
    buf[1] = (speed > 0.0f) ? (1.0f / speed) : 1.0f;
    buf[2] = (noise_scale_w > 0.0f) ? noise_scale_w : 0.8f;

    int64_t shape[1] = {3};
    return ugjy_onnx_create_tensor(
        onnx,
        buf,
        3 * sizeof(float),
        shape,
        1,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
    );
}

// sid（話者ID: int64_t）をONNXのテンソルに変換
static OrtValue *build_sid_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    uint32_t sid
) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!buf) return NULL;
    *buf = (int64_t)sid;

    int64_t shape[1] = {1};
    return ugjy_onnx_create_tensor(
        onnx,
        buf,
        sizeof(int64_t),
        shape,
        1,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
    );
}

// f0（歌声用ピッチ: float[1, T]）をONNXのテンソルに変換
static OrtValue *build_f0_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    const float *f0,
    size_t f0_len
) {
    float *buf = (float *)ugjy_arena_alloc(arena, f0_len * sizeof(float));
    if (!buf) return NULL;
    memcpy(buf, f0, f0_len * sizeof(float));

    int64_t shape[2] = {1, (int64_t)f0_len};
    return ugjy_onnx_create_tensor(
        onnx,
        buf,
        f0_len * sizeof(float),
        shape,
        2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
    );
}

// 言語ID (日本語: 0)
static OrtValue *build_lid_tensor(ugjy_onnx_session_t *onnx, ugjy_arena_t *arena, uint32_t lid) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!buf) return NULL;
    *buf = (int64_t)lid; // 日本語
    int64_t shape[1] = {1};
    return ugjy_onnx_create_tensor(onnx, buf, sizeof(int64_t), shape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
}

// プロソディ特徴量 (A1, A2, A3)
static OrtValue *build_prosody_tensor(
    ugjy_onnx_session_t *onnx,
    ugjy_arena_t *arena,
    const int64_t *prosody_features,
    size_t num_tokens
) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, num_tokens * 3 * sizeof(int64_t));
    if (!buf) return NULL;
    if (prosody_features) {
        memcpy(buf, prosody_features, num_tokens * 3 * sizeof(int64_t));
    } else {
        memset(buf, 0, num_tokens * 3 * sizeof(int64_t));
    }
    int64_t shape[3] = {1, (int64_t)num_tokens, 3};
    return ugjy_onnx_create_tensor(onnx, buf, num_tokens * 3 * sizeof(int64_t), shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
}

// 話者埋め込みベクトル (256次元)
static OrtValue *build_speaker_embedding_tensor(ugjy_onnx_session_t *onnx, ugjy_arena_t *arena) {
    float *buf = (float *)ugjy_arena_alloc_zero(arena, 256 * sizeof(float));
    if (!buf) return NULL;
    int64_t shape[2] = {1, 256};
    return ugjy_onnx_create_tensor(onnx, buf, 256 * sizeof(float), shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
}

// 話者マスク (値: 0: 通常の学習済み話者ID・言語IDを使用)
static OrtValue *build_speaker_embedding_mask_tensor(ugjy_onnx_session_t *onnx, ugjy_arena_t *arena) {
    int64_t *buf = (int64_t *)ugjy_arena_alloc(arena, sizeof(int64_t));
    if (!buf) return NULL;
    *buf = 0; // 0 = 通常話者モード (1にすると未学習のzero-shot話者転送層にルーティングされてしまう)
    int64_t shape[2] = {1, 1};
    return ugjy_onnx_create_tensor(onnx, buf, sizeof(int64_t), shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
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
    if (!req->tokens || req->num_tokens == 0) return -2;

    *out_samples = 0;

    // アリーナの現在位置をしおりとして保存
    size_t arena_marker = ugjy_arena_mark(arena);
    int ret_code = 0;

    // テンソル保持用
    const char *input_names[16];
    const OrtValue *input_tensors[16];
    OrtValue *allocated_tensors[16];
    size_t num_inputs = 0;
    size_t num_allocated = 0;
    
    #define PUSH_INPUT(name, tensor_expr) do { \
        OrtValue *t = (tensor_expr); \
        if (!t) { ret_code = -10; goto cleanup; } \
        allocated_tensors[num_allocated++] = t; \
        input_names[num_inputs] = (name); \
        input_tensors[num_inputs] = t; \
        num_inputs++; \
    } while (0)

    PUSH_INPUT("input", build_tokens_tensor(&model->onnx, arena, req->tokens, req->num_tokens));
    PUSH_INPUT("input_lengths", build_length_tensor(&model->onnx, arena, req->num_tokens));
    PUSH_INPUT("scales", build_scales_tensor(&model->onnx, arena, req->speed, req->noise_scale, req->noise_scale_w));
    if (model->has_speaker_id) {
        PUSH_INPUT("sid", build_sid_tensor(&model->onnx, arena, req->speaker_id));
    }
    if (model->has_f0_input && req->f0_sequence && req->f0_length > 0) {
        PUSH_INPUT("f0", build_f0_tensor(&model->onnx, arena, req->f0_sequence, req->f0_length));
    }
    if (model->has_lid) {
        PUSH_INPUT("lid", build_lid_tensor(&model->onnx, arena, req->language_id));
    }
    if (model->has_prosody) {
        PUSH_INPUT("prosody_features", build_prosody_tensor(&model->onnx, arena, req->prosody_features, req->num_tokens));
    }
    if (model->has_speaker_embedding) {
        PUSH_INPUT("speaker_embedding", build_speaker_embedding_tensor(&model->onnx, arena));
    }
    if (model->has_speaker_embedding_mask) {
        PUSH_INPUT("speaker_embedding_mask", build_speaker_embedding_mask_tensor(&model->onnx, arena));
    }
    #undef PUSH_INPUT

    // 推論実行
    const char *output_names[1] = {"output"};
    OrtValue *output_tensors[1] = {NULL};

    int run_ret = ugjy_onnx_run(
        &model->onnx,
        input_names,
        input_tensors,
        num_inputs,
        output_names,
        1,
        output_tensors
    );
    if (run_ret != 0 || !output_tensors[0]) {
        ret_code = -30;
        goto cleanup;
    }

    float *audio_raw = NULL;
    ORT_CHECK(model->onnx.api, model->onnx.api->GetTensorMutableData(output_tensors[0], (void **)&audio_raw));
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    ORT_CHECK(model->onnx.api, model->onnx.api->GetTensorTypeAndShape(output_tensors[0], &shape_info));
    size_t total_elements = 0;
    ORT_CHECK(model->onnx.api, model->onnx.api->GetTensorShapeElementCount(shape_info, &total_elements));
    model->onnx.api->ReleaseTensorTypeAndShapeInfo(shape_info);

    // 出力波形をout_pcmにコピー
    size_t copy_samples = (total_elements > max_samples) ? max_samples : total_elements;
    if (audio_raw && copy_samples > 0) {
        memcpy(out_pcm, audio_raw, copy_samples * sizeof(float));
        *out_samples = copy_samples;
    }
    model->onnx.api->ReleaseValue(output_tensors[0]);

    cleanup:
        // 生成されたテンソルを解放
        for (size_t i = 0; i < num_allocated; i++) {
            if (allocated_tensors[i]) {
                model->onnx.api->ReleaseValue(allocated_tensors[i]);
            }
        }
        // アリーナをしおりまで巻き戻す
        ugjy_arena_restore(arena, arena_marker);
        return ret_code;
}
