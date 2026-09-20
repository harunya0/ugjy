#include "ugjy_stream.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>


// 1チャンク（1句・最大8秒分）の一時PCMバッファ
// 48000Hz * 8秒 = 384,000 サンプル (約1.5MB)
static float g_stream_pcm_buf[48000 * 8];
static ugjy_viseme_t g_stream_viseme_buf[2048];

// UTF-8 の区切り文字を検出し、その「末尾のオフセット（バイト数）」を返す
// 見つからない場合は 0 を返す
static size_t find_delimiter_end(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        // 1バイト記号: 改行, 感嘆符, 疑問符, ピリオド, カンマ
        if (c == '\n' || c == '\r' || c == '!' || c == '?' || c == '.' || c == ',') {
            return i + 1;
        }
        // 3バイト UTF-8 日本語句読点
        if (i + 2 < len && c == 0xE3 && (unsigned char)buf[i + 1] == 0x80) {
            // E3 80 81: 、
            // E3 80 82: 。
            unsigned char c2 = (unsigned char)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x82) {
                return i + 3;
            }
        }
        // 3バイト 全角！, 全角？
        if (i + 2 < len && c == 0xEF && (unsigned char)buf[i + 1] == 0xBC) {
            // EF BC 81: ！
            // EF BC 9F: ？
            unsigned char c2 = (unsigned char)buf[i + 2];
            if (c2 == 0x81 || c2 == 0x9F) {
                return i + 3;
            }
        }
        // 3バイト 三点リーダー … (E2 80 A6)
        if (i + 2 < len && c == 0xE2 && (unsigned char)buf[i + 1] == 0x80 && (unsigned char)buf[i + 2] == 0xA6) {
            return i + 3;
        }
    }
    return 0;
}

// 1つの句を合成してコールバックを発火
static int synthesize_and_emit(
    ugjy_stream_t *stream,
    const char    *chunk_text,
    int            is_last
) {
    if (!chunk_text || chunk_text[0] == '\0') {
        if (is_last) {
            return stream->callback(NULL, NULL, 0, NULL, 0, 1, stream->user_data);
        }
        return 0;
    }

    size_t out_samples = 0;
    int ret = ugjy_synthesize_text(
        stream->ctx,
        stream->g2p,
        chunk_text,
        "ja",
        &stream->params,
        g_stream_pcm_buf,
        sizeof(g_stream_pcm_buf) / sizeof(g_stream_pcm_buf[0]),
        &out_samples
    );

    if (ret != 0 || out_samples == 0) {
        if (is_last) {
            return stream->callback(NULL, NULL, 0, NULL, 0, 1, stream->user_data);
        }
        return ret;
    }

    if (!is_last) {
        float pause_sec = 0.0f;
        // 句点・感嘆符・改行など (450ms)
        if (strstr(chunk_text, "。") || strstr(chunk_text, "！") || strstr(chunk_text, "？") ||
            strstr(chunk_text, "!") || strstr(chunk_text, "?") || strstr(chunk_text, "\n")) {
            pause_sec = 0.45f;
        }
        // 読点 (250ms)
        else if (strstr(chunk_text, "、") || strstr(chunk_text, ",")) {
            pause_sec = 0.25f;
        }
        size_t pause_samples = (size_t)(pause_sec * 48000.0f);
        if (out_samples + pause_samples < sizeof(g_stream_pcm_buf) / sizeof(g_stream_pcm_buf[0])) {
            memset(g_stream_pcm_buf + out_samples, 0, pause_samples * sizeof(float));
            out_samples += pause_samples;
        }
    }

    // 1. 口パクデータの取得（推論で生成された分）
    size_t num_visemes = 0;
    const ugjy_viseme_t *v = ugjy_get_visemes(stream->ctx, &num_visemes);
    if (v && num_visemes > 0) {
        if (num_visemes > 2048) num_visemes = 2048;
        memcpy(g_stream_viseme_buf, v, num_visemes * sizeof(ugjy_viseme_t));
    }
    // 2. 音声全体の長さ（out_samples）に合わせた総フレーム数を計算 (512サンプル = 1フレーム)
    size_t total_visemes = out_samples / 512;
    if (total_visemes > 2048) total_visemes = 2048;
    // 3. 句読点ポーズ（無音）などで伸びた末尾の余白フレームは、口を閉じる (0.0f)
    for (size_t k = num_visemes; k < total_visemes; k++) {
        g_stream_viseme_buf[k].mouth_open = 0.0f;
        g_stream_viseme_buf[k].mouth_form = 0.0f;
    }
    stream->chunk_count++;
    return stream->callback(chunk_text, g_stream_pcm_buf, out_samples, g_stream_viseme_buf, total_visemes, is_last, stream->user_data);
}

int ugjy_stream_init(
    ugjy_stream_t       *stream,
    ugjy_context_t      *ctx,
    ugjy_g2p_t          *g2p,
    const ugjy_t        *params,
    ugjy_pcm_chunk_cb_t  callback,
    void                *user_data
) {
    if (!stream || !ctx || !g2p || !callback) return -1;

    memset(stream, 0, sizeof(ugjy_stream_t));
    stream->ctx = ctx;
    stream->g2p = g2p;
    stream->params = params ? *params : UGJY_DEFAULT_PARAMS;
    stream->callback = callback;
    stream->user_data = user_data;
    stream->text_len = 0;
    stream->chunk_count = 0;

    return 0;
}

int ugjy_stream_feed(ugjy_stream_t *stream, const char *text_chunk) {
    if (!stream || !text_chunk) return -1;

    size_t chunk_len = strlen(text_chunk);
    if (chunk_len == 0) return 0;

    // バッファに追加
    if (stream->text_len + chunk_len >= sizeof(stream->text_buf)) {
        // バッファがいっぱいになった場合は、強制的にフラッシュして処理する
        ugjy_stream_flush(stream);
    }
    memcpy(stream->text_buf + stream->text_len, text_chunk, chunk_len);
    stream->text_len += chunk_len;
    stream->text_buf[stream->text_len] = '\0';

    // 区切り文字を探す
    while (stream->text_len > 0) {
        size_t delim_end = find_delimiter_end(stream->text_buf, stream->text_len);
        if (delim_end == 0 && stream->text_len >= 120) {
            // 区切り文字が見つからず、かつバッファが120文字以上になった場合は強制的にフラッシュする
            delim_end = 90;
            while (delim_end < stream->text_len && ((unsigned char)stream->text_buf[delim_end] & 0xC0) == 0x80) {
                delim_end++;
            }
        }

        if (delim_end == 0) {
            // 区切り文字が見つからなかった場合は、まだフラッシュせずに待つ
            break;
        }

        // 区切り文字までのテキストを合成してコールバックを発火
        char temp_buf[2048];
        memcpy(temp_buf, stream->text_buf, delim_end);
        temp_buf[delim_end] = '\0';

        // 残りのテキストをバッファに残す
        size_t remaining_len = stream->text_len - delim_end;
        memmove(stream->text_buf, stream->text_buf + delim_end, remaining_len);
        stream->text_len = remaining_len;
        stream->text_buf[stream->text_len] = '\0';

        // 合成してコールバックを発火
        int cb_ret = synthesize_and_emit(stream, temp_buf, 0);
        if (cb_ret != 0) {
            return cb_ret;
        }
    }
    return 0;
}

int ugjy_stream_flush(ugjy_stream_t *stream) {
    if (!stream) return -1;
    
    if (stream->text_len > 0) {
        char temp_buf[2048];
        memcpy(temp_buf, stream->text_buf, stream->text_len);
        temp_buf[stream->text_len] = '\0';
        stream->text_len = 0;

        return synthesize_and_emit(stream, temp_buf, 1);
    }
    // バッファが空の場合、完了通知を送信
    return stream->callback(NULL, NULL, 0, NULL, 0, 1, stream->user_data);
}
