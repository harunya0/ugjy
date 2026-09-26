#include "ugjy_splitter.h"
#include <string.h>

// UTF-8 句読点検知（ビットマスク・高速判定）
static size_t find_delimiter_end(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        // 1バイト記号: 改行, 感嘆符, 疑問符, ピリオド, カンマ
        if (c == '\n' || c == '\r' || c == '!' || c == '?' || c == '.' || c == ',') {
            return i + 1;
        }
        // 3バイト UTF-8 日本語句読点: 「、」(E3 80 81) / 「。」(E3 80 82)
        if (i + 2 < len && c == 0xE3 && (uint8_t)buf[i + 1] == 0x80) {
            uint8_t c2 = (uint8_t)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x82) return i + 3;
        }
        // 3バイト 全角「！」(EF BC 81) / 「？」(EF BC 9F)
        if (i + 2 < len && c == 0xEF && (uint8_t)buf[i + 1] == 0xBC) {
            uint8_t c2 = (uint8_t)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x9F) return i + 3;
        }
        // 3バイト 三点リーダー「…」(E2 80 A6)
        if (i + 2 < len && c == 0xE2 && (uint8_t)buf[i + 1] == 0x80 && (uint8_t)buf[i + 2] == 0xA6) {
            return i + 3;
        }
    }
    return 0;
}

void ugjy_splitter_init(ugjy_splitter_t *s, ugjy_sentence_cb_t on_sentence, void *user_data) {
    if (!s) return;
    s->buf[0] = '\0';
    s->len = 0;
    s->on_sentence = on_sentence;
    s->user_data = user_data;
}

void ugjy_splitter_feed(ugjy_splitter_t *s, const char *token) {
    if (!s || !token || token[0] == '\0') return;

    size_t token_len = strlen(token);

    // バッファ溢れ防止
    if (s->len + token_len >= sizeof(s->buf) - 1) {
        if (s->len > 0 && s->on_sentence) {
            s->on_sentence(s->buf, s->user_data);
            s->len = 0;
            s->buf[0] = '\0';
        }
    }

    strncat(s->buf, token, sizeof(s->buf) - s->len - 1);
    s->len += (uint32_t)token_len;

    // 句読点検知ループ
    while (s->len > 0) {
        size_t delim_end = find_delimiter_end(s->buf, s->len);
        if (delim_end == 0 && s->len >= 120) {
            delim_end = 90;
            // UTF-8 の境界判定 (上位2ビットが 10 = 0x80 は後続バイト)
            while (delim_end < s->len && ((uint8_t)s->buf[delim_end] & 0xC0) == 0x80) {
                delim_end++;
            }
        }

        if (delim_end == 0) {
            break;
        }

        char sentence[1024];
        if (delim_end >= sizeof(sentence)) delim_end = sizeof(sentence) - 1;
        memcpy(sentence, s->buf, delim_end);
        sentence[delim_end] = '\0';

        size_t rem = s->len - delim_end;
        memmove(s->buf, s->buf + delim_end, rem);
        s->len = (uint32_t)rem;
        s->buf[s->len] = '\0';

        if (s->on_sentence) {
            s->on_sentence(sentence, s->user_data);
        }
    }
}

void ugjy_splitter_flush(ugjy_splitter_t *s) {
    if (!s) return;
    if (s->len > 0 && s->on_sentence) {
        s->on_sentence(s->buf, s->user_data);
        s->len = 0;
        s->buf[0] = '\0';
    }
}

void ugjy_splitter_clear(ugjy_splitter_t *s) {
    if (!s) return;
    s->len = 0;
    s->buf[0] = '\0';
}
