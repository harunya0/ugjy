#ifndef UGJY_H
#define UGJY_H

#include "ugjy_error.h"
#include "ugjy_g2p.h"
#include "model/ugjy_note.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(UGJY_BUILDING_SHARED)
    #define UGJY_API __declspec(dllexport)
  #else
    #define UGJY_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define UGJY_API __attribute__((visibility("default")))
#else
  #define UGJY_API
#endif

// 音声設定パラメータ
typedef struct {
    uint32_t       speaker_id;         // 固定話者ID
    uint32_t       language_id;        // 言語ID (日本語: 0, 英語: 1)
    const float   *speaker_embed;      // 自分の声の特徴量ベクトル（NULLなら speaker_id を使用）
    size_t         embed_dim;          // ベクトルの長さ

    float          speed;              // 話速 (1.0f)
    float          pitch;              // ピッチ (1.0f)
    float          energy;             // 音量 (1.0f)
    float          noise_scale;        // 音色揺らぎ/息成分 (既定: 0.4f)
    float          noise_scale_w;      // 音素長揺らぎ (既定: 0.6f)
    uint8_t        emotion;            // 感情プリセット
    uint8_t        style;              // スタイル
    uint8_t        reserved[2];
    const int64_t *prosody_features;   // 韻律情報 (NULLなら自動推定)
} ugjy_t;

// デフォルト値マクロ
#define UGJY_DEFAULT_PARAMS ((ugjy_t){ \
    .speaker_id = 0, \
    .language_id = 0, \
    .speaker_embed = NULL, \
    .embed_dim = 0, \
    .speed = 1.0f, \
    .pitch = 1.0f, \
    .energy = 1.0f, \
    .noise_scale = 0.4f, \
    .noise_scale_w = 0.6f, \
    .emotion = 0, \
    .style = 0, \
    .reserved = {0}, \
    .prosody_features = NULL \
})

// 発声スタイル
typedef enum {
    UGJY_STYLE_NORMAL  = 0, // 通常会話
    UGJY_STYLE_WHISPER = 1, // ささやき声 (ASMR / ウィスパー)
    UGJY_STYLE_SINGING = 2, // 歌唱 (SVS)
} ugjy_style_t;

// 機嫌・感情プリセット
typedef enum {
    UGJY_MOOD_NORMAL = 0, // 通常
    UGJY_MOOD_HAPPY,      // 上機嫌
    UGJY_MOOD_ANGRY,      // 不機嫌・怒り
    UGJY_MOOD_SAD,        // 悲しい・落ち込み
    UGJY_MOOD_RELAXED,    // まったり・穏やか
} ugjy_mood_t;

// 口の形状データ (Live2D ParamMouthOpenY / ParamMouthForm に 1:1 対応)
typedef struct {
    float mouth_open; // 0.0 (閉) 〜 1.0 (全開 'あ')
    float mouth_form; // -1.0 ('う') 〜 0.0 (標準) 〜 +1.0 ('い')
} ugjy_viseme_t;

// 音声再生コールバック（キューから文が生成されるたびに発火）
typedef int (*ugjy_speech_cb_t)(
    const char          *text,
    const float         *pcm,
    size_t               num_samples,
    const ugjy_viseme_t *visemes,
    size_t               num_visemes,
    void                *user_data
);

// 不透明ポインタ
typedef struct ugjy_context ugjy_context_t;

// ============================================================================
// 1. エンジン初期化・破棄 (全体総括)
// ============================================================================

// モデルとG2Pを一括初期化（malloc完全禁止、渡されたアリーナメモリのみ使用）
UGJY_API ugjy_context_t* ugjy_init(
    const char  *model_path,
    const char  *config_path, // G2P 設定パス (NULLなら models/tsukuyomi-v3-1/model_config.json)
    void        *memory_pool,
    size_t       pool_size
);

// 終了・リソース解放
UGJY_API void ugjy_destroy(ugjy_context_t *ctx);

// ============================================================================
// 2. リアルタイム・キュー＆非同期発話 API (話しかけられた時の停止対応)
// ============================================================================

// バックグラウンド・キューワーカースレッドの開始
UGJY_API int  ugjy_start(
    ugjy_context_t   *ctx,
    const ugjy_t     *params,
    ugjy_speech_cb_t  callback,
    void             *user_data
);

// LLM等のトークンを逐次投入（句読点検知で自動的にキューへ push して発話）
UGJY_API int  ugjy_feed(ugjy_context_t *ctx, const char *token);

// 未完トークンバッファの強制フラッシュ（文末など）
UGJY_API int  ugjy_flush(ugjy_context_t *ctx);

// 1文を直接キューに追加（動的追加）
UGJY_API int  ugjy_push(ugjy_context_t *ctx, const char *sentence);

// ★ 人間に話しかけられた時の即座のTTS停止＆キュー全消去 (Barge-in)
UGJY_API int  ugjy_stop(ugjy_context_t *ctx);

// 待機・状態確認
UGJY_API void     ugjy_wait_idle(ugjy_context_t *ctx);
UGJY_API uint32_t ugjy_get_queue_count(ugjy_context_t *ctx);
UGJY_API bool     ugjy_is_speaking(ugjy_context_t *ctx);
UGJY_API bool     ugjy_is_interrupted(ugjy_context_t *ctx);

// ============================================================================
// 3. 単体同期合成・ユーティリティ API
// ============================================================================

// 生テキストから一発で PCM 波形を直接合成
UGJY_API int ugjy_synthesize_text(
    ugjy_context_t *ctx,
    const char     *text,
    const char     *lang,
    const ugjy_t   *params,
    float          *out_pcm,
    size_t          max_samples,
    size_t         *out_samples
);

// ============================================================================
// 4. 歌唱 (SVS: Singing Voice Synthesis) 公開 API
// ============================================================================

// 楽譜（ノート配列）から PCM 波形を直接合成
UGJY_API int ugjy_synthesize_score(
    ugjy_context_t    *ctx,
    const ugjy_note_t *notes,
    size_t             num_notes,
    const ugjy_t      *params,
    float             *out_pcm,
    size_t             max_samples,
    size_t            *out_samples
);

// 歌詞付きMIDIファイルから直接合成
UGJY_API int ugjy_synthesize_midi(
    ugjy_context_t    *ctx,
    const char        *midi_path,
    int                track_index,
    const ugjy_t      *params,
    float             *out_pcm,
    size_t             max_samples,
    size_t            *out_samples
);

// MIDIファイル ＋ 歌詞文字列（流し込み）から直接合成
UGJY_API int ugjy_synthesize_midi_with_lyrics(
    ugjy_context_t    *ctx,
    const char        *midi_path,
    int                track_index,
    const char        *lyrics_text,
    const ugjy_t      *params,
    float             *out_pcm,
    size_t             max_samples,
    size_t            *out_samples
);

// 非同期発話キューへの歌唱投入（会話とシームレスに混在可能）
UGJY_API int ugjy_push_score(ugjy_context_t *ctx, const ugjy_note_t *notes, size_t num_notes);
UGJY_API int ugjy_push_midi(ugjy_context_t *ctx, const char *midi_path, int track_index);
UGJY_API int ugjy_push_midi_with_lyrics(ugjy_context_t *ctx, const char *midi_path, int track_index, const char *lyrics_text);

// 直前の推論で生成された口パク Viseme 配列を取得
UGJY_API const ugjy_viseme_t* ugjy_get_visemes(ugjy_context_t *ctx, size_t *out_count);

// float PCM 配列を 16-bit WAV ファイルとして保存
UGJY_API int ugjy_write_wav(
    const char  *filepath,
    const float *pcm,
    size_t       num_samples,
    int          sample_rate
);

UGJY_API size_t   ugjy_get_required_memory(const char *model_path);
UGJY_API uint32_t ugjy_get_num_speakers(ugjy_context_t *ctx);
UGJY_API int      ugjy_get_sample_rate(ugjy_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // UGJY_H
