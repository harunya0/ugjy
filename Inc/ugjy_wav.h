#ifndef UGJY_WAV_H
#define UGJY_WAV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// float PCMデータを16bitモノラルwavファイルとして保存する
// 0ならば成功、失敗ならエラーコード
int ugjy_wav_save(
    const char *filename,
    const float *pcm,
    size_t num_samples,
    uint32_t sample_rate
);

// 44バイトのwavヘッダを作成する
void ugjy_wav_create_header(
    uint8_t header[44],
    size_t num_samples,
    uint32_t sample_rate
);

#ifdef __cplusplus
}
#endif

#endif // UGJY_WAV_H
