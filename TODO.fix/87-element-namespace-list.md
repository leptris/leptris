# TODO 87: Implement proper namespace list on elements

**Priority**: P3 (correctness — addressed at the API level)
**Status**: Closed — implemented in PR #32
**Effort**: Done

## Original concern

`src/taurus/dom/element.c:557`:

```c
/* TODO: Implement proper namespace list */
```

`taurus_element_add_namespace_inplace` set `elem->prefix` and
`elem->namespace_uri` directly but did NOT add an entry to
`elem->namespaces`. That meant `taurus_element_lookup_namespace`
could not find namespaces registered through this path.

## Resolution (PR #32)

`taurus_element_add_namespace_inplace` now does both:

1. Sets `elem->prefix` / `elem->namespace_uri` (zero-copy direct
   fields, as before).
2. When a pool is provided, allocates a `struct taurus_namespace`
   via `taurus_namespace_new_pooled` and adds it to
   `elem->namespaces` via `taurus_element_add_namespace`.

If pool allocation fails, the direct fields are still set — graceful
degradation. The function is currently unused (the parser uses the
regular `taurus_element_add_namespace` + `taurus_namespace_new_pooled`
path), but it is now correct for future callers.

The misleading TODO comment is removed.

## Acceptance

- All 105 existing tests pass unchanged.
- Function correctly registers namespaces on the linked list when
  pool is provided.
