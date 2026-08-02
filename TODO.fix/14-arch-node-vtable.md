# TODO 14: Architecture — introduce `TaurusNodeVTable`

**Priority**: P3 (architecture)
**Status**: Design only this session
**Effort**: L (multi-phase)

## Problem

The codebase uses `switch (node->type)` to dispatch on node kind. Three
sites, each a near-copy of the others:

1. **Serializer** (`src/taurus/serialize/serialize.c`): switch on
   `node->type` to pick the right `serialize_*_internal` function.
2. **Free path** (will exist after TODO 05): switch on `node->type`
   to call the right destructor.
3. **Parser node dispatch** (`src/taurus/parse/parser_new.c:1596`):
   switch on first-character + lookahead to pick which parser to invoke.

Each new node type requires editing every switch. That violates the
Open/Closed Principle: the serializer is closed for modification in
name only — adding a `TaurusNotationNode` would still touch three
files.

There's also implicit duplication: each switch statement's case order
must be kept in sync with the `TaurusNodeTypeEnum`, and there's no
compiler-checked contract ensuring all cases are handled.

## Root cause

The DOM was modeled as a tagged union (struct with a `type` enum +
per-type data via cast) without the corresponding vtable. That's a
classic OOP-in-C oversight.

## Fix (design)

### Phase 1: Define the vtable

`src/taurus/dom/node.h`:

```c
typedef struct taurus_node_vtable {
    /* Serialize this node and its descendants to buf. */
    void (*serialize)(TaurusNode* node, SerializeBuffer* buf);

    /* Walk this node's children (for tree traversal). */
    TaurusNode* (*first_child)(TaurusNode* node);
    TaurusNode* (*next_sibling)(TaurusNode* node);

    /* Deep-clone into a target pool (post-TODO 05). */
    TaurusNode* (*clone)(TaurusNode* node, TaurusMemoryPool* target_pool);

    /* Human-readable type name, for diagnostics. */
    const char* type_name;
} TaurusNodeVTable;
```

The first field of every node struct becomes a `const TaurusNodeVTable*`:

```c
typedef struct taurus_node {
    const TaurusNodeVTable* vtable;   // was: TaurusNodeTypeEnum type
    // ... existing fields
} TaurusNode;
```

### Phase 2: Each node type registers its vtable

`src/taurus/dom/text.c`:

```c
static void text_serialize(TaurusNode* n, SerializeBuffer* buf) {
    serialize_text_internal((TaurusTextNode*)n, buf);
}
static const TaurusNodeVTable TEXT_VTABLE = {
    .serialize     = text_serialize,
    .first_child   = NULL,                  // text nodes have no children
    .next_sibling  = NULL,                  // managed by parent element
    .clone         = text_clone,
    .type_name     = "text",
};
```

Same for `element.c`, `comment.c`, `cdata.c`, `pi.c`, `doctype.c`.

The vtable is constructed at link time — it's a `static const`, so it
lives in read-only memory and costs zero runtime allocation.

### Phase 3: Replace switch statements with vtable dispatch

`serialize.c`:

```c
// Before: 40-line switch statement
// After:
void taurus_serialize_node(TaurusNode* node, SerializeBuffer* buf) {
    node->vtable->serialize(node, buf);
}
```

The free path post-TODO 05:

```c
// Instead of a switch on type, the vtable exposes free.
// (Or skip the vtable entirely — pool-allocated nodes don't need a
// per-type free; only the element type has internal state to clean up,
// and that's exposed as element_destroy_if_dynamic.)
```

### Phase 4: Public API

Expose `taurus_node_type_name(TaurusNodeRef)` → string, replacing the
opaque `int taurus_node_get_type(TaurusNodeRef)` whose return values
are magic numbers documented only in the header comment.

## Trade-offs

**Pros**:
- OCP: adding `TaurusNotationNode` = one new file + registration. No
  switch to update. No way to forget a case.
- DRY: the dispatch lives in exactly one place (the vtable field).
- Type-safe: `node->vtable->serialize(node, buf)` is a function call,
  not a cast-plus-function-pointer dance.
- Diagnostics: the `type_name` field makes error messages immediately
  readable ("text node cannot have children").

**Cons**:
- One extra pointer per node (8 bytes). For the 96-byte compact
  element, that's an 8% memory increase.
- Indirect call (slightly slower than a switch, theoretically). On
  modern CPUs with branch prediction, this is typically a wash — the
  branch predictor learns the dispatch pattern quickly.

The 8% memory increase can be mitigated: only elements and a few other
types need the full vtable; for nodes where dispatch is trivial (text,
comment), keep the current `type` enum and have the serializer do a
small switch on it. Hybrid approach, but only for the hot path.

## Tests

After the refactor, the existing serializer/parser specs must all pass
unchanged — the vtable is purely an internal refactor.

Add a new spec in `test/dom/test_vtable.cpp`:

```cpp
TEST(NodeVTable, EveryNodeTypeHasARegisteredVTable) {
    // Build one of each node type; verify the vtable pointer is set.
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusTextNode*    text    = taurus_text_create(pool);
    TaurusCommentNode* comment = taurus_comment_create(pool);
    // ... etc

    EXPECT_NE(((TaurusNode*)text)->vtable,    nullptr);
    EXPECT_NE(((TaurusNode*)comment)->vtable, nullptr);
    EXPECT_STREQ(((TaurusNode*)text)->vtable->type_name, "text");

    taurus_pool_destroy(pool);
}

TEST(NodeVTable, SerializeDispatchesViaVTable) {
    // Round-trip test: parse a doc, serialize it, verify the output
    // matches the input character-for-character.
    // This was already verified manually; now it's a regression guard.
}
```

## Architecture notes

This is the textbook OOP-in-C pattern, used by `libxml2` (with
`xmlElementType` + per-type `xmlFreeFunc`), ` GObject`, `pugixml`
(ad-hoc). The benefit isn't novelty — it's that the dispatch table
becomes data, not code, so adding a new type is purely additive.

**Why this is phased**: phase 1-3 is a refactor of the dispatch path
only. Phase 4 (public API change) is a breaking change and needs a
major version bump. Don't rush it.

**Phase 1 deliverable this session**: design document only. Sketch the
vtable shape, document which switches it replaces, identify the test
coverage needed. Implement in a follow-up after TODOs 05 and 07 land
(both touch the dispatch path).

## Verification

After phase 1-3 lands:
1. All existing specs pass.
2. New vtable specs pass.
3. `benchmarks/dom/dom_benchmark_v2` shows parse+serialize within
   ±5% of pre-refactor numbers (the indirect call overhead is real
   but small).
4. Adding a synthetic `TaurusNotationNode` for testing takes <50 lines
   in one new file, with no edits to the serializer or parser.
