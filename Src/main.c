#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "ugjy.h"
#include "ugjy_stream.h"

static uint8_t g_memory_pool[64 * 1024 * 1024]; // 64MB アリーナ

// 出力モードの切り替え
typedef enum {
    STREAM_MODE_PLAY_REALTIME, // 即座にスピーカーから再生
    STREAM_MODE_SAVE_WAV,      // 各チャンクを WAV ファイルとして保存
    STREAM_MODE_BOTH           // 再生しながら WAV も保存
} stream_output_mode_t;

typedef struct {
    int chunk_count;
    stream_output_mode_t mode;
    FILE *audio_pipe; // 即時再生用パイプ
} stream_context_t;

static int on_stream_pcm_chunk(
    const float *pcm_chunk,
    size_t       num_samples,
    int          is_last,
    void        *user_data
) {
    stream_context_t *sctx = (stream_context_t *)user_data;

    if (pcm_chunk && num_samples > 0) {
        sctx->chunk_count++;
        double duration_sec = (double)num_samples / 48000.0;
        printf("    >>> [Chunk #%d] %zu サンプル (%.3f 秒分) 受信！ (is_last=%d)\n",
               sctx->chunk_count, num_samples, duration_sec, is_last);

        // A. 即時再生モード (スピーカーへ直接流し込む)
        if (sctx->mode == STREAM_MODE_PLAY_REALTIME || sctx->mode == STREAM_MODE_BOTH) {
            if (sctx->audio_pipe) {
                fwrite(pcm_chunk, sizeof(float), num_samples, sctx->audio_pipe);
                fflush(sctx->audio_pipe); // 即座にオーディオドライバへフラッシュ
            }
        }

        // B. WAV 保存モード
        if (sctx->mode == STREAM_MODE_SAVE_WAV || sctx->mode == STREAM_MODE_BOTH) {
            char chunk_filename[64];
            snprintf(chunk_filename, sizeof(chunk_filename), "stream_chunk_%d.wav", sctx->chunk_count);
            ugjy_write_wav(chunk_filename, pcm_chunk, num_samples, 48000);
        }
    } else if (is_last) {
        printf("    >>> [Stream EOF] ストリーム完了通知 (is_last=1)\n");
    }
    return 0; // 0: 継続
}

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

        printf("\n========================================\n");
    printf("  リアルタイム・ストリーミング TTS テスト\n");
    printf("========================================\n");

    stream_context_t sctx = {
        .chunk_count = 0,
        // STREAM_MODE_PLAY_REALTIME: スピーカーから即座再生
        // STREAM_MODE_SAVE_WAV:      ファイル保存
        // STREAM_MODE_BOTH:          再生＋保存
        .mode = STREAM_MODE_PLAY_REALTIME,
        .audio_pipe = NULL
    };

    // 48kHz float32 モノラルのリアルタイムオーディオパイプを開く
    // aplay または pw-cat (PipeWire)
    if (sctx.mode == STREAM_MODE_PLAY_REALTIME || sctx.mode == STREAM_MODE_BOTH) {
        sctx.audio_pipe = popen("pacat --playback --format=float32le --rate=48000 --channels=1", "w");
        if (!sctx.audio_pipe) {
            fprintf(stderr, "警告: オーディオ再生パイプを開けませんでした！\n");
        }
    }

    ugjy_stream_t stream;
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 40000000; // 40ms ごとに 1 トークン到着シミュレート

    ugjy_stream_init(&stream, ctx, g2p, NULL, on_stream_pcm_chunk, &sctx);

    const char *simulated_llm_tokens[] = {
        "ローカル", "AIの", "音声対話", "システム、",
        "信じられないほど", "爆速で", "動いています！",
        "質問が", "来たら", "即座に", "声が", "返ってきますよ。"
    };
    int num_tokens_stream = sizeof(simulated_llm_tokens) / sizeof(simulated_llm_tokens[0]);

    printf("  [ストリーミング再生開始] モード: %s\n", 
           sctx.mode == STREAM_MODE_PLAY_REALTIME ? "即時スピーカー再生" :
           (sctx.mode == STREAM_MODE_SAVE_WAV ? "WAV保存" : "再生＋保存"));

    double stream_start = get_time_ms();
    for (int t = 0; t < num_tokens_stream; t++) {
        printf("  [LLM Token Feed] \"%s\"\n", simulated_llm_tokens[t]);
        ugjy_stream_feed(&stream, simulated_llm_tokens[t]);
        nanosleep(&req, NULL);
    }
    // 最後にフラッシュ（文末の掃き出し）
    ugjy_stream_flush(&stream);
    double stream_end = get_time_ms();
    printf("  ストリーミング合成完了！ (全所要時間: %.2f ms)\n", stream_end - stream_start);

    // 再生完了を待ってパイプを閉じる
    if (sctx.audio_pipe) {
        pclose(sctx.audio_pipe);
        sctx.audio_pipe = NULL;
    }

    ugjy_g2p_destroy(g2p);
    ugjy_destroy(ctx);

    return 0;
}
