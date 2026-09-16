#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "ugjy.h"

static uint8_t g_memory_pool[64 * 1024 * 1024]; // 64MB アリーナ

// 高精度ミリ秒タイマー (所要時間計測用)
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main(void) {
    printf("========================================\n");
    printf("  ugjy 自動G2P＋音声合成テスト (つくよみちゃん)\n");
    printf("========================================\n");

    const char *model_path = "models/tsukuyomi-chan-6lang-fp16.onnx";
    const char *config_path = "models/config.json";

    // 音声合成エンジンの初期化
    printf("モデル読み込み中: %s\n", model_path);
    ugjy_context_t *ctx = ugjy_init(model_path, g_memory_pool, sizeof(g_memory_pool));
    if (!ctx) {
        fprintf(stderr, "エンジン初期化エラー！\n");
        return 1;
    }

    // G2P（形態素解析・音素・アクセント推定）エンジンの初期化
    printf("G2P辞書読み込み中: %s\n", config_path);
    ugjy_g2p_t *g2p = ugjy_g2p_create(config_path);
    if (!g2p) {
        fprintf(stderr, "G2P初期化エラー！\n");
        ugjy_destroy(ctx);
        return 1;
    }

    static float out_pcm[22050 * 30]; // 最大30秒分
    size_t out_samples = 0;

    // テストケース (日本語と英語)
    struct {
        const char *text;
        const char *lang;
        const char *filename;
        const char *desc;
    } test_cases[] = {
        {
            "おはようございます！今日も一日、頑張りましょう。",
            "ja",
            "test_auto_ohayou.wav",
            "【日本語】朝の挨拶"
        },
        {
            "ローカルAIの音声対話システム、爆速で完成しそうです！",
            "ja",
            "test_auto_local_ai.wav",
            "【日本語】漢字・カタカナ混じり文"
        },
        {
            "Hello world! I can speak English now. Nice to meet you!",
            "en",
            "test_auto_english.wav",
            "【英語】ツクヨミちゃん英語発声"
        },
    };

    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
    for (int i = 0; i < num_tests; i++) {
        printf("\n[%d/%d] %s\n", i + 1, num_tests, test_cases[i].desc);
        printf("  テキスト: \"%s\"\n", test_cases[i].text);

        // G2P + ONNX推論の合計時間を計測
        double t0 = get_time_ms();
        int ret = ugjy_synthesize_text(
            ctx,
            g2p,
            test_cases[i].text,
            test_cases[i].lang,
            NULL, // デフォルトパラメータ使用
            out_pcm,
            sizeof(out_pcm) / sizeof(out_pcm[0]),
            &out_samples
        );
        double t1 = get_time_ms();

        if (ret == 0 && out_samples > 0) {
            ugjy_write_wav(test_cases[i].filename, out_pcm, out_samples, 22050);
            double audio_sec = (double)out_samples / 22050.0;
            double elapsed_ms = t1 - t0;
            double rtf = (elapsed_ms / 1000.0) / audio_sec;

            printf("  保存完了: %s (%.3f 秒)\n", test_cases[i].filename, audio_sec);
            printf("  処理時間: %.2f ms (RTF: %.3f -> 実時間の %.1f 倍速)\n",
                   elapsed_ms, rtf, 1.0 / rtf);
        } else {
            fprintf(stderr, "  合成失敗 (code: %d)\n", ret);
        }
    }

    printf("\n========================================\n");
    printf("全テスト完了\n");

    ugjy_g2p_destroy(g2p);
    ugjy_destroy(ctx);
    return 0;
}
