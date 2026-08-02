# TODO 19: Fix `TaurusElement` ↔ `TaurusNode*` pointer-type warnings

**Priority**: P2 (code quality)
**Status**: Planned
**Effort**: M

## Problem

`cmake --build build` emits 21 warnings of the form:

```
src/taurus/dom/element_modify.c:133:59: warning: incompatible pointer
  types assigning to 'struct taurus_node *' from 'TaurusElement'
  (aka 'struct taurus_element *') [-Wincompatible-pointer-types]
```

Locations:
- `src/taurus/dom/element_modify.c` (~15 sites)
- `src/taurus/dom/element.c` (~3 sites)
- `src/taurus/dom/compact.c` (1 site)

## Root cause

The codebase freely mixes two related types:

- `TaurusNode*` — the generic node base (tagged-union discriminator).
- `TaurusElement*` — a specific node type that inherits from
  `TaurusNode` (its struct begins with `TaurusNode base;`).

In C, distinct typedefs are distinct types even if they point to
compatible structs. The compiler is correct to warn.

The DOM was designed around the assumption that
"`TaurusElement` IS-A `TaurusNode`, so casting is safe" — and that's
true at the layout level. But the type system doesn't know that,
leading to noise.

## Fix

Three options; pick by ergonomic impact:

### Option A — Explicit casts at every site

```c
// Before
TaurusNode* n = child;
// After
TaurusNode* n = (TaurusNode*)child;
```

Mechanical, verbose, and silences the warning without changing
behavior. ~20 sites.

### Option B — Add a tiny inline coercion helper

```c
// dom/node.h
static inline TaurusNode* taurus_element_as_node(TaurusElement e) {
    return (TaurusNode*)e;
}
```

(We already added this as a public API in TODO 09.) Callers use it
explicitly. Slightly cleaner than option A.

### Option C — Make `TaurusElement` and `TaurusNode*` the same typedef

```c
typedef struct taurus_element* TaurusElement;
typedef struct taurus_element* TaurusNodeRef;  // was: struct taurus_node*
```

This is technically a regression (loses the type distinction that
*should* exist between elements and other node kinds) but matches the
codebase's actual usage pattern. **Not recommended** unless paired
with a real OCP refactor (TODO 23).

### Recommendation: Option B

Use `taurus_element_as_node(elem)` everywhere a generic node handle is
needed. The reverse direction already has `taurus_node_as_element`
(with a NULL return for non-elements). Both directions are explicit,
type-checked where it matters, and readable.

## Tests

No behavioral change — warnings only. Existing specs cover
correctness.

Add one spec to document the relationship:

```cpp
TEST(DomTypeRelationship, ElementAsNodeIsAlwaysValid) {
    TaurusMemoryPool* pool = taurus_pool_create();
    TaurusElement elem = taurus_element_create("r", 1, pool);
    ASSERT_NE(elem, nullptr);

    // Every element IS-A node; the cast is always safe.
    TaurusNodeRef node = taurus_element_as_node(elem);
    EXPECT_EQ(taurus_node_get_type(node), kNodeTypeElement);

    // The reverse direction returns NULL for non-elements.
    TaurusElement back = taurus_node_as_element(node);
    EXPECT_EQ(back, elem);

    taurus_pool_destroy(pool);
}
```

## Architecture notes

The current mixing is a DRY violation in spirit — the same fact
("TaurusElement begins with TaurusNode") is encoded in both the
layout (the struct definition) and at every call site (the implicit
cast). Option B makes the cast explicit, which is honest about what's
happening.

The real fix is TODO 23 (node vtable) — once dispatch is data-driven,
the type distinction becomes meaningful again and option C stops
being tempting.

## Verification

```bash
cmake --build build 2>&1 | grep -c "incompatible-pointer-types"
# Expected: 0
```
