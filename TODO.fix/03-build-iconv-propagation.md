# TODO 03: Propagate iconv link dependency through OBJECT library

**Priority**: P0 (build blocker when iconv is enabled)
**Status**: Planned
**Effort**: S

## Problem

With `-DTAURUS_ENABLE_ICONV=ON` (the default), the CLI link step fails:

```
ld: Undefined symbols:
  _iconv, referenced from: _taurus_encoding_convert in libtaurus.a
  _iconv_close, referenced from: _taurus_encoding_convert in libtaurus.a
  _iconv_open, referenced from: _taurus_encoding_convert in libtaurus.a
```

## Root cause

`src/CMakeLists.txt` builds an OBJECT library and aggregates it into the
static and shared libs:

```cmake
add_library(taurus_objects OBJECT ${TAURUS_SOURCES})
target_link_libraries(taurus_objects PRIVATE Iconv::Iconv)   # line 143

add_library(taurus_static STATIC $<TARGET_OBJECTS:taurus_objects>)
add_library(taurus_shared SHARED $<TARGET_OBJECTS:taurus_objects>)
```

CMake's documented behavior: **PRIVATE link dependencies on an OBJECT
library do not propagate to consumers of the static/shared lib that
aggregate those objects** (CMake issue #16053, plus
[target-object-libraries] in the docs). The link requirement needs to be
attached to the final static / shared targets.

`utf8proc` (line 122) has the same bug — but its symbol is
`taurus_unicode_*` which lives in `unicode.c`, which is only compiled
when `TAURUS_ENABLE_UTF8PROC=ON`, so it surfaces the moment utf8proc is
enabled too.

## Fix

Two equivalent options. We pick **option B** because it preserves the
single source of truth at the OBJECT library.

### Option A — duplicate the links on the static/shared targets

```cmake
target_link_libraries(taurus_static PUBLIC Iconv::Iconv)
target_link_libraries(taurus_shared PUBLIC Iconv::Iconv)
```

### Option B (preferred) — make the OBJECT library's link PUBLIC, then
propagate via `$<TARGET_PROPERTY:taurus_objects,LINK_ONLY>`

```cmake
target_link_libraries(taurus_objects PUBLIC Iconv::Iconv utf8proc)

# And on the static/shared targets:
target_link_libraries(taurus_static
    PUBLIC  $<TARGET_PROPERTY:taurus_objects,INTERFACE_LINK_LIBRARIES>)
target_link_libraries(taurus_shared
    PUBLIC  $<TARGET_PROPERTY:taurus_objects,INTERFACE_LINK_LIBRARIES>)
```

`LINK_ONLY` strips the include dirs / compile defs but keeps the libraries
— exactly the right semantic for "I link against this OBJECT lib's
objects, so my consumers also need to link these."

Apply the same pattern to `m` and to `utf8proc` (when enabled).

## Tests

Build-only — verify with:

```bash
cmake -B build -S . -DBUILD_TESTING=ON -DTAURUS_BUILD_CLI=ON \
                          -DTAURUS_ENABLE_ICONV=ON -DTAURUS_ENABLE_UTF8PROC=ON
cmake --build build
# link step succeeds, no undefined _iconv* / _utf8proc* symbols
```

A runtime spec in `test/memory/test_pool.cpp` that exercises iconv (parse
an ISO-8859-1 document, verify the converted UTF-8 output) will exercise
this transitively once TODO 05 lands.

## Architecture notes

The build graph should mirror the runtime ownership graph:

```
taurus-cli  →  taurus_static  →  taurus_objects (compilation)  →  iconv, utf8proc
```

Each arrow's link dependency must be expressible in *one* place. Today it
isn't (PRIVATE on OBJECT doesn't propagate). After this fix, the OBJECT
library is the single source of truth.

## Verification

1. `cmake --build build` succeeds with both iconv and utf8proc enabled.
2. `nm build/cli/taurus | grep iconv` shows the symbol referenced from
   the executable (i.e. it's correctly resolved at link time, not
   dynamically dlopened).
3. `otool -L build/cli/taurus` (macOS) or `ldd` (Linux) shows libiconv
   in the dependency list.
