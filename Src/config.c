#include "config.h"

static const ugjy_config_t DEFAULT_CONFIG = {
    .default_arena_size = UGJY_DEFAULT_ARENA_SIZE,
    .sample_rate = 16000,
    .max_tokens = 512,
    .max_audio_sec = 30,
    .flags = UGJY_FLAG_NONE
};

static ugjy_config_t g_config = {
    .default_arena_size = UGJY_DEFAULT_ARENA_SIZE,
    .sample_rate = 16000,
    .max_tokens = 512,
    .max_audio_sec = 30,
    .flags = UGJY_FLAG_NONE
};

// デフォルト設定の取得
ugjy_config_t ugjy_config_default(void) {
    return DEFAULT_CONFIG;
}

// 現在の設定の取得
ugjy_config_t ugjy_config_get(void) {
    return g_config;
}

// 設定を一括変更
void ugjy_config_set(const ugjy_config_t *config) {
    if (config != NULL) {
        g_config = *config;
    }
}

// 設定を個別変更
void ugjy_config_set_sample_rate(uint32_t sample_rate) {
    if (sample_rate > 0) {
        g_config.sample_rate = sample_rate;
    }
}

void ugjy_config_set_arena_size(size_t size) {
    if (size > 0) {
        g_config.default_arena_size = size;
    }
}

void ugjy_config_set_max_tokens(uint32_t max_tokens) {
    if (max_tokens > 0) {
        g_config.max_tokens = max_tokens;
    }
}

void ugjy_config_set_max_audio_sec(uint32_t max_audio_sec) {
    if (max_audio_sec > 0) {
        g_config.max_audio_sec = max_audio_sec;
    }
}

void ugjy_config_set_flags(uint32_t flags) {
    g_config.flags = flags;
}
