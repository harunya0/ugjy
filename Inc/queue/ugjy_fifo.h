#ifndef UGJY_FIFO_H
#define UGJY_FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// 2の累乗（ビットマスク & で除算・剰余演算を完全排除）
#define UGJY_FIFO_SHIFT        6                        // 2^6 = 64
#define UGJY_FIFO_CAPACITY     (1 << UGJY_FIFO_SHIFT)   // 64
#define UGJY_FIFO_MASK         (UGJY_FIFO_CAPACITY - 1) // 0x3F (63)
#define UGJY_FIFO_ITEM_MAX_LEN 512

typedef struct {
    char data[UGJY_FIFO_ITEM_MAX_LEN];
} ugjy_fifo_item_t;

// 純粋なスレッドセーフ固定長リングバッファ (malloc完全禁止)
typedef struct {
    ugjy_fifo_item_t items[UGJY_FIFO_CAPACITY];
    uint32_t         head;
    uint32_t         tail;
    uint32_t         count;
    pthread_mutex_t  mutex;
    pthread_cond_t   not_empty;
} ugjy_fifo_t;

#include "ugjy_error.h"

void     ugjy_fifo_init(ugjy_fifo_t *f);
void     ugjy_fifo_destroy(ugjy_fifo_t *f);
int      ugjy_fifo_push(ugjy_fifo_t *f, const char *item);
int      ugjy_fifo_pop(ugjy_fifo_t *f, char *out_item, uint32_t max_len, volatile bool *is_running);
uint32_t ugjy_fifo_clear(ugjy_fifo_t *f);
uint32_t ugjy_fifo_count(ugjy_fifo_t *f);

#ifdef __cplusplus
}
#endif

#endif // UGJY_FIFO_H
