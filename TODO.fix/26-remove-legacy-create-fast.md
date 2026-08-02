# TODO 26: Remove legacy `_create_fast` wrappers

**Priority**: P2 (cleanup)
**Status**: Planned
**Effort**: S

## Problem

After TODO 18, the codebase was supposed to consolidate every node
type's `_create` / `_create_fast` pair into a single pool-routed
create.  The implementation got most of the way there but left two
"backwards-compat wrappers" that just call through to the canonical
function:

```
src/taurus/dom/comment.c:  taurus_comment_create_fast() → taurus_comment_create()
src/taurus/dom/pi.c:       taurus_pi_create_fast()       → taurus_pi_create()
```

And one function declaration that should have been deleted:

```
src/taurus/dom/element.h:  taurus_element_create_fast()
```

These wrappers violate DRY (two names for the same operation) and
make the API surface confusing — callers have to choose between two
identical-behavior functions.

## Root cause

I (Claude) left the wrappers in during TODO 18 to avoid breaking
external callers.  But there are no external callers — the codebase
is self-contained, and the wrappers are not in any public header.

## Fix

1. Delete `taurus_comment_create_fast` from `comment.c` and
   `comment.h`.
2. Delete `taurus_pi_create_fast` from `pi.c` and `pi.h`.
3. Delete `taurus_element_create_fast` from `element.c` and
   `element.h`.
4. Update any in-repo callers (likely none, but check `grep -rn
   "_create_fast"`).

## Tests

No behavioral change.  Existing specs cover correctness.

## Architecture notes

DRY: one creation path per node type.  After this fix, `grep -rn
"_create_fast" src/` returns zero hits in `.c` files (only in
comments explaining the consolidation).

## Verification

```bash
grep -rn "_create_fast" src/taurus/ | grep -v "//\|/\*"   # zero hits
cmake --build build                                        # clean
ctest --test-dir build                                     # 100% pass
```
