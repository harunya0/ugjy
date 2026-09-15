#ifndef UGJY_H
#define UGJY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t speaker_id;        // 固定話者ID（プリセットを使う場合）
    const float* speaker_embed; // 自分の声の特徴量ベクトル（NULLなら speaker_id を使用）
    size_t embed_dim;           // ベクトルの長さ

    float speed;                // 話速
    float pitch;                // ピッチ
    float energy;               // 音量
    uint8_t emotion;            // 感情
    uint8_t style;              // スタイル
    uint8_t reserved[2];
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

// モデルに必要な推奨メモリプールサイズ（バイト数）を返す
size_t ugjy_get_required_memory(const char* model_path);

// モデルに含まれる話者数を取得
uint32_t ugjy_get_num_speakers(ugjy_context_t* ctx);

// float の PCM 配列を 16-bit PCM WAV ファイルとして保存
int ugjy_write_wav(
    const char* filepath,
    const float* pcm,
    size_t num_samples,
    int sample_rate
);

// ストリーミング用コールバック型
typedef int (*ugjy_stream_callback_t)(
    const float* pcm_chunk, 
    size_t chunk_samples, 
    void* user_data
);

// できた音声から順次コールバックに流す
int ugjy_synthesize_stream(
    ugjy_context_t* ctx,
    const int64_t* tokens,
    size_t num_tokens,
    const ugjy_t* params,
    ugjy_stream_callback_t callback,
    void* user_data
);

void ugjy_destroy(ugjy_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // UGJY_H
