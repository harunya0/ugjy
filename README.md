# ugjy

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C11-00599C.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Rust](https://img.shields.io/badge/Rust-2024-DEA584.svg)](https://www.rust-lang.org/)

**ugjy** は、ONNX Runtime 上で動作する **超高速・ゼロアロケーション（Zero-Allocation）** なローカル TTS（テキスト音声合成）エンジンです。

実行時の動的メモリ確保（`malloc` / `free`）を一切行わず、事前に割り当てた **アリーナ（Arena）メモリプール** のみで完結。薄型ノートPC（省電力モバイル CPU）でも **実時間の 20 倍以上（RTF 0.04）** のスピードでネイティブ音声合成を実行します。

---

## 主な特徴

* **完全ゼロアロケーション (Zero Dynamic Allocation)**
  * 推論・波形生成処理中に `malloc` / `free` を一切呼び出しません。
  * アリーナメモリプール駆動により、メモリの断片化やリークの懸念が原理的にゼロ。
* **爆速の推論パフォーマンス**
  * 15W TDP の Intel Core i7-1355U（薄型ノートPC / 内蔵GPUのみ）において、CPU 単体で **実時間の 20〜24 倍速 (RTF: 0.04〜0.05)** を達成。
  * 3秒の音声をわずか **約 120ms** で出力可能。
* **ONNX 入力の完全動的自動判別**
  * ONNX モデルの入力ノードをロード時に自動解析 (`input`, `scales`, `lid`, `prosody_features`, `speaker_embedding`, `sid` 等)。
  * 初代 Piper モデル、多言語 piper-plus、話者埋め込み付きモデル、SVS（歌声合成）モデルまで、エンジン側の再コンパイル不要で透過的に対応。
* **高速多言語 G2P (形態素解析・韻律抽出) 内蔵**
  * Rust で書かれた高速な G2P を C ABI 静的ライブラリ (`libugjy_g2p.a`) として直接リンク。
  * 漢字かな混じりの日本語テキストや英語テキストをそのまま渡すだけで、一発で WAV/PCM を生成。
* **C11 / 組み込み・低遅延指向**
  * `double` を排除し `float` / `int` で統一。
  * ストリーミング音声対話（Whisper.cpp / llama.cpp 連携）への組み込みに最適化。

---

## ベンチマーク

> テスト環境: Intel Core i7-1355U (Base 1.7GHz / Max 5.0GHz, 10コア/12スレッド, TDP 15W), CPU 推論

| テスト内容 | 出力音声長 | 処理時間 (Latency) | RTF (Real-Time Factor) | 生成速度 |
| :--- | :---: | :---: | :---: | :---: |
| **日本語（朝の挨拶）**<br>「おはようございます！今日も一日、頑張りましょう。」 | 2.69 秒 | **124.26 ms** | **0.046** | **21.7 倍速** |
| **日本語（漢字・カタカナ混じり文）**<br>「ローカルAIの音声対話システム、爆速で完成しそうです！」 | 4.13 秒 | **186.82 ms** | **0.045** | **22.1 倍速** |
| **英語（短文）**<br>「Hello world! I can speak English now. Nice to meet you!」 | 2.09 秒 | **103.20 ms** | **0.049** | **20.2 倍速** |

---

## クイックスタート

全自動セットアップスクリプト [`setup.sh`](./setup.sh) を実行するだけで、依存ライブラリの取得・モデルダウンロード・G2P ビルド・CMake コンパイルまでが一発で完了します。

### 前提条件
- `cmake` (3.12 以上)
- `cargo` / Rust ツールチェーン
- `curl`, `tar`, Cコンパイラ (`gcc` または `clang`)

### ビルドと実行

```bash
# 1. リポジトリのクローン
git clone https://github.com/harunya0/ugjy.git
cd ugjy

# 2. 全自動セットアップの実行
./setup.sh

# 3. テスト実行
./build/ugjy
```

実行すると、テスト音声（`test_auto_ohayou.wav` など）がカレントディレクトリに出力されます。

---

## C API の使用例

生テキストを渡して一瞬で PCM 音声バッファを取り出せる高水準 API を提供しています。

```c
#include <stdio.h>
#include "ugjy.h"
#include "ugjy_g2p.h"

// 1. 静的アリーナメモリプールを確保 (ヒープの動的確保は一切禁止)
static uint8_t g_memory_pool[64 * 1024 * 1024]; // 64MB

int main(void) {
    const char *model_path = "models/tsukuyomi-chan-6lang-fp16.onnx";
    const char *config_path = "models/config.json";

    // 2. エンジンと G2P の初期化
    ugjy_context_t *ctx = ugjy_init(model_path, g_memory_pool, sizeof(g_memory_pool));
    ugjy_g2p_t *g2p = ugjy_g2p_create(config_path);

    // 3. テキストから直接音声を合成 (生テキスト -> 音素化 -> 韻律抽出 -> ONNX推論 -> PCM)
    static float out_pcm[22050 * 30]; // 22.05kHz, 最大30秒分
    size_t out_samples = 0;

    ugjy_synthesize_text(
        ctx,
        g2p,
        "ローカルAIの音声対話システム、爆速で完成しそうです！",
        "ja",   // 言語コード ("ja", "en" 等)
        NULL,   // デフォルトパラメータ (話者ID=0, 速度=1.0)
        out_pcm,
        sizeof(out_pcm) / sizeof(out_pcm[0]),
        &out_samples
    );

    // 4. WAV ファイルへ保存 (サンプリングレート: 22050 Hz)
    ugjy_write_wav("output.wav", out_pcm, out_samples, 22050);

    // 5. 解放
    ugjy_g2p_destroy(g2p);
    ugjy_destroy(ctx);
    return 0;
}
```

---

## ディレクトリ構成

```text
ugjy/
├── CMakeLists.txt        # CMake ビルド定義
├── setup.sh              # 全自動セットアップスクリプト
├── README.md             # 本ドキュメント
├── Inc/                  # 公開ヘッダー
│   ├── ugjy.h            # ugjy 高水準 API
│   ├── ugjy_g2p.h        # G2P (Rust FFI) インターフェース
│   ├── ugjy_model.h      # ONNX ランタイム動的入力ラッパー
│   └── ...
├── Src/                  # C 実装コード
│   ├── main.c            # エンドツーエンド検証・ベンチマーク
│   ├── ugjy.c            # 音声合成メインロジック
│   ├── ugjy_model.c      # 動的テンソル解決・ONNX セッション管理
│   └── ...
├── g2p/                  # 高速多言語 G2P クレート (Rust)
│   ├── Cargo.toml
│   ├── src/              # 形態素解析、韻律抽出、FFI ブリッジ
│   └── data/             # 内蔵辞書データ
├── Lib/                  # 外部ライブラリ (*git除外)
│   ├── onnxruntime/      # ONNX Runtime C API
│   └── libugjy_g2p.a     # ビルドされた G2P 静的ライブラリ
└── models/               # 音声モデル (*git除外)
    ├── tsukuyomi-chan-6lang-fp16.onnx
    └── config.json
```

---

## ロードマップ

- [x] **Phase 1: コアエンジン基板完成**
  - [x] Zero-Allocation Arena メモリプール
  - [x] ONNX 入力テンソルの動的自動検出 (Piper / piper-plus)
  - [x] 高速 Rust G2P との C ABI 静的リンク
  - [x] 生テキストからのワンストップ音声合成 (`ugjy_synthesize_text`)
- [ ] **Phase 2: リアルタイム・ストリーミング再生**
  - [ ] `miniaudio` による超低遅延 PCM ストリーミング再生
  - [ ] 句読点・文スライサーによる逐次チャンク合成
- [ ] **Phase 3: ローカル音声対話パイプラインへの統合**
  - [ ] `whisper.cpp` (STT) + `llama.cpp` (LLM) + `ugjy` (TTS) の C/C++ 完全ネイティブ結合
  - [ ] エンドツーエンド Voice-to-Voice 遅延 150〜250ms の実現
- [ ] **Phase 4: 多言語バインディング**
  - [ ] C# (.NET) P/Invoke バインディング
  - [ ] Rust クレートバインディング

---

## ライセンス

MIT License
