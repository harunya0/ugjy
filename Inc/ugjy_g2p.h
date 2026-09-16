
#ifndef UGJY_G2P_H
#define UGJY_G2P_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// G2Pハンドル
typedef struct ugjy_g2p ugjy_g2p_t;

// config.json を読み込んで G2P を初期化
ugjy_g2p_t *ugjy_g2p_create(const char *config_path);

// テキストからトークン列と韻律特徴量を直接出力
int ugjy_g2p_convert(
    ugjy_g2p_t *g2p,
    const char *text,
    const char *lang,
    int64_t *out_tokens,
    int64_t *out_prosody,
    size_t max_tokens,
    size_t *out_num_tokens
);

// 解放
void ugjy_g2p_destroy(ugjy_g2p_t *g2p);

#ifdef __cplusplus
}
#endif

#endif // UGJY_G2P_H
