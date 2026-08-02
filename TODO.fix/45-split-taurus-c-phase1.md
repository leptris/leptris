# TODO 45: Split `taurus.c` — phase 1 (extract `encoding/wrapper.c`)

**Priority**: P3 (architecture — first execution step of TODO 24/42)
**Status**: DESIGN — extraction sketched, execution deferred
**Effort**: M

## Why deferred

Started the extraction; the function has cross-module dependencies
(`taurus_parse`, `taurus_strdup`, `taurus_free_hook`, encoding
helpers) that need careful handling to avoid link errors.  A
half-finished extraction would break the build; better to do this
as a focused refactor session than to rush it in passing.

The plan below is the sketch.  When execution happens, follow it
step-by-step with tests passing between each step.

## Problem

`taurus.c` is 2900+ lines.  TODO 24/42 designed a 5-phase split;
this TODO is phase 1: extract the encoding wrapper functions into
`src/taurus/encoding/wrapper.c`.

The encoding wrappers are self-contained:

- `taurus_parse_string_with_encoding` (~125 lines, handles UTF-16 BOM,
  UTF-16 heuristic, iconv dispatch)
- Helper UTF-16 detection glue

## Plan

1. Create `src/taurus/encoding/wrapper.c` with the function moved
   verbatim.
2. Use forward declarations for cross-module dependencies:
   - `extern struct taurus_document* taurus_parse(const char*, size_t);`
   - `extern char* taurus_strdup(const char*);`
   - `extern void taurus_free_hook(void*);`
3. Remove the function from `taurus.c`.
4. Update `src/CMakeLists.txt` `TAURUS_SOURCES` to include the new file.
5. Build + test — zero regressions.

## Why the sketch was abandoned mid-execution

The function body uses:

- `#include "encoding/utf16.h"` mid-function (inside the function body)
- `#include "encoding/encoding.h"` mid-function inside `#ifdef TAURUS_HAS_ICONV`
- `TAURUS_FREE` macro defined in taurus.c's translation unit
- `taurus_strdup` which is in a different file

Moving these requires either:
- Hoisting the includes to the file top of wrapper.c (cleanest)
- Or duplicating the macro

Cleanest fix: includes at top, `extern` declarations for cross-file
calls, define a local `TAURUS_FREE` macro that calls `taurus_free_hook`.

## Verification

After execution:

```bash
wc -l src/taurus/taurus.c           # shrinks by ~125
wc -l src/taurus/encoding/wrapper.c # ~125
cmake --build build
ctest --test-dir build              # 100% pass
```

