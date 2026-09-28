#include "ugjy_queue.h"
#include "ugjy_error.h"
#include "ugjy.h"
#include <string.h>
#include <time.h>
#include <stdatomic.h>

// リングのインデックスを 1 つ進める (UGJY_QUEUE_SLOTS は 2 の累乗でなくてよい)
static inline uint32_t slot_next(uint32_t i) {
    return (i + 1 >= UGJY_QUEUE_SLOTS) ? 0 : i + 1;
}

// 1 文の処理が終わった (再生済み / 破棄 / 合成失敗) ことを記録する
static inline void finish_one(ugjy_queue_t *q) {
    atomic_fetch_sub(&q->inflight, 1);
}

// FIFO へ 1 文積む。積めた場合だけ inflight を数える
static int enqueue_sentence(ugjy_queue_t *q, const char *sentence) {
    atomic_fetch_add(&q->inflight, 1);
    int ret = ugjy_fifo_push(&q->fifo, sentence);
    if (ret != UGJY_OK) finish_one(q);
    return ret;
}

static void on_sentence_sliced(const char *sentence, void *user_data) {
    ugjy_queue_t *q = (ugjy_queue_t *)user_data;
    if (!q || !sentence || sentence[0] == '\0') return;
    atomic_store(&q->is_interrupted, false);
    enqueue_sentence(q, sentence);
}

// 合成スレッド: 文 FIFO → PCM スロット
//   再生スレッドが 1 文を再生している間に、次の文を先に合成しておく。
static void* synth_thread_fn(void *arg) {
    ugjy_queue_t *q = (ugjy_queue_t *)arg;
    char text[UGJY_FIFO_ITEM_MAX_LEN];

    while (atomic_load(&q->is_running)) {
        // FIFOから 1 文取り出す (空なら待機)
        if (ugjy_fifo_pop(&q->fifo, text, sizeof(text), &q->is_running) != UGJY_OK) break;

        // この文が属する世代。stop されたら世代が進むので、古い文だと分かる
        uint32_t gen = atomic_load(&q->generation);
        if (atomic_load(&q->is_interrupted)) {
            finish_one(q);
            continue;
        }

        // 空きスロットができるまで待つ (満杯なら先読みはここで止まる)
        pthread_mutex_lock(&q->slot_mutex);
        while (q->slot_count >= UGJY_QUEUE_SLOTS &&
               atomic_load(&q->is_running) &&
               gen == atomic_load(&q->generation)) {
            pthread_cond_wait(&q->slot_not_full, &q->slot_mutex);
        }
        bool ok = atomic_load(&q->is_running) && gen == atomic_load(&q->generation);
        uint32_t idx = q->slot_tail;
        pthread_mutex_unlock(&q->slot_mutex);
        if (!ok) {
            finish_one(q);
            continue;
        }

        // 空きスロット (slot_tail) へ直接合成する。
        // slot_count < UGJY_QUEUE_SLOTS の間、tail は再生スレッドが触らないので排他不要。
        ugjy_slot_t *slot = &q->slots[idx];
        size_t samples = 0, visemes = 0;
        int ret = ugjy_synth_process(&q->synth, text, slot->pcm, UGJY_SYNTH_MAX_SAMPLES,
                                     &samples, slot->visemes, UGJY_SYNTH_MAX_VISEMES, &visemes);
        if (ret != UGJY_OK || samples == 0 ||
            gen != atomic_load(&q->generation) || !atomic_load(&q->is_running)) {
            finish_one(q);
            continue;
        }

        strncpy(slot->text, text, sizeof(slot->text) - 1);
        slot->text[sizeof(slot->text) - 1] = '\0';
        slot->num_samples = samples;
        slot->num_visemes = visemes;
        slot->generation  = gen;

        // スロットを再生スレッドへ公開
        pthread_mutex_lock(&q->slot_mutex);
        q->slot_tail = slot_next(q->slot_tail);
        q->slot_count++;
        pthread_cond_signal(&q->slot_not_empty);
        pthread_mutex_unlock(&q->slot_mutex);
    }
    return NULL;
}

// 再生スレッド: PCM スロット → callback (再生は callback が実時間でブロックして行う)
static void* play_thread_fn(void *arg) {
    ugjy_queue_t *q = (ugjy_queue_t *)arg;

    for (;;) {
        pthread_mutex_lock(&q->slot_mutex);
        while (q->slot_count == 0 && atomic_load(&q->is_running)) {
            pthread_cond_wait(&q->slot_not_empty, &q->slot_mutex);
        }
        if (q->slot_count == 0) {   // 停止要求で、残りもない
            pthread_mutex_unlock(&q->slot_mutex);
            break;
        }
        ugjy_slot_t *slot = &q->slots[q->slot_head];
        pthread_mutex_unlock(&q->slot_mutex);

        // stop 後の古いスロットや、終了中のスロットは再生せずに捨てる
        if (atomic_load(&q->is_running) && slot->generation == atomic_load(&q->generation)) {
            atomic_store(&q->is_speaking, true);
            if (q->callback) {
                q->callback(slot->text, slot->pcm, slot->num_samples,
                            slot->visemes, slot->num_visemes, q->user_data);
            }
            atomic_store(&q->is_speaking, false);
        }

        // 再生 (または破棄) が終わってからスロットを空ける
        pthread_mutex_lock(&q->slot_mutex);
        q->slot_head = slot_next(q->slot_head);
        q->slot_count--;
        pthread_cond_signal(&q->slot_not_full);
        pthread_mutex_unlock(&q->slot_mutex);
        finish_one(q);
    }
    return NULL;
}

// 待機中のスレッドをすべて起こす
static void wake_all(ugjy_queue_t *q) {
    ugjy_fifo_wakeup(&q->fifo);
    pthread_mutex_lock(&q->slot_mutex);
    pthread_cond_broadcast(&q->slot_not_empty);
    pthread_cond_broadcast(&q->slot_not_full);
    pthread_mutex_unlock(&q->slot_mutex);
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
    pthread_mutex_init(&q->slot_mutex, NULL);
    pthread_cond_init(&q->slot_not_empty, NULL);
    pthread_cond_init(&q->slot_not_full, NULL);

    atomic_init(&q->is_running, true);
    atomic_init(&q->is_interrupted, false);
    atomic_init(&q->is_speaking, false);
    atomic_init(&q->generation, 0);
    atomic_init(&q->inflight, 0);

    if (pthread_create(&q->synth_thread, NULL, synth_thread_fn, q) != 0) {
        goto fail_nothread;
    }
    if (pthread_create(&q->play_thread, NULL, play_thread_fn, q) != 0) {
        atomic_store(&q->is_running, false);
        wake_all(q);
        pthread_join(q->synth_thread, NULL);
        goto fail_nothread;
    }
    return UGJY_OK;

fail_nothread:
    atomic_store(&q->is_running, false);
    ugjy_fifo_destroy(&q->fifo);
    pthread_mutex_destroy(&q->splitter_mutex);
    pthread_mutex_destroy(&q->slot_mutex);
    pthread_cond_destroy(&q->slot_not_empty);
    pthread_cond_destroy(&q->slot_not_full);
    return UGJY_ERR_QUEUE_THREAD;
}

void ugjy_queue_destroy(ugjy_queue_t *q) {
    if (!q) return;
    atomic_store(&q->is_running, false);
    atomic_store(&q->is_interrupted, true);   // 再生中の callback を中断させる

    wake_all(q);

    pthread_join(q->synth_thread, NULL);
    pthread_join(q->play_thread, NULL);

    ugjy_fifo_destroy(&q->fifo);
    pthread_mutex_destroy(&q->splitter_mutex);
    pthread_mutex_destroy(&q->slot_mutex);
    pthread_cond_destroy(&q->slot_not_empty);
    pthread_cond_destroy(&q->slot_not_full);
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
    atomic_store(&q->is_interrupted, false);
    return enqueue_sentence(q, sentence);
}

int ugjy_queue_stop(ugjy_queue_t *q) {
    if (!q) return 0;
    atomic_store(&q->is_interrupted, true);
    atomic_fetch_add(&q->generation, 1);   // 合成済み・合成中の文を古い世代にする

    pthread_mutex_lock(&q->splitter_mutex);
    ugjy_splitter_clear(&q->splitter);
    pthread_mutex_unlock(&q->splitter_mutex);

    uint32_t cleared = ugjy_fifo_clear(&q->fifo);
    atomic_fetch_sub(&q->inflight, (int)cleared);

    // 空きスロット待ちの合成スレッドを起こして、世代の変化に気づかせる
    pthread_mutex_lock(&q->slot_mutex);
    pthread_cond_broadcast(&q->slot_not_full);
    pthread_mutex_unlock(&q->slot_mutex);

    return (int)cleared;
}

void ugjy_queue_wait_idle(ugjy_queue_t *q) {
    if (!q) return;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 30000000};
    while (atomic_load(&q->inflight) > 0) nanosleep(&ts, NULL);
}

uint32_t ugjy_queue_count(ugjy_queue_t *q) { return q ? ugjy_fifo_count(&q->fifo) : 0; }
bool     ugjy_queue_is_speaking(ugjy_queue_t *q) { return q ? atomic_load(&q->is_speaking) : false; }
bool     ugjy_queue_is_interrupted(ugjy_queue_t *q) { return q ? atomic_load(&q->is_interrupted) : false; }
