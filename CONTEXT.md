# CONTEXT.md — libleptris domain vocabulary

Terms that stabilized during the architecture review (2026-08-22)
and its follow-up work. Code, commits, and future reviews should
use these names.

## Modules

- **document module** — parse entry points, document lifecycle,
  serialization wrappers. Lives in `src/leptris/leptris.c`.
- **element module** — the element/attribute surface. Queries and
  typed accessors in `dom/element_query.c` (the one face for
  attribute access), mutation and copy machinery in
  `dom/element_modify.c`. The DOM internals behind them are NOT part
  of its interface.
- **binding mirror** — a hand-maintained copy of the public
  interface inside a language binding (Ruby `attach_function` list,
  Python `cdef`). Mirrors are adapters over the same seam; drift
  between a mirror and the headers is a bug, caught by
  `scripts/check_ffi_mirrors.py` (the **drift gate**).
- **mixed nodeset** — an XPath nodeset result containing element
  nodes alongside synthetic attribute nodes. Consumed through the
  result-scoped kind quartet, never through raw node tags (the tree
  tag space and the XPath tag space assign different values to the
  same small integers).

## Contracts

- **layer contract** — the CLI never touches XML structures
  directly; it goes through the public API in `src/include/`.
  Enforced by construction since 2026-08-22 (no internal includes
  under `cli/`).
- **first-declaration-wins** — XML 1.0 rule for redeclared DTD
  names; also gives internal-subset declarations precedence over
  external subsets.
- **document-owned memory** — handles and strings returned from
  document-scoped accessors live until `leptris_document_free`;
  `Memory:` comments in the headers are the contract every binding
  builds on.

## Landmarks

- **the parse wall** — `direct_parse_internal` is a measured
  compiler-global optimum (11 failed experiments + the SIMD floor
  probe). Do not propose loop-shape changes to it.
- **the 40-byte attribute law** — attribute struct size and layout
  are measured optima; the split-stream alternative is closed (the
  TODO.remaining/08 closure note lives in git history, v1.1.2-era
  main; the verdict is summarized in TODO.md).
