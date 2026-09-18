use std::collections::HashMap;
use std::ffi::CStr;
use std::io::Read;
use std::os::raw::c_char;
use std::panic::AssertUnwindSafe;
use std::ptr;

use crate::encode::{PiperEncoder, UnknownTokenMode};
use crate::japanese::JapanesePhonemizer;
use crate::phonemizer::Phonemizer;

/// C側に不透明ポインタとして渡すハンドル
pub struct UgjyG2p {
    ja_phonemizer: Option<JapanesePhonemizer>,
    encoder: Option<PiperEncoder>,
    is_sharevox: bool,
}

/// config.json (または model_config.json) を読み込んでG2Pインスタンスを作成
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_create(
    config_path: *const c_char,
) -> *mut UgjyG2p {
    if config_path.is_null() {
        return ptr::null_mut();
    }

    let result = std::panic::catch_unwind(|| {
        let config_str = unsafe { CStr::from_ptr(config_path) }.to_str().ok()?;

        let mut ja_phonemizer = None;
        #[cfg(feature = "naist-jdic")]
        {
            if let Ok(ja) = JapanesePhonemizer::new_bundled() {
                ja_phonemizer = Some(ja);
            }
        }

        // ツクヨミちゃん (SHAREVOX) モデルの判定
        if config_str.contains("tsukuyomi") || config_str.contains("sharevox") {
            return Some(Box::into_raw(Box::new(UgjyG2p {
                ja_phonemizer,
                encoder: None,
                is_sharevox: true,
            })));
        }

        // 既存のPiperモデル用の読み込み処理
        let mut f = std::fs::File::open(config_str).ok()?;
        let mut content = String::new();
        f.read_to_string(&mut content).ok()?;
        let json: serde_json::Value = serde_json::from_str(&content).ok()?;

        if json.get("synthesis_system").is_some() {
            return Some(Box::into_raw(Box::new(UgjyG2p {
                ja_phonemizer,
                encoder: None,
                is_sharevox: true,
            })));
        }

        let id_map_json = json.get("phoneme_id_map")?.as_object()?;
        let mut id_map = HashMap::new();
        for (k, v) in id_map_json {
            if let Some(arr) = v.as_array() {
                let ids: Vec<i64> = arr.iter().filter_map(|x| x.as_u64().map(|n| n as i64)).collect();
                id_map.insert(k.clone(), ids);
            }
        }

        let encoder = PiperEncoder::new(id_map, UnknownTokenMode::Skip).ok()?;
        Some(Box::into_raw(Box::new(UgjyG2p {
            ja_phonemizer,
            encoder: Some(encoder),
            is_sharevox: false,
        })))
    });

    result.unwrap_or(None).unwrap_or(ptr::null_mut())
}

/// テキストからトークン列とアクセント/韻律特徴量を生成し、Cバッファに直接書き込み
/// 0: 成功, 負数: 失敗
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_convert(
    g2p: *mut UgjyG2p,
    text: *const c_char,
    _lang: *const c_char,
    out_tokens: *mut i64,
    out_prosody: *mut i64,
    max_tokens: usize,
    out_num_tokens: *mut usize,
) -> i32 {
    if g2p.is_null() || text.is_null() || out_tokens.is_null() || out_num_tokens.is_null() {
        return -1;
    }

    let result = std::panic::catch_unwind(AssertUnwindSafe(|| {
        let g = unsafe { &*g2p };
        let text_str = unsafe { CStr::from_ptr(text) }.to_str().ok()?;

        if g.is_sharevox {
            let ja = g.ja_phonemizer.as_ref()?;
            let labels = ja.extract_labels(text_str).ok()?;
            let (phoneme_ids, accent_ids) = crate::sharevox::extract_sharevox_features(&labels);

            if phoneme_ids.len() > max_tokens {
                return None;
            }

            unsafe {
                ptr::copy_nonoverlapping(phoneme_ids.as_ptr(), out_tokens, phoneme_ids.len());
                if !out_prosody.is_null() {
                    ptr::copy_nonoverlapping(accent_ids.as_ptr(), out_prosody, accent_ids.len());
                }
                *out_num_tokens = phoneme_ids.len();
            }
            return Some(0);
        }

        // Piper用フォールバック
        let ja = g.ja_phonemizer.as_ref()?;
        let encoder = g.encoder.as_ref()?;
        let (tokens, prosody) = ja.phonemize_with_prosody(text_str).ok()?;
        let (ids, _) = encoder.encode_with_prosody_and_eos(&tokens, &prosody, None).ok()?;

        if ids.len() > max_tokens {
            return None;
        }

        unsafe {
            ptr::copy_nonoverlapping(ids.as_ptr(), out_tokens, ids.len());
            *out_num_tokens = ids.len();
        }
        Some(0)
    }));

    match result {
        Ok(Some(code)) => code,
        _ => -2,
    }
}

/// 解放
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_destroy(g2p: *mut UgjyG2p) {
    if !g2p.is_null() {
        unsafe {
            drop(Box::from_raw(g2p));
        }
    }
}
