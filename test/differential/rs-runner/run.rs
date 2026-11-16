//! Differential corpus runner (Rust binding) — the A6 fourth leg
//! (#2048 slice 3). Prints one canonical line per corpus doc:
//!   docname|xpath_count|sha256(normalized_serialization)
//! byte-identical to run.rb / run.py / run.mjs. Normalization: the
//! prolog is stripped; comparison starts at the root element (first
//! '<' followed by a name character).

use sha2::{Digest, Sha256};
use std::fs;
use std::path::PathBuf;

fn docs() -> &'static [&'static str] {
    &[
        "basic.xml",
        "cdata.xml",
        "comments_pis.xml",
        "dtd_model.xml",
        "entities_dtd.xml",
        "mixed_deep.xml",
        "namespaces.xml",
        "xpath_target.xml",
        "01-namespaces.xml",
        "02-entities-predef.xml",
        "03-entities-numeric.xml",
        "04-cdata.xml",
        "05-comments-pi.xml",
        "06-doctype-internal.xml",
        "07-unicode.xml",
        "08-deep-nesting.xml",
        "09-mixed-content.xml",
        "10-big-attrs.xml",
        "11-whitespace.xml",
        "12-dup-xmlns.xml",
        "13-custom-dtd-entity.xml",
        "14-processing-instr-xml-decl.xml",
        "15-empty-and-selfclose.xml",
        "16-attrs-special-chars.xml",
        "17-rss-like.xml",
        "18-soap-like.xml",
        "19-xinclude.xml",
        "20-single-root.xml",
        "21-text-only-elems.xml",
        "22-long-text.xml",
    ]
}

fn xpath_for(name: &str) -> &'static str {
    match name {
        "basic.xml" => "count(//ns:elem)",
        "cdata.xml" => "count(//text)",
        "comments_pis.xml" => "count(//comment())",
        "dtd_model.xml" => "count(//item[@sku])",
        "entities_dtd.xml" => "count(//title)",
        "mixed_deep.xml" => "count(//p/*)",
        "namespaces.xml" => "count(//b:book)",
        "xpath_target.xml" => "count(//order[@status='active']/item)",
        _ => "count(//*)",
    }
}

/// First '<' followed by a name character — the root element start.
/// Every prolog node (declaration, DOCTYPE subset, leading
/// comments/PIs) starts "<?" or "<!".
fn root_start(full: &str) -> Option<usize> {
    let b = full.as_bytes();
    for i in 0..b.len().saturating_sub(1) {
        if b[i] == b'<' && (b[i + 1].is_ascii_alphabetic() || b[i + 1] == b'_') {
            return Some(i);
        }
    }
    None
}

fn main() {
    let corpus = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .join("corpus");

    for name in docs() {
        let xml = fs::read(corpus.join(name)).expect(name);
        let doc = leptris::Document::parse(&xml)
            .unwrap_or_else(|e| panic!("{}: {e}", name));
        let count = doc
            .xpath(xpath_for(name))
            .unwrap_or_else(|e| panic!("{}: {e}", name))
            .number() as usize;
        let full = doc.serialize().unwrap_or_else(|e| panic!("{}: {e}", name));
        let start = root_start(&full).unwrap_or_else(|| panic!("{}: no root", name));
        let hash = Sha256::digest(full[start..].as_bytes());
        let hex: String = hash.iter().map(|b| format!("{:02x}", b)).collect();
        println!("{}|{}|{}", name, count, hex);
    }
}
