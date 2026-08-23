# TODO.engine/03-rust-publish — Rust crate publishing (crates.io)

bindings/rust builds and tests in CI but has no publish path. Add a release workflow (workflow_dispatch + workflow_call, mirroring the leptris-py pattern) that publishes to crates.io with the CARGO_REGISTRY_TOKEN secret. crates.io has no OIDC trusted publishing — the secret is the mechanism; document the one-time setup.

DONE 2026-08-24: .github/workflows/rust-release.yml (workflow_dispatch +
workflow_call): builds the C core, cargo test, then cargo publish —
packs with a warning when CARGO_REGISTRY_TOKEN is absent (one-time
setup: crates.io token, publish-new scope, repo secret). Crate version
bumped to engine lockstep 1.4.0 (was 1.1.1).
