# TODO 150 — Compact-pointer Phase 2e: element struct compaction (88 → 72 bytes)

## Why

The element struct is 88 bytes (see `_Static_assert` in
`element.h`). pugixml's compact node is 44-56 bytes. Closing
this 2× cache-footprint gap is the single highest-impact perf
work remaining: every tree traversal touches the element struct,
so halving its size halves the cache pressure.

## Current layout (88 bytes)

```
offset  field                    bytes  notes
──────  ──────                   ─────  ─────
  0     LeptrisNode base           12    type(4) + frozen/version(4) + line(4)
 12     LeptrisCompactHeader        2
 14     attr_count                 1
 15     child_count                2
 17     (padding)                  3    ← wasted
 20     name (char*)               8
 28     prefix (char*)             8
 36     namespace_uri (char*)      8
 44     parent_off (int32)         4
 48     first_child_off (int32)    4
 52     last_child_off (int32)     4
 56     next_sibling_off (int32)   4
 60     first_attribute_off(int32) 4
 64     last_attribute_off(int32)  4
 68     namespaces (ptr)           8    ← target for removal
 76     document (ptr)             8    ← target for removal
 84     (padding)                  4
 ────                              88
```

## Phase 2e targets

### 2e-A: Drop per-element `document` pointer (−8 bytes)

**Current**: every element carries `elem->document` (8 bytes).
Used by:
- `leptris_element_create` to set `elem->document = doc`
- `leptris_element_append_child_internal` to propagate to children
- `leptris_element_get_namespace_uri` to access `doc->pool`
- `leptris_element_index_invalidate` to invalidate on mutation
- Clone/copy operations to set document on copies

**Plan**: store document ONLY on the root element. Non-root
elements recover via parent walk (`leptris_elem_parent` until
parent_off==0 → that's the root, root->document is the doc).
Cache the result in a thread-local for O(1) amortized access.

**Risk**: every code path that reads `elem->document` must be
updated. The `leptris_element_get_document(elem)` helper replaces
direct field access.

### 2e-B: Drop per-element `namespaces` head pointer (−8 bytes)

**Current**: every element carries `elem->namespaces` (8 bytes),
pointing to a linked list of xmlns declarations on this element.

**Plan**: store namespace declarations as regular attributes with
a `LEPTRIS_ATTR_NS_DECL` flag bit on `struct leptris_attribute`.
This removes the separate namespaces linked list entirely.
`leptris_element_namespace_count` walks the attr list filtering
on the flag; `leptris_element_namespace_decl_prefix/_uri` index
into the filtered list.

**Risk**: attr lookup becomes O(N+M) where M is the ns-decl
count. For typical elements (0-2 ns-decls) this is negligible.
The namespace lookup path (`leptris_element_lookup_namespace`)
already walks the list — adding attr-scan overhead is ~1 cycle.

### Combined impact

Removing both pointers drops element from 88 → 72 bytes.
With alignment, the struct might be 72 or 80 bytes. Either way,
~20% cache-footprint reduction on every tree traversal.

## Migration plan

1. Add `leptris_element_get_document(elem)` helper that walks to
   root. Update all direct `elem->document` reads to use it.
   Keep `elem->document` field temporarily for compatibility.
2. Set `elem->document` only on root. Non-root elements leave
   it NULL. The helper falls back to root walk on NULL.
3. Migrate `namespaces` to attr-flag approach. Add
   `LEPTRIS_ATTR_NS_DECL` bit. Update namespace accessors.
4. Remove `elem->document` and `elem->namespaces` fields.
5. Update `_Static_assert` to 72.

## Expected perf

- Parse: ~5% faster (smaller element struct = better cache
  utilization during promote/direct_parse tree-building)
- Serialize: ~3% faster (tree walk touches fewer cache lines)
- XPath: ~5-10% faster on deep trees (descendant-axis
  traversal is cache-bound)

## Prerequisites

- Phase 2e-A (document pointer) is safe to ship independently.
- Phase 2e-B (namespaces as attrs) requires auditing all
  namespace lookup paths and is higher-risk.

## Status

Phase 2e-B (ns_cache merge) shipped in v0.7.0 — element 88 → 80 bytes.

Phase 2e-A (drop `document` field) and Phase 2e-B-original (drop
`namespaces` field) are **deferred**:

- **2e-A**: Removing the `document` field requires an alternative
  O(1) doc-lookup mechanism. Parent-walk to root doesn't help — the
  field on root still costs 8 bytes (struct is uniform). Pool
  back-pointer (page header → pool → owner_doc) adds page-list walk
  overhead. Side-table (hash elem→doc) adds global-state complexity.
  None of these cleanly trade 8 bytes of field for less overhead.
  The existing `element_owning_document()` helper in element_query.c
  already walks the parent chain for callers that need robustness;
  direct field access on hot paths is preserved for speed.

- **2e-B-original** (`namespaces` head pointer → attr flag): the
  linked list is deeply integrated into c14n (`serialize/c14n.c:192`,
  `:637`), serialize (`serialize/serialize.c:357`), and the
  `leptris_namespace_*` public API (`element_query.c:920-1066`).
  Migrating to attr-flag semantics is a multi-file refactor with
  high regression risk on canonicalization conformance, which is
  W3C-xml-c14n-test-suite gated. Worth doing only with dedicated
  test-coverage bandwidth.

Net: element stays at 80 bytes. To reach 72, revisit 2e-A after
adding a page-header pool back-pointer (O(1) pool lookup), or
revisit 2e-B-original after decoupling c14n from the linked list.
