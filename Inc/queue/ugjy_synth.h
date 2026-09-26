#ifndef UGJY_SYNTH_H
#define UGJY_SYNTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "ugjy.h"

#define UGJY_SYNTH_SAMPLE_RATE 48000
#define UGJY_SYNTH_MAX_SAMPLES (UGJY_SYNTH_SAMPLE_RATE * 4) // 最大4秒分
#define UGJY_SYNTH_MAX_VISEMES 4096

// 単一文の音声合成器 (スレッド/キュー非依存)
typedef struct {
    ugjy_context_t *ctx;
    ugjy_t          params;
} ugjy_synth_t;

void ugjy_synth_init(ugjy_synth_t *s, ugjy_context_t *ctx, const ugjy_t *params);

// 1文を合成し、PCM・Viseme・ポーズを適用して出力 (double/除算ゼロ)
int ugjy_synth_process(
    ugjy_synth_t   *s,
    const char     *text,
    float          *out_pcm,
    size_t          max_pcm_samples,
    size_t         *out_num_samples,
    ugjy_viseme_t  *out_visemes,
    size_t          max_visemes,
    size_t         *out_num_visemes
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_SYNTH_H
