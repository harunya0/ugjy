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

# ディレクトリ作成
mkdir -p Lib models build

# 2. ONNX Runtime のセットアップ (1.19.0)
ORT_VERSION="1.19.0"
ORT_DIR="Lib/onnxruntime"
if [ ! -f "$ORT_DIR/lib/libonnxruntime.so" ]; then
    echo "[1/4] ONNX Runtime v${ORT_VERSION} をダウンロード中..."
    ORT_TAR="onnxruntime-linux-x64-${ORT_VERSION}.tgz"
    curl -L -O "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${ORT_TAR}"
    tar -xzf "$ORT_TAR"
    rm -rf "$ORT_DIR"
    mv "onnxruntime-linux-x64-${ORT_VERSION}" "$ORT_DIR"
    rm -f "$ORT_TAR"
    echo "  ONNX Runtime セットアップ完了。"
else
    echo "[1/4] ONNX Runtime は既に存在します。"
fi

# 3. 音声合成モデル (つくよみちゃん 6lang) のセットアップ
MODEL_FILE="models/tsukuyomi-chan-6lang-fp16.onnx"
CONFIG_FILE="models/config.json"
if [ ! -f "$MODEL_FILE" ] || [ ! -f "$CONFIG_FILE" ]; then
    echo "[2/4] つくよみちゃん音声モデルをダウンロード中..."
    if [ ! -f "$MODEL_FILE" ]; then
        curl -L -o "$MODEL_FILE" "https://huggingface.co/ayousanz/piper-plus-tsukuyomi-chan/resolve/main/tsukuyomi-chan-6lang-fp16.onnx"
    fi
    if [ ! -f "$CONFIG_FILE" ]; then
        curl -L -o "$CONFIG_FILE" "https://huggingface.co/ayousanz/piper-plus-tsukuyomi-chan/resolve/main/config.json"
    fi
    echo "  モデルダウンロード完了。"
else
    echo "[2/4] 音声モデルは既に存在します。"
fi

# 4. G2P 静的ライブラリ (libugjy_g2p.a) のビルド
G2P_LIB="Lib/libugjy_g2p.a"
if [ ! -f "$G2P_LIB" ]; then
    echo "[3/4] G2P 静的ライブラリをビルド中 (Rust)..."
    cargo build --manifest-path g2p/Cargo.toml --release
    cp g2p/target/release/libugjy_g2p.a "$G2P_LIB"
    echo "  G2P ライブラリ配置完了: $G2P_LIB"
else
    echo "[3/4] G2P ライブラリは既に存在します。"
fi

# 5. CMake ビルドの構成
echo "[4/4] CMake ビルドを実行中..."
cmake -B build -S .
cmake --build build

echo "=========================================="
echo "  セットアップ完了！"
echo "  実行方法: ./build/ugjy"
echo "=========================================="
