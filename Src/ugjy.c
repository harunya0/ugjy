#include "ugjy.h"
#include "ugjy_arena.h"
#include "ugjy_model.h"
#include "ugjy_wav.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

struct ugjy_context {
    ugjy_model_t model;
    ugjy_arena_t arena;
};

ugjy_context_t *ugjy_init(
    const char *model_path,
    void * memory_pool,
    size_t pool_size
) {
    if (!model_path || !memory_pool || pool_size < sizeof(ugjy_context_t)) return NULL;

    // アリーナを初期化
    ugjy_arena_t arena;
    ugjy_arena_init(&arena, memory_pool, pool_size);
    
    // コンテキストをアリーナから割り当て
    ugjy_context_t *ctx = (ugjy_context_t *)ugjy_arena_alloc(&arena, sizeof(ugjy_context_t));
    if (!ctx) return NULL;

    ctx->arena = arena;

    // configからスレッド数を取得
    ugjy_config_t cfg = ugjy_config_get();
    int threads = (cfg.num_threads > 0) ? (int)cfg.num_threads : 4;

    // モデルをロード
    if (ugjy_model_load(&ctx->model, model_path, threads) != 0) {
        return NULL;
    }
    return ctx;
}

int ugjy_synthesize(
    ugjy_context_t *ctx,
    const int64_t *tokens,
    size_t num_tokens,
    const ugjy_t *params,
    float *out_pcm,
    size_t max_samples,
    size_t *out_samples
) {
    if (!ctx || !tokens || num_tokens == 0 || !out_pcm || !out_samples) return -1;
    // パラメータが NULL ならデフォルトを使用
    ugjy_t p = params ? *params : UGJY_DEFAULT_PARAMS;

    // モデルリクエストの組み立て
    ugjy_model_request_t req = {
        .tokens           = tokens,
        .num_tokens       = num_tokens,
        .speaker_id       = p.speaker_id,
        .language_id      = p.language_id,
        .speed            = p.speed,
        .noise_scale      = (p.noise_scale > 0.0f) ? p.noise_scale : 0.4f,
        .noise_scale_w    = (p.noise_scale_w > 0.0f) ? p.noise_scale_w : 0.6f,
        .prosody_features = p.prosody_features,
        .f0_sequence      = NULL, // 通常TTS
        .f0_length        = 0,
        .durations        = NULL,
        .durations_length = 0
    };
    return ugjy_model_infer(
        &ctx->model,
        &ctx->arena,
        &req,
        out_pcm,
        max_samples,
        out_samples
    );
}

size_t ugjy_get_required_memory(const char *model_path) {
    (void)model_path;
    return UGJY_DEFAULT_ARENA_SIZE; // 32MB
}

uint32_t ugjy_get_num_speakers(ugjy_context_t *ctx) {
    return ctx ? ctx->model.num_speakers : 0;
}

int ugjy_write_wav(
    const char *filename,
    const float *pcm,
    size_t num_samples,
    int sample_rate
) {
    uint32_t rate = (sample_rate > 0) ? (uint32_t)sample_rate : UGJY_SAMPLE_RATE;
    return ugjy_wav_save(filename, pcm, num_samples, rate);
}

int ugjy_synthesize_stream(
    ugjy_context_t *ctx,
    const int64_t *tokens,
    size_t num_tokens,
    const ugjy_t *params,
    ugjy_stream_callback_t callback,
    void *user_data
) {
    if (!ctx || !callback) return -1;

    ugjy_config_t cfg = ugjy_config_get();

    // アリーナから一時的にバッファを切り出して合成
    size_t marker = ugjy_arena_mark(&ctx->arena);
    size_t max_s = (size_t)cfg.sample_rate * cfg.max_audio_sec;
    float *pcm_buf = (float *)ugjy_arena_alloc(&ctx->arena, max_s * sizeof(float));
    if (!pcm_buf) {
        ugjy_arena_restore(&ctx->arena, marker);
        return -1; // メモリ不足
    }

    // 合成
    size_t gen_samples = 0;
    int ret = ugjy_synthesize(ctx, tokens, num_tokens, params, pcm_buf, max_s, &gen_samples);
    
    // できた音声をコールバックに渡す
    if (ret == 0 && gen_samples > 0) {
        size_t chunk_size = 1024;
        for (size_t offset = 0; offset < gen_samples; offset += chunk_size) {
            size_t batch = (gen_samples - offset > chunk_size) ? chunk_size : (gen_samples - offset);

            // コールバックが非ゼロを返したら中断
            if (callback(pcm_buf + offset, batch, user_data) != 0) {
                break;
            }
        }
    }

    ugjy_arena_restore(&ctx->arena, marker);
    return ret;
}

int ugjy_synthesize_text(
    ugjy_context_t *ctx,
    ugjy_g2p_t     *g2p,
    const char     *text,
    const char     *lang,
    const ugjy_t   *params,
    float          *out_pcm,
    size_t          max_samples,
    size_t         *out_samples
) {
    if (!ctx || !g2p || !text || !out_pcm || !out_samples) {
        return -1;
    }

    int64_t tokens[512];
    int64_t prosody[512 * 3];
    size_t num_tokens = 0;

    // テキストからトークンと韻律を自動生成
    int ret = ugjy_g2p_convert(g2p, text, lang, tokens, prosody, 512, &num_tokens);
    if (ret != 0 || num_tokens == 0) {
        return ret ? ret : -2;
    }

    // 言語IDと韻律を設定
    ugjy_t p = params ? *params : UGJY_DEFAULT_PARAMS;
    p.prosody_features = prosody;
    if (lang && strcmp(lang, "en") == 0) {
        p.language_id = 1;
    } else {
        p.language_id = 0; // "ja"
    }

    // ONNX 推論
    return ugjy_synthesize(ctx, tokens, num_tokens, &p, out_pcm, max_samples, out_samples);
}

void ugjy_destroy(ugjy_context_t *ctx) {
    if (!ctx) return;
    ugjy_model_destroy(&ctx->model);
    ugjy_arena_reset(&ctx->arena);
}
