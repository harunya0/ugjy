#ifndef UGJY_H
#define UGJY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t speaker_id; // 話者ID（モデルによっては32bitあると安心）
    float speed;         // 話速 (1.0 = 標準)
    float pitch;         // ピッチ (1.0 = 標準)
    float energy;        // 音量・エネルギー (1.0 = 標準)
    uint8_t emotion;     // 感情
    uint8_t style;       // スタイル
    uint8_t reserved[2]; // 4バイトアライメント用のパディング
} ugjy_t;

// デフォルト値マクロ
#define UGJY_DEFAULT_PARAMS ((ugjy_t){ \
    .speaker_id = 0, \
    .speed = 1.0f, \
    .pitch = 1.0f, \
    .energy = 1.0f, \
    .emotion = 0, \
    .style = 0, \
    .reserved = {0} \
})

// 不透明ポインタ
typedef struct ugjy_context ugjy_context_t;

// ユーザーが用意した固定バッファを渡して初期化
ugjy_context_t* ugjy_init(
    const char* model_path,
    void* memory_pool,
    size_t pool_size
);

// 返り値はエラーコード
int ugjy_synthesize(
    ugjy_context_t* ctx,
    const int64_t* tokens,       // トークンIDの配列
    size_t num_tokens,           // トークン数
    const ugjy_t* params,        // 音声設定（NULLならデフォルト）
    float* out_pcm,              // 出力先バッファ（PCM float）
    size_t max_samples,          // 出力バッファの最大容量
    size_t* out_samples          // 実際に生成されたサンプル数
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_H
