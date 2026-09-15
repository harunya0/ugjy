#include "ugjy_onnx.h"
#include "onnxruntime_c_api.h"
#include <stdio.h>
#include <string.h>

// エラーチェック用マクロ
#define ORT_CHECK(api, expr) do { \
    OrtStatus* status = (expr); \
    if (status != NULL) { \
        fprintf(stderr, "[ugjy_onnx error] %s\n", (api)->GetErrorMessage(status)); \
        (api)->ReleaseStatus(status); \
        return -1; \
    } \
} while (0)

int ugjy_onnx_session_init(
    ugjy_onnx_session_t *s,
    const char *model_path,
    int num_threads
) {
    if (!s || !model_path) return -1;
    memset(s, 0, sizeof(*s));

    // API関数テーブルの取得
    s->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!s->api) {
        fprintf(stderr, "[ugjy_onnx error] Failed to get ONNX Runtime API\n");
        return -1;
    }

    // 実行環境の作成
    ORT_CHECK(s->api, s->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ugjy", &s->env));

    // セッションオプションの作成
    ORT_CHECK(s->api, s->api->CreateSessionOptions(&s->session_options));

    // スレッド数の設定
    if (num_threads > 0) {
        ORT_CHECK(s->api, s->api->SetInterOpNumThreads(s->session_options, num_threads));
        ORT_CHECK(s->api, s->api->SetIntraOpNumThreads(s->session_options, 1));
    }

    // グラフ最適化
    ORT_CHECK(s->api, s->api->SetSessionGraphOptimizationLevel(s->session_options, ORT_ENABLE_ALL));

    // モデルの読み込み
    ORT_CHECK(s->api, s->api->CreateSession(s->env, model_path, s->session_options, &s->session));

    // CPUメモリ情報オブジェクトの作成
    ORT_CHECK(s->api, s->api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &s->mem_info));

    return 0;
}

void ugjy_onnx_destroy(ugjy_onnx_session_t *s) {
    if (!s || !s->api) return;

    if (s->mem_info) {
        s->api->ReleaseMemoryInfo(s->mem_info);
        s->mem_info = NULL;
    }
    if (s->session) {
        s->api->ReleaseSession(s->session);
        s->session = NULL;
    }
    if (s->session_options) {
        s->api->ReleaseSessionOptions(s->session_options);
        s->session_options = NULL;
    }
    if (s->env) {
        s->api->ReleaseEnv(s->env);
        s->env = NULL;
    }

    memset(s, 0, sizeof(*s));
}

OrtValue *ugjy_onnx_create_tensor(
    ugjy_onnx_session_t *s,
    void *data,
    size_t data_bytes,
    const int64_t *shape,
    size_t size_len,
    ONNXTensorElementDataType type
) {
    if (!s || !s->api || !s->mem_info || !data) return NULL;

    OrtValue *tensor = NULL;

    OrtStatus *status = s->api->CreateTensorWithDataAsOrtValue(
        s->mem_info,
        data,
        data_bytes,
        shape,
        size_len,
        type,
        &tensor
    );

    if (status != NULL) {
        fprintf(stderr, "[ugjy_onnx error] %s\n", s->api->GetErrorMessage(status));
        s->api->ReleaseStatus(status);
        return NULL;
    }

    return tensor;
}

int ugjy_onnx_run(
    ugjy_onnx_session_t *s,
    const char* const* input_names,
    const OrtValue* const* input_tensors,
    size_t num_inputs,
    const char* const* output_names,
    size_t num_outputs,
    OrtValue** output_tensors
) {
    if (!s || !s->api || !s->session) return -1;
    // 推論を実行
    ORT_CHECK(s->api, s->api->Run(
        s->session,
        NULL,
        input_names,
        input_tensors,
        num_inputs,
        output_names,
        num_outputs,
        output_tensors
    ));
    return 0;
}
