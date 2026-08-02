# TODO 02: Recreate `test/` directory with Google Test

**Priority**: P0 (build blocker + correctness safety net)
**Status**: Planned
**Effort**: M

## Problem

`CMakeLists.txt:54` does `add_subdirectory(test)` and `BUILD_TESTING=ON`
is the default, but the `test/` directory does not exist in the tree. The
configuration step aborts:

```
CMake Error at CMakeLists.txt:54 (add_subdirectory):
  add_subdirectory given source "test" which is not an existing directory.
```

Worse: there are **zero tests** committed. Every other fix in this plan
needs a test harness to be verifiable.

## Root cause

Tests were never committed in the initial `feat: initial commit` (only
one commit on `main`).

## Fix

Create `test/` with the layout below. Google Test via CMake
`FetchContent` (works on all platforms without vcpkg/Homebrew).

```
test/
├── CMakeLists.txt
├── README.md
├── fixtures/
│   ├── basic.xml
│   ├── namespaces.xml
│   ├── cdata.xml
│   ├── malformed.xml
│   └── deep.xml
├── smoke/
│   └── test_smoke.cpp            # parse + free, no leaks
├── parser/
│   └── test_parser.cpp           # malformed input, encoding, BOM, decl
├── dom/
│   └── test_dom.cpp              # node creation, traversal, modification
├── memory/
│   └── test_pool.cpp             # pool lifecycle, oversized alloc, stats
├── xpath/
│   └── test_xpath.cpp            # axes, functions, predicates
└── serializer/
    └── test_serialize.cpp        # round-trip, escaping, realloc growth
```

`test/CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_rt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
add_custom_target(check_all)

function(taurus_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE taurus GTest::gtest_main)
    target_include_directories(${name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include)
    gtest_discover_tests(${name})
    add_dependencies(check_all ${name})
endfunction()

taurus_add_test(test_smoke       smoke/test_smoke.cpp)
taurus_add_test(test_parser      parser/test_parser.cpp)
taurus_add_test(test_dom         dom/test_dom.cpp)
taurus_add_test(test_pool        memory/test_pool.cpp)
taurus_add_test(test_xpath       xpath/test_xpath.cpp)
taurus_add_test(test_serialize   serializer/test_serialize.cpp)
```

## Tests

The harness itself is the deliverable. Each subsequent TODO adds specs
under the matching module directory.

**Policy (project-wide, applies to every spec):**
- Use **real model instances** — no `Mock*`, no test doubles. If a model
  is hard to construct in a test, build a fixture factory, not a stub.
- Assert on observable behavior: return codes, serialized output, document
  state. Do **not** assert on call counts or internal flag states.
- One `TEST` per behavior; group related behaviors in a `TEST_F` fixture.
- Each test file compiles as its own executable so failures are isolated.

## Architecture notes

MECE for test layout: one directory per module under `src/taurus/`. A
behavior change in `src/taurus/memory/pool.c` adds a test in
`test/memory/test_pool.cpp`. No "miscellaneous" bucket — every test has a
natural home.

OCP: the `taurus_add_test` function above makes adding a new test
executable a one-line declaration. No CMake boilerplate to copy-paste.

## Verification

```bash
cmake -B build -S . -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Every test executable is registered with `gtest_discover_tests`, so each
`TEST` shows up as an individual ctest entry.
