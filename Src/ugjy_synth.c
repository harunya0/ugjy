#include "ugjy_synth.h"
#include <string.h>

void ugjy_synth_init(ugjy_synth_t *s, ugjy_context_t *ctx, const ugjy_t *params) {
    if (!s) return;
    s->ctx = ctx;
    s->params = params ? *params : UGJY_DEFAULT_PARAMS;
}

int ugjy_synth_process(
    ugjy_synth_t   *s,
    const char     *text,
    float          *out_pcm,
    size_t          max_pcm_samples,
    size_t         *out_num_samples,
    ugjy_viseme_t  *out_visemes,
    size_t          max_visemes,
    size_t         *out_num_visemes
) {
    if (!s || !s->ctx || !text || !out_pcm || !out_num_samples) {
        return -1;
    }

    size_t samples = 0;
    int ret = ugjy_synthesize_text(
        s->ctx,
        text,
        "ja",
        &s->params,
        out_pcm,
        max_pcm_samples,
        &samples
    );

    if (ret != 0 || samples == 0) {
        *out_num_samples = 0;
        if (out_num_visemes) *out_num_visemes = 0;
        return ret ? ret : -1;
    }

    // 口パク visemes 取得
    size_t viseme_count = 0;
    const ugjy_viseme_t *v = ugjy_get_visemes(s->ctx, &viseme_count);
    if (out_visemes && v && viseme_count > 0) {
        if (viseme_count > max_visemes) viseme_count = max_visemes;
        memcpy(out_visemes, v, viseme_count * sizeof(ugjy_viseme_t));
    }

    // ポーズ付加（掛け算・割り算をビットシフトで代替）
    // 読点: 48000 >> 2 = 12000 サンプル (0.25秒)
    // 句点: (48000 >> 2) + (48000 >> 3) = 18000 サンプル (0.375秒)
    size_t pause_samples = 0;
    if (strstr(text, "。") || strstr(text, "！") || strstr(text, "？") ||
        strstr(text, "!") || strstr(text, "?") || strstr(text, "\n")) {
        pause_samples = (UGJY_SYNTH_SAMPLE_RATE >> 2) + (UGJY_SYNTH_SAMPLE_RATE >> 3); // 18000
    } else if (strstr(text, "、") || strstr(text, ",")) {
        pause_samples = (UGJY_SYNTH_SAMPLE_RATE >> 2); // 12000
    }

    if (samples + pause_samples < max_pcm_samples) {
        memset(out_pcm + samples, 0, pause_samples * sizeof(float));
        samples += pause_samples;
    }

    // 1フレーム 512 サンプル -> samples >> 9 で高速計算 (/ 512 の完全排除)
    size_t total_visemes = samples >> 9;
    if (out_visemes) {
        if (total_visemes > max_visemes) total_visemes = max_visemes;
        for (size_t k = viseme_count; k < total_visemes; k++) {
            out_visemes[k].mouth_open = 0.0f;
            out_visemes[k].mouth_form = 0.0f;
        }
    }

    *out_num_samples = samples;
    if (out_num_visemes) *out_num_visemes = total_visemes;
    return 0;
}
