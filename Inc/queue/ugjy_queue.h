#ifndef UGJY_QUEUE_H
#define UGJY_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "ugjy_fifo.h"
#include "ugjy_splitter.h"
#include "ugjy_synth.h"

// 前方宣言
typedef struct ugjy_context ugjy_context_t;

typedef int (*ugjy_speech_cb_t)(
    const char          *text,
    const float         *pcm,
    size_t               num_samples,
    const ugjy_viseme_t *visemes,
    size_t               num_visemes,
    void                *user_data
);

// Queue管理レイヤー構造体
typedef struct ugjy_queue {
    ugjy_fifo_t         fifo;
    ugjy_splitter_t     splitter;
    ugjy_synth_t        synth;
    pthread_mutex_t     splitter_mutex;

    pthread_t           worker_thread;
    volatile bool       is_running;
    volatile bool       is_interrupted;
    volatile bool       is_speaking;
    volatile bool       is_busy;

    ugjy_speech_cb_t    callback;
    void               *user_data;
    char                current_text[UGJY_FIFO_ITEM_MAX_LEN];
} ugjy_queue_t;

int      ugjy_queue_init(ugjy_queue_t *q, ugjy_context_t *ctx, const ugjy_t *params, ugjy_speech_cb_t cb, void *user_data);
void     ugjy_queue_destroy(ugjy_queue_t *q);
int      ugjy_queue_feed(ugjy_queue_t *q, const char *token);
int      ugjy_queue_flush(ugjy_queue_t *q);
int      ugjy_queue_push(ugjy_queue_t *q, const char *sentence);
int      ugjy_queue_stop(ugjy_queue_t *q);
void     ugjy_queue_wait_idle(ugjy_queue_t *q);
uint32_t ugjy_queue_count(ugjy_queue_t *q);
bool     ugjy_queue_is_speaking(ugjy_queue_t *q);
bool     ugjy_queue_is_interrupted(ugjy_queue_t *q);

#endif // UGJY_QUEUE_H
