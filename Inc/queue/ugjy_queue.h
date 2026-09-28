#ifndef UGJY_QUEUE_H
#define UGJY_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include "ugjy_fifo.h"
#include "ugjy_splitter.h"
#include "ugjy_synth.h"

// ugjy_speech_cb_t は ugjy.h で定義済み (ugjy_synth.h → ugjy.h 経由で取得)

// 前方宣言
typedef struct ugjy_context ugjy_context_t;

// PCM スロット数。再生中の 1 枠 + 先読み (UGJY_QUEUE_SLOTS - 1) 文ぶん。
// 1 スロット ≒ 1.95MB (480000 サンプル float + viseme)。
// ugjy_context_t の中に丸ごと入るので、増やしたらアリーナ (メモリプール) も増やすこと。
#ifndef UGJY_QUEUE_SLOTS
#define UGJY_QUEUE_SLOTS 2
#endif

// 合成済み 1 文ぶん (合成スレッド → 再生スレッドへの受け渡し単位)
typedef struct {
    char          text[UGJY_FIFO_ITEM_MAX_LEN];
    size_t        num_samples;
    size_t        num_visemes;
    uint32_t      generation;   // 作られた時点の世代 (stop で古くなったら破棄)
    float         pcm[UGJY_SYNTH_MAX_SAMPLES];
    ugjy_viseme_t visemes[UGJY_SYNTH_MAX_VISEMES];
} ugjy_slot_t;

// Queue管理レイヤー構造体
//
//   feed/push → splitter → [文FIFO] → 合成スレッド → [PCMスロット] → 再生スレッド → callback
//
// 注意: model / arena を触るのは合成スレッドだけ。
//       キュー稼働中に ugjy_synthesize_text 等の同期 API を並行して呼ばないこと。
typedef struct ugjy_queue {
    ugjy_fifo_t         fifo;
    ugjy_splitter_t     splitter;
    ugjy_synth_t        synth;
    pthread_mutex_t     splitter_mutex;

    pthread_t           synth_thread;
    pthread_t           play_thread;

    // PCM スロットのリングバッファ (slot_mutex で保護)
    pthread_mutex_t     slot_mutex;
    pthread_cond_t      slot_not_empty;
    pthread_cond_t      slot_not_full;
    uint32_t            slot_head;
    uint32_t            slot_tail;
    uint32_t            slot_count;
    ugjy_slot_t         slots[UGJY_QUEUE_SLOTS];

    atomic_bool         is_running;
    atomic_bool         is_interrupted;
    atomic_bool         is_speaking;
    atomic_uint         generation;   // ugjy_queue_stop() のたびに +1
    atomic_int          inflight;     // 未完了の文の数 (FIFO + 合成中 + スロット + 再生中)

    ugjy_speech_cb_t    callback;
    void               *user_data;
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
