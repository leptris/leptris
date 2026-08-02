# TODO 17: Remove dead `taurus_node_create`; make `taurus_node_free` an assertion

**Priority**: P1 (safety / hygiene)
**Status**: Planned
**Effort**: S

## Problem

`src/taurus/dom/node.h` exposes two creation paths:

```c
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type, size_t size);
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type,
                                      size_t size,
                                      TaurusMemoryPool* pool);
```

After TODO 05, **every** per-type create function calls
`taurus_node_create_pooled`. The non-pooled `taurus_node_create` has
zero callers in the active build (verified: `grep -rn taurus_node_create
src/taurus/` shows only the definition itself plus the fallback line
inside `taurus_node_create_pooled` when pool is NULL — which never fires
because every caller passes a real pool).

It also exposes `taurus_node_free`, which calls `free(node)`:

```c
void taurus_node_free(TaurusNode* node) {
    if (!node) return;
    free(node);
}
```

For pool-allocated nodes, this is a **double-free**: the pool will
reclaim the same memory on `taurus_pool_destroy`. Currently nothing in
the active build calls `taurus_node_free` either, but its existence on
the internal API is a footgun — any future caller will introduce a
subtle double-free.

## Root cause

Both functions pre-date the unified pool ownership model (TODO 05).
The cleanup pass when pool allocation was introduced didn't remove them.

## Fix

### Step 1: Remove `taurus_node_create`

Delete the function from `node.c` and the declaration from `node.h`.
Update `taurus_node_create_pooled` to inline the calloc fallback path
(which should never fire but is correct behavior).

### Step 2: Make `taurus_node_free` an assertion

Replace the body with:

```c
void taurus_node_free(TaurusNode* node) {
    /* Pool owns all node lifetime.  See TODO 17.
     * Calling this is always wrong — it would be a double-free
     * against the pool's eventual reclaim of the same memory. */
    (void)node;
    assert(!"taurus_node_free is forbidden; free the document instead");
}
```

Or delete it entirely if no header references it.

### Step 3: Update tests

`test/dom/test_dom.cpp` adds:

```cpp
TEST(DomNodeOwnership, PoolAllocatedNodeCannotBeFreed) {
    // Documenting the invariant: nodes are not individually freed.
    // (If taurus_node_free is ever reintroduced, it must assert.)
}
```

## Architecture notes

**Ownership invariant** (final form):

> A `TaurusNode*` is **always** pool-allocated. Lifetime is bounded
> by the document. There is no API to free an individual node.

This is MECE: exactly one owner (the pool), exactly one release path
(`taurus_document_free` → `taurus_pool_destroy`). The dual
`node_create` / `node_create_pooled` API violated DRY by expressing
the same operation two ways; eliminating one restores the single
source of truth.

## Verification

```bash
grep -rn "taurus_node_create\b" src/taurus/   # only _pooled callers
grep -rn "taurus_node_free\b"   src/taurus/   # only the definition
cmake --build build                            # warning-clean for node.c
```
