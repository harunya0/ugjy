#ifndef UGJY_ERROR_H
#define UGJY_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ugjy 統合エラーコード体系 (固有値・単一責任)
// ============================================================================
typedef enum {
    UGJY_OK                       = 0,

    // 基本エラー (-1 〜 -9)
    UGJY_ERR_GENERIC              = -1,   // 一般エラー
    UGJY_ERR_INVALID_ARG          = -2,   // 不正な引数
    UGJY_ERR_OUT_OF_MEMORY        = -3,   // アリーナメモリ不足
    UGJY_ERR_FILE_NOT_FOUND       = -4,   // ファイルが見つからない
    UGJY_ERR_IO                   = -5,   // 入出力エラー
    UGJY_ERR_NULL_POINTER         = -6,   // ヌルポインタ参照
    UGJY_ERR_BUFFER_OVERFLOW      = -7,   // バッファ長超過

    // ONNX Runtime レイヤー (-10 〜 -19)
    UGJY_ERR_ONNX_API             = -10,  // ORT API取得失敗
    UGJY_ERR_ONNX_ENV             = -11,  // ORT Env初期化失敗
    UGJY_ERR_ONNX_OPTIONS         = -12,  // ORT SessionOptions設定失敗
    UGJY_ERR_ONNX_SESSION         = -13,  // ORT Session生成失敗
    UGJY_ERR_ONNX_TENSOR          = -14,  // ORT テンソル生成失敗
    UGJY_ERR_ONNX_RUN             = -15,  // ORT 推論実行失敗
    UGJY_ERR_ONNX_DATA            = -16,  // ORT テンソルデータ取得失敗

    // モデル推論レイヤー (-20 〜 -29)
    UGJY_ERR_MODEL_LOAD           = -20,  // モデルファイル読み込み失敗
    UGJY_ERR_MODEL_EMBEDDER       = -21,  // Embedder推論失敗
    UGJY_ERR_MODEL_VARIANCE       = -22,  // Variance推論失敗
    UGJY_ERR_MODEL_REGULATOR      = -23,  // Length Regulator伸張失敗
    UGJY_ERR_MODEL_DECODER        = -24,  // Decoder波形生成推論失敗
    UGJY_ERR_MODEL_NO_TOKENS      = -25,  // トークン列が空

    // G2P 音素変換レイヤー (-30 〜 -39)
    UGJY_ERR_G2P_INIT             = -30,  // G2P初期化/辞書読み込み失敗
    UGJY_ERR_G2P_CONVERT          = -31,  // G2P音素変換失敗

    // キュー / パイプラインレイヤー (-40 〜 -49)
    UGJY_ERR_QUEUE_FULL           = -40,  // FIFOキューが満杯
    UGJY_ERR_QUEUE_EMPTY          = -41,  // FIFOキューが空
    UGJY_ERR_QUEUE_THREAD         = -42,  // ワーカースレッド生成/停止失敗
    UGJY_ERR_QUEUE_INTERRUPTED    = -43,  // 発話割り込み(Barge-in)発生
    UGJY_ERR_QUEUE_SYNTH          = -44,  // 文合成失敗
} ugjy_error_t;

// エラーコードに対応する静的文字列を取得 (printf不要のエラー可視化)
static inline const char* ugjy_error_str(int err) {
    switch (err) {
        case UGJY_OK:                     return "Success";
        case UGJY_ERR_GENERIC:            return "Generic error";
        case UGJY_ERR_INVALID_ARG:        return "Invalid argument";
        case UGJY_ERR_OUT_OF_MEMORY:      return "Arena memory exhausted";
        case UGJY_ERR_FILE_NOT_FOUND:     return "File not found";
        case UGJY_ERR_IO:                 return "I/O error";
        case UGJY_ERR_NULL_POINTER:       return "Null pointer";
        case UGJY_ERR_BUFFER_OVERFLOW:    return "Buffer overflow";
        case UGJY_ERR_ONNX_API:           return "ONNX Runtime API acquisition failed";
        case UGJY_ERR_ONNX_ENV:           return "ONNX Runtime Env creation failed";
        case UGJY_ERR_ONNX_OPTIONS:       return "ONNX Runtime SessionOptions setup failed";
        case UGJY_ERR_ONNX_SESSION:       return "ONNX Runtime Session creation failed";
        case UGJY_ERR_ONNX_TENSOR:        return "ONNX Runtime Tensor creation failed";
        case UGJY_ERR_ONNX_RUN:           return "ONNX Runtime Run execution failed";
        case UGJY_ERR_ONNX_DATA:          return "ONNX Runtime Tensor data access failed";
        case UGJY_ERR_MODEL_LOAD:         return "Model loading failed";
        case UGJY_ERR_MODEL_EMBEDDER:     return "Embedder stage failed";
        case UGJY_ERR_MODEL_VARIANCE:     return "Variance stage failed";
        case UGJY_ERR_MODEL_REGULATOR:    return "Length Regulator stage failed";
        case UGJY_ERR_MODEL_DECODER:      return "Decoder stage failed";
        case UGJY_ERR_MODEL_NO_TOKENS:    return "No input tokens provided";
        case UGJY_ERR_G2P_INIT:           return "G2P engine initialization failed";
        case UGJY_ERR_G2P_CONVERT:        return "G2P conversion failed";
        case UGJY_ERR_QUEUE_FULL:         return "FIFO speech queue is full";
        case UGJY_ERR_QUEUE_EMPTY:        return "FIFO speech queue is empty";
        case UGJY_ERR_QUEUE_THREAD:       return "Queue worker thread operation failed";
        case UGJY_ERR_QUEUE_INTERRUPTED:  return "Speech playback was interrupted";
        case UGJY_ERR_QUEUE_SYNTH:        return "Sentence synthesis failed";
        default:                          return "Unknown error";
    }
}

#ifdef __cplusplus
}
#endif

#endif // UGJY_ERROR_H
