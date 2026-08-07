# TODO 72: vcpkg.json gtest alignment

**Priority**: P3 (hygiene — affects CI in vcpkg mode)
**Status**: Planned
**Effort**: S

## Problem

`vcpkg.json` declares `gtest` as a dependency:

```json
"dependencies": [
    { "name": "gtest", "platform": "!windows" },
    { "name": "utf8proc", ... },
    { "name": "libiconv", ... }
]
```

But the test harness (in `test/CMakeLists.txt`) uses CMake
`FetchContent` to download Google Test 1.14.0 when an installed
GTest isn't found.  This is intentional — local builds without vcpkg
work via FetchContent.

The issue: when vcpkg is used, the manifest pulls gtest AND FetchContent
also pulls gtest (because GTest::gtest_main isn't found at configure
time — vcpkg installs it as a different target name).

## Fix

Two options:

1. **Drop gtest from vcpkg.json** — let FetchContent always handle it.
   Pro: consistent across environments.  Con: always downloads.

2. **Drop FetchContent fallback** — require vcpkg/Homebrew/system
   gtest.  Pro: no network dependency at build time.  Con: harder
   local dev.

Option 1 is simpler; commit to it.

## Verification

```bash
cmake -B build -S .   # no vcpkg, no system gtest → FetchContent fires
cmake --build build
ctest --test-dir build
```
