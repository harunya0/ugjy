#ifndef UGJY_MODEL_H
#define UGJY_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ugjy_arena.h"
#include "ugjy_onnx.h"

#ifdef __cplusplus
extern "C" {
#endif

// モデル管理構造体
typedef struct {
    ugjy_onnx_session_t onnx;         // ONNXセッション
    uint32_t            sample_rate;  // サンプルレート
    uint32_t            num_speakers; // スピーカー数
    bool                has_speaker_id; // sid入力が必要か
    bool                has_f0_input;   // f0入力が必要か
} ugjy_model_t;

typedef struct {
    const int64_t *tokens;      // 音素ID列
    size_t         num_tokens;  // トークン数
    uint32_t       speaker_id;  // 話者ID
    float          speed;       // 話速(1.0 = 通常速度)
    float          noise_scale; // 音色表現(0.667)
    float          noise_scale_w; // 音素長揺らぎ(0.8)

    // 歌唱用パラメーター(NULLならTTS)
    const float   *f0_sequence;  // 各フレームのピッチ(Hz)
    size_t         f0_length;    // f0配列の長さ(フレーム数)
    const int64_t *durations;    // 各音素の連続フレーム数
    size_t         durations_length; // durations配列の長さ
} ugjy_model_request_t;

// モデル読み込み・初期化
int ugjy_model_load(
    ugjy_model_t *model,
    const char *model_path,
    int num_threads
);

// モデル解放
void ugjy_model_destroy(ugjy_model_t *model);

// 推論実行
// 入力波形はout_pcmに格納され、実際のサンプル数がout_samplesに入る
int ugjy_model_infer(
    ugjy_model_t *model,
    ugjy_arena_t *arena,
    const ugjy_model_request_t *req,
    float *out_pcm,
    size_t max_samples,
    size_t *out_samples
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_MODEL_H
