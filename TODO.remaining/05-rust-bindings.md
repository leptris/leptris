# 05 — Rust bindings (docs/83-rust-binding.md)

bindgen over the public headers + an idiomatic wrapper crate
(`leptris-rs`): Document/Element smart wrappers, error enum mapping
LeptrisStatus → Result, zero-copy parse from &[u8], SAX callbacks via
closures, serde optional interop.

Follow the leptris-ruby and leptris-py layout (bindings/<lang>).
Public API surface needed first: 06 (attribute iteration) so the
binding does not have to reach into internals.
