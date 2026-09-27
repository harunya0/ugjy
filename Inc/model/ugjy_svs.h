#ifndef UGJY_SVS_H
#define UGJY_SVS_H

#include "ugjy_note.h"
#include "ugjy_arena.h"
#include "ugjy_g2p.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 歌唱1フレーズの合成パラメータ（G2P結果＋時間配分＋ピッチカーブ）
typedef struct {
    int64_t *tokens;          // 音素ID配列 (Embedder入力用)
    int     *frame_counts;    // 音素ごとのフレーム数配列 (LengthRegulator用)
    size_t   num_tokens;      // 音素数
    size_t   total_frames;    // 全フレーム数
    float   *pitches;         // 各フレームの対数F0配列 (HiFi-GAN入力用)
} ugjy_svs_phrase_t;

// 1フレーズのノート配列から歌唱用パラメータを一括生成 (Zero-Allocation: arenaから確保)
int ugjy_svs_build_phrase(
    const ugjy_note_t *notes,
    size_t             num_notes,
    ugjy_g2p_t        *g2p,
    ugjy_arena_t      *arena,
    ugjy_svs_phrase_t *out_phrase
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_SVS_H
