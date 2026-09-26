#ifndef UGJY_VISEME_H
#define UGJY_VISEME_H

#include "ugjy.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 音素ID列とフレーム配分から Live2D 対応 Viseme（口形）配列を生成・平滑化
void ugjy_viseme_generate(
    const int64_t *tokens,
    const int     *frame_counts,
    size_t         num_tokens,
    ugjy_viseme_t *out_visemes,
    size_t         max_visemes,
    size_t        *out_num_visemes
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_VISEME_H
