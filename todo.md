## 確認するもの(ストリームの途切れ)

- [x] `main.c` の `on_chunk` の先頭に `printf("samples=%zu (%.2fs)\n", num_samples, num_samples / 48000.0);` を入れて実行する。（反映済）
- [x] `grep -n "SAMPLES\|48000\|pcm" Src/queue/*.c Inc/queue/*.h` で、`ugjy_synth_process` に渡している `max_pcm_samples` を探す。（`UGJY_SYNTH_MAX_SAMPLES` が 4秒分=192000 だった）
- [x] 切れる位置を確認する。どのチャンクの途中で切れるのか、最後のチャンクの終わりだけなのか。->さいごの「様々な情報を提供したり、人間の役立つ情報を探したりも」できれる
よってバッファが原因

## 直すもの

- [x] キュー側の1文あたりの PCM バッファを増やす。最長チャンクの想定は約5.4秒+ポーズなので、余裕をみて10秒(480000サンプル)以上にする。（`UGJY_SYNTH_MAX_SAMPLES` を 10秒=480000 に拡張済）
- [x] `ugjy_model_infer` の `copy_samples` の切り捨てを、警告またはエラーにして、黙って切れないようにする。`num_wav_samples > max_samples` のときに、stderr にログを出す。（警告ログ追加済）

## 抑揚の調査(まだ未解決)

- [ ] `ugjy_model_infer` の Step 3 直前に、`ph`/`ac`/`f0`/`dur` のデバッグ出力を入れる。variance の予測ピッチが最初から狭いのか、後段で縮むのかを切り分ける。
- [ ] `ugjy_prosody.c` の `intonation_scale` を 1.05 → 1.5 にして、出力 F0 の標準偏差が増えるかを見る。
- [ ] 読点の間(現状0.42〜0.48秒)が長いと感じたら、`compute_frames` の中間ポーズに上限(例: 0.3秒)を入れて聞き比べる。

## 前回までの未完了

- [x] `ugjy_model.c` の `step_length_regulator` のゼロクリア。（適用済）
- [x] `compute_frames` の `< 0.15f` → `< 0.18f`。（適用済）
- [x] ポーズの二重付加の解消(`ugjy_synth.c`)。（適用済）
- [x] `sharevox.rs` の `#` の母音条件(公式の仕様を確認してから)。（適用済）
- [ ] 口パクのゲージが2チャンク目以降0%になる件(`model->visemes` の競合を疑う)。
