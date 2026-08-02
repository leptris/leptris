# TODO 29: Vtable phase 3 — register per-type vtables

**Priority**: P3 (architecture)
**Status**: Planned
**Effort**: M

## Problem

TODO 23 phase 2 added the `TaurusNodeVTable` struct to `dom/node.h`
and a contract test that verifies the type enum values.  Phases 3-4
were deferred.

Phase 3 (this TODO): each node type registers its vtable, and the
`_create` functions set the vtable pointer on the new node.  After
this lands, dispatch *can* go through the vtable even if no caller
uses it yet — phase 4 (TODO 30) is what switches the serializer over.

## Fix

### Step 1: Add vtable pointer to `TaurusNode`

```c
// dom/node.h
typedef struct taurus_node {
    const TaurusNodeVTable* vtable;   /* NEW: per-type dispatch table */
    TaurusNodeTypeEnum type;          /* kept for backwards compat */
    unsigned int frozen : 1;
    unsigned int version : 31;
} TaurusNode;
```

Memory cost: 8 bytes per node.  For a 96-byte element that's ~8%;
acceptable for the maintainability win.

### Step 2: Per-type vtable definitions

Each node type's `.c` file defines a `static const TaurusNodeVTable`:

```c
// dom/text.c
static void text_serialize(TaurusNode* self, SerializeBuffer* buf);

static const TaurusNodeVTable TEXT_VTABLE = {
    .serialize  = text_serialize,
    .type_name  = "text",
    .type_enum  = TAURUS_NODE_TYPE_TEXT,
};
```

Same for `element.c`, `comment.c`, `cdata.c`, `pi.c`, `doctype.c`.

### Step 3: Set vtable on creation

Each `*_create` function sets `node->base.vtable = &TEXT_VTABLE;`
(or equivalent).  After this, every node has a non-NULL vtable.

### Step 4: Public query API

Add:

```c
TAURUS_API const char* taurus_node_type_name(TaurusNodeRef node);
```

Returns `node->vtable->type_name`.  Replaces the magic-number
`taurus_node_get_type()` for diagnostic use.

## Tests

`test/dom/test_vtable.cpp`:

```cpp
TEST(NodeVTable, TextNodeHasRegisteredVTable) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusTextNode* t = taurus_text_create("hi", 2, pool);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(((TaurusNode*)t)->vtable, nullptr);
    EXPECT_STREQ(((TaurusNode*)t)->vtable->type_name, "text");
    EXPECT_EQ(((TaurusNode*)t)->vtable->type_enum, TAURUS_NODE_TYPE_TEXT);
    taurus_pool_destroy(pool);
}

TEST(NodeVTable, EveryNodeTypeHasVtable) {
    // Build one of each, assert vtable != NULL on each.
}

TEST(NodeVTable, TypeNameIsHumanReadable) {
    // Public API: taurus_node_type_name returns "text"/"element"/etc.
}
```

## Architecture notes

Phase 3 is **additive** — the vtable field exists but no caller uses
it for dispatch yet.  The existing switch statements still work
unchanged.  This keeps the change low-risk; phase 4 (TODO 30) then
swaps the dispatch.

## Verification

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R VTable
# All NodeVTable specs pass.

grep -rn "->vtable = &" src/taurus/dom/   # one per node type
```
