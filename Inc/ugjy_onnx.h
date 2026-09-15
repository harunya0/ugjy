#ifndef UGJY_ONNX_H
#define UGJY_ONNX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "onnxruntime_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const OrtApi      *api;              // API関数テーブル
    OrtEnv            *env;              // ONNX Runtimeの環境
    OrtSession        *session;          // ONNX Runtimeのセッション
    OrtSessionOptions *session_options;  // スレッド数等の設定
    OrtMemoryInfo      *mem_info;        // CPUメモリ情報
} ugjy_onnx_session_t;

// セッションの初期化とモデル読み込み
// 成功なら0、失敗ならエラーコード
int ugjy_onnx_session_init(
    ugjy_onnx_session_t *s,
    const char *model_path,
    int num_threads
);

// セッションの解放
void ugjy_onnx_destroy(ugjy_onnx_session_t *s);

// アリーナのメモリを直接ラップしたOrtValueを作成する
OrtValue *ugjy_onnx_create_tensor (
    ugjy_onnx_session_t *s,
    void *data,
    size_t data_bytes,
    const int64_t *shape,
    size_t shape_len,
    ONNXTensorElementDataType type
);

// 入力テンソル配列を渡して推論を実行し、出力テンソル配列を取得する
int ugjy_onnx_run (
    ugjy_onnx_session_t *s,
    const char* const* input_names,
    const OrtValue* const* input_tensors,
    size_t num_inputs,
    const char* const* output_names,
    size_t num_outputs,
    OrtValue** output_tensors
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_ONNX_H
