#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// 2のべき乗かチェック
#define UGJY_IS_POW2(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))
// 指定アライメント（2のべき乗）に切り上げ (例: ALIGN_UP(15, 64) -> 64)
#define UGJY_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
// メモリアライメント（AVX-512 / キャッシュラインに合わせた64バイト境界）
#define UGJY_DEFAULT_ALIGNMENT 64

// 単純なビット操作
#define UGJY_BIT_SET(val, mask)    ((val) |= (mask))
#define UGJY_BIT_CLEAR(val, mask)  ((val) &= ~(mask))
#define UGJY_BIT_TOGGLE(val, mask) ((val) ^= (mask))
#define UGJY_BIT_CHECK(val, mask)  (((val) & (mask)) != 0)

// ビットフィールド（パッキング用）：特定の位置(shift)から幅(mask)分を読み書き
// 例: BF_GET(byte, 0, 0x0F) で下位4bit、BF_GET(byte, 4, 0x0F) で上位4bit
#define UGJY_BF_GET(val, shift, mask) (((val) >> (shift)) & (mask))
#define UGJY_BF_SET(val, new_val, shift, mask) \
    ((val) = ((val) & ~((mask) << (shift))) | (((new_val) & (mask)) << (shift)))

typedef enum {
    UGJY_FLAG_NONE          = 0,
    UGJY_FLAG_STREAMING     = (1U << 0), // ストリーミング生成モード
    UGJY_FLAG_NORMALIZE     = (1U << 1), // 音量自動正規化（ノーマライズ）
    UGJY_FLAG_SILENCE_TRIM  = (1U << 2), // 前後の無音カット
    UGJY_FLAG_FAST_INFER    = (1U << 3), // 高速推論優先（低ステップ等）
} ugjy_flags_t;


typedef struct {
    size_t   default_arena_size; // アリーナサイズ (バイト数)
    uint32_t sample_rate;        // サンプリングレート (例: 16000)
    uint32_t max_tokens;         // 1文の最大トークン数
    uint32_t max_audio_sec;      // 最大生成秒数
    uint32_t flags;              // 各種フラグ (ugjy_flags_t)
} ugjy_config_t;

ugjy_config_t ugjy_config_default(void);
ugjy_config_t ugjy_config_get(void);

// 設定を一括変更
void ugjy_config_set(const ugjy_config_t *config);

// 設定を個別変更
void ugjy_config_set_sample_rate(uint32_t sample_rate);
void ugjy_config_set_arena_size(size_t size);
void ugjy_config_set_max_tokens(uint32_t max_tokens);
void ugjy_config_set_max_audio_sec(uint32_t max_audio_sec);
void ugjy_config_set_flags(uint32_t flags);

// 推奨アリーナサイズ
#define UGJY_DEFAULT_ARENA_SIZE (32 * 1024 * 1024)

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H
