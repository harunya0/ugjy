#include "ugjy_viseme.h"

static inline void get_phoneme_viseme(int64_t token, float *open_y, float *form) {
    switch (token) {
        case 1:  // A
        case 7:  // a
            *open_y = 1.0f; *form = 0.0f; break;
        case 3:  // I
        case 21: // i
            *open_y = 0.3f; *form = 1.0f; break;
        case 6:  // U
        case 40: // u
            *open_y = 0.25f; *form = -1.0f; break;
        case 2:  // E
        case 14: // e
            *open_y = 0.6f; *form = 0.5f; break;
        case 5:  // O
        case 30: // o
            *open_y = 0.8f; *form = -0.6f; break;
        case 4:  // N (ん)
            *open_y = 0.1f; *form = 0.0f; break;
        case 0:  // pau (ポーズ)
        case 11: // cl (っ)
            *open_y = 0.0f; *form = 0.0f; break;
        default: // その他の子音
            *open_y = 0.15f; *form = 0.0f; break;
    }
}

void ugjy_viseme_generate(
    const int64_t *tokens,
    const int     *frame_counts,
    size_t         num_tokens,
    ugjy_viseme_t *out_visemes,
    size_t         max_visemes,
    size_t        *out_num_visemes
) {
    if (!tokens || !frame_counts || !out_visemes || !out_num_visemes || max_visemes == 0) {
        if (out_num_visemes) *out_num_visemes = 0;
        return;
    }

    size_t curr_frame = 0;
    float smooth_open = 0.0f;
    float smooth_form = 0.0f;

    for (size_t i = 0; i < num_tokens; i++) {
        int cnt = frame_counts[i];
        float target_open = 0.0f, target_form = 0.0f;
        get_phoneme_viseme(tokens[i], &target_open, &target_form);

        for (int f = 0; f < cnt; f++) {
            if (curr_frame >= max_visemes) break;

            // 指数移動平均で滑らかに遷移（Live2Dのカクつき防止）
            smooth_open += 0.35f * (target_open - smooth_open);
            smooth_form += 0.35f * (target_form - smooth_form);

            out_visemes[curr_frame].mouth_open = smooth_open;
            out_visemes[curr_frame].mouth_form = smooth_form;
            curr_frame++;
        }
    }

    *out_num_visemes = curr_frame;
}
