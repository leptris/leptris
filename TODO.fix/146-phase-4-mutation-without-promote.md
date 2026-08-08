# TODO 146 — Phase 4: mutation without mandatory promote

## The fundamental challenge

Mutation on a parsed-but-unpromoted document requires either:

1. **Mutable/growable FlatDoc** — the node array must accept new
   entries. Today it's a fixed-size malloc. Growing means realloc
   (invalidates existing FlatNode* pointers) or a chunked layout
   (indirection on every access).

2. **Mixed representation** — some nodes live in FlatDoc, others in
   the compact tree. Every accessor must dispatch on node type.
   Tagged pointers add a branch to every call site.

3. **Orphan tracking** — new elements are compact tree nodes created
   outside any tree. They're tracked in a "pending" list on the doc.
   On promote, the pending list is wired into the promoted tree.
   This delays promote but doesn't eliminate it.

None of these are session-scale. The pugixml design avoids the
problem entirely by making the parse representation IS the mutable
tree — but that's a ground-up rewrite of our data model.

## Achievable Phase 4 work (this session)

### Phase 4a — Bulk pool allocation in promote (TODO 141 Phase B)

Pre-allocate all compact elements in one pool_alloc call per type.
Replaces N per-element allocs + N memsets with 1 alloc + 1 memset
per type. Saves ~3-10 µs on a 50-element doc.

### Phase 4b — Document the full mutation-on-flat design

Write TODO 147 with the detailed design for mutable FlatDoc or
mixed representation. Future session implements it.

## Expected impact

Phase 4a: promote cost drops ~10-15% on medium docs. Combined
with Phase A (wire_child inline), promote goes from 78 µs to
~60 µs on a 5 KB doc.

Phase 4b: no code change, just documentation for future work.
