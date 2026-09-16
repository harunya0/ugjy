#ifndef UGJY_H
#define UGJY_H

#include "ugjy_g2p.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t       speaker_id;         // 固定話者ID（プリセットを使う場合）
    uint32_t       language_id;        // 言語ID (日本語: 0)
    const float   *speaker_embed;      // 自分の声の特徴量ベクトル（NULLなら speaker_id を使用）
    size_t         embed_dim;          // ベクトルの長さ

    float          speed;              // 話速
    float          pitch;              // ピッチ
    float          energy;             // 音量
    float          noise_scale;        // 音色揺らぎ/息成分 (0なら既定0.4f, 0.333〜0.667)
    float          noise_scale_w;      // 音素長揺らぎ (0なら既定0.6f, 0.4〜0.8)
    uint8_t        emotion;            // 感情
    uint8_t        style;              // スタイル
    uint8_t        reserved[2];
    const int64_t *prosody_features;   // 韻律情報 (A1/A2/A3, NULLなら全ゼロ)
} ugjy_t;

// デフォルト値マクロ
#define UGJY_DEFAULT_PARAMS ((ugjy_t){ \
    .speaker_id = 0, \
    .language_id = 0, \
    .speaker_embed = NULL, \
    .embed_dim = 0, \
    .speed = 1.0f, \
    .pitch = 1.0f, \
    .energy = 1.0f, \
    .noise_scale = 0.4f, \
    .noise_scale_w = 0.6f, \
    .emotion = 0, \
    .style = 0, \
    .reserved = {0}, \
    .prosody_features = NULL \
})

// 不透明ポインタ
typedef struct ugjy_context ugjy_context_t;

// ユーザーが用意した固定バッファを渡して初期化
ugjy_context_t* ugjy_init(
    const char  *model_path,
    void        *memory_pool,
    size_t       pool_size
);

// 返り値はエラーコード
int ugjy_synthesize(
    ugjy_context_t *ctx,
    const int64_t  *tokens,        // トークンIDの配列
    size_t          num_tokens,    // トークン数
    const ugjy_t   *params,        // 音声設定（NULLならデフォルト）
    float          *out_pcm,       // 出力先バッファ（PCM float）
    size_t          max_samples,   // 出力バッファの最大容量
    size_t         *out_samples    // 実際に生成されたサンプル数
);

// モデルに必要な推奨メモリプールサイズ（バイト数）を返す
size_t ugjy_get_required_memory(const char* model_path);

// モデルに含まれる話者数を取得
uint32_t ugjy_get_num_speakers(ugjy_context_t* ctx);

// float の PCM 配列を 16-bit PCM WAV ファイルとして保存
int ugjy_write_wav(
    const char  *filepath,
    const float *pcm,
    size_t       num_samples,
    int          sample_rate
);

// ストリーミング用コールバック型
typedef int (*ugjy_stream_callback_t)(
    const float *pcm_chunk, 
    size_t       chunk_samples, 
    void        *user_data
);

// できた音声から順次コールバックに流す
int ugjy_synthesize_stream(
    ugjy_context_t        *ctx,
    const int64_t         *tokens,
    size_t                 num_tokens,
    const ugjy_t          *params,
    ugjy_stream_callback_t callback,
    void                  *user_data
);

int ugjy_synthesize_text(
    ugjy_context_t *ctx,
    ugjy_g2p_t     *g2p,
    const char     *text,
    const char     *lang,        // "ja", "en" 等
    const ugjy_t   *params,      // NULLならデフォルト
    float          *out_pcm,
    size_t          max_samples,
    size_t         *out_samples
);

void ugjy_destroy(ugjy_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // UGJY_H
