//! Link against libleptris.
//!
//! Resolution order:
//!   1. LEPTRIS_LIB_PATH — full path to the shared library file, or a
//!      directory containing it. CI sets this after building the C
//!      library from source.
//!   2. Fallback: system linker search (`-lleptris`).

use std::path::{Path, PathBuf};
use std::{env, fs};

fn main() {
    let name = "leptris";
    if let Ok(path) = env::var("LEPTRIS_LIB_PATH") {
        // Tolerate repo-relative paths: cargo runs build.rs from the
        // crate directory, so a path like `build-shared/src` (natural
        // when set from the repository root) would not resolve.
        let mut p = PathBuf::from(&path);
        if !p.exists() {
            if let Ok(manifest) = env::var("CARGO_MANIFEST_DIR") {
                let repo_relative = Path::new(&manifest).parent().map(|root| root.join(&p));
                if let Some(cand) = repo_relative {
                    if cand.exists() {
                        p = cand;
                    }
                }
            }
        }
        let dir: PathBuf = if p.is_file() {
            p.parent().unwrap().to_path_buf()
        } else {
            p.clone()
        };
        // Absolute for -rpath: a relative rpath resolves against the
        // test binary's directory, not the crate.
        let dir = dir.canonicalize().unwrap_or(dir);
        let found = if p.is_file() { Some(p) } else { find_lib(&p) };
        assert!(
            found.is_some(),
            "LEPTRIS_LIB_PATH set but no libleptris shared library found under {path}"
        );
        println!("cargo:rustc-link-search=native={}", dir.display());
        println!("cargo:rustc-link-lib=dylib={name}");
        // Let test binaries find the dylib at run time without
        // requiring DYLD_LIBRARY_PATH.
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
        println!("cargo:rustc-env=LEPTRIS_LIB_DIR={}", dir.display());
        return;
    }
    println!("cargo:rustc-link-lib=dylib={name}");
}

fn find_lib(dir: &Path) -> Option<PathBuf> {
    let prefixes = ["lib", ""];
    let exts = ["dylib", "so", "dll"];
    for ext in exts {
        for prefix in prefixes {
            let cand = dir.join(format!("{prefix}leptris.{ext}"));
            if cand.exists() {
                return Some(cand);
            }
        }
    }
    // Windows often drops the prefix.
    let _ = fs::metadata(dir);
    None
}
