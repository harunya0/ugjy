#include "core/ugjy_midi.h"
#include "ugjy_error.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ビッグエンディアン整数読み取りヘルパー
static inline uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// 可変長数量 (VLQ: Variable-Length Quantity) デコード
static inline uint32_t read_vlq(const uint8_t **p, const uint8_t *end) {
    uint32_t val = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

// UTF-8 1文字のバイト長を取得
static inline size_t utf8_char_len(uint8_t lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

// 日本語の小書き文字（拗音: ゃ, ゅ, ょ, ぁ, ぃ, ぅ, ぇ, ぉ 等）判定 (UTF-8 3バイト)
static inline bool is_japanese_small_vowel(const char *s) {
    const uint8_t *u = (const uint8_t *)s;
    if (u[0] == 0xE3 && u[1] == 0x82) {
        // ぁ(81), ぃ(83), ぅ(85), ぇ(87), ぉ(89), ゃ(83?), ゃ=E3 82 83
        if (u[2] == 0x81 || u[2] == 0x83 || u[2] == 0x85 || u[2] == 0x87 || u[2] == 0x89) return true;
    }
    if (u[0] == 0xE3 && u[1] == 0x83) {
        // ゃ(83 83), ゅ(83 85), ょ(83 87), ゎ(83 8E)
        if (u[2] == 0x83 || u[2] == 0x85 || u[2] == 0x87 || u[2] == 0x8E) return true;
    }
    return false;
}

// 歌詞文字列から次の1音節（拗音合体つき）を取り出す
static const char* extract_next_syllable(const char **src, ugjy_arena_t *arena) {
    if (!src || !*src || **src == '\0') return "ら"; // 歌詞枯渇時のデフォルト

    // 空白スキップ
    while (**src == ' ' || **src == '\t' || **src == '\r' || **src == '\n') (*src)++;
    if (**src == '\0') return "ら";

    const char *start = *src;
    size_t len = utf8_char_len((uint8_t)*start);
    *src += len;

    // 次の文字が拗音（ゃ, ゅ, ょなど）なら合体して1音符にまとめる
    if (**src != '\0') {
        size_t next_len = utf8_char_len((uint8_t)**src);
        if (next_len == 3 && is_japanese_small_vowel(*src)) {
            len += next_len;
            *src += next_len;
        }
    }

    char *buf = (char *)ugjy_arena_alloc(arena, len + 1);
    if (!buf) return "ら";
    memcpy(buf, start, len);
    buf[len] = '\0';
    return buf;
}

// パース中の一時音符構造体
typedef struct {
    uint32_t    start_tick;
    uint32_t    end_tick;
    float       start_ms;
    float       end_ms;
    uint8_t     key;
    const char *lyric;
} raw_note_t;

int ugjy_midi_parse_mem(
    const uint8_t *data,
    size_t         data_size,
    int           track_index,
    const char   *lyrics_override,
    ugjy_arena_t  *arena,
    ugjy_note_t **out_notes,
    size_t       *out_num_notes
) {
    if (!data || data_size < 14 || !arena || !out_notes || !out_num_notes) {
        return UGJY_ERR_INVALID_ARG;
    }
    *out_notes = NULL;
    *out_num_notes = 0;

    // 1. MThd ヘッダチャンク検証
    if (memcmp(data, "MThd", 4) != 0) return UGJY_ERR_INVALID_ARG;
    uint32_t header_len = read_be32(data + 4);
    if (header_len < 6) return UGJY_ERR_INVALID_ARG;

    uint16_t format      = read_be16(data + 8);
    uint16_t num_tracks  = read_be16(data + 10);
    (void)num_tracks;
    uint16_t division    = read_be16(data + 12);
    if (division == 0 || (division & 0x8000)) {
        // SMPTEタイムコード等は簡略化のため480基準にフォールバック
        division = 480;
    }
    float ticks_per_beat = (float)division;

    // 2. トラックの位置を探索
    const uint8_t *p = data + 8 + header_len;
    const uint8_t *end = data + data_size;

    // 対象トラックの決定
    int target_trk = (track_index >= 0) ? track_index : ((format == 0) ? 0 : 1);
    int curr_trk = 0;
    const uint8_t *trk_data = NULL;
    uint32_t trk_len = 0;

    while (p + 8 <= end) {
        if (memcmp(p, "MTrk", 4) == 0) {
            uint32_t len = read_be32(p + 4);
            if (curr_trk == target_trk) {
                trk_data = p + 8;
                trk_len = len;
                break;
            }
            p += 8 + len;
            curr_trk++;
        } else {
            p++;
        }
    }

    if (!trk_data || trk_data + trk_len > end) {
        // 対象トラックが見つからない場合、トラック0を使用
        p = data + 8 + header_len;
        if (p + 8 <= end && memcmp(p, "MTrk", 4) == 0) {
            trk_data = p + 8;
            trk_len = read_be32(p + 4);
        } else {
            return UGJY_ERR_INVALID_ARG;
        }
    }

    // 3. トラック内イベントのスキャン
    // 最大 512 音符を一時バッファに確保
    const size_t MAX_RAW = 512;
    raw_note_t *raw_notes = (raw_note_t *)ugjy_arena_alloc(arena, MAX_RAW * sizeof(raw_note_t));
    if (!raw_notes) return UGJY_ERR_OUT_OF_MEMORY;

    // Note-On 保持用 (各ノート番号の最新開始時刻とLyric)
    uint32_t on_ticks[128];
    const char *on_lyrics[128];
    memset(on_ticks, 0xFF, sizeof(on_ticks)); // 0xFFFFFFFF = off
    memset(on_lyrics, 0, sizeof(on_lyrics));

    const uint8_t *tp = trk_data;
    const uint8_t *tend = trk_data + trk_len;

    uint32_t current_tick = 0;
    uint32_t tempo_us = 500000; // デフォルト 120 BPM (500,000 us/beat)
    uint8_t running_status = 0;
    size_t raw_count = 0;
    const char *last_lyric = NULL;

    while (tp < tend && raw_count < MAX_RAW) {
        uint32_t delta = read_vlq(&tp, tend);
        if (delta > 0) {
            current_tick += delta;
        }
        if (tp >= tend) break;

        uint8_t status = *tp;
        if (status < 0x80) {
            status = running_status; // Running Status
        } else {
            tp++;
            running_status = status;
        }

        if (status == 0xFF) {
            // Meta Event
            if (tp >= tend) break;
            uint8_t meta_type = *tp++;
            uint32_t meta_len = read_vlq(&tp, tend);
            if (tp + meta_len > tend) break;

            if (meta_type == 0x51 && meta_len == 3) {
                // Set Tempo
                tempo_us = ((uint32_t)tp[0] << 16) | ((uint32_t)tp[1] << 8) | (uint32_t)tp[2];
            } else if (meta_type == 0x05 || meta_type == 0x01) {
                // Lyric or Text Event
                char *lbuf = (char *)ugjy_arena_alloc(arena, meta_len + 1);
                if (lbuf) {
                    memcpy(lbuf, tp, meta_len);
                    lbuf[meta_len] = '\0';
                    last_lyric = lbuf;
                }
            } else if (meta_type == 0x2F) {
                // End of Track
                break;
            }
            tp += meta_len;
        } else if ((status & 0xF0) == 0x90) {
            // Note On
            if (tp + 2 > tend) break;
            uint8_t key = *tp++;
            uint8_t vel = *tp++;
            if (key < 128) {
                if (vel > 0) {
                    on_ticks[key] = current_tick;
                    on_lyrics[key] = last_lyric;
                    last_lyric = NULL;
                } else if (on_ticks[key] != 0xFFFFFFFF) {
                    // Note Off (vel == 0)
                    raw_notes[raw_count].start_tick = on_ticks[key];
                    raw_notes[raw_count].end_tick   = current_tick;
                    raw_notes[raw_count].key        = key;
                    raw_notes[raw_count].lyric      = on_lyrics[key];
                    raw_count++;
                    on_ticks[key] = 0xFFFFFFFF;
                }
            }
        } else if ((status & 0xF0) == 0x80) {
            // Note Off
            if (tp + 2 > tend) break;
            uint8_t key = *tp++;
            tp++; // vel
            if (key < 128 && on_ticks[key] != 0xFFFFFFFF) {
                raw_notes[raw_count].start_tick = on_ticks[key];
                raw_notes[raw_count].end_tick   = current_tick;
                raw_notes[raw_count].key        = key;
                raw_notes[raw_count].lyric      = on_lyrics[key];
                raw_count++;
                on_ticks[key] = 0xFFFFFFFF;
            }
        } else if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) {
            tp += 1; // 1バイト引数
        } else if ((status & 0xF0) == 0xA0 || (status & 0xF0) == 0xB0 || (status & 0xF0) == 0xE0) {
            tp += 2; // 2バイト引数
        } else if (status == 0xF0 || status == 0xF7) {
            uint32_t sysex_len = read_vlq(&tp, tend);
            tp += sysex_len;
        }
    }

    if (raw_count == 0) return UGJY_ERR_INVALID_ARG;

    // 4. Tick からミリ秒の正確な算出（開始時刻順にソート）
    // 単純なバブルソート (raw_count は最大512程度なので十分高速)
    for (size_t i = 0; i < raw_count; i++) {
        for (size_t j = i + 1; j < raw_count; j++) {
            if (raw_notes[j].start_tick < raw_notes[i].start_tick) {
                raw_note_t tmp = raw_notes[i];
                raw_notes[i] = raw_notes[j];
                raw_notes[j] = tmp;
            }
        }
    }

    // 5. 休符の自動挿入 ＆ 歌詞の流し込み ＆ 出力配列生成
    // 音符間の隙間（ギャップ > 15ms）に休符（key=0）を挿入するため最大 2 * raw_count 分確保
    ugjy_note_t *notes = (ugjy_note_t *)ugjy_arena_alloc(arena, (raw_count * 2 + 2) * sizeof(ugjy_note_t));
    if (!notes) return UGJY_ERR_OUT_OF_MEMORY;

    size_t note_idx = 0;
    float ms_per_tick_final = ((float)tempo_us / (ticks_per_beat * 1000.0f));
    uint32_t prev_end_tick = raw_notes[0].start_tick; // 最初の音符の前
    const char *lyrics_ptr = lyrics_override;

    // 冒頭に休符がある場合
    if (raw_notes[0].start_tick > 0) {
        uint32_t rest_dur_ms = (uint32_t)roundf((float)raw_notes[0].start_tick * ms_per_tick_final);
        if (rest_dur_ms >= 15) {
            notes[note_idx].lyric       = "R";
            notes[note_idx].key         = 0;
            notes[note_idx].vibrato     = 0;
            notes[note_idx].flags       = UGJY_NOTE_FLAG_NONE;
            notes[note_idx].reserved    = 0;
            notes[note_idx].duration_ms = rest_dur_ms;
            note_idx++;
        }
    }

    for (size_t i = 0; i < raw_count; i++) {
        // 前の音符との間に休符があるか判定
        if (i > 0 && raw_notes[i].start_tick > prev_end_tick) {
            uint32_t gap_ticks = raw_notes[i].start_tick - prev_end_tick;
            uint32_t rest_ms = (uint32_t)roundf((float)gap_ticks * ms_per_tick_final);
            if (rest_ms >= 15) {
                notes[note_idx].lyric       = "R";
                notes[note_idx].key         = 0;
                notes[note_idx].vibrato     = 0;
                notes[note_idx].flags       = UGJY_NOTE_FLAG_NONE;
                notes[note_idx].reserved    = 0;
                notes[note_idx].duration_ms = rest_ms;
                note_idx++;
            }
        }

        uint32_t dur_ticks = (raw_notes[i].end_tick > raw_notes[i].start_tick) ? 
                             (raw_notes[i].end_tick - raw_notes[i].start_tick) : 1;
        uint32_t dur_ms = (uint32_t)roundf((float)dur_ticks * ms_per_tick_final);
        if (dur_ms < 20) dur_ms = 20;

        // 歌詞の解決
        const char *syl = NULL;
        if (lyrics_ptr && *lyrics_ptr != '\0') {
            syl = extract_next_syllable(&lyrics_ptr, arena);
        } else if (raw_notes[i].lyric) {
            syl = raw_notes[i].lyric;
        } else {
            syl = "ら";
        }

        notes[note_idx].lyric       = syl;
        notes[note_idx].key         = raw_notes[i].key;
        notes[note_idx].vibrato     = (dur_ms >= 400) ? 5 : 0; // 400ms以上はデフォルトビブラート
        notes[note_idx].flags       = UGJY_NOTE_FLAG_NONE;
        notes[note_idx].reserved    = 0;
        notes[note_idx].duration_ms = dur_ms;
        note_idx++;

        prev_end_tick = raw_notes[i].end_tick;
    }

    *out_notes = notes;
    *out_num_notes = note_idx;
    return UGJY_OK;
}

int ugjy_midi_parse_file(
    const char   *file_path,
    int           track_index,
    const char   *lyrics_override,
    ugjy_arena_t *arena,
    ugjy_note_t **out_notes,
    size_t       *out_num_notes
) {
    if (!file_path || !arena || !out_notes || !out_num_notes) return UGJY_ERR_INVALID_ARG;

    FILE *fp = fopen(file_path, "rb");
    if (!fp) return UGJY_ERR_FILE_NOT_FOUND;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) { // 10MB上限
        fclose(fp);
        return UGJY_ERR_IO;
    }

    // arena から一時バッファ確保
    uint8_t *data = (uint8_t *)ugjy_arena_alloc(arena, (size_t)sz);
    if (!data) {
        fclose(fp);
        return UGJY_ERR_OUT_OF_MEMORY;
    }

    size_t rd = fread(data, 1, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) return UGJY_ERR_IO;

    return ugjy_midi_parse_mem(data, (size_t)sz, track_index, lyrics_override, arena, out_notes, out_num_notes);
}
