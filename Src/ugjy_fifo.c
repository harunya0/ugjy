#include "ugjy_fifo.h"
#include <string.h>

void ugjy_fifo_init(ugjy_fifo_t *f) {
    if (!f) return;
    f->head = 0;
    f->tail = 0;
    f->count = 0;
    pthread_mutex_init(&f->mutex, NULL);
    pthread_cond_init(&f->not_empty, NULL);
}

void ugjy_fifo_destroy(ugjy_fifo_t *f) {
    if (!f) return;
    pthread_mutex_destroy(&f->mutex);
    pthread_cond_destroy(&f->not_empty);
}

// ビットマスク & 0x3F によるインデックス加算（除算・剰余 % なし）
bool ugjy_fifo_push(ugjy_fifo_t *f, const char *item) {
    if (!f || !item || item[0] == '\0') return false;

    pthread_mutex_lock(&f->mutex);
    if (f->count >= UGJY_FIFO_CAPACITY) {
        pthread_mutex_unlock(&f->mutex);
        return false;
    }

    strncpy(f->items[f->tail].data, item, UGJY_FIFO_ITEM_MAX_LEN - 1);
    f->items[f->tail].data[UGJY_FIFO_ITEM_MAX_LEN - 1] = '\0';
    f->tail = (f->tail + 1) & UGJY_FIFO_MASK;
    f->count++;

    pthread_cond_signal(&f->not_empty);
    pthread_mutex_unlock(&f->mutex);
    return true;
}

// 先頭から取り出して消去 (FIFO)
bool ugjy_fifo_pop(ugjy_fifo_t *f, char *out_item, uint32_t max_len, volatile bool *is_running) {
    if (!f || !out_item || max_len == 0) return false;

    pthread_mutex_lock(&f->mutex);
    while (f->count == 0 && *is_running) {
        pthread_cond_wait(&f->not_empty, &f->mutex);
    }

    if (!*is_running && f->count == 0) {
        pthread_mutex_unlock(&f->mutex);
        return false;
    }

    strncpy(out_item, f->items[f->head].data, max_len - 1);
    out_item[max_len - 1] = '\0';
    f->head = (f->head + 1) & UGJY_FIFO_MASK;
    f->count--;

    pthread_mutex_unlock(&f->mutex);
    return true;
}

// キューの全消去 (O(1))
uint32_t ugjy_fifo_clear(ugjy_fifo_t *f) {
    if (!f) return 0;

    pthread_mutex_lock(&f->mutex);
    uint32_t cleared = f->count;
    f->head = 0;
    f->tail = 0;
    f->count = 0;
    pthread_mutex_unlock(&f->mutex);

    return cleared;
}

uint32_t ugjy_fifo_count(ugjy_fifo_t *f) {
    if (!f) return 0;

    pthread_mutex_lock(&f->mutex);
    uint32_t c = f->count;
    pthread_mutex_unlock(&f->mutex);

    return c;
}
