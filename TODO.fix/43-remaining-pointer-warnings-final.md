# TODO 43: Kill 27 remaining pointer-type warnings

**Priority**: P2 (code quality)
**Status**: Planned
**Effort**: M

## Problem

After TODO 19/28's bulk fixes, 27 `incompatible-pointer-types` warnings
remain in `element_modify.c`.  Each is a `TaurusElement` value being
assigned to / initialized as a `struct taurus_node*` lvalue (or vice
versa).  The compiler is technically correct — the types are distinct
typedefs — even though they're layout-compatible.

## Fix

Walk every warning site (visible via `cmake --build build 2>&1 | grep
incompatible-pointer`) and add an explicit cast.  Two acceptable forms:

```c
// Direct cast — clearest at internal call sites
TaurusNode* n = (TaurusNode*)elem;

// Via the public-API helper — clearest at module boundaries
TaurusNodeRef n = taurus_element_as_node(elem);
```

Prefer direct casts when both source and target are inside the same
module (e.g., `element_modify.c` knows the layout).  Use the helper
when crossing module boundaries (documents intent).

## Tests

No behavioral change.  Existing 69 specs cover correctness.

## Architecture notes

The real fix is the vtable refactor (TODO 23) — once dispatch is
data-driven, the type distinction becomes meaningful again.  Until
then, explicit casts are honest about what's happening.

## Verification

```bash
touch src/taurus/dom/*.c
cmake --build build 2>&1 | grep "incompatible-pointer" | wc -l
# Expected: 0
```
