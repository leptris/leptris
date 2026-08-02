# TODO 52: Remove unused compact_allocator code

**Priority**: P3 (hygiene)
**Status**: Planned
**Effort**: S

## Problem

`src/taurus/dom/compact_allocator.c` has functions flagged as unused
by `-Wunused-function`:

```
src/taurus/dom/compact_allocator.c:XX: warning: unused function '...' [-Wunused-function]
```

Dead code is reader-tax and confuses greps.

## Root cause

The compact allocator was written for the original compact-pointer
design.  After the architecture migrated to the hybrid model (regular
pointers for hot paths, compact for cold), some functions became
unreachable.  They were never deleted.

## Fix

1. **Verify each flagged function is truly unused**: `grep -rn
   "<function_name>" src/` — if zero callers, it's dead.
2. **Move dead functions to `archive/`** (not `rm` — per the global
   rule "NEVER DELETE source files").  Specifically:
   `archive/dom_legacy/compact_allocator_unused.c`.
3. **Or mark with `__attribute__((unused))`** if the function is part
   of a public-ish API that's just not used internally yet.
4. **Or delete entirely** if the user agrees (this is the global rule
   exception — only with explicit approval).

Default to option 1 (archive) per the global rule.

## Tests

No behavioral change.  Existing specs cover correctness.

## Verification

```bash
cmake --build build 2>&1 | grep "compact_allocator" | grep "unused"
# Expected: zero hits.
```
