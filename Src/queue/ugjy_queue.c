#include "ugjy_queue.h"
#include "ugjy_error.h"
#include "ugjy.h"
#include <string.h>
#include <time.h>

static float g_queue_pcm[UGJY_SYNTH_MAX_SAMPLES];
static ugjy_viseme_t g_queue_visemes[UGJY_SYNTH_MAX_VISEMES];

static void on_sentence_sliced(const char *sentence, void *user_data) {
    ugjy_queue_t *q = (ugjy_queue_t *)user_data;
    if (!q || !sentence || sentence[0] == '\0') return;
    q->is_interrupted = false;
    ugjy_fifo_push(&q->fifo, sentence);
}

static void* queue_worker_thread(void *arg) {
    ugjy_queue_t *q = (ugjy_queue_t *)arg;
    char text[UGJY_FIFO_ITEM_MAX_LEN];

    while (q->is_running) {
        if (ugjy_fifo_pop(&q->fifo, text, sizeof(text), &q->is_running) != UGJY_OK) break;

        q->is_busy = true;
        if (q->is_interrupted) { q->is_busy = false; continue; }

        strncpy(q->current_text, text, sizeof(q->current_text) - 1);
        q->current_text[sizeof(q->current_text) - 1] = '\0';

        size_t samples = 0, visemes = 0;
        int ret = ugjy_synth_process(&q->synth, text, g_queue_pcm, UGJY_SYNTH_MAX_SAMPLES, &samples, g_queue_visemes, UGJY_SYNTH_MAX_VISEMES, &visemes);
        if (ret != UGJY_OK || samples == 0 || q->is_interrupted) { q->is_busy = false; continue; }

        q->is_speaking = true;
        if (q->callback) q->callback(text, g_queue_pcm, samples, g_queue_visemes, visemes, q->user_data);

        q->is_speaking = false;
        q->is_busy = false;
        q->current_text[0] = '\0';
    }
    return NULL;
}

int ugjy_queue_init(ugjy_queue_t *q, ugjy_context_t *ctx, const ugjy_t *params, ugjy_speech_cb_t cb, void *user_data) {
    if (!q || !ctx || !cb) return UGJY_ERR_INVALID_ARG;
    memset(q, 0, sizeof(ugjy_queue_t));
    q->callback = cb;
    q->user_data = user_data;

    ugjy_fifo_init(&q->fifo);
    ugjy_splitter_init(&q->splitter, on_sentence_sliced, q);
    ugjy_synth_init(&q->synth, ctx, params);
    pthread_mutex_init(&q->splitter_mutex, NULL);

    q->is_running = true;
    if (pthread_create(&q->worker_thread, NULL, queue_worker_thread, q) != 0) {
        q->is_running = false;
        ugjy_fifo_destroy(&q->fifo);
        pthread_mutex_destroy(&q->splitter_mutex);
        return UGJY_ERR_QUEUE_THREAD;
    }
    return UGJY_OK;
}

void ugjy_queue_destroy(ugjy_queue_t *q) {
    if (!q) return;
    q->is_running = false;
    q->is_interrupted = true;

    pthread_mutex_lock(&q->fifo.mutex);
    pthread_cond_broadcast(&q->fifo.not_empty);
    pthread_mutex_unlock(&q->fifo.mutex);

    pthread_join(q->worker_thread, NULL);
    ugjy_fifo_destroy(&q->fifo);
    pthread_mutex_destroy(&q->splitter_mutex);
}

int ugjy_queue_feed(ugjy_queue_t *q, const char *token) {
    if (!q || !token || token[0] == '\0') return UGJY_ERR_INVALID_ARG;
    pthread_mutex_lock(&q->splitter_mutex);
    ugjy_splitter_feed(&q->splitter, token);
    pthread_mutex_unlock(&q->splitter_mutex);
    return UGJY_OK;
}

int ugjy_queue_flush(ugjy_queue_t *q) {
    if (!q) return UGJY_ERR_INVALID_ARG;
    pthread_mutex_lock(&q->splitter_mutex);
    ugjy_splitter_flush(&q->splitter);
    pthread_mutex_unlock(&q->splitter_mutex);
    return UGJY_OK;
}

int ugjy_queue_push(ugjy_queue_t *q, const char *sentence) {
    if (!q || !sentence || sentence[0] == '\0') return UGJY_ERR_INVALID_ARG;
    q->is_interrupted = false;
    return ugjy_fifo_push(&q->fifo, sentence);
}

int ugjy_queue_stop(ugjy_queue_t *q) {
    if (!q) return 0;
    q->is_interrupted = true;

    pthread_mutex_lock(&q->splitter_mutex);
    ugjy_splitter_clear(&q->splitter);
    pthread_mutex_unlock(&q->splitter_mutex);

    return (int)ugjy_fifo_clear(&q->fifo);
}

void ugjy_queue_wait_idle(ugjy_queue_t *q) {
    if (!q) return;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 30000000};
    while (ugjy_fifo_count(&q->fifo) > 0 || q->is_busy) nanosleep(&ts, NULL);
}

uint32_t ugjy_queue_count(ugjy_queue_t *q) { return q ? ugjy_fifo_count(&q->fifo) : 0; }
bool ugjy_queue_is_speaking(ugjy_queue_t *q) { return q ? q->is_speaking : false; }
bool ugjy_queue_is_interrupted(ugjy_queue_t *q) { return q ? q->is_interrupted : false; }
