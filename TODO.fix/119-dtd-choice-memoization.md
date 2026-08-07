# TODO 119 — DTD content-model memoization

**Priority**: P3
**Status**: Open. Deferred from TODO 91 Phase 8c.

## Why

DTD content-model validation in `src/taurus/dtd/content_check.c` uses
recursive descent. Each `xi:include parse="xml"` of an element with a
complex content model re-parses the model string and re-walks the
children. For DocBook-style schemas where the same content model
appears on many elements, this is redundant work.

The TODO's original premise ("exponential backtracking on ambiguous
models like `(a|b)*,a,b,a`") does not apply to the current matcher:
each choice has bounded backtracking (one rollback per `|`). But the
**per-call cost** is still O(N*M) where N is children count and M is
model length, and the model is re-tokenized on every call.

## Current state

`taurus_content_model_match(model, elem_name, child_names, child_count)`
in `content_check.c`:
- Tokenizes the model string in-place on every call (no caching).
- Recursive-descent match against the children array.
- Bounded backtracking per `|` (single rollback).

The function is called once per element from `validator.c` line 186.

## Plan

### Phase A — pre-compile content model to token array

At DTD parse time, convert each `DTDElementDecl->content_model` string
into a normalized token array stored on the decl. Tokens are pool-
allocated, freed with the DTD's pool.

This avoids re-tokenizing the model string on every match call.

Token types: `NAME`, `LPAREN`, `RPAREN`, `COMMA`, `PIPE`, `Q`, `S`,
`P`. Each token also carries the name string (pool-owned).

### Phase B — memoize match per (decl, children signature)

Walk the NFA-equivalent of the model over the actual children. Memo
key is `(decl, hash(children))`. Memo value is match result + error
message index.

Memo table is per-validation-run (allocated in `taurus_dtd_validate`,
freed before return). Bounded by #distinct (decl, children) pairs in
the document — typically small.

### Phase C — accept

- DocBook-style schemas: O(N*M) becomes O(#distinct-children-shapes).
- New spec: validate 1000 elements with the same content model —
  match function called once, not 1000 times.
- Existing DTD validation tests pass.

## Implementation notes

- Phase A requires extending `DTDElementDecl` with a `void*
  content_model_tokens` field (opaque pointer to a pool-allocated
  array). Set during DTD parse, NULL on legacy decls.
- Phase B requires a small memo table type. Can reuse the existing
  pool hashtable (`pool.h`'s `StringHashTable`) with a custom key
  encoding.
- Phase C just verifies the win.

The whole thing is ~200-300 lines of code. Lower priority than the
parse-perf and XInclude work; deferred until DTD validation shows up
in a real perf profile.
