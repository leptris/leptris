# 05 — CLOSED (2026-08-23)

DONE via PR: bindings/rust — crate `leptris` (v1.1.1 lockstep).
Hand-maintained FFI mirror (src/ffi.rs) like the Python/Ruby
bindings (bindgen dropped: needs libclang everywhere, and the drift
gate pattern already exists for hand mirrors); safe Document/Element
wrappers with Drop, LeptrisStatus→Result error enum, attribute +
child iteration over the 1.1.0 linked-list face, mixed-nodeset XPath
accessors, SAX with closures via trampolines, cross-platform CI
(rust.yml). serde interop deferred until a consumer asks for it.

# 05 — Rust bindings (docs/83-rust-binding.md)

bindgen over the public headers + an idiomatic wrapper crate
(`leptris-rs`): Document/Element smart wrappers, error enum mapping
LeptrisStatus → Result, zero-copy parse from &[u8], SAX callbacks via
closures, serde optional interop.

Follow the leptris-ruby and leptris-py layout (bindings/<lang>).
Public API surface needed first: 06 (attribute iteration) so the
binding does not have to reach into internals.
