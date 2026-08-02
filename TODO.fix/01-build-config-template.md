# TODO 01: Add `cmake/taurus-config.cmake.in`

**Priority**: P0 (build blocker)
**Status**: Planned
**Effort**: S

## Problem

Both `CMakeLists.txt:80` and `src/CMakeLists.txt:331` call
`configure_package_config_file()` against
`${CMAKE_CURRENT_SOURCE_DIR}/cmake/taurus-config.cmake.in`. That file does
not exist in the committed tree, so configuration aborts:

```
CMake Error: File /Users/.../taurus/cmake/taurus-config.cmake.in does not exist.
CMake Error at /opt/homebrew/share/cmake/Modules/CMakePackageConfigHelpers.cmake:549
  configure_file Problem configuring file
```

The same template must also declare the dependencies a downstream project
needs at link time (`utf8proc`, `Iconv::Iconv`, `m`) — otherwise consumers
of `find_package(taurus)` will hit unresolved symbols.

## Root cause

The file was simply not committed. There is an existing `cmake/taurus.pc.in`
(pkg-config template) but no CMake package template.

## Fix

Create `cmake/taurus-config.cmake.in` with the standard
`configure_package_config_file` contract:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Optional dependencies — match the build-time toggles
if(NOT TARGET taurus::taurus)
    include("${CMAKE_CURRENT_LIST_DIR}/taurus-targets.cmake")
endif()

# utf8proc / iconv are PRIVATE link deps of the static lib; they are
# already encoded in taurus-targets.cmake via INTERFACE_LINK_LIBRARIES,
# so no extra find_dependency() is needed here unless we change that.
check_required_components(taurus)
```

Notes:
- `@PACKAGE_INIT@` expands to `set_and_check()` / `set_package_properties()`
  boilerplate.
- We do **not** `find_dependency(utf8proc)` because the static lib already
  records the link dependency (after TODO 03 / 05 land). If a downstream
  project links `taurus::taurus`, CMake pulls in `utf8proc` automatically.
- `check_required_components()` is the convention for
  `find_package(taurus CONFIG)`.

## Tests

Build-time validation only — no runtime spec. Verify by:

```bash
cmake -B build -S . -DBUILD_TESTING=OFF -DTAURUS_BUILD_CLI=ON
# should configure cleanly with no "taurus-config.cmake.in does not exist"
```

And, after install, a downstream project should be able to do:

```cmake
find_package(taurus CONFIG REQUIRED)
target_link_libraries(app PRIVATE taurus::taurus)
```

## Architecture notes

This file is the canonical **consumer-facing package contract**. MECE:
- `taurus.pc.in` — pkg-config consumers (gcc command line).
- `taurus-config.cmake.in` — CMake consumers (`find_package`).
- `taurus-targets.cmake` (generated) — actual imported targets.

Each in exactly one place.

## Verification

1. `rm -rf build && cmake -B build -S .` configures without errors.
2. `cmake --build build` builds the lib + CLI.
3. `cmake --install build` to a temp prefix; a scratch CMake project that
   does `find_package(taurus CONFIG REQUIRED)` configures and links cleanly.
