#ifndef UGJY_SPLITTER_H
#define UGJY_SPLITTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define UGJY_SPLITTER_BUF_MAX 2048

// 切り出された1文を受け取るコールバック型
typedef void (*ugjy_sentence_cb_t)(const char *sentence, void *user_data);

// 純粋な句読点検知・ストリーム文スライサー (スレッド/キュー/TTS非依存)
typedef struct {
    char               buf[UGJY_SPLITTER_BUF_MAX];
    uint32_t           len;
    ugjy_sentence_cb_t on_sentence;
    void              *user_data;
} ugjy_splitter_t;

void ugjy_splitter_init(ugjy_splitter_t *s, ugjy_sentence_cb_t on_sentence, void *user_data);
void ugjy_splitter_feed(ugjy_splitter_t *s, const char *token);
void ugjy_splitter_flush(ugjy_splitter_t *s);
void ugjy_splitter_clear(ugjy_splitter_t *s);

#ifdef __cplusplus
}
#endif

#endif // UGJY_SPLITTER_H
