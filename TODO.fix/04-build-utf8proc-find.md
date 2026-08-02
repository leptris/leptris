# TODO 04: Find utf8proc portably (not vcpkg-only)

**Priority**: P0 (build blocker outside vcpkg)
**Status**: Planned
**Effort**: S

## Problem

`src/CMakeLists.txt:120`:

```cmake
find_package(unofficial-utf8proc CONFIG REQUIRED)
```

`unofficial-utf8proc` is a name specific to the vcpkg ecosystem. On
Homebrew (`brew install utf8proc`), on system packages
(`libutf8proc-dev`), or on a manual build, that name is never going to
match. So enabling utf8proc outside vcpkg fails configuration:

```
CMake Error ... Could not find a package configuration file provided by
"unofficial-utf8proc" ...
```

The repo ships `cmake/FindUtf8proc.cmake` — a perfectly good
module-mode finder — but it's never used.

## Root cause

Two finders exist (`unofficial-utf8proc` CONFIG and a `FindUtf8proc.cmake`
module), and only the vcpkg-specific one is wired in.

## Fix

Use a unified lookup that prefers vcpkg (when present) and falls back to
the module finder:

```cmake
if(TAURUS_ENABLE_UTF8PROC)
    # Prefer vcpkg's unofficial-utf8proc CONFIG package when present
    find_package(unofficial-utf8proc CONFIG QUIET)
    if(unofficial-utf8proc_FOUND)
        set(TAURUS_UTF8PROC_TARGET unofficial::utf8proc)
    else()
        # Fall back to the module-mode finder shipped in this repo
        find_package(Utf8proc REQUIRED)
        set(TAURUS_UTF8PROC_TARGET Utf8proc::Utf8proc)
    endif()

    target_link_libraries(taurus_objects PUBLIC ${TAURUS_UTF8PROC_TARGET})
    target_compile_definitions(taurus_objects PUBLIC TAURUS_HAS_UTF8PROC=1)
    target_sources(taurus_objects PRIVATE taurus/unicode/unicode.c)
    target_include_directories(taurus_objects PRIVATE taurus/unicode)
endif()
```

Also update `cmake/FindUtf8proc.cmake` to provide an imported target
named `Utf8proc::Utf8proc` (the convention). The current module likely
only sets `UTF8PROC_LIBRARIES` / `UTF8PROC_INCLUDE_DIRS` variables; if so,
modernize it:

```cmake
# cmake/FindUtf8proc.cmake
find_path(UTF8PROC_INCLUDE_DIR NAMES utf8proc.h)
find_library(UTF8PROC_LIBRARY  NAMES utf8proc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Utf8proc
    REQUIRED_VARS UTF8PROC_LIBRARY UTF8PROC_INCLUDE_DIR)

if(UTF8PROC_FOUND AND NOT TARGET Utf8proc::Utf8proc)
    add_library(Utf8proc::Utf8proc UNKNOWN IMPORTED)
    set_target_properties(Utf8proc::Utf8proc PROPERTIES
        IMPORTED_LOCATION             "${UTF8PROC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${UTF8PROC_INCLUDE_DIR}")
endif()

mark_as_advanced(UTF8PROC_INCLUDE_DIR UTF8PROC_LIBRARY)
```

## Tests

Build-only verification:

```bash
# vcpkg path still works
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/.../vcpkg.cmake \
                   -DTAURUS_ENABLE_UTF8PROC=ON

# Homebrew path now works too
brew install utf8proc
cmake -B build -S . -DTAURUS_ENABLE_UTF8PROC=ON
```

A runtime spec in `test/parser/test_parser.cpp` will exercise this once
TODO 05 lands: parse a UTF-8 file containing multibyte characters and
verify the codepoints round-trip exactly.

## Architecture notes

Two finders for the same dependency is a violation of MECE — there is one
truth (the dependency exists or it doesn't), and we should look it up in
the most portable way first. Keeping `unofficial-utf8proc` as the
**preferred** path preserves vcpkg integration without forcing it.

The imported-target convention (`Utf8proc::Utf8proc`,
`unofficial::utf8proc`) is the modern CMake idiom: targets carry their
own usage requirements (include dirs, compile defs, transitive links).
Variable-based finders (`UTF8PROC_LIBRARIES`) are the legacy form.

## Verification

1. Build with vcpkg `unofficial-utf8proc` — still configures.
2. Build with Homebrew `utf8proc` — configures and links.
3. `nm build/src/libtaurus.a | grep utf8proc` shows resolved symbols.
