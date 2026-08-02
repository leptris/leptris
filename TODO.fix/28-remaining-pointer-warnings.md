# TODO 28: Fix remaining 25+6 pointer / visibility warnings

**Priority**: P2 (code quality)
**Status**: Planned
**Effort**: M

## Problem

After TODO 19's bulk sed, 31 warnings remain in the DOM module:

```
  25  incompatible pointer types assigning to 'struct taurus_node *'
       from 'TaurusElement' (aka 'struct taurus_element *')
   6  declaration of 'struct taurus_node' will not be visible outside
       of this function
   5  incompatible pointer types initializing 'TaurusElement' with
       expression of type 'struct taurus_node *'
```

## Root cause

The `TaurusElement` and `TaurusNode*` types are layout-compatible
(every element struct begins with a `TaurusNode base;` header) but
formally distinct typedefs.  Assigning one to the other without an
explicit cast trips `-Wincompatible-pointer-types`.

The bulk sed in TODO 19 caught the most common patterns
(`elem->first_child = child`, etc.) but missed many call-site
specific assignments in `element_modify.c` where the variable types
vary by context.

## Fix

Walk every warning site and add an explicit cast.  Two acceptable
forms:

```c
// Direct C cast (simplest, clearest)
TaurusNode* node = (TaurusNode*)elem;

// Via helper (already in the public API — see TODO 09)
TaurusNodeRef node = taurus_element_as_node(elem);
```

Prefer the direct cast when the code is internal (sibling modules
know the layout); use the helper when crossing module boundaries
(documents intent).

For the 6 visibility warnings, include `dom/node.h` (which defines
the full struct) instead of relying on a forward declaration.

## Tests

No behavioral change.  Existing specs cover correctness.

## Architecture notes

The real fix is TODO 23 (vtable): once dispatch is data-driven, the
type distinction becomes meaningful again and the casts become
unnecessary.  Until then, explicit casts are honest about what's
happening.

## Verification

```bash
touch src/taurus/dom/*.c
cmake --build build 2>&1 | grep "warning:" | wc -l
# Expected: 0 incompatible-pointer-types warnings.
```
