# TODO 63: Doxygen documentation

**Priority**: P3 (docs)
**Status**: Planned
**Effort**: M

## Problem

The public API in `src/include/taurus/` has inline docstrings but
they're inconsistent — some thorough, others one-line.  No generated
API reference exists.

## Fix

### Step 1: add Doxyfile + CMake target

```
docs/Doxyfile:
  PROJECT_NAME = libtaurus
  INPUT        = src/include
  RECURSIVE    = YES
  ...

CMakeLists.txt:
  option(TAURUS_BUILD_DOCS "Build Doxygen API docs" OFF)
  if(TAURUS_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    add_custom_target(docs ...)
  endif()
```

### Step 2: audit docstrings

Walk every public function.  For each:
- `@param` for every parameter (incl. NULL handling)
- `@return` describing return value
- `@since` for the version
- Memory ownership contract ("caller must free with X" or "pool-owned")

## Tests

Documentation itself is the deliverable.

## Verification

```bash
cmake -B build -S . -DTAURUS_BUILD_DOCS=ON
cmake --build build --target docs
ls docs/html/index.html
```
