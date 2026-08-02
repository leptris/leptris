# TODO 20: Replace magic-number node-type checks with enum constants

**Priority**: P2 (readability / maintainability)
**Status**: Planned
**Effort**: S

## Problem

`src/taurus/taurus.c` uses magic numbers for node-type dispatch:

```c
// taurus.c:2829
if (!node || node->type != 0) return NULL;            // 0 = ELEMENT

// taurus.c:2838-2842
if (node->type == 1) { /* TAURUS_NODE_TYPE_TEXT */     // 1 = TEXT
    return ((TaurusTextNode*)node)->content;
}
if (node->type == 3) { /* TAURUS_NODE_TYPE_CDATA */    // 3 = CDATA
    return ((TaurusCDATANode*)node)->content;
}
```

The enum exists (`src/taurus/dom/node.h:20-28`):

```c
typedef enum {
    TAURUS_NODE_TYPE_ELEMENT = 0,
    TAURUS_NODE_TYPE_TEXT = 1,
    TAURUS_NODE_TYPE_COMMENT = 2,
    TAURUS_NODE_TYPE_CDATA = 3,
    TAURUS_NODE_TYPE_PI = 4,
    TAURUS_NODE_TYPE_DOCTYPE = 5,
    TAURUS_NODE_TYPE_ATTRIBUTE = 6
} TaurusNodeTypeEnum;
```

But the implementation uses the integer values directly, with the
enum names only in comments. This is a textbook DRY violation: the
mapping from name to value lives in two places (the enum and the
comments), and they can drift.

## Root cause

The node-type enum is declared in `dom/node.h` (an internal header)
but `taurus.c` historically avoided including it. The magic numbers
were a way to keep `taurus.c` decoupled from the DOM internal layout.

That coupling exists anyway (the file does `(TaurusTextNode*)node`
casts that require full struct visibility), so the avoidance was
misplaced.

## Fix

1. `#include "dom/node.h"` from `taurus.c` (already transitively
   pulled in via other headers; just make it explicit).

2. Replace every `node->type == <int>` with the enum name.

3. Same for any other file with the same pattern (`grep -rn
   "type == [0-9]" src/taurus/`).

```c
// Before
if (!node || node->type != 0) return NULL;
// After
if (!node || node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;
```

## Tests

No behavioral change. Existing specs cover correctness.

## Architecture notes

**Model-driven naming**: the enum names are domain concepts
("element", "text", "CDATA"). The integers are implementation
mechanics. Code should speak in domain concepts.

This change also makes the public API honest. The docstring for
`taurus_node_get_type` says:

> @return Node type code (0=Element, 1=Text, 2=Comment, ...)

That's a documentation of magic numbers. After this fix, the enum
names are the canonical reference; the docstring can simply say "see
`TaurusNodeTypeEnum`" (which we should also expose publicly as part
of TODO 23 — see vtable design).

## Verification

```bash
grep -rn "type == [0-9]" src/taurus/    # zero hits
grep -rn "type != [0-9]" src/taurus/    # zero hits
cmake --build build                     # clean
```
