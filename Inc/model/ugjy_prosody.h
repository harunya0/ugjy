#ifndef UGJY_PROSODY_H
#define UGJY_PROSODY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 無声音・ポーズ・促音の判定 (0:pau, 1:A, 2:E, 3:I, 5:O, 6:U, 11:cl)
bool ugjy_prosody_is_unvoiced(int64_t phoneme_id);

// 音素列の無声音に対応するピッチをゼロクリア
void ugjy_prosody_zero_unvoiced(
    const int64_t *tokens,
    size_t         num_tokens,
    float         *pitches
);

// 各音素の予測持続時間(durations)から発話タイミング・ポーズ補正を適用し、フレーム数配列を算出
size_t ugjy_prosody_compute_frames(
    const int64_t *tokens,
    const float   *durations,
    size_t         num_tokens,
    float          speed,
    int           *out_frame_counts
);

// ピッチ（F0）列に対して声帯慣性平滑化、感情抑揚ブースト、1/f揺らぎ、ささやき制御を適用
// temp_buf は total_frames * sizeof(float) 以上の作業領域
int ugjy_prosody_process(
    float       *pitches,
    size_t       total_frames,
    float       *temp_buf,
    uint8_t      emotion,
    uint8_t      style
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_PROSODY_H
