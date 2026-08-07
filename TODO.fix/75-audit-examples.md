# TODO 75: Audit examples/c

**Priority**: P3 (docs — examples may not compile after refactor)
**Status**: Planned
**Effort**: S

## Problem

`examples/c/` contains 5 example programs:

- `basic_example.c`
- `dom_traversal_example.c`
- `error_handling_example.c`
- `namespace_example.c`
- `xpath_example.c`

These were written against the public API as of the initial commit.
The refactor changed several signatures (e.g., node creation now
requires a pool).  The examples may not compile against the new API.

## Fix

1. `cmake -B build -S . -DBUILD_EXAMPLES=ON`
2. `cmake --build build`
3. For each compile error, update the example to match the new API.
4. Run each example manually and verify output.

If examples reveal API gaps (e.g., user-facing "create document and
add element" path is missing), surface as new TODOs.

## Verification

```bash
./build/examples/c/basic_example
./build/examples/c/xpath_example
# All examples run and produce sensible output.
```
