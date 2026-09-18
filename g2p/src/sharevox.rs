//! SHAREVOX / VOICEVOX style phoneme & accent sequence extractor for Tsukuyomi-chan.

use std::collections::HashMap;
use std::sync::LazyLock;
use regex::Regex;

/// SHAREVOX 公式の 45 音素テーブル
pub const PHONEME_LIST: &[&str] = &[
    "pau", "A", "E", "I", "N", "O", "U", "a", "b", "by", "ch", "cl", "d", "dy", "e",
    "f", "g", "gw", "gy", "h", "hy", "i", "j", "k", "kw", "ky", "m", "my", "n", "ny",
    "o", "p", "py", "r", "ry", "s", "sh", "t", "ts", "ty", "u", "v", "w", "y", "z",
];

static PHONEME_MAP: LazyLock<HashMap<&'static str, i64>> = LazyLock::new(|| {
    let mut m = HashMap::with_capacity(PHONEME_LIST.len());
    for (i, &s) in PHONEME_LIST.iter().enumerate() {
        m.insert(s, i as i64);
    }
    m
});

/// SHAREVOX 公式の 5 種アクセント記号テーブル
/// [ : アクセント句頭 / 上昇
/// ] : アクセント核 / 下降
/// # : アクセント句境界
/// ? : 疑問文境界
/// _ : その他（無変化）
pub const ACCENT_LIST: &[&str] = &["[", "]", "#", "?", "_"];

static ACCENT_MAP: LazyLock<HashMap<&'static str, i64>> = LazyLock::new(|| {
    let mut m = HashMap::with_capacity(ACCENT_LIST.len());
    for (i, &s) in ACCENT_LIST.iter().enumerate() {
        m.insert(s, i as i64);
    }
    m
});

// OpenJTalk fullcontext labels 正規表現 (SHAREVOX 公式学習器準拠)
static RE_PHONEME: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"-([^+]+)\+").unwrap());
static RE_A1: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/A:([0-9\-]+)\+").unwrap());
static RE_A2: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\+(\d+)\+").unwrap());
static RE_A3: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\+(\d+)/").unwrap());
static RE_F1: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"/F:(\d+)_").unwrap());
static RE_F3: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"#(\d+)_").unwrap());

#[inline]
fn numeric_feature(re: &Regex, s: &str) -> i32 {
    re.captures(s)
        .and_then(|c| c.get(1))
        .and_then(|m| m.as_str().parse::<i32>().ok())
        .unwrap_or(-50)
}

/// OpenJTalkのラベル列から SHAREVOX 公式と 100% 完全一致する音素ID列とアクセントID列を抽出
pub fn extract_sharevox_features(labels: &[String]) -> (Vec<i64>, Vec<i64>) {
    let n_labels = labels.len();
    if n_labels == 0 {
        return (Vec::new(), Vec::new());
    }

    let mut phoneme_strs: Vec<String> = Vec::with_capacity(n_labels);
    let mut accent_strs: Vec<String> = Vec::with_capacity(n_labels);

    for n in 0..n_labels {
        let lab_curr = &labels[n];
        let p3 = match RE_PHONEME.captures(lab_curr).and_then(|c| c.get(1)) {
            Some(m) => m.as_str(),
            None => continue,
        };

        // 1. 文頭・文末の sil は FastSpeech2 では使用しないためスキップ (公式仕様)
        if p3 == "sil" {
            continue;
        } else {
            phoneme_strs.push(p3.to_string());
            // 内部の読点・ポーズ (pau) はアクセント記号 '#'
            if p3 == "pau" {
                accent_strs.push("#".to_string());
                continue;
            }
        }

        // 2. アクセント特徴量の取得
        let a1 = numeric_feature(&RE_A1, lab_curr);
        let a2 = numeric_feature(&RE_A2, lab_curr);
        let a3 = numeric_feature(&RE_A3, lab_curr);
        let f1 = numeric_feature(&RE_F1, lab_curr);

        let a2_next = if n + 1 < n_labels {
            numeric_feature(&RE_A2, &labels[n + 1])
        } else {
            -50
        };

        // 3. アクセント記号の厳密判定 (公式コード完全移植)
        // ① アクセント句境界 または 文末
        if (a3 == 1 && a2_next == 1) || n == n_labels - 2 {
            let f3 = numeric_feature(&RE_F3, lab_curr);
            if f3 == 1 {
                accent_strs.push("?".to_string()); // 疑問文末尾
            } else {
                accent_strs.push("#".to_string()); // 通常句境界
            }
        }
        // ② ピッチの立ち下がり（アクセント核）
        else if a1 == 0 && a2_next == a2 + 1 && a2 != f1 {
            accent_strs.push("]".to_string());
        }
        // ③ ピッチの立ち上がり
        else if a2 == 1 && a2_next == 2 {
            accent_strs.push("[".to_string());
        }
        // ④ 変化なし
        else {
            accent_strs.push("_".to_string());
        }
    }

    if !phoneme_strs.is_empty() && phoneme_strs.last().map(|s| s.as_str()) != Some("pau") {
        phoneme_strs.push("pau".to_string());
        accent_strs.push("#".to_string());
    }

    // 文字列から ID へのマッピング
    let phoneme_ids: Vec<i64> = phoneme_strs
        .iter()
        .map(|s| *PHONEME_MAP.get(s.as_str()).unwrap_or(&0))
        .collect();

    let accent_ids: Vec<i64> = accent_strs
        .iter()
        .map(|s| *ACCENT_MAP.get(s.as_str()).unwrap_or(&4))
        .collect();

    (phoneme_ids, accent_ids)
}
