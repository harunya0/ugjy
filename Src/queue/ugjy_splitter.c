#include "ugjy_splitter.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// 発音可能な文字（ASCII英数字、または全角文字等）を含むか判定
static bool has_pronounceable_chars(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)str[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '!' || c == '?' || c == '.' || c == ',' || c == '-' || c == '~') {
            continue;
        }
        // 全角記号の除外チェック: 「！」(EF BC 81) / 「？」(EF BC 9F) / 「。」(E3 80 82) / 「、」(E3 80 81) / 「…」(E2 80 A6) / 全角空白 (E3 80 80)
        if (i + 2 < len) {
            if (c == 0xEF && (uint8_t)str[i+1] == 0xBC && ((uint8_t)str[i+2] == 0x81 || (uint8_t)str[i+2] == 0x9F)) {
                i += 2; continue;
            }
            if (c == 0xE3 && (uint8_t)str[i+1] == 0x80 && ((uint8_t)str[i+2] == 0x80 || (uint8_t)str[i+2] == 0x81 || (uint8_t)str[i+2] == 0x82)) {
                i += 2; continue;
            }
            if (c == 0xE2 && (uint8_t)str[i+1] == 0x80 && (uint8_t)str[i+2] == 0xA6) {
                i += 2; continue;
            }
        }
        return true; // 発音可能なテキストが存在
    }
    return false;
}

// 連続する記号・空白を同一の文末としてまとめて消費
static size_t consume_trailing_delimiters(const char *buf, size_t len, size_t start) {
    size_t i = start;
    while (i < len) {
        uint8_t c = (uint8_t)buf[i];
        if (c == '!' || c == '?' || c == '.' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            i++;
            continue;
        }
        if (i + 2 < len) {
            // 全角 「！」/「？」
            if (c == 0xEF && (uint8_t)buf[i+1] == 0xBC && ((uint8_t)buf[i+2] == 0x81 || (uint8_t)buf[i+2] == 0x9F)) {
                i += 3;
                continue;
            }
            // 全角 「。」/「、」/ 全角空白
            if (c == 0xE3 && (uint8_t)buf[i+1] == 0x80 && ((uint8_t)buf[i+2] == 0x80 || (uint8_t)buf[i+2] == 0x81 || (uint8_t)buf[i+2] == 0x82)) {
                i += 3;
                continue;
            }
            // 三点リーダー
            if (c == 0xE2 && (uint8_t)buf[i+1] == 0x80 && (uint8_t)buf[i+2] == 0xA6) {
                i += 3;
                continue;
            }
        }
        break;
    }
    return i;
}

// UTF-8 句読点検知（句点・読点・感嘆符・改行等で即座に切り出し）
static size_t find_delimiter_end(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)buf[i];
        // 1バイト 区切り: 改行, 感嘆符, 疑問符, ピリオド, カンマ（即切り）
        if (c == '\n' || c == '\r' || c == '!' || c == '?' || c == '.' || c == ',') {
            return consume_trailing_delimiters(buf, len, i + 1);
        }
        // 3バイト UTF-8 日本語句読点: 「、」(E3 80 81) / 「。」(E3 80 82)（即切り）
        if (i + 2 < len && c == 0xE3 && (uint8_t)buf[i + 1] == 0x80) {
            uint8_t c2 = (uint8_t)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x82) {
                return consume_trailing_delimiters(buf, len, i + 3);
            }
        }
        // 3バイト 全角「！」(EF BC 81) / 「？」(EF BC 9F)
        if (i + 2 < len && c == 0xEF && (uint8_t)buf[i + 1] == 0xBC) {
            uint8_t c2 = (uint8_t)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x9F) {
                return consume_trailing_delimiters(buf, len, i + 3);
            }
        }
        // 3バイト 三点リーダー「…」(E2 80 A6)
        if (i + 2 < len && c == 0xE2 && (uint8_t)buf[i + 1] == 0x80 && (uint8_t)buf[i + 2] == 0xA6) {
            return consume_trailing_delimiters(buf, len, i + 3);
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
    s->len = (uint32_t)strlen(s->buf);

    // 句読点検知ループ
    while (s->len > 0) {
        size_t delim_end = find_delimiter_end(s->buf, s->len);
        if (delim_end == 0 && s->len >= 300) {
            delim_end = 240;
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

        // 発音可能な文字を含んでいる場合のみキューに投入（記号だけの空文を破棄）
        if (s->on_sentence && has_pronounceable_chars(sentence, delim_end)) {
            s->on_sentence(sentence, s->user_data);
        }
    }
}

void ugjy_splitter_flush(ugjy_splitter_t *s) {
    if (!s) return;
    if (s->len > 0 && s->on_sentence) {
        if (has_pronounceable_chars(s->buf, s->len)) {
            s->on_sentence(s->buf, s->user_data);
        }
        s->len = 0;
        s->buf[0] = '\0';
    }
}

void ugjy_splitter_clear(ugjy_splitter_t *s) {
    if (!s) return;
    s->len = 0;
    s->buf[0] = '\0';
}
