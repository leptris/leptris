# TODO 71: Clean compact_allocator state

**Priority**: P3 (hygiene)
**Status**: Planned
**Effort**: S

## Problem

Two compact allocator files exist:

1. `src/leptris/memory/compact_allocator.c` — referenced by
   `src/CMakeLists.txt:33` (`leptris/memory/compact_allocator.c`).
2. `src/leptris/dom/compact_allocator.c` — untracked, 0 bytes (leftover).

The 0-byte `dom/compact_allocator.c` is dead weight — not in the build,
never compiled, but lingers on disk and confuses readers.

## Fix

1. Verify `src/leptris/memory/compact_allocator.c` is the active one
   (it is — see `src/CMakeLists.txt`).
2. Delete the 0-byte `dom/compact_allocator.c`.

Per the global rule, we usually archive.  But this is a 0-byte file
that was never committed and never used — it's a stray from an
earlier abortive Write call.  Safe to remove.

## Verification

```bash
ls src/leptris/dom/compact_allocator.c    # should not exist
ls src/leptris/memory/compact_allocator.c # should still exist
cmake --build build                       # clean
```
