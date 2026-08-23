# Open work

Everything from the TODO.fix (110 executed plans) and TODO.remaining
(10 closed items) boards has shipped or was closed as measured-dead —
those files are removed from the tree and preserved in git history
(release v1.1.2-era `main` has the full set). This file tracks what
is genuinely still open, with pointers.

## Library

- **Document node** — the engine has no document node. `//node()`
  selects the root element via the folded absolute walk, but the
  generic expansion (`/descendant-or-self::node()/child::node()`
  written explicitly) cannot; a real document-node model is the
  eventual fix. Also the boundary for `count()`-style expressions
  written out longhand.

## Bindings

- **Rust: serde interop** — deferred until a consumer asks for it
  (TODO.remaining/05 closure note, git history).
- **Rust: crates.io publishing** — publish workflow shipped
  (.github/workflows/rust-release.yml, TODO.engine/03); the crate
  version rides engine lockstep. One-time secret remains: set
  CARGO_REGISTRY_TOKEN (crates.io token, publish-new scope) and
  dispatch — without it the workflow packs and warns.
- **leptris (py)** — own repo, own pin, wheels shipping since 1.3.x
  (trusted publishing is set up; releases dispatch from leptris-py).

## Adjacent repos

- **leptris-ruby** — release bump now computes major/minor/patch
  from the current version (fixed in their release.yml).
