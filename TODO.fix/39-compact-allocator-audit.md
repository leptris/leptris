# TODO 39: Compact allocator audit

**Priority**: P2 (correctness — untested subsystem)
**Status**: Planned
**Effort**: M

## Problem

`src/taurus/dom/compact_allocator.c` is a separate allocator path
used by the compact-pointer encoding (the 4-byte pointers in the
"compact" DOM architecture).  It has zero test coverage and hasn't
been audited for leaks.

The compact allocator has its own `g_overflow_table` global (now
thread-local per TODO 27).  Operations on it (allocate, decode,
cleanup) are not specced.

## Fix

### Phase 1: read + understand

Document the compact allocator's lifecycle:
- When does an allocation use the compact path vs. the regular pool?
- When do entries get added to the overflow table?
- When are they cleaned up?

### Phase 2: specs

`test/dom/test_compact.cpp`:

```cpp
TEST(CompactAllocator, AllocatesAndDecodes) { /* ... */ }
TEST(CompactAllocator, OverflowTableGrowsWithAllocations) { /* ... */ }
TEST(CompactAllocator, CleanupReleasesOverflowEntries) { /* ... */ }
TEST(CompactAllocator, NoLeaksAcrossManyDocuments) {
    /* Parse N documents; verify overflow table doesn't grow unbounded. */
}
```

### Phase 3: fix what the audit finds

Likely findings:
- Overflow table cleanup happens per-document but if a document is
  freed while another is mid-parse, entries may be prematurely freed.
- The table grows by doubling but never shrinks — long-running
  processes that hit a peak then idle will retain memory.

## Tests

Phase 2 specs are the deliverable.

## Architecture notes

The compact allocator is an **internal optimization** — invisible to
the public API.  But its correctness is load-bearing: a bug here
corrupts the DOM in ways that are hard to debug (random crashes,
silent data loss).

The `g_overflow_table` being `__thread` is the right scope for now —
thread-local makes it impossible for one thread's parse to corrupt
another's.  But the cleanup-vs-in-use race (phase 3) is still a
concern within a single thread if the user does re-entrant parsing.

## Verification

```bash
ctest --test-dir build --output-on-failure -R Compact
# All specs pass; under leaks, zero bytes leaked.
```
