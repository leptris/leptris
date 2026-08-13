# TODO 170 — Amalgamation build

## Status

Pending. pugixml's actual technique — generate a single ~50k-line
`.c` that includes all internal sources, compile as one TU. Gets
cross-TU inlining without requiring LTO.

## Why

With LTO already on by default (TODO 167), amalgamation's main
wins are:
1. Compile-time inlining across "TU boundaries" — compiler sees
   the whole library at once, can specialize and constant-fold
   more aggressively than LTO's link-time pass.
2. Faster link time (one object instead of 55).
3. Distribution as a single .c file (for embedding in other
   projects).

## Plan

### Phase A — CMake generation

Add `TAURUS_AMALGAMATED` option (default OFF). When enabled:
1. Generate `${CMAKE_BINARY_DIR}/taurus_amalgamated.c` from
   `${TAURUS_SOURCES}` via `configure_file` or `file(WRITE)`.
2. Build the library from this single file instead of the
   separate .c files.
3. The generated file is just a series of `#include "foo.c"`
   lines, with relative paths resolved against `src/`.

### Phase B — Verification

- Build with `-DTAURUS_AMALGAMATED=ON` and run all 464 tests.
- Verify no symbol collisions on static functions (audit by
  listing all `static` definitions and checking uniqueness).
- Benchmark: amalgamation vs multi-TU+LTO. The expectation is
  small (<5%) on top of LTO.

## Risk

- **Static name collisions.** If two .c files declare `static
  int helper()`, amalgamation fails. Need to audit and rename
  if any conflicts.
- **Macro re-definition.** Two .c files might `#define FOO`
  differently. Need to check.
- **Conditional compilation.** Source files guarded by
  `TAURUS_HAS_ICONV` etc. must keep their guards.

## Expected impact

- Without LTO: 20-30% (cross-TU inlining is the only path).
- With LTO (default): <5% (LTO already does most of it).
- Compile time: 1.5-2× faster link.

## Status

Pending.
