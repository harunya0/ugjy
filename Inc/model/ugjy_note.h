#ifndef UGJY_NOTE_H
#define UGJY_NOTE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// 音符フラグ
#define UGJY_NOTE_FLAG_NONE       0x00
#define UGJY_NOTE_FLAG_SLUR       0x01 // 前の音から滑らかに繋ぐ（アタック低減）
#define UGJY_NOTE_FLAG_STACCATO   0x02 // 音を短く切る

// 1つの音符（ノート）定義: 16バイト固定
typedef struct {
    const char *lyric;       // 歌詞（例: "さ", "く", "ら"。休符なら NULL または "R" / "r"）
    uint32_t    duration_ms; // 音の長さ（ミリ秒。例: 500 = 0.5秒）
    uint8_t     key;         // MIDIノート番号 (0: 休符, 21: A0 〜 108: C8。例: 60=C4, 69=A4)
    uint8_t     vibrato;     // ビブラート強度 (0: なし, 1〜10: 自然な揺らぎ)
    uint8_t     flags;       // UGJY_NOTE_FLAG_*
    uint8_t     reserved;    // 予約パディング (0)
} ugjy_note_t;

// 楽譜（スコア）
typedef struct {
    const ugjy_note_t *notes;     // ノート配列へのポインタ
    size_t             num_notes; // ノート数
} ugjy_score_t;

// 休符判定インラインヘルパー関数
static inline bool ugjy_note_is_rest(const ugjy_note_t *note) {
    if (!note) return true;
    if (note->key == 0) return true;
    if (!note->lyric) return true;
    if (note->lyric[0] == '\0') return true;
    if ((note->lyric[0] == 'R' || note->lyric[0] == 'r') && note->lyric[1] == '\0') return true;
    return false;
}

#ifdef __cplusplus
}
#endif

#endif // UGJY_NOTE_H
