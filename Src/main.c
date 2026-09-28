#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include "ugjy.h"

// 16MB 静的アリーナ (queue インスタンスバッファ + 推論ワーキング領域)
static uint8_t g_memory_pool[16 * 1024 * 1024];

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
    printf("\n  ▶ [発声開始] \"%s\" (待機キュー: %u件, samples=%zu / %.2fs)\n",
           text, ugjy_get_queue_count(app->ctx), num_samples, (double)num_samples / 48000.0);

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

        // 1フレーム 512 サンプル -> (sent + 256) >> 9 で最近傍四捨五入
        size_t frame_idx = (sent + 256) >> 9;
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

    // 1. 通常発話パラメータ（機嫌: 上機嫌 HAPPY, 話速: 1.05）
    ugjy_t params = UGJY_DEFAULT_PARAMS;
    params.speaker_id = 0;
    params.speed = 1.05f;
    params.emotion = UGJY_MOOD_HAPPY;
    params.style = UGJY_STYLE_NORMAL;

    // 非同期キュー発話エンジンを開始
    int ret = ugjy_start(ctx, &params, on_chunk, &app);
    if (ret != UGJY_OK) {
        fprintf(stderr, "ugjy_start に失敗しました: [%d] %s\n", ret, ugjy_error_str(ret));
        if (app.audio_pipe) pclose(app.audio_pipe);
        ugjy_destroy(ctx);
        return 1;
    }

    // LLM トークンストリーム投入（句読点検知で自動キューイング＆非同期発声）
    const char *tokens[] = {
        "こんにちは！",
        "私はAIアシスタントで、",
        "質問に答えるだけでなく、",
        "様々な情報を提供したり、",
        "人間の役立つ情報を探したりもします。"
    };
    int num_tokens = sizeof(tokens) / sizeof(tokens[0]);

    printf("\n>>> 通常発声: トークンストリーム投入開始 <<<\n");
    struct timespec token_delay = {.tv_sec = 0, .tv_nsec = 50000000}; // 50ms
    for (int i = 0; i < num_tokens; i++) {
        printf("  [Token] \"%s\"\n", tokens[i]);
        int feed_ret = ugjy_feed(ctx, tokens[i]);
        if (feed_ret != UGJY_OK) {
            fprintf(stderr, "ugjy_feed エラー: [%d] %s\n", feed_ret, ugjy_error_str(feed_ret));
        }
        nanosleep(&token_delay, NULL);
    }
    ugjy_flush(ctx);

    // 発話終了を待機
    ugjy_wait_idle(ctx);
    printf("\n✨ 通常発話が完了しました。\n");

    // 音声ファイルとしても保存 (Zero-Allocation: 静的バッファを使用)
    static float s_wav_pcm[96000 * 10];
    size_t wav_samples = 0;
    {
        ugjy_t p_save = UGJY_DEFAULT_PARAMS;
        p_save.speaker_id = 0;
        p_save.style = UGJY_STYLE_NORMAL;
        p_save.speed = 1.05f;
        p_save.emotion = UGJY_MOOD_HAPPY;
        if (ugjy_synthesize_text(ctx, "こんにちは！私はAIアシスタントで、質問に答えるだけでなく、様々な情報を提供したり、人間の役立つ情報を探したりもします。", "ja", &p_save, s_wav_pcm, 96000 * 10, &wav_samples) == UGJY_OK) {
            ugjy_write_wav("normal.wav", s_wav_pcm, wav_samples, 48000);
            printf("💾 normal.wav を保存しました (%zu サンプル)\n", wav_samples);
        }
        p_save.style = UGJY_STYLE_WHISPER;
        p_save.speed = 0.95f;
        if (ugjy_synthesize_text(ctx, "内緒のお話だよ…今日も一日、本当にお疲れ様…ふふっ。", "ja", &p_save, s_wav_pcm, 48000 * 10, &wav_samples) == UGJY_OK) {
            ugjy_write_wav("whisper.wav", s_wav_pcm, wav_samples, 48000);
            printf("💾 whisper.wav を保存しました (%zu サンプル)\n", wav_samples);
        }

        // 3. 歌唱 (SVS) デモ: かえるの合唱（ドレミファミレド〜）
        printf("\n>>> 歌唱 (SVS / Singing) テスト <<<\n");
        ugjy_note_t song[] = {
            {.lyric = "か", .key = 60, .duration_ms = 350, .vibrato = 2},
            {.lyric = "え", .key = 62, .duration_ms = 350, .vibrato = 2},
            {.lyric = "る", .key = 64, .duration_ms = 350, .vibrato = 2},
            {.lyric = "の", .key = 65, .duration_ms = 350, .vibrato = 2},
            {.lyric = "う", .key = 64, .duration_ms = 350, .vibrato = 2},
            {.lyric = "た", .key = 62, .duration_ms = 350, .vibrato = 2},
            {.lyric = "が", .key = 60, .duration_ms = 700, .vibrato = 3},
            {.lyric = "き", .key = 64, .duration_ms = 350, .vibrato = 2},
            {.lyric = "こ", .key = 65, .duration_ms = 350, .vibrato = 2},
            {.lyric = "え", .key = 67, .duration_ms = 350, .vibrato = 2},
            {.lyric = "て", .key = 69, .duration_ms = 350, .vibrato = 2},
            {.lyric = "く", .key = 67, .duration_ms = 350, .vibrato = 2},
            {.lyric = "る", .key = 65, .duration_ms = 350, .vibrato = 2},
            {.lyric = "よ", .key = 64, .duration_ms = 700, .vibrato = 3},
        };
        size_t song_notes = sizeof(song) / sizeof(song[0]);
        p_save.style = UGJY_STYLE_SINGING;
        if (ugjy_synthesize_score(ctx, song, song_notes, &p_save, s_wav_pcm, 48000 * 10, &wav_samples) == UGJY_OK) {
            ugjy_write_wav("sing.wav", s_wav_pcm, wav_samples, 48000);
            printf("💾 sing.wav を保存しました (%zu サンプル)\n", wav_samples);
        }
    }

    // クリーンアップ
    ugjy_destroy(ctx);
    if (app.audio_pipe) pclose(app.audio_pipe);

    return 0;
}
