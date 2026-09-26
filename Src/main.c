#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "ugjy.h"

// 2MB 静的アリーナ
static uint8_t g_memory_pool[2 * 1024 * 1024];

typedef struct {
    FILE           *audio_pipe;
    ugjy_context_t *ctx;
} app_context_t;

// 30ms = 1440 サンプル (定数)
#define CHUNK_SAMPLES 1440

// 口の開閉度（0.0 〜 1.0）を ASCII ゲージで描画（母音推定つき）
static void render_mouth_gauge(float open, float form) {
    const int bar_width = 15;
    int filled = (int)(open * (float)bar_width);
    if (filled > bar_width) filled = bar_width;
    if (filled < 0) filled = 0;
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

// 音声再生コールバック（30msチャンク送信＆口パク描画＆中断チェック）
static int on_chunk(
    const char          *text,
    const float         *pcm,
    size_t               num_samples,
    const ugjy_viseme_t *visemes,
    size_t               num_visemes,
    void                *user_data
) {
    app_context_t *app = (app_context_t *)user_data;
    printf("\n  ▶ [発声開始] \"%s\" (待機キュー: %u件)\n", text, ugjy_get_queue_count(app->ctx));

    size_t sent = 0;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 30000000}; // 30ms

    while (sent < num_samples) {
        // ★ 話しかけられたら即時脱出！
        if (ugjy_is_interrupted(app->ctx)) {
            break;
        }

        size_t to_write = num_samples - sent;
        if (to_write > CHUNK_SAMPLES) to_write = CHUNK_SAMPLES;

        if (app->audio_pipe) {
            fwrite(pcm + sent, sizeof(float), to_write, app->audio_pipe);
            fflush(app->audio_pipe);
        }
        sent += to_write;

        // 1フレーム 512 サンプル -> sent >> 9 で除算排除
        size_t frame_idx = sent >> 9;
        if (frame_idx >= num_visemes && num_visemes > 0) frame_idx = num_visemes - 1;

        if (num_visemes > 0) {
            render_mouth_gauge(visemes[frame_idx].mouth_open, visemes[frame_idx].mouth_form);
        }
        nanosleep(&ts, NULL);
    }

    render_mouth_gauge(0.0f, 0.0f);
    printf("\n");
    return 0;
}

int main(void) {
    printf("========================================================\n");
    printf("  ugjy リアルタイムTTS (全体総括アーキテクチャ)\n");
    printf("========================================================\n");

    const char *model_dir = "models/ugjy-v1";
    const char *config_path = "models/ugjy-v1/model_config.json";

    // モデルとG2Pを一括初期化
    ugjy_context_t *ctx = ugjy_init(model_dir, config_path, g_memory_pool, sizeof(g_memory_pool));
    if (!ctx) {
        fprintf(stderr, "ugjy_init に失敗しました\n");
        return 1;
    }

    app_context_t app = {
        .audio_pipe = popen("pacat --playback --format=float32le --rate=48000 --channels=1", "w"),
        .ctx = ctx
    };

    // 音声パラメータ（機嫌: 上機嫌 HAPPY）
    ugjy_t params = UGJY_DEFAULT_PARAMS;
    params.speaker_id = 0;
    params.speed = 1.05f;
    params.emotion = UGJY_MOOD_HAPPY;

    // 非同期キュー発話エンジンを開始
    if (ugjy_start(ctx, &params, on_chunk, &app) != 0) {
        fprintf(stderr, "ugjy_start に失敗しました\n");
        if (app.audio_pipe) pclose(app.audio_pipe);
        ugjy_destroy(ctx);
        return 1;
    }

    // 1. LLM トークンストリーム投入（句読点検知で自動キューイング＆非同期発声）
    const char *tokens[] = {
        "こんにちは！",
        "私の", "名前は", "ツクヨミちゃんです！",
        "よろしくね！"
    };
    int num_tokens = sizeof(tokens) / sizeof(tokens[0]);

    printf("\n>>> トークンストリーム投入開始 <<<\n");
    struct timespec token_delay = {.tv_sec = 0, .tv_nsec = 50000000}; // 50ms
    for (int i = 0; i < num_tokens; i++) {
        printf("  [Token] \"%s\"\n", tokens[i]);
        ugjy_feed(ctx, tokens[i]);
        nanosleep(&token_delay, NULL);
    }
    ugjy_flush(ctx);

    // 発話終了を待機
    ugjy_wait_idle(ctx);
    printf("\n✨ すべての発話が正常に完了しました。\n");

    // クリーンアップ
    ugjy_destroy(ctx);
    if (app.audio_pipe) pclose(app.audio_pipe);

    return 0;
}
