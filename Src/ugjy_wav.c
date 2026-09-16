#include "ugjy_wav.h"
#include <stdio.h>

void ugjy_wav_create_header(
    uint8_t header[44],
    size_t num_samples,
    uint32_t sample_rate
) {
    uint32_t data_size = (uint32_t)(num_samples * sizeof(int16_t));
    uint32_t file_size = 36 + data_size;
    uint16_t num_channels = 1;      // モノラル
    uint16_t bits_per_sample = 16;  // 16ビット
    uint16_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = num_channels * (bits_per_sample / 8);

    // RIFFヘッダ
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = (uint8_t)(file_size & 0xFF);
    header[5] = (uint8_t)((file_size >> 8) & 0xFF);
    header[6] = (uint8_t)((file_size >> 16) & 0xFF);
    header[7] = (uint8_t)((file_size >> 24) & 0xFF);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmtチャンク
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0; // fmtチャンクのサイズ
    header[20] = 1; header[21] = 0; // PCMフォーマット
    header[22] = (uint8_t)num_channels; header[23] = 0;
    header[24] = (uint8_t)(sample_rate & 0xFF);
    header[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    header[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    header[27] = (uint8_t)((sample_rate >> 24) & 0xFF);
    header[28] = (uint8_t)(byte_rate & 0xFF);
    header[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    header[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    header[31] = (uint8_t)((byte_rate >> 24) & 0xFF);
    header[32] = (uint8_t)block_align; header[33] = 0;
    header[34] = (uint8_t)bits_per_sample; header[35] = 0;

    // dataチャンク
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = (uint8_t)(data_size & 0xFF);
    header[41] = (uint8_t)((data_size >> 8) & 0xFF);
    header[42] = (uint8_t)((data_size >> 16) & 0xFF);
    header[43] = (uint8_t)((data_size >> 24) & 0xFF);
}

int ugjy_wav_save(
    const char *filename,
    const float *pcm,
    size_t num_samples,
    uint32_t sample_rate
) {
    if (!filename || (!pcm && num_samples > 0)) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) return -2;

    // ヘッダを作成して書き込む
    uint8_t header[44];
    ugjy_wav_create_header(header, num_samples, sample_rate);
    if (fwrite(header, 1, 44, fp) != 44) {
        fclose(fp);
        return -3;
    }

    #define CHUNK_SAMPLES 1024
    int16_t chunk[CHUNK_SAMPLES];
    size_t remaining = num_samples;
    size_t offset = 0;

    while (remaining > 0) {
        size_t batch = (remaining > CHUNK_SAMPLES) ? CHUNK_SAMPLES : remaining;

        for (size_t i = 0; i < batch; i++) {
            float s = pcm[offset + i];

            // クリッピング
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;

            // 16bit PCMに変換
            chunk[i] = (int16_t)(s * 32767.0f);
        }

        if (fwrite(chunk, sizeof(int16_t), batch, fp) != batch) {
            fclose(fp);
            return -4;
        }

        remaining -= batch;
        offset += batch;
    }

    #undef CHUNK_SAMPLES

    fclose(fp);
    return 0;
}
