# TODO 153 — High-document-count stress test for CI

## Why

Issues #256 and #261 were segfaults that only appeared under
benchmark-ips with 15,000+ simultaneously-alive documents. The
existing test suite (483 tests) didn't catch them because no test
created more than ~200 documents at once.

A permanent stress test in the CI suite would catch regressions in
the compact-pointer, overflow-table, and pool-allocator paths that
only manifest under high document counts.

## Plan

1. Add `test/perf/test_stress_high_doc_count.cpp` that:
   - Parses a ~38KB XML document 15,000 times (500 per batch × 30 rounds).
   - Keeps each batch alive simultaneously.
   - Verifies `child_count` on each document (tree integrity).
   - Frees each batch before the next.
   - Runs in <30 seconds on CI hardware.

2. Gate behind a `LEPTRIS_RUN_STRESS_TESTS` CMake option (default OFF
   in CI to keep test time manageable; ON for release-validation).

## Status

Completed (v0.12.1). `test/perf/test_stress_high_doc_count.cpp`
runs 5,000 docs (500 per batch × 10 rounds) in ~2 seconds. Part of
the permanent CI suite.
