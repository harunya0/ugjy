#ifndef UGJY_ARENA_H
#define UGJY_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buffer;       // メモリプールの先頭アドレス
    size_t   capacity;     // 全体サイズ
    size_t   offset;       // 現在の使切り出し位置
    size_t   peak_offset;  // 過去最大の使用量
} ugjy_arena_t;

// アリーナの初期化
void ugjy_arena_init(ugjy_arena_t *arena, void *buffer, size_t size);

// 一括解放
void ugjy_arena_reset(ugjy_arena_t *arena);

// 64バイトアライメントで切り出し
void *ugjy_arena_alloc(ugjy_arena_t *arena, size_t size);

// ゼロクリアで切り出し
void *ugjy_arena_alloc_zero(ugjy_arena_t *arena, size_t size);

// 現在位置のしおりを記憶
size_t ugjy_arena_mark(const ugjy_arena_t *arena);

// しおりまで巻き戻す
void ugjy_arena_restore(ugjy_arena_t *arena, size_t marker);

// 現在の使用量を取得
size_t ugjy_arena_get_used(const ugjy_arena_t *arena);

// 過去最大の使用量を取得
size_t ugjy_arena_get_peak(const ugjy_arena_t *arena);

#ifdef __cplusplus
}
#endif


#endif // UGJY_ARENA_H
