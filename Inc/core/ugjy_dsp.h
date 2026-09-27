#ifndef UGJY_DSP_H
#define UGJY_DSP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// チャンク間をまたいでフィルター状態を保持する構造体
typedef struct {
    float whisper_hp_x[4];
    float whisper_hp_y[4];
    float normal_hp_x;
    float normal_hp_y;
    float lpf_x1, lpf_x2;
    float lpf_y1, lpf_y2;
    bool  initialized;
} ugjy_dsp_state_t;

#include "ugjy_arena.h"

// PCM 波形に対する音響信号処理を一括適用
// (ハイパス・ローパス・ささやき低域除去・ケプストラム平滑化息声化・ピーク正規化)
void ugjy_dsp_postprocess(
    float               *pcm,
    size_t               num_samples,
    uint32_t             sample_rate,
    uint8_t              style,
    ugjy_dsp_state_t    *state,
    ugjy_arena_t        *arena
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_DSP_H
