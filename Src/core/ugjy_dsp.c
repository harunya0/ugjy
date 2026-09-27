#include "ugjy_dsp.h"
#include "ugjy.h"
#include <string.h>
#include <math.h>

void ugjy_dsp_state_reset(ugjy_dsp_state_t *state) {
    if (state) memset(state, 0, sizeof(*state));
}

// ささやき声 DSP (完全ノイズレス・自然な息声)
// 1. 850Hz 4次バターワースHPF (声帯の有声振動・260Hz基音を急峻遮断)
// 2. 人工的な乱数ノイズ加算を完全排除 (HiFi-GAN本来の極上の自然なフォルマントと息感を100%活かす)
// 3. 高域の過剰ブーストを排除し、自然で柔らかな周波数バランスを維持
// 4. 無音・息継ぎ・低エネルギー区間のノイズゲート (memset による完全静寂化)
static void apply_whisper_dsp_clean(
    float            *pcm,
    size_t            num_samples,
    ugjy_dsp_state_t *state
) {
    if (!pcm || num_samples == 0) return;
    (void)state;

    // 1. 200Hz 2次 Butterworth HPF
    // 93.75Hz チェッカーボード -20dB 抑制・声道 F1(300-850Hz) を温存し過渡応答のゴソつきを解消
    // 48kHz: b0=0.9817, b1=-1.9633, b2=0.9817, a1=-1.9631, a2=0.9636
    const float hpf_b0 =  0.9817f, hpf_b1 = -1.9633f, hpf_b2 =  0.9817f;
    const float hpf_a1 = -1.9631f, hpf_a2 =  0.9636f;
    float hpf_x1 = 0.0f, hpf_x2 = 0.0f, hpf_y1 = 0.0f, hpf_y2 = 0.0f;

    for (size_t i = 0; i < num_samples; i++) {
        float x0 = pcm[i];
        float y0 = hpf_b0 * x0 + hpf_b1 * hpf_x1 + hpf_b2 * hpf_x2 - hpf_a1 * hpf_y1 - hpf_a2 * hpf_y2;
        hpf_x2 = hpf_x1; hpf_x1 = x0;
        hpf_y2 = hpf_y1; hpf_y1 = y0;
        pcm[i] = y0;
    }

    // 1.5 呼気エアバンド・エンハンサー (>6kHz 呼気成分を +18% 加算して息掛かり感を再現)
    const float alpha_air = 0.50f; // 約6kHz以上のハイパス
    float air_prev_x = 0.0f, air_prev_y = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float x = pcm[i];
        float y_hp = alpha_air * (air_prev_y + x - air_prev_x);
        air_prev_x = x;
        air_prev_y = y_hp;
        pcm[i] += 0.18f * y_hp; // 吐息のエア感をブレンド
    }

    // 2. ピーク正規化 (耳元・ASMR適正音量: -7dBFS = 0.45f)
    const float target_peak = 0.45f;
    float max_peak = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float a = fabsf(pcm[i]);
        if (a > max_peak) max_peak = a;
    }
    float gain = (max_peak > 1e-6f) ? (target_peak / max_peak) : 1.0f;
    for (size_t i = 0; i < num_samples; i++) {
        pcm[i] *= gain;
    }

    // 3. 無音判定フレームの出力ゲート処理 (memset による完全消音)
    // 語尾の呼気余韻（ブレス）を消さないようしきい値を 1.2% に緩和
    const float silence_thresh = target_peak * 0.012f; // 約 0.0054f
    const size_t frame_sz = 512;
    const size_t fade_len = 480; // 10ms クロスフェード (吐息が自然に消え入る余韻)
    bool prev_silent = true;

    for (size_t start = 0; start < num_samples; start += frame_sz) {
        size_t end = start + frame_sz;
        if (end > num_samples) end = num_samples;
        size_t len = end - start;

        // フレーム内エネルギー (RMS)
        float sum_sq = 0.0f;
        for (size_t i = start; i < end; i++) {
            sum_sq += pcm[i] * pcm[i];
        }
        float rms = sqrtf(sum_sq / (float)len);

        if (rms < silence_thresh) {
            // 完全無音判定: memset で力技で完全消音！
            if (!prev_silent && len >= fade_len) {
                // 有音から無音へのスムーズな 1ms フェードアウト
                for (size_t f = 0; f < fade_len; f++) {
                    float ramp = 1.0f - ((float)f / (float)fade_len);
                    pcm[start + f] *= ramp;
                }
                memset(pcm + start + fade_len, 0, (len - fade_len) * sizeof(float));
            } else {
                memset(pcm + start, 0, len * sizeof(float));
            }
            prev_silent = true;
        } else {
            if (prev_silent && len >= fade_len) {
                // 無音から有音へのスムーズな 1ms フェードイン
                for (size_t f = 0; f < fade_len; f++) {
                    float ramp = (float)f / (float)fade_len;
                    pcm[start + f] *= ramp;
                }
            }
            prev_silent = false;
        }
    }
}

void ugjy_dsp_postprocess(
    float            *pcm,
    size_t            num_samples,
    uint32_t          sample_rate,
    uint8_t           style,
    ugjy_dsp_state_t *state,
    ugjy_arena_t     *arena
) {
    if (!pcm || num_samples == 0) return;
    (void)sample_rate;
    (void)arena;

    if (style == UGJY_STYLE_WHISPER) {
        // ささやき (ASMR): 人工乱数ノイズゼロ・低周波ビープ音ゼロ・無音ゲート完備
        apply_whisper_dsp_clean(pcm, num_samples, state);
        return;
    }

    // 歌唱専用 DSP (UGJY_STYLE_SINGING: 超高域シャリつき・チリつきの完全蒸発)
    if (style == UGJY_STYLE_SINGING) {
        // 1. DCオフセット除去 HPF (約30Hz)
        const float alpha_hp = 0.9960f;
        float prev_x = pcm[0], prev_y = pcm[0];
        for (size_t i = 0; i < num_samples; i++) {
            float x = pcm[i];
            float y = alpha_hp * (prev_y + x - prev_x);
            prev_x = x; prev_y = y;
            pcm[i] = y;
        }

        // 2. 3タップ対称FIR移動平均 (ナイキスト24kHz折り返しノイズ完全消滅)
        float prev_s = pcm[0];
        for (size_t i = 1; i + 1 < num_samples; i++) {
            float curr_s = pcm[i];
            pcm[i] = 0.25f * prev_s + 0.50f * curr_s + 0.25f * pcm[i + 1];
            prev_s = curr_s;
        }

        // 3. 超軽量 1次 IIR ローパス (カットオフ ≒ 10.5kHz, alpha = 0.58f)
        // 声の芯(〜5kHz)を100%温存し、10kHz超のチリつき・粒子感を完全に蒸発！
        const float alpha_lp = 0.58f;
        float prev_lp = pcm[0];
        for (size_t i = 0; i < num_samples; i++) {
            pcm[i] = prev_lp + alpha_lp * (pcm[i] - prev_lp);
            prev_lp = pcm[i];
        }

        // 4. 固定ゲイン (0.92f)
        for (size_t i = 0; i < num_samples; i++) {
            pcm[i] *= 0.92f;
        }
        return;
    }

    // 通常発話 (UGJY_STYLE_NORMAL): 完璧な高音質を100%維持
    // 1. 低域カット (DCカットHPF)
    const float alpha_hp = 0.9935f;
    float prev_x = state ? state->normal_hp_x : pcm[0];
    float prev_y = state ? state->normal_hp_y : pcm[0];
    for (size_t i = 0; i < num_samples; i++) {
        float x = pcm[i];
        float y = alpha_hp * (prev_y + x - prev_x);
        prev_x = x;
        prev_y = y;
        pcm[i] = y;
    }
    if (state) {
        state->normal_hp_x = prev_x;
        state->normal_hp_y = prev_y;
    }

    // 2. 超高音カット (12kHz 2次LPF)
    const float b0 = 0.292893f, b1 = 0.585786f, b2 = 0.292893f;
    const float a2 = 0.171573f;
    float x1 = state ? state->lpf_x1 : 0.0f;
    float x2 = state ? state->lpf_x2 : 0.0f;
    float y1 = state ? state->lpf_y1 : 0.0f;
    float y2 = state ? state->lpf_y2 : 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float x0 = pcm[i];
        float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a2 * y2;
        x2 = x1; x1 = x0;
        y2 = y1; y1 = y0;
        pcm[i] = y0;
    }
    if (state) {
        state->lpf_x1 = x1; state->lpf_x2 = x2;
        state->lpf_y1 = y1; state->lpf_y2 = y2;
    }

    // 3. 固定ゲイン (0.90f)
    for (size_t i = 0; i < num_samples; i++) {
        pcm[i] *= 0.90f;
    }
}
