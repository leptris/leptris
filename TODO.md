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
- **Rust: crates.io publishing** — the crate builds and tests in CI
  but has no publish workflow yet; mirror the pyleptris
  workflow_call pattern when wanted.
- **pyleptris PyPI publish** — code path verified end-to-end; blocked
  on the one-time trusted-publisher setup on pypi.org (project
  pyleptris, workflow .github/workflows/python-publish.yml), then a
  workflow_dispatch run ships the wheel.

## Adjacent repos

- **leptris-ruby** — release bump job accepts explicit x.y.z only
  (the input description's major/minor/patch values are not
  computed); fix in their release.yml when convenient.
