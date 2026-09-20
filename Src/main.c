#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ugjy.h"
#include "ugjy_stream.h"

static uint8_t g_memory_pool[64 * 1024 * 1024]; // 64MB アリーナ

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// 口の開閉度（0.0 〜 1.0）を ASCII ゲージで描画
static void render_mouth_gauge(float open, float form) {
    const int bar_width = 15;
    int filled = (int)(open * bar_width);
    if (filled > bar_width) filled = bar_width;
    char bar[32];
    for (int i = 0; i < bar_width; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[bar_width] = '\0';
    // 母音の推定
    const char *vowel = "閉";
    if (open > 0.15f) {
        if (form > 0.5f) vowel = "い";
        else if (form < -0.3f && open < 0.5f) vowel = "う";
        else if (form < -0.3f && open >= 0.5f) vowel = "お";
        else if (open > 0.65f) vowel = "あ";
        else vowel = "え";
    }
    printf("\r    [口開閉] [%s] %3.0f%% (口形: %s)  ", 
           bar, open * 100.0f, vowel);
    fflush(stdout);
}
typedef struct {
    FILE *audio_pipe;
} player_context_t;

static int on_stream_chunk(
    const char          *chunk_text,
    const float         *pcm_chunk,
    size_t               num_samples,
    const ugjy_viseme_t *visemes,
    size_t               num_visemes,
    int                  is_last,
    void                *user_data
) {
    player_context_t *player = (player_context_t *)user_data;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 30000000; // 30ms
    if (chunk_text && pcm_chunk && num_samples > 0) {
        double dur_sec = (double)num_samples / 48000.0;
        printf("\n  ▶ [発声開始] \"%s\" (%.2f秒分)\n", chunk_text, dur_sec);
        // 1. 音声は一瞬でパイプに流す！（これでバッファ不足や音飛びは絶対に起きない）
        if (player->audio_pipe) {
            fwrite(pcm_chunk, sizeof(float), num_samples, player->audio_pipe);
            fflush(player->audio_pipe);
        }
        // 2. 音声がスピーカーから流れている「実時間」に合わせて、口の開閉度をリアルタイム描画！
        if (visemes && num_visemes > 0) {
            double start_t = get_time_sec();
            while (1) {
                double elapsed = get_time_sec() - start_t;
                if (elapsed >= dur_sec) break;
                // 経過時間から現在の口フレーム（1秒間に約93.75フレーム）を割り出す
                size_t frame_idx = (size_t)(elapsed * (48000.0 / 512.0));
                if (frame_idx >= num_visemes) frame_idx = num_visemes - 1;
                // 口の開閉度を描画
                render_mouth_gauge(visemes[frame_idx].mouth_open, visemes[frame_idx].mouth_form);
                // 30fps（約33ミリ秒）周期で更新
                nanosleep(&ts, NULL);
            }
            // 句の終わりは口を閉じる
            render_mouth_gauge(0.0f, 0.0f);
            printf("\n");
        }
    }
    if (is_last) {
        printf("  ✔ [ストリーム完了] すべての発話が終了しました。\n");
    }
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("  ugjy リアルタイム・ストリーミング TTS\n");
    printf("========================================\n");

    const char *model_dir = "models/tsukuyomi-v3-1";
    const char *config_path = "models/tsukuyomi-v3-1/model_config.json";

    ugjy_context_t *ctx = ugjy_init(model_dir, g_memory_pool, sizeof(g_memory_pool));
    if (!ctx) return 1;

    ugjy_g2p_t *g2p = ugjy_g2p_create(config_path);
    if (!g2p) { ugjy_destroy(ctx); return 1; }

    player_context_t player = {
        .audio_pipe = popen("pacat --playback --format=float32le --rate=48000 --channels=1", "w")
    };

    // 音声パラメータ（機嫌: 上機嫌 HAPPY でテスト！）
    ugjy_t params = UGJY_DEFAULT_PARAMS;
    params.emotion = UGJY_MOOD_HAPPY;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 30000000; // 30ms

    ugjy_stream_t stream;
    ugjy_stream_init(&stream, ctx, g2p, &params, on_stream_chunk, &player);

    // LLM からのトークンストリーム
    const char *tokens[] = {
        "こんにちは！",
        "私の", "名前は", "ツクヨミちゃんです！",
        "あなたの", "声や", "表情に合わせて、",
        "リアルタイムで", "おはなし", "できますよ！"
    };
    int num_tokens = sizeof(tokens) / sizeof(tokens[0]);

    printf("  [LLM トークン受信中...]\n");
    for (int i = 0; i < num_tokens; i++) {
        printf("    [Token] \"%s\"\n", tokens[i]);
        ugjy_stream_feed(&stream, tokens[i]);
        nanosleep(&ts, NULL);
    }

    // 文末の掃き出し
    ugjy_stream_flush(&stream);

    if (player.audio_pipe) {
        pclose(player.audio_pipe);
    }
    ugjy_g2p_destroy(g2p);
    ugjy_destroy(ctx);

    return 0;
}
