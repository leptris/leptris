# TODO 30: Vtable phase 4 — serializer dispatch through vtable

**Priority**: P3 (architecture)
**Status**: Planned
**Effort**: M

## Problem

`src/taurus/serialize/serialize.c::serialize_node_internal()` is a
40-line switch statement on `node->type`.  Adding a new node type
requires editing this switch — Open/Closed Principle violation.

After TODO 29, every node has a vtable.  Phase 4 replaces the switch
with a vtable dispatch.

## Fix

```c
// Before
void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    if (!node) return;
    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT:  serialize_element_internal(...);  break;
        case TAURUS_NODE_TYPE_TEXT:     serialize_text_internal(...);     break;
        case TAURUS_NODE_TYPE_COMMENT:  serialize_comment_internal(...);  break;
        case TAURUS_NODE_TYPE_CDATA:    serialize_cdata_internal(...);    break;
        case TAURUS_NODE_TYPE_PI:       serialize_pi_internal(...);       break;
        case TAURUS_NODE_TYPE_DOCTYPE:  serialize_doctype_internal(...);  break;
    }
}

// After
void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    if (!node || !node->vtable || !node->vtable->serialize) return;
    node->vtable->serialize(node, buf);
}
```

Each per-type vtable's `serialize` callback wraps the existing
`serialize_*_internal` function — no behavior change, just dispatch
routing.

## Tests

Existing round-trip specs in `test/serializer/test_serialize.cpp`
verify correctness end-to-end.  Add one new spec that exercises
**every** node type in a single document — guarantees each vtable's
serialize is wired correctly:

```cpp
TEST(SerializeVTableDispatch, EveryNodeTypeSerializesCorrectly) {
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- doc-level comment -->"
        "<r attr='v'><!-- nested -->text<![CDATA[raw]]><?pi data?>"
        "<child/></r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_document_serialize(doc, NULL);
    EXPECT_STREQ(out, xml);
    taurus_free_string(out);
    taurus_document_free(doc);
}
```

## Architecture notes

After this lands, adding `TaurusNotationNode` is purely additive:

1. Create `dom/notation.c` + `notation.h` with a `NOTATION_VTABLE`.
2. Wire it into the parser.
3. The serializer picks it up automatically via vtable dispatch —
   **no edit to serialize.c**.

This is the textbook OCP payoff.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Serialize
# All serialize specs pass, including the new EveryNodeType spec.

grep "switch.*node->type" src/taurus/serialize/serialize.c
# Expected: zero hits — dispatch is via vtable, not switch.
```
