# TODO 05: Fix per-parse leak — unify node allocation through the pool

**Priority**: P0 (correctness, security)
**Status**: Planned
**Effort**: M

## Problem

`leaks --atExit -- build/cli/taurus parse basic.xml` reports **43 leaks
for ~944 bytes on a 218-byte input**. The stack trace pins the cause:

```
taurus_parse_string
  → parser_parse_document
    → parser_parse_element
      → taurus_text_create
        → taurus_node_create
          → calloc                        ← direct malloc
```

Every text node allocated during parsing is never freed.
`taurus_document_free()` calls `taurus_pool_destroy()`, which frees the
pool's pages — but text nodes were allocated via `calloc`, not the pool,
so they leak.

## Root cause

`src/taurus/dom/node.c` exposes two creation paths:

```c
// 1. Direct calloc — must be freed individually
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type, size_t size);

// 2. Pool-allocated — freed in bulk by taurus_pool_destroy
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type,
                                      size_t size,
                                      TaurusMemoryPool* pool);
```

`taurus_text_create`, `taurus_comment_create`, `taurus_cdata_create`,
`taurus_pi_create`, `taurus_doctype_create` all call path **1**. Only
the element creation path uses the pool. So every non-element node in a
document leaks.

Compounding the issue, `taurus_node_free` (node.c:61) calls `free(node)`
unconditionally. For calloc'd nodes that is correct; for pool'd nodes it
is a **double-free** (the pool will free the same memory on destroy).

This is a fundamental ownership ambiguity: the same tree contains nodes
with different lifecycles and there is no marker on the node itself to
distinguish them.

## Fix

The architectural answer is **every node is pool-allocated**. The pool is
the single owner of all node memory. `taurus_document_free` destroys the
pool, and that releases everything.

### Step 1: Make the pool argument mandatory

In `src/taurus/dom/node.h`:

```c
// Before
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type, size_t size);
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type,
                                      size_t size,
                                      TaurusMemoryPool* pool);

// After — single entry point, pool required
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type,
                               size_t size,
                               TaurusMemoryPool* pool);
```

`taurus_node_create_pooled` is removed (DRY). `taurus_node_free` is
removed (lifecycle is pool-scoped; calling it is always wrong).

### Step 2: Update every per-type create function

`text.c`, `comment.c`, `cdata.c`, `pi.c`, `doctype.c`,
`element_compact.c` — add a `TaurusMemoryPool* pool` parameter, plumb it
through to `taurus_node_create(type, size, pool)`.

```c
// text.c
TaurusTextNode* taurus_text_create(TaurusMemoryPool* pool) {
    TaurusTextNode* text = (TaurusTextNode*)taurus_node_create(
        TAURUS_NODE_TYPE_TEXT, sizeof(TaurusTextNode), pool);
    if (!text) return NULL;
    text->content = NULL;
    return text;
}
```

### Step 3: Update every caller

The parser already holds `p->pool`. Every `parser_parse_text`,
`parser_parse_comment`, etc. now passes `p->pool` to the matching create
function. Other callers (CLI commands that build documents, tests) take
the pool from `taurus_document_pool(doc)` (add the accessor if missing).

### Step 4: Remove `taurus_node_free`

After step 1-3, no caller should free a node directly. Either delete the
function or have it assert (`taurus_node_free is forbidden; free the
document instead`).

### Step 5: Verify the doctype pool

`parser_parse_doctype` allocates the doctype node and a copy of the
internal subset. Both must come from `p->pool`. Currently they mix
calloc and pool — unify.

## Tests

`test/memory/test_pool.cpp` adds:

```cpp
TEST(TaurusDocumentOwnership, AllNodeTypesArePoolOwned) {
    // Parse a document that contains one of every node type.
    const char xml[] = "<?xml version='1.0'?>\n"
                       "<!-- c --><r><?pi data?>"
                       "<![CDATA[x]]>text</r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, sizeof(xml)-1, &st);
    ASSERT_NE(doc, nullptr);

    // No calloc should remain unfreed after document_free. Verified
    // by running this test under `leaks` / valgrind in CI.
    taurus_document_free(doc);
}
```

CI runs the test under `valgrind --leak-check=full --error-exitcode=1`
(Linux) and `leaks --atExit --` (macOS) — both must report zero leaks.

`test/dom/test_dom.cpp` adds specs asserting each node-type create
function requires a non-NULL pool (returns NULL otherwise).

## Architecture notes

**Ownership invariant** (must hold for the lifetime of the library):

> Every `TaurusNode*` reachable from a `TaurusDocument` is allocated
> from that document's pool. Freeing the document frees the pool, which
> frees every node. There is no other path that allocates or frees
> nodes.

This is MECE: exactly one owner (the pool), exactly one free path
(pool destroy). It is OCP-friendly: new node types add a `*_create`
function that calls `taurus_node_create(type, size, pool)`; they do not
modify the free path. It is model-driven: ownership flows through the
document, which is the domain concept the user actually manipulates.

The compact-pointer architecture already assumes pool allocation (the
4-byte-alignment assertion in `pool.c:168`); this fix makes that
assumption actually true.

## Verification

1. `cmake --build build && ctest --test-dir build --output-on-failure` —
   all specs pass.
2. `leaks --atExit -- build/cli/taurus parse test/fixtures/basic.xml` —
   **zero leaks**.
3. `leaks --atExit -- build/cli/taurus parse test/fixtures/small.xml` —
   **zero leaks**.
4. `valgrind --leak-check=full --error-exitcode=1 build/test/dom/test_dom`
   on Linux CI — exit 0.
5. Repeat the parse benchmark from the validation report; throughput
   should not regress (the pool's bump-pointer is faster than `calloc`).
