#ifndef UGJY_STREAM_H
#define UGJY_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "ugjy.h"

// pcm_chunk   : 48kHz float32 の PCM 配列（正規化済み）
// num_samples : 今回のチャンクのサンプル数
// is_last     : 文章全体の最後のチャンクなら 1、まだ続くなら 0
// user_data   : C# の GCHandle や Rust の Context ポインタ
// 戻り値      : 0 なら継続、非ゼロなら中断（Abort）
typedef int (*ugjy_pcm_chunk_cb_t)(
    const float *pcm_chunk,
    size_t       num_samples,
    int          is_last,
    void        *user_data
);

typedef struct ugjy_stream {
    ugjy_context_t      *ctx;
    ugjy_g2p_t          *g2p;
    ugjy_t               params;
    ugjy_pcm_chunk_cb_t  callback;
    void                *user_data;
    
    char   text_buf[2048]; // LLM のトークンをためる内部バッファ
    size_t text_len;
    size_t chunk_count;
} ugjy_stream_t;

// ストリームの初期化（スタックまたはアリーナ上に配置可能）
int ugjy_stream_init(
    ugjy_stream_t       *stream,
    ugjy_context_t      *ctx,
    ugjy_g2p_t          *g2p,
    const ugjy_t        *params,
    ugjy_pcm_chunk_cb_t  callback,
    void                *user_data
);

// LLM のトークン（テキストの断片）を 1 文字ずつでも単語単位でも流し込む
// 句読点（、。！？\n）に達した瞬間、即座に裏で合成が走り、callback が発火する！
int ugjy_stream_feed(ugjy_stream_t *stream, const char *text_chunk);

// 入力完了を通知（バッファに残っているテキストをすべて合成し、is_last=1 で発火）
int ugjy_stream_flush(ugjy_stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif // UGJY_STREAM_H
