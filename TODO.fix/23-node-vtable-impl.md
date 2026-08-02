# TODO 23: Implement `TaurusNodeVTable` (phases 2-4 of TODO 14)

**Priority**: P3 (architecture)
**Status**: Planned — implementation
**Effort**: L (multi-phase)

## Problem

TODO 14 designed the vtable; this TODO implements it. The current
dispatch is `switch (node->type)` in three places:

1. `src/taurus/serialize/serialize.c` — picks `serialize_*_internal`.
2. The free path (post-TODO 05) — currently a no-op since pool owns
   everything, but if anything ever needs per-type cleanup, it'll be
   a switch.
3. Various ad-hoc type checks scattered through `taurus.c` (e.g.,
   `if (node->type == 1) return ((TaurusTextNode*)node)->content;`).

Each new node type requires editing every switch — Open/Closed
Principle violation.

## Implementation plan

### Phase 2: Define and register the vtable

`src/taurus/dom/node.h` (extended):

```c
struct SerializeBuffer;  // forward
struct TaurusMemoryPool;

typedef struct taurus_node_vtable {
    /* Serialize this node + descendants into buf. */
    void (*serialize)(struct taurus_node* self, struct SerializeBuffer* buf);

    /* Walk children. NULL for leaf types (text, comment, cdata, pi). */
    struct taurus_node* (*first_child)(struct taurus_node* self);
    struct taurus_node* (*next_sibling)(struct taurus_node* self);

    /* Deep clone into target pool. */
    struct taurus_node* (*clone)(struct taurus_node* self,
                                  struct TaurusMemoryPool* target);

    /* Diagnostic name (for error messages). */
    const char* type_name;

    /* The TaurusNodeTypeEnum value — lets generic code recover the
     * type without a separate field on the node. */
    int type_enum;
} TaurusNodeVTable;
```

Every node struct's first member becomes `const TaurusNodeVTable* vtable`:

```c
typedef struct taurus_node {
    const TaurusNodeVTable* vtable;
} TaurusNode;

typedef struct taurus_text_node {
    TaurusNode base;          // vtable lives here
    char* content;
    void* next_sibling;
} TaurusTextNode;
```

Wait — `TaurusNode` currently has `type`, `frozen`, `version`. We need
to preserve those. Either:

- Keep them as separate fields after the vtable pointer.
- Move `type` into the vtable (`vtable->type_enum`) and keep `frozen`
  /`version` as fields.

Option B is cleaner — one source of truth for the type identity.

### Phase 3: Per-type vtable definitions

`src/taurus/dom/text.c`:

```c
static void text_serialize(TaurusNode* self, SerializeBuffer* buf) {
    serialize_text_internal((TaurusTextNode*)self, buf);
}

static const TaurusNodeVTable TEXT_VTABLE = {
    .serialize     = text_serialize,
    .first_child   = NULL,                       // leaf
    .next_sibling  = NULL,                       // managed by parent element
    .clone         = text_clone,
    .type_name     = "text",
    .type_enum     = TAURUS_NODE_TYPE_TEXT,
};
```

Same for `element.c`, `comment.c`, `cdata.c`, `pi.c`, `doctype.c`.

The vtable is `static const` — lives in read-only memory, costs zero
runtime allocation.

Each `*_create` function sets `node->base.vtable = &TEXT_VTABLE;`
(or equivalent).

### Phase 4: Replace switch statements

`src/taurus/serialize/serialize.c`:

```c
// Before: 40-line switch
void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT: serialize_element_internal(...);
        case TAURUS_NODE_TYPE_TEXT:    serialize_text_internal(...);
        // ... etc
    }
}

// After: one-line dispatch
void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf) {
    if (node && node->vtable && node->vtable->serialize) {
        node->vtable->serialize(node, buf);
    }
}
```

`taurus.c`:

```c
// Before
const char* taurus_text_node_get_content(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == 1) return ((TaurusTextNode*)node)->content;
    if (node->type == 3) return ((TaurusCDATANode*)node)->content;
    return NULL;
}

// After (sketch — would need to expose `content` via the vtable or
// keep this as a type-checked switch)
```

Note: not every switch becomes vtable dispatch. Only switches where
the operation is genuinely polymorphic (serialize, clone, free).
Type-specific accessors like `taurus_text_node_get_content` should
stay explicit — they document the contract that the caller knows the
node type.

## Trade-offs (recap from TODO 14)

- **Memory cost**: one pointer per node (~1% increase on a 96-byte
  element).
- **Indirect call**: marginally slower than a switch in theory, but
  branch prediction makes it a wash on modern CPUs.
- **Phase 4 is a breaking public API change** if we expose the vtable
  in `taurus.h`. Don't expose it — keep it internal for now.

## Phasing for this session

Phase 2 + a sliced phase 3/4:

1. Define `TaurusNodeVTable` and add the `vtable` field to `TaurusNode`.
2. Register vtables for **`text` and `comment`** (smallest types, proof
   of concept).
3. Refactor `serialize_node_internal` to dispatch via vtable for these
   two types; fall back to switch for others.
4. Add a test that exercises both paths.

Future sessions extend to all types. The pattern is established.

## Tests

`test/dom/test_vtable.cpp` (new):

```cpp
#include "dom/node.h"  // internal header — needs the vtable definition

TEST(NodeVTable, TextNodeHasRegisteredVTable) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusTextNode* t = taurus_text_create("hi", 2, pool);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(((TaurusNode*)t)->vtable, nullptr);
    EXPECT_STREQ(((TaurusNode*)t)->vtable->type_name, "text");
    EXPECT_EQ(((TaurusNode*)t)->vtable->type_enum, TAURUS_NODE_TYPE_TEXT);
    taurus_pool_destroy(pool);
}

TEST(NodeVTable, SerializeDispatchesViaVTable) {
    // Round-trip a doc containing one text node; verify output.
    // This implicitly exercises vtable dispatch in the serializer.
    const char xml[] = "<r>hello</r>";
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

This is the textbook OOP-in-C pattern, used by `libxml2`, `GObject`,
and `pugixml`. The benefit isn't novelty — it's that dispatch becomes
**data, not code**, so adding a new node type is purely additive.

After this lands, adding `TaurusNotationNode` is a new file with a new
vtable, and the serializer / cloner pick it up automatically.

## Verification

```bash
cmake --build build
ctest --test-dir build
./bench benchmarks/data/small.xml 5000     # no perf regression
```
