#include "ugjy.h"
#include "ugjy_arena.h"
#include "ugjy_model.h"
#include "ugjy_wav.h"
#include "ugjy_queue.h"
#include "config.h"
#include <string.h>

// 最上位レイヤー構造体 (全体管理)
struct ugjy_context {
    ugjy_model_t model;
    ugjy_arena_t arena;
    ugjy_g2p_t  *g2p;

    // Queue管理レイヤー (発話パイプライン)
    ugjy_queue_t queue;
    bool         queue_active;
};

// ============================================================================
// 1. エンジン初期化・破棄 (全体総括)
// ============================================================================

ugjy_context_t* ugjy_init(
    const char  *model_path,
    const char  *config_path,
    void        *memory_pool,
    size_t       pool_size
) {
    if (!model_path || !memory_pool || pool_size < sizeof(ugjy_context_t)) {
        return NULL;
    }

    // アリーナ初期化
    ugjy_arena_t arena;
    ugjy_arena_init(&arena, memory_pool, pool_size);

    ugjy_context_t *ctx = (ugjy_context_t *)ugjy_arena_alloc(&arena, sizeof(ugjy_context_t));
    if (!ctx) return NULL;

    memset(ctx, 0, sizeof(ugjy_context_t));
    ctx->arena = arena;

    // config からスレッド数取得
    ugjy_config_t cfg = ugjy_config_get();
    int threads = (cfg.num_threads > 0) ? (int)cfg.num_threads : 4;

    // モデルをロード
    if (ugjy_model_load(&ctx->model, model_path, threads) != 0) {
        return NULL;
    }

    // G2P 設定パスの解決
    const char *cfg_path = config_path ? config_path : "models/tsukuyomi-v3-1/model_config.json";
    ctx->g2p = ugjy_g2p_create(cfg_path);
    if (!ctx->g2p) {
        ugjy_model_destroy(&ctx->model);
        return NULL;
    }

    ctx->queue_active = false;
    return ctx;
}

void ugjy_destroy(ugjy_context_t *ctx) {
    if (!ctx) return;

    // Queue管理レイヤーの破棄
    if (ctx->queue_active) {
        ugjy_queue_destroy(&ctx->queue);
        ctx->queue_active = false;
    }

    // G2P・モデル・アリーナの破棄
    if (ctx->g2p) {
        ugjy_g2p_destroy(ctx->g2p);
        ctx->g2p = NULL;
    }

    ugjy_model_destroy(&ctx->model);
    ugjy_arena_reset(&ctx->arena);
}

// ============================================================================
// 2. リアルタイム・キュー＆非同期発話 API (Queue管理レイヤーへの委譲)
// ============================================================================

int ugjy_start(
    ugjy_context_t   *ctx,
    const ugjy_t     *params,
    ugjy_speech_cb_t  callback,
    void             *user_data
) {
    if (!ctx || !callback) return -1;

    // 既に起動中なら再初期化前に一旦破棄
    if (ctx->queue_active) {
        ugjy_queue_destroy(&ctx->queue);
        ctx->queue_active = false;
    }

    int ret = ugjy_queue_init(&ctx->queue, ctx, params, callback, user_data);
    if (ret == 0) {
        ctx->queue_active = true;
    }
    return ret;
}

int ugjy_feed(ugjy_context_t *ctx, const char *token) {
    if (!ctx || !ctx->queue_active) return -1;
    return ugjy_queue_feed(&ctx->queue, token);
}

int ugjy_flush(ugjy_context_t *ctx) {
    if (!ctx || !ctx->queue_active) return -1;
    return ugjy_queue_flush(&ctx->queue);
}

int ugjy_push(ugjy_context_t *ctx, const char *sentence) {
    if (!ctx || !ctx->queue_active) return -1;
    return ugjy_queue_push(&ctx->queue, sentence);
}

int ugjy_stop(ugjy_context_t *ctx) {
    if (!ctx || !ctx->queue_active) return 0;
    return ugjy_queue_stop(&ctx->queue);
}

void ugjy_wait_idle(ugjy_context_t *ctx) {
    if (ctx && ctx->queue_active) {
        ugjy_queue_wait_idle(&ctx->queue);
    }
}

uint32_t ugjy_get_queue_count(ugjy_context_t *ctx) {
    if (!ctx || !ctx->queue_active) return 0;
    return ugjy_queue_count(&ctx->queue);
}

bool ugjy_is_speaking(ugjy_context_t *ctx) {
    if (!ctx || !ctx->queue_active) return false;
    return ugjy_queue_is_speaking(&ctx->queue);
}

bool ugjy_is_interrupted(ugjy_context_t *ctx) {
    if (!ctx || !ctx->queue_active) return false;
    return ugjy_queue_is_interrupted(&ctx->queue);
}

// ============================================================================
// 3. 単体同期合成・ユーティリティ API
// ============================================================================

int ugjy_synthesize_text(
    ugjy_context_t *ctx,
    const char     *text,
    const char     *lang,
    const ugjy_t   *params,
    float          *out_pcm,
    size_t          max_samples,
    size_t         *out_samples
) {
    if (!ctx || !ctx->g2p || !text || !out_pcm || !out_samples) {
        return -1;
    }

    int64_t tokens[512];
    int64_t prosody[512 * 3];
    size_t num_tokens = 0;

    int ret = ugjy_g2p_convert(ctx->g2p, text, lang ? lang : "ja", tokens, prosody, 512, &num_tokens);
    if (ret != 0 || num_tokens == 0) {
        return ret ? ret : -2;
    }

    ugjy_t p = params ? *params : UGJY_DEFAULT_PARAMS;
    p.prosody_features = prosody;
    if (lang && strcmp(lang, "en") == 0) {
        p.language_id = 1;
    } else {
        p.language_id = 0;
    }

    ugjy_model_request_t req = {
        .tokens           = tokens,
        .num_tokens       = num_tokens,
        .speaker_id       = p.speaker_id,
        .prosody_features = p.prosody_features,
        .speed            = (p.speed > 0.0f) ? p.speed : 1.0f,
        .emotion          = p.emotion
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

const ugjy_viseme_t* ugjy_get_visemes(ugjy_context_t *ctx, size_t *out_count) {
    if (!ctx) { if (out_count) *out_count = 0; return NULL; }
    if (out_count) *out_count = ctx->model.num_visemes;
    return ctx->model.visemes;
}

int ugjy_write_wav(
    const char  *filename,
    const float *pcm,
    size_t       num_samples,
    int          sample_rate
) {
    uint32_t rate = (sample_rate > 0) ? (uint32_t)sample_rate : UGJY_SAMPLE_RATE;
    return ugjy_wav_save(filename, pcm, num_samples, rate);
}

size_t ugjy_get_required_memory(const char *model_path) {
    (void)model_path;
    return UGJY_DEFAULT_ARENA_SIZE;
}

uint32_t ugjy_get_num_speakers(ugjy_context_t *ctx) {
    (void)ctx;
    return 5;
}

int ugjy_get_sample_rate(ugjy_context_t *ctx) {
    return ctx ? (int)ctx->model.sample_rate : 24000;
}
