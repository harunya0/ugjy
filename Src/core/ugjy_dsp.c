#include "ugjy_dsp.h"
#include "ugjy.h"
#include <math.h>

void ugjy_dsp_postprocess(
    float       *pcm,
    size_t       num_samples,
    uint32_t     sample_rate,
    uint8_t      style
) {
    if (!pcm || num_samples == 0) return;
    (void)sample_rate;
    // 1. 低域カットフィルター
    if (style == UGJY_STYLE_WHISPER) {
        float dc_sum = 0.0f;
        for (size_t i = 0; i < num_samples; i++) dc_sum += pcm[i];
        float dc_mean = dc_sum / (float)num_samples;
        for (size_t i = 0; i < num_samples; i++) pcm[i] -= dc_mean;

        const float alpha = 0.840f;        
        // 前方向パス (0 -> num_samples-1)
        float prev_x = 0.0f, prev_y = 0.0f;
        for (size_t i = 0; i < num_samples; i++) {
            float x = pcm[i];
            float y = alpha * (prev_y + x - prev_x);
            prev_x = x; prev_y = y;
            pcm[i] = y;
        }
        // 逆方向パス (num_samples-1 -> 0: 位相歪みとボコつきを打ち消す)
        prev_x = 0.0f; prev_y = 0.0f;
        for (size_t i = num_samples; i > 0; i--) {
            size_t idx = i - 1;
            float x = pcm[idx];
            float y = alpha * (prev_y + x - prev_x);
            prev_x = x; prev_y = y;
            pcm[idx] = y;
        }
    } else {
        // 通常声用: 50Hz 1次HPF (DCオフセット除去)
        const float alpha_hp = 0.9935f;
        float prev_x = pcm[0];
        float prev_y = pcm[0];
        for (size_t i = 0; i < num_samples; i++) {
            float x = pcm[i];
            float y = alpha_hp * (prev_y + x - prev_x);
            prev_x = x;
            prev_y = y;
            pcm[i] = y;
        }
    }
    // 2. 超高音カット (12kHz 2次バターワースLPF)
    const float b0 = 0.292893f, b1 = 0.585786f, b2 = 0.292893f;
    const float a2 = 0.171573f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float x0 = pcm[i];
        float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a2 * y2;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        pcm[i] = y0;
    }
    // ささやき声は息が小さいため 0.008f 前後、通常声は 0.003f 前後が目安
    const float gate_threshold = (style == UGJY_STYLE_WHISPER) ? 0.012f : 0.003f;
    for (size_t i = 0; i < num_samples; i++) {
        float a = fabsf(pcm[i]);
        if (a < gate_threshold) {
            // しきい値以下は 2乗カーブでゼロへ滑らかに落とす (クリックノイズ完全防止)
            float ratio = a / gate_threshold; // 0.0 〜 1.0
            pcm[i] *= (ratio * ratio);
        }
    }
    // 3. ピーク正規化 (ささやきは 0.70f で耳元感)
    float target_peak = (style == UGJY_STYLE_WHISPER) ? 0.70f : 0.95f;
    float max_abs = 0.001f;
    for (size_t i = 0; i < num_samples; i++) {
        float a = fabsf(pcm[i]);
        if (a > max_abs) max_abs = a;
    }
    float scale = (max_abs > target_peak) ? (target_peak / max_abs) : 1.0f;
    for (size_t i = 0; i < num_samples; i++) {
        pcm[i] *= scale;
    }
    // 4. 末尾フェードアウト
    size_t fade_len = 480;
    if (num_samples > fade_len) {
        for (size_t k = 0; k < fade_len; k++) {
            size_t idx = num_samples - fade_len + k;
            float w = 0.5f * (1.0f + cosf(3.14159265f * (float)k / (float)fade_len));
            pcm[idx] *= w;
        }
    }
}
