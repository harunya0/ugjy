#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "ugjy.h"

static uint8_t g_memory_pool[64 * 1024 * 1024]; // 64MB アリーナ

// 高精度ミリ秒タイマー (所要時間・RTF計測用)
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(void) {
    printf("========================================\n");
    printf("  ugjy 高性能ツクヨミちゃん 48kHz 音声合成テスト\n");
    printf("========================================\n");

    const char *model_dir = "models/tsukuyomi-v3-1";
    const char *config_path = "models/tsukuyomi-v3-1/model_config.json";

    // 1. 音声合成エンジンの初期化 (embedder, variance, decoder)
    printf("モデル読み込み中: %s\n", model_dir);
    ugjy_context_t *ctx = ugjy_init(model_dir, g_memory_pool, sizeof(g_memory_pool));
    if (!ctx) {
        fprintf(stderr, "エンジン初期化エラー！\n");
        return 1;
    }

    // 2. G2P（日本語形態素解析・音素・ピッチアクセント推定）の初期化
    printf("G2P辞書読み込み中: %s\n", config_path);
    ugjy_g2p_t *g2p = ugjy_g2p_create(config_path);
    if (!g2p) {
        fprintf(stderr, "G2P初期化エラー！\n");
        ugjy_destroy(ctx);
        return 1;
    }

    int sample_rate = ugjy_get_sample_rate(ctx);
    printf("出力サンプリングレート: %d Hz (HiFi-GAN)\n", sample_rate);

    static float out_pcm[48000 * 30]; // 最大30秒分
    size_t out_samples = 0;

    // ローカルAIの声帯として妥協のないテストケース
    struct {
        const char *text;
        const char *filename;
        const char *desc;
    } test_cases[] = {
        {
            "おはようございます！今日も一日、一緒に頑張りましょうね。",
            "test_tsukuyomi_ohayou.wav",
            "【挨拶】朝の明るい挨拶"
        },
        {
            "ローカルAIの音声対話システム、信じられないほど爆速で動いています！",
            "test_tsukuyomi_local_ai.wav",
            "【漢字・カタカナ】自然な日常会話調"
        },
        {
            "私はツクヨミちゃんです。声の音質やイントネーションはいかがでしょうか？",
            "test_tsukuyomi_self_intro.wav",
            "【自己紹介・疑問文】自然な抑揚とピッチの検証"
        },
    };

    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    for (int i = 0; i < num_tests; i++) {
        printf("\n[%d/%d] %s\n", i + 1, num_tests, test_cases[i].desc);
        printf("  テキスト: \"%s\"\n", test_cases[i].text);

        double t0 = get_time_ms();
        int ret = ugjy_synthesize_text(
            ctx,
            g2p,
            test_cases[i].text,
            "ja",
            NULL, // デフォルトパラメータ (話者ID: 4 つくよみちゃん)
            out_pcm,
            sizeof(out_pcm) / sizeof(out_pcm[0]),
            &out_samples
        );
        double t1 = get_time_ms();

        if (ret == 0 && out_samples > 0) {
            ugjy_write_wav(test_cases[i].filename, out_pcm, out_samples, sample_rate);
            double audio_sec = (double)out_samples / (double)sample_rate;
            double elapsed_ms = t1 - t0;
            double rtf = (elapsed_ms / 1000.0) / audio_sec;

            printf("  保存完了: %s (%.3f 秒 @ %dHz)\n", test_cases[i].filename, audio_sec, sample_rate);
            printf("  処理時間: %.2f ms (RTF: %.3f -> 実時間の %.1f 倍速)\n",
                   elapsed_ms, rtf, 1.0 / rtf);
        } else {
            fprintf(stderr, "  合成失敗 (code: %d)\n", ret);
        }
    }

    printf("\n========================================\n");
    printf("全テスト完了！\n");

    ugjy_g2p_destroy(g2p);
    ugjy_destroy(ctx);
    return 0;
}
