# TODO 68: Pool stress test

**Priority**: P2 (correctness — verify the pool under load)
**Status**: Planned
**Effort**: S

## Problem

The pool's hash table grows (TODO 36) but doesn't shrink.  The
oversized-allocation side list (TODO 06) is also untested under
high churn.  These paths need stress specs.

## Fix

Add specs to `test/memory/test_pool.cpp`:

```cpp
TEST(PoolStress, HighChurnDoesNotLeak) {
    /* 1000 documents, each parsing a small XML.  Verify pool
     * destroy cleans up after each. */
}

TEST(PoolStress, ManyOversizedAllocationsTracked) {
    /* Allocate 100 oversized blocks; verify all freed at destroy. */
}

TEST(PoolStress, HashTableGrowsAndShrinksCorrectly) {
    /* Insert 1000 unique strings; verify all resolvable; grow
     * happened; no entries lost. */
}

TEST(PoolStress, LargeDocumentDoesNotLeak) {
    /* Parse a 10MB document; verify zero leaks. */
}
```

## Tests

The specs above are the deliverable.

## Verification

```bash
ctest --test-dir build --output-on-failure -R PoolStress
leaks --atExit -- build/test/test_pool
```
