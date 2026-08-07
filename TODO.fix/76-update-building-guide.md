# TODO 76: Update docs/guide/building.md

**Priority**: P3 (docs)
**Status**: Planned
**Effort**: S

## Problem

`docs/guide/building.md` was written for the initial commit and
references build commands that don't match the current state:

- Mentions `TAURUS_BUILD_CLI` default OFF; it's now ON.
- Doesn't mention `TAURUS_ENABLE_ASAN` / `TAURUS_ENABLE_FUZZING`.
- Doesn't mention `TAURUS_BUILD_DOCS`.
- The "Quick Start" doesn't reference the test suite.

## Fix

Rewrite the file to match the current build matrix:

1. Quick start (configure + build + test).
2. All build options (the 8 in CMakeLists).
3. Platform notes (macOS, Linux, Windows).
4. ASAN build instructions.
5. Fuzzer build instructions.
6. Doxygen target.
7. vcpkg integration (still works via `CMAKE_TOOLCHAIN_FILE`).

## Verification

```bash
asciidoctor docs/guide/building.md   # renders cleanly
```
