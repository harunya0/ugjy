#include "ugjy_arena.h"
#include "config.h"
#include <string.h>

void ugjy_arena_init(ugjy_arena_t *arena, void *buffer, size_t capacity) {
    if (!arena) return;
    arena->buffer = (uint8_t *)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
    arena->peak_offset = 0;
}

void ugjy_arena_reset(ugjy_arena_t *arena) {
    if (!arena) return;
    arena->offset = 0;
}

void *ugjy_arena_alloc(ugjy_arena_t *arena, size_t size) {
    if(!arena || !arena->buffer || size == 0) return NULL;

    // 現在のアドレスを計算
    uintptr_t current_addr = (uintptr_t)(arena->buffer + arena->offset);

    // 64バイト境界に切り上げ
    uintptr_t aligned_addr = UGJY_ALIGN_UP(current_addr, UGJY_DEFAULT_ALIGNMENT);
    size_t padding = (size_t)(aligned_addr - current_addr);

    if (arena->offset + padding + size > arena->capacity) {
        // メモリ不足
        return NULL;
    }

    // オフセットを更新
    arena->offset += padding + size;

    // 過去最大使用量を更新
    if (arena->offset > arena->peak_offset) {
        arena->peak_offset = arena->offset;
    }

    return (void *)aligned_addr;
}

void *ugjy_arena_alloc_zero(ugjy_arena_t *arena, size_t size) {
    void *ptr = ugjy_arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

size_t ugjy_arena_mark(const ugjy_arena_t *arena) {
    return arena ? arena->offset : 0;
}

void ugjy_arena_restore(ugjy_arena_t *arena, size_t marker) {
    if (!arena) return;
    if (marker <= arena->offset) {
        arena->offset = marker;
    }
}

size_t ugjy_arena_get_used(const ugjy_arena_t *arena) {
    return arena ? arena->offset : 0;
}

size_t ugjy_arena_get_peak(const ugjy_arena_t *arena) {
    return arena ? arena->peak_offset : 0;
}
