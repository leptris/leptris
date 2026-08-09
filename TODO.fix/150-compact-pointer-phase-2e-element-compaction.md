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
  0     TaurusNode base           12    type(4) + frozen/version(4) + line(4)
 12     TaurusCompactHeader        2
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
- `taurus_element_create` to set `elem->document = doc`
- `taurus_element_append_child_internal` to propagate to children
- `taurus_element_get_namespace_uri` to access `doc->pool`
- `taurus_element_index_invalidate` to invalidate on mutation
- Clone/copy operations to set document on copies

**Plan**: store document ONLY on the root element. Non-root
elements recover via parent walk (`taurus_elem_parent` until
parent_off==0 → that's the root, root->document is the doc).
Cache the result in a thread-local for O(1) amortized access.

**Risk**: every code path that reads `elem->document` must be
updated. The `taurus_element_get_document(elem)` helper replaces
direct field access.

### 2e-B: Drop per-element `namespaces` head pointer (−8 bytes)

**Current**: every element carries `elem->namespaces` (8 bytes),
pointing to a linked list of xmlns declarations on this element.

**Plan**: store namespace declarations as regular attributes with
a `TAURUS_ATTR_NS_DECL` flag bit on `struct taurus_attribute`.
This removes the separate namespaces linked list entirely.
`taurus_element_namespace_count` walks the attr list filtering
on the flag; `taurus_element_namespace_decl_prefix/_uri` index
into the filtered list.

**Risk**: attr lookup becomes O(N+M) where M is the ns-decl
count. For typical elements (0-2 ns-decls) this is negligible.
The namespace lookup path (`taurus_element_lookup_namespace`)
already walks the list — adding attr-scan overhead is ~1 cycle.

### Combined impact

Removing both pointers drops element from 88 → 72 bytes.
With alignment, the struct might be 72 or 80 bytes. Either way,
~20% cache-footprint reduction on every tree traversal.

## Migration plan

1. Add `taurus_element_get_document(elem)` helper that walks to
   root. Update all direct `elem->document` reads to use it.
   Keep `elem->document` field temporarily for compatibility.
2. Set `elem->document` only on root. Non-root elements leave
   it NULL. The helper falls back to root walk on NULL.
3. Migrate `namespaces` to attr-flag approach. Add
   `TAURUS_ATTR_NS_DECL` bit. Update namespace accessors.
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

Documented, not started. Each sub-phase ships as a separate PR.
