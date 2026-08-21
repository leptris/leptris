# TODO 73: Execute encoding wrapper extraction

**Priority**: P2 (architecture — first execution step of TODO 24/42)
**Status**: Planned — execution this time
**Effort**: M

## Problem

`leptris.c` is 2900+ lines.  TODO 24/42/45 designed a 5-phase split.
Phase 1 (extract `leptris_parse_string_with_encoding` into
`encoding/wrapper.c`) was attempted before and backed out due to
cross-module dependency concerns.

## Fix (revised, careful)

The previous attempt failed because:
- `LEPTRIS_FREE` macro references `leptris_free_hook` which isn't visible.
- `leptris_strdup` is a private symbol in another file.
- The function body has `#include "encoding/utf16.h"` mid-function.

### Plan

1. Hoist the `#include`s to the top of the new file.
2. Forward-declare cross-file helpers (`extern`):
   - `extern struct leptris_document* leptris_parse(const char*, size_t);`
   - `extern char* leptris_strdup(const char*);`
   - `extern void leptris_free_hook(void*);`
3. Define a local `LEPTRIS_FREE(ptr)` macro that wraps `leptris_free_hook`.
4. Copy the function verbatim into `encoding/wrapper.c`.
5. Remove the function from `leptris.c`.
6. Add `encoding/wrapper.c` to `src/CMakeLists.txt` `LEPTRIS_SOURCES`.
7. Build + test — zero regressions.

### Why this is safe

The function has a single caller (`leptris_parse_string`).  No other
code references it.  Its dependencies are well-defined (UTF-16 /
iconv helpers + the parser entry point).  Moving it is purely
structural.

## Tests

No behavioral change.  Existing 97 specs cover correctness.  The
encoding specs in `test/parser/test_encoding.cpp` exercise every
path the function handles.

## Verification

```bash
cmake --build build
ctest --test-dir build -R Encoding
# All encoding specs pass.

wc -l src/leptris/leptris.c              # ~2780 (was 2900)
wc -l src/leptris/encoding/wrapper.c    # ~150
```
