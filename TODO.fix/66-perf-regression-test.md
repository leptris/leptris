# TODO 66: Performance regression test

**Priority**: P2 (CI — catch silent perf regressions)
**Status**: Planned
**Effort**: S

## Problem

The validation pass showed attrs.xml had a 3.4× regression.  That
would have been caught earlier with a CI-tracked perf test.

## Fix

Add a Google Test that measures parse throughput and asserts it
stays above a budget:

```cpp
TEST(PerfRegression, ParseThroughputStaysAboveBudget) {
    using namespace std::chrono;
    /* Parse small.xml 1000 times; assert < N ms total. */
    auto start = steady_clock::now();
    for (int i = 0; i < 1000; i++) {
        TaurusDocument doc = taurus_parse_string(...);
        taurus_document_free(doc);
    }
    auto ms = duration_cast<milliseconds>(steady_clock::now() - start);
    EXPECT_LT(ms.count(), 100);  /* budget: 100 ms for 1000 parses */
}
```

Register as a separate ctest label so it can be excluded from quick
runs:

```cmake
set_target_properties(test_perf PROPERTIES LABELS "perf")
```

CI runs the perf label on every PR.

## Tests

The perf test itself is the deliverable.

## Architecture notes

Perf budgets are tricky — they depend on the machine.  Options:

1. **Absolute**: "less than 100ms".  Fragile across CI runners.
2. **Relative to a baseline**: "within 10% of the previous build".
   Requires storing a baseline; more work.
3. **Relative to libxml2**: "taurus time / libxml2 time < 1.5".
   Requires libxml2 as a test dependency.

Start with absolute; refine later.

## Verification

```bash
ctest --test-dir build -L perf --output-on-failure
```
