#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "  ugjy セットアップスクリプト"
echo "=========================================="

# 1. 依存ツールの確認
for cmd in cmake cargo curl tar; do
    if ! command -v "$cmd" &> /dev/null; then
        echo "エラー: '$cmd' がインストールされていません。" >&2
        exit 1
    fi
done

if ! command -v pacat &> /dev/null; then
    echo "警告: 'pacat' がインストールされていません。" >&2
    echo "  スピーカーからリアルタイム再生を行うには 'sudo apt install -y pulseaudio-utils' を実行してください。" >&2
fi

# ディレクトリ作成
mkdir -p Lib models build

# 2. ONNX Runtime のセットアップ (1.19.0)
ORT_VERSION="1.19.0"
ORT_DIR="Lib/onnxruntime"
if [ ! -f "$ORT_DIR/lib/libonnxruntime.so" ]; then
    echo "[1/5] ONNX Runtime v${ORT_VERSION} をダウンロード中..."
    ORT_TAR="onnxruntime-linux-x64-${ORT_VERSION}.tgz"
    curl -L -O "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_TAR}"
    tar -xzf "$ORT_TAR"
    rm -rf "$ORT_DIR"
    mv "onnxruntime-linux-x64-${ORT_VERSION}" "$ORT_DIR"
    rm -f "$ORT_TAR"
    echo "  ONNX Runtime セットアップ完了。"
else
    echo "[1/5] ONNX Runtime は既に存在します。"
fi

# 3. 音声合成モデルのセットアップ
# 3-1. つくよみちゃん 48kHz HiFi-GAN (SHAREVOX v3-1)
TSUKUYOMI_V3_DIR="models/tsukuyomi-v3-1"
SHAREVOX_SRC="models/SHAREVOX/SHAREVOX.app/Contents/MacOS/model/official-v3-1"

if [ ! -f "$TSUKUYOMI_V3_DIR/embedder_model.onnx" ]; then
    echo "[2/5] ツクヨミちゃん 48kHz モデル (v3-1) をセットアップ中..."
    if [ -d "$SHAREVOX_SRC" ]; then
        ln -sfn "SHAREVOX/SHAREVOX.app/Contents/MacOS/model/official-v3-1" "$TSUKUYOMI_V3_DIR"
        echo "  SHAREVOX official-v3-1 からリンクを作成しました: $TSUKUYOMI_V3_DIR"
    elif command -v unzip &> /dev/null || command -v python3 &> /dev/null; then
        echo "  SHAREVOX リリースからモデルを取得中..."
        TMP_ZIP="models/sharevox_tmp.zip"
        curl -L -o "$TMP_ZIP" "https://github.com/SHAREVOX/sharevox/releases/download/0.2.1/sharevox-windows-cpu-0.2.1.zip"
        if command -v unzip &> /dev/null; then
            unzip -q -o "$TMP_ZIP" "model/official-v3-1/*" -d "models"
        else
            python3 -c "import zipfile; z = zipfile.ZipFile('$TMP_ZIP'); [z.extract(m, 'models') for m in z.namelist() if m.startswith('model/official-v3-1/')]"
        fi
        mkdir -p "$TSUKUYOMI_V3_DIR"
        mv models/model/official-v3-1/* "$TSUKUYOMI_V3_DIR/"
        rm -rf models/model "$TMP_ZIP"
        echo "  ツクヨミちゃん 48kHz モデル配置完了。"
    else
        echo "  警告: $SHAREVOX_SRC が見つからず、unzip / python3 も利用できないため 48kHz モデルを自動展開できませんでした。" >&2
    fi
else
    echo "[2/5] ツクヨミちゃん 48kHz モデル (v3-1) は既に存在します。"
fi

# 3-2. つくよみちゃん 6lang (Piper 多言語モデル) のセットアップ (フォールバック用)
MODEL_FILE="models/tsukuyomi-chan-6lang-fp16.onnx"
CONFIG_FILE="models/config.json"
if [ ! -f "$MODEL_FILE" ] || [ ! -f "$CONFIG_FILE" ]; then
    echo "  Piper 多言語モデルをダウンロード中..."
    if [ ! -f "$MODEL_FILE" ]; then
        curl -L -o "$MODEL_FILE" "https://huggingface.co/ayousanz/piper-plus-tsukuyomi-chan/resolve/main/tsukuyomi-chan-6lang-fp16.onnx"
    fi
    if [ ! -f "$CONFIG_FILE" ]; then
        curl -L -o "$CONFIG_FILE" "https://huggingface.co/ayousanz/piper-plus-tsukuyomi-chan/resolve/main/config.json"
    fi
    echo "  Piper モデルダウンロード完了。"
fi

# 4. G2P 静的ライブラリ (libugjy_g2p.a) のビルド
G2P_LIB="Lib/libugjy_g2p.a"
echo "[3/5] G2P 静的ライブラリをビルド中 (Rust)..."
cargo build --manifest-path g2p/Cargo.toml --release
cp g2p/target/release/libugjy_g2p.a "$G2P_LIB"
echo "  G2P ライブラリ配置完了: $G2P_LIB"

# 5. CMake ビルドの構成
echo "[4/5] CMake ビルドを実行中..."
cmake -B build -S .
cmake --build build

# 6. 音声再生コマンドの確認
echo "[5/5] 再生環境の確認..."
if command -v pw-play &>/dev/null || command -v pw-cat &>/dev/null; then
    echo "  PipeWire 検出: OK"
elif command -v aplay &>/dev/null; then
    echo "  ALSA (aplay) 検出: OK"
elif command -v pacat &>/dev/null; then
    echo "  PulseAudio (pacat) 検出: OK"
else
    echo "  ヒント: 音声再生ツール (pw-cat / aplay / pacat) が見つかりません。"
    echo "  スピーカーから再生する場合は 'sudo apt install alsa-utils' または 'pulseaudio-utils' を推奨します。"
fi

echo "=========================================="
echo "  セットアップ完了！"
echo "  実行方法: ./build/ugjy"
echo "=========================================="
