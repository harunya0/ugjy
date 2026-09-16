use std::collections::HashMap;
use std::ffi::CStr;
use std::io::Read;
use std::os::raw::c_char;
use std::panic::AssertUnwindSafe;
use std::ptr;

use crate::encode::{PiperEncoder, UnknownTokenMode};
use crate::phonemizer::PhonemizerRegistry;

/// C側に不透明ポインタとして渡すハンドル
pub struct UgjyG2p {
    registry: PhonemizerRegistry,
    encoder: PiperEncoder,
}

/// config.jsonを読み込んでG2Pインスタンスを作成
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_create(
    config_path: *const c_char,
) -> *mut UgjyG2p {
    if config_path.is_null() {
        return ptr::null_mut();
    }

    let result = std::panic::catch_unwind(|| {
        let config_str = unsafe { CStr::from_ptr(config_path)}.to_str().ok()?;

        // config.jsonを読み込む
        let mut f = std::fs::File::open(config_str).ok()?;
        let mut content = String::new();
        f.read_to_string(&mut content).ok()?;
        let json: serde_json::Value = serde_json::from_str(&content).ok()?;

        let id_map_json = json.get("phoneme_id_map")?.as_object()?;
        let mut id_map = HashMap::new();
        for (k, v) in id_map_json {
            if let Some(arr) = v.as_array() {
                let ids: Vec<i64> = arr.iter().filter_map(|x| x.as_u64().map(|n| n as i64)).collect();
                id_map.insert(k.clone(), ids);
            }
        }

        let encoder = PiperEncoder::new(
            id_map,
            UnknownTokenMode::Skip,
        ).ok()?;
        let mut registry = PhonemizerRegistry::new();

        // 日本語
        #[cfg(feature = "naist-jdic")]
        if let Ok(ja) = 
            crate::japanese::JapanesePhonemizer::new_bundled() {
            registry.register("ja", Box::new(ja));
        }

        // 英語
        #[cfg(all(feature = "english", feature = "bundled-dicts"))]
        if let Ok(en) = crate::english::EnglishPhonemizer::new_bundled() {
            registry.register("en", Box::new(en));
        }
        #[cfg(all(feature = "english", not(feature = "bundled-dicts")))]
        if let Ok(en) = crate::english::EnglishPhonemizer::new() {
            registry.register("en", Box::new(en));
        }
        Some(Box::into_raw(Box::new(UgjyG2p { registry, encoder })))
    });

    result.unwrap_or(None).unwrap_or(ptr::null_mut())
}

/// テキストからトークン列と韻律特徴量を一括生成し、Cバッファに直接書き込み
/// 0: 成功, 負数: 失敗
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_convert(
    g2p: *mut UgjyG2p,
    text: *const c_char,
    lang: *const c_char,
    out_tokens: *mut i64,
    out_prosody: *mut i64,
    max_tokens: usize,
    out_num_tokens: *mut usize
) -> i32 {
    if g2p.is_null() || text.is_null() || out_tokens.is_null() || out_num_tokens.is_null() {
        return -1; // 引数が不正
    }

    let result = std::panic::catch_unwind(AssertUnwindSafe(|| {
        let g = unsafe { &*g2p };
        let text_str = unsafe { CStr::from_ptr(text) }.to_str().ok()?;
        let lang_str = if lang.is_null() {
            "ja" // デフォルトは日本語
        } else {
            unsafe { CStr::from_ptr(lang) }.to_str().ok()?
        };

        let phonemizer = g.registry.get(lang_str)?;

        // テキスト->音素数・韻律情報
        let (tokens, prosody) = phonemizer.phonemize_with_prosody(text_str).ok()?;

        // 音素列->トークンID列
        let (ids, pros_feats) = g.encoder.encode_with_prosody(&tokens, &prosody).ok()?;

        if ids.len() > max_tokens {
            return None; // バッファが足りない
        }

        unsafe {
            // トークンID列をCバッファにコピー
            ptr::copy_nonoverlapping(ids.as_ptr(), out_tokens, ids.len());

            // 韻律特徴量もコピー
            if !out_prosody.is_null() {
                for (i, feat) in pros_feats.iter().enumerate() {
                    *out_prosody.add(i * 3 + 0) = feat[0] as i64;
                    *out_prosody.add(i * 3 + 1) = feat[1] as i64;
                    *out_prosody.add(i * 3 + 2) = feat[2] as i64;
                }
            }
            *out_num_tokens = ids.len();
        }
        Some(0) // 成功
    }));

    match result {
        Ok(Some(code)) => code,
        _ => -2, // 内部エラー
    }
}

/// 解放
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ugjy_g2p_destroy(g2p: *mut UgjyG2p) {
    if !g2p.is_null() {
        unsafe {
            drop(Box::from_raw(g2p)); // Boxを解放してメモリを返す
        }
    }
}
