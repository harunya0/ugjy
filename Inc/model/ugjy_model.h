#ifndef UGJY_MODEL_H
#define UGJY_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ugjy_arena.h"
#include "ugjy_onnx.h"
#include "ugjy.h"

#ifdef __cplusplus
extern "C" {
#endif

// 高性能ツクヨミちゃん (SHAREVOX v3-1 3-stage) 専用モデル構造体
typedef struct {
    ugjy_onnx_session_t embedder;       // 1. 音素埋め込み (embedder_model.onnx)
    ugjy_onnx_session_t variance;       // 2. ピッチ・音素長予測 (variance_model.onnx)
    ugjy_onnx_session_t decoder;        // 3. 24kHz波形生成デコーダー (decoder_model.onnx)
    uint32_t            sample_rate;    // 24000 Hz
    uint32_t            default_speaker;// 4 (つくよみちゃん「おしとやかv3」)
    ugjy_viseme_t       visemes[2048];
    size_t              num_visemes;
} ugjy_model_t;

// 推論リクエスト構造体
typedef struct {
    const int64_t *tokens;              // 音素ID列 (0..44)
    size_t         num_tokens;          // 音素数
    const int64_t *prosody_features;    // アクセントID列 (0..4)
    uint32_t       speaker_id;          // 話者ID (未指定時: 4)
    float          speed;               // 話速 (1.0 = 標準)
    uint8_t        emotion;             // 感情プリセット
    uint8_t        style;               // 発声スタイル (0=通常, 1=ささやき, 2=歌唱)
} ugjy_model_request_t;

// モデルディレクトリ（models/tsukuyomi-v3-1）から3モデルを初期化
int ugjy_model_load(
    ugjy_model_t *model,
    const char *model_dir,
    int num_threads
);

// モデル解放
void ugjy_model_destroy(ugjy_model_t *model);

// 推論実行 (ゼロアロケーション・Arena使用)
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
