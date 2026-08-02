# TODO 35: ASAN build target

**Priority**: P2 (testing infrastructure)
**Status**: Planned
**Effort**: S

## Problem

The library has had multiple memory-safety bugs (use-after-free in the
hash table, double-free risk in doctype, pool-list corruption).
Several were caught by `leaks`/valgrind during the validation passes,
but there's no automated CI check.

AddressSanitizer (ASAN) catches these bugs at the moment they happen,
with stack traces, instead of waiting for `leaks` to find them at
process exit.

## Fix

Add a CMake option:

```cmake
option(TAURUS_ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(TAURUS_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: ENABLED")
endif()
```

Add a CI job:

```yaml
# .github/workflows/test.yml
asan:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Configure with ASAN
      run: cmake -B build -S . -DTAURUS_ENABLE_ASAN=ON -DBUILD_TESTING=ON
    - name: Build
      run: cmake --build build
    - name: Test under ASAN
      run: ctest --test-dir build --output-on-failure
      env:
        ASAN_OPTIONS: detect_leaks=1:halt_on_error=1
```

## Tests

ASAN is itself the test infrastructure.  No new specs needed; existing
59 specs run under ASAN.

## Architecture notes

ASAN catches:
- Heap-buffer-overflow
- Stack-buffer-overflow
- Use-after-free
- Double-free
- Memory leaks (detect_leaks=1)

This complements `leaks`/valgrind — ASAN is faster and gives better
stack traces, but only on Linux/macOS x86-64.  Run both in CI.

## Verification

```bash
cmake -B build -S . -DTAURUS_ENABLE_ASAN=ON
cmake --build build
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build
# Zero ASAN errors, zero leaks reported by ASAN itself.
```
