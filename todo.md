# ugjy — 目標仕様案
# 完成
# 次やること：
- [x]テスト
- []イントネーション自動調整

# 以下アーカイブ
## 最重要事項：高性能なTTSを作る、メモリ削減は最悪これの次でもよい
1. 音声品質・別言語（特にC#とrust）からのアクセス
2. 推論性能
3. リアルタイム性
4. メモリ効率
5. バイナリサイズ

### 基本

* **言語:** C
* **推論:** ONNX
* **用途:** ローカルTTS
* **出力:** PCM / WAV
* **OS:** Linux / Windows
* **CPU:** x86-64を第一目標
* **依存:** 最小限

### 音声品質

* 高品質TTSを目標
* モデル側の品質を極力維持
* INT8量子化を基本候補
* 音声品質を犠牲にした過剰な軽量化はしない

### 話者・音声設定

```c
typedef struct {
    uint8_t speaker;
    float speed;
    float pitch;
    float energy;
    uint8_t emotion;
    uint8_t style;
} ugjy_t;
```

みたいな設定構造体を用意。

```c
ugjy_t p = ugjyDefaultP;

p.speaker = 2;
p.speed = 1.1f;
p.pitch = 0.9f;
```

のように**デフォルトを部分的に上書き可能**。

### メモリ

ここが `ugjy` の本丸。

* 動的メモリ確保を一切使用しない
* 静的バッファ / arena方式を基本とする
* 中間バッファを積極的に再利用
* 不要なデータコピーを避ける
* メタデータは可能な限り圧縮
* bit packing / bit maskを積極利用

そして具体的な目標として、

> **通常のTTSランタイムより大幅に少ないRAMで動作する**

を掲げる。

数値目標は実装してから決めてもいい。

### パフォーマンス

* リアルタイム再生可能
* ストリーミング生成対応
* CPU使用率を可能な限り低減
* SIMD最適化を検討
* スレッド並列化を検討

### API

Cから簡単に使えること。

```c
ugjy_init();
ugjy_t p = ugjyDefaultP;
ugjy_synthesize(text, &p);
ugjy_destroy();
```
