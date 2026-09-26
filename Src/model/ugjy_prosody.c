#include "ugjy_prosody.h"
#include "ugjy.h"
#include "ugjy_error.h"
#include <math.h>

// 無声母音・ポーズ・促音判定 (SHAREVOX音素インデックス: 0:pau, 1:A, 2:E, 3:I, 5:O, 6:U, 11:cl)
bool ugjy_prosody_is_unvoiced(int64_t ph) {
    return ph == 0 || ph == 1 || ph == 2 || ph == 3 || ph == 5 || ph == 6 || ph == 11;
}

void ugjy_prosody_zero_unvoiced(
    const int64_t *tokens,
    size_t         num_tokens,
    float         *pitches
) {
    if (!tokens || !pitches) return;
    for (size_t i = 0; i < num_tokens; i++) {
        if (ugjy_prosody_is_unvoiced(tokens[i])) {
            pitches[i] = 0.0f;
        }
    }
}

size_t ugjy_prosody_compute_frames(
    const int64_t *tokens,
    const float   *durations,
    size_t         num_tokens,
    float          speed,
    int           *out_frame_counts
) {
    if (!tokens || !durations || !out_frame_counts || num_tokens == 0) return 1;

    const float regulation_base = 93.75f; // 48000Hz / 512hop
    float spd = (speed > 0.0f) ? speed : 1.0f;
    size_t total_frames = 4;

    for (size_t i = 0; i < num_tokens; i++) {
        float dur_sec = durations[i] / spd;

        // 句読点（、や！、。）のポーズ時間補正ルール
        if (tokens[i] == 0) {
            if (i > 0 && i + 1 < num_tokens) {
                if (dur_sec < 0.22f) dur_sec = 0.22f; // 中間の読点ポーズ
            } else if (i == 0) {
                if (dur_sec < 0.08f) dur_sec = 0.08f; // 文頭の微小ポーズ
            } else {
                if (dur_sec < 0.15f) dur_sec = 0.18f; // 文末の余韻ポーズ
            }
        }

        int frames = (int)roundf(dur_sec * regulation_base);
        if (frames < 1 && tokens[i] != 0) {
            frames = 1; // pause 以外は最低1フレーム保証
        }
        if (frames < 0) frames = 0;

        out_frame_counts[i] = frames;
        total_frames += frames;
    }

    return (total_frames == 0) ? 1 : total_frames;
}

int ugjy_prosody_process(
    float       *pitches,
    size_t       total_frames,
    float       *temp_buf,
    uint8_t      emotion,
    uint8_t      style
) {
    if (!pitches || total_frames == 0) return UGJY_ERR_INVALID_ARG;

    // ささやき声 (Whisper / ASMR): 声帯の有声振動を完全オフにする
    if (style == UGJY_STYLE_WHISPER) {
        for (size_t f = 0; f < total_frames; f++) {
            pitches[f] = 0.0f;
        }
        return UGJY_OK;
    }

    if (!temp_buf) return UGJY_ERR_OUT_OF_MEMORY;

    // 1. 声帯の慣性平滑化（カクつき・詰まり音解消）
    for (int pass = 0; pass < 2; pass++) {
        const float *src = (pass == 0) ? pitches : temp_buf;
        float *dst = (pass == 0) ? temp_buf : pitches;
        for (size_t f = 0; f < total_frames; f++) {
            if (src[f] <= 0.0f) {
                dst[f] = 0.0f;
                continue;
            }
            float prev = (f > 0 && src[f - 1] > 0.0f) ? src[f - 1] : src[f];
            float next = (f + 1 < total_frames && src[f + 1] > 0.0f) ? src[f + 1] : src[f];
            dst[f] = 0.25f * prev + 0.50f * src[f] + 0.25f * next;
        }
    }

    // 2. 対数F0マイクロダイナミクス（抑揚ブースト ＆ 1/f揺らぎ）
    float pitch_shift      = -0.08f;
    float intonation_scale = 1.05f;
    float flutter_depth    = 0.008f;

    switch (emotion) {
        case 1: // UGJY_MOOD_HAPPY: 嬉しい・上機嫌
            pitch_shift = -0.06f;
            intonation_scale = 1.10f;
            break;
        case 2: // UGJY_MOOD_ANGRY: 怒り・不機嫌
            pitch_shift = -0.10f;
            intonation_scale = 1.10f;
            break;
        case 3: // UGJY_MOOD_SAD: 悲しい・落ち込み
            pitch_shift = -0.10f;
            intonation_scale = 0.85f;
            break;
        case 4: // UGJY_MOOD_RELAXED: まったり
            pitch_shift = -0.09f;
            intonation_scale = 0.98f;
            break;
        default: // UGJY_MOOD_NORMAL
            break;
    }

    float f0_sum = 0.0f;
    size_t voiced_count = 0;
    for (size_t f = 0; f < total_frames; f++) {
        if (pitches[f] > 0.0f) {
            f0_sum += pitches[f];
            voiced_count++;
        }
    }

    if (voiced_count > 0) {
        float mean_log_f0 = f0_sum / (float)voiced_count;
        uint32_t rng = 123456789;
        float brownian = 0.0f;
        for (size_t f = 0; f < total_frames; f++) {
            rng = rng * 1664525u + 1013904223u;
            float white = ((float)(rng & 0xFFFF) / 32768.0f) - 1.0f;
            brownian = 0.85f * brownian + 0.15f * white;
            if (pitches[f] > 0.0f) {
                float p = mean_log_f0 + (pitches[f] - mean_log_f0) * intonation_scale;
                p += pitch_shift;
                p += brownian * flutter_depth;
                pitches[f] = p;
            }
        }
    }

    return UGJY_OK;
}
