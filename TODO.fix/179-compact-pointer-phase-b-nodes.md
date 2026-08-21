# TODO 179 — Compact pointer Phase B (text/comment/cdata/pi migration)

**Priority**: P0 (first migration after [[178-compact-pointer-phase-a]])
**Status**: scoped

## Goal

Migrate `leptris_text_node`, `leptris_comment_node`, `leptris_cdata_node`,
`leptris_pi_node` to use `compact_pointer_1byte` for their
`next_sibling` field. Lowest-risk migration — simpler pointer
topology than the element tree.

## Why

These four node types each carry one 8-byte `next_sibling` pointer.
Replacing with 1-byte compact_pointer saves 7 bytes per node. On
text-heavy docs (lots of mixed-content text), this can be 1000+
nodes — meaningful cache-locality win.

The migration also validates the [[178-compact-pointer-phase-a]]
infrastructure end-to-end on real allocations, before the riskier
element migration in [[180-compact-pointer-phase-c-element]].

## Per-node-type phases

### Phase 1 — `leptris_text_node`

- Rename `next_sibling` → `next_sibling_cp` (compact_pointer_1byte).
- Add inline accessor `text_node_next(n, doc)`.
- Update `leptris_node_get_next_sibling()` dispatch.
- Update all writes (parser, mutation API) to call `cp1_set`.

### Phase 2 — `leptris_comment_node`

Same shape. Adds overflow-table awareness to comment mutation paths.

### Phase 3 — `leptris_cdata_node`

Same shape.

### Phase 4 — `leptris_pi_node`

Same shape. PI nodes also live at the document level (siblings of
the root element), so the document's PI chain also migrates.

### Phase 5 — Test + benchmark

- All 464 tests pass.
- `leptris_node_get_next_sibling` benchmark: should improve 5–10% on
  text-heavy docs.
- Overflow-table stress test: docs with 1000+ text nodes spanning
  > 1 KB pool pages.

## Estimated impact

5–10% on text-heavy docs. Negligible on attr-heavy docs.

## Risk

Medium — every node-type migration touches traversal code. But
text/comment/cdata/pi have simpler topology than elements (single
sibling chain, no children/parent).

## References

- Depends on: [[178-compact-pointer-phase-a]]
- Next: [[180-compact-pointer-phase-c-element]]
