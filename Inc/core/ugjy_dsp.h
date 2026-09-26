#ifndef UGJY_DSP_H
#define UGJY_DSP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PCM 波形に対する音響信号処理を一括適用
// (ハイパス・ローパス・ささやき低域除去・ピーク正規化・末尾フェードアウト)
void ugjy_dsp_postprocess(
    float       *pcm,
    size_t       num_samples,
    uint32_t     sample_rate,
    uint8_t      style
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_DSP_H
