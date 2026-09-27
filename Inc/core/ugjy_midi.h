#ifndef UGJY_MIDI_H
#define UGJY_MIDI_H

#include "model/ugjy_note.h"
#include "ugjy_arena.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// MIDIファイル/メモリからノート配列を抽出 (Zero-Allocation: arenaから確保)
// lyrics_override が NULL でない場合、MIDIの音符に歌詞テキストを一音ずつ流し込む
int ugjy_midi_parse_file(
    const char   *file_path,
    int           track_index,     // 対象トラック番号 (-1 で最初のノート保有トラック)
    const char   *lyrics_override, // 外部歌詞流し込み (NULLならMIDI内Lyricイベント使用)
    ugjy_arena_t *arena,
    ugjy_note_t **out_notes,
    size_t       *out_num_notes
);

int ugjy_midi_parse_mem(
    const uint8_t *data,
    size_t         data_size,
    int           track_index,
    const char   *lyrics_override,
    ugjy_arena_t  *arena,
    ugjy_note_t **out_notes,
    size_t       *out_num_notes
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_MIDI_H
