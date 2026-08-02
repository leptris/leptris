# TODO 51: Doxygen documentation pass

**Priority**: P3 (documentation)
**Status**: Planned
**Effort**: M

## Problem

The public API headers (`src/include/taurus/`) have inline docstrings
but they're inconsistent — some functions are thoroughly documented,
others have one-line comments.  There's no generated API reference.

For an XML parser library aiming at production use, a generated API
reference is table stakes.  Doxygen is the standard tool.

## Fix

### Step 1: add Doxyfile

`docs/api/Doxyfile`:

```
PROJECT_NAME           = libtaurus
INPUT                  = src/include
RECURSIVE              = YES
OUTPUT_DIRECTORY       = docs/api/generated
EXTRACT_ALL            = YES
GENERATE_HTML          = YES
GENERATE_LATEX         = NO
QUIET                  = YES
WARN_IF_UNDOCUMENTED   = YES
```

### Step 2: CMake target

```cmake
option(TAURUS_BUILD_DOCS "Build Doxygen API docs" OFF)

if(TAURUS_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} docs/api/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating Doxygen API docs")
endif()
```

### Step 3: audit docstrings

Walk every public function declaration in `src/include/taurus/`.  For
each:

- `@param` for every parameter (incl. NULL handling)
- `@return` describing the return value
- `@since` for the version that added it
- A short example for non-trivial functions

Document the **memory ownership contract** explicitly — "caller must
free with X" or "owned by document, do not free."  This is the most
common source of user error.

### Step 4: cross-reference

Use `@see` to point at related functions.  Helps users navigate.

## Tests

Documentation is itself the deliverable.  No specs needed.

## Architecture notes

Inline docstrings + Doxygen generation gives:

- IDE tooltips (via compile_commands.json + clangd).
- HTML reference (generated on demand).
- PDF reference (via Doxygen + LaTeX).

The single source of truth is the header.  No separate docs to drift.

## Verification

```bash
cmake -B build -S . -DTAURUS_BUILD_DOCS=ON
cmake --build build --target docs
ls docs/api/generated/html/
# Index + module pages exist.
```
