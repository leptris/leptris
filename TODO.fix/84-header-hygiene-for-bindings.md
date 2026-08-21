# TODO 84: Header hygiene for bindings

**Priority**: P1 (foundation — required before any binding ships)
**Status**: Planned
**Effort**: S

## Problem

The public headers (`src/include/leptris/`) compile cleanly from C
but may trip up binding generators:

1. **Macros that expand to attributes** (`LEPTRIS_API` is `__declspec`
   on Windows, `__attribute__((visibility))` on GCC).  Binding
   generators like `bindgen` need to handle these or be told to
   ignore.

2. **Conditional compilation** (`#ifdef LEPTRIS_HAS_UTF8PROC`) means
   the same header produces different APIs depending on the build.
   Bindings need to know which features were enabled.

3. **Anonymous struct fields** in some places — bindgen handles but
   cffi's parser may not.

4. **`size_t` definition** — comes from `<stddef.h>` (C) or
   `<cstddef>` (C++).  Bindings need to know.

5. **No `_Static_assert` for ABI sizes** — a struct field added
   silently changes the ABI; no compile-time check.

## Fix

### Step 1: header self-contained

Each public header must compile standalone:

```c
#include <leptris.h>
int main(void) { return 0; }
```

No missing includes.

### Step 2: feature macros documented

For each `#ifdef LEPTRIS_HAS_*`:

- Document what it gates.
- Provide a `leptris/features.h` that lists the enabled features at
  build time.
- Bindings read this header to know the API surface.

### Step 3: ABI asserts

Add to `leptris.h`:

```c
#include <stdint.h>
_Static_assert(sizeof(intptr_t) == sizeof(void*),
               "intptr_t/pointer mismatch");
_Static_assert(sizeof(LeptrisDocument) == sizeof(void*),
               "LeptrisDocument must be a pointer");
```

Catches accidental struct-field exposure.

### Step 4: bindgen-friendly mode

Add a `LEPTRIS_FOR_BINDGEN` macro that expands API attributes to
nothing:

```c
#ifdef LEPTRIS_FOR_BINDGEN
#define LEPTRIS_API
#else
#ifdef _WIN32
#define LEPTRIS_API __declspec(dllimport)
#else
#define LEPTRIS_API __attribute__((visibility("default")))
#endif
#endif
```

Binding generators define `LEPTRIS_FOR_BINDGEN` to get a clean parse.

## Tests

```bash
# Self-contained check
for h in src/include/leptris.h src/include/leptris/*.h src/include/leptris/dom/*.h; do
    echo '#include "'$h'"' > /tmp/check.c
    echo 'int main(void){return 0;}' >> /tmp/check.c
    cc -Isrc/include /tmp/check.c -o /tmp/check || echo "FAIL: $h"
done

# Bindgen smoke test (requires Rust toolchain)
bindgen src/include/leptris.h --output /tmp/bindings.rs
```

## Verification

```bash
cc -DLEPTRIS_FOR_BINDGEN -Isrc/include -c src/include/leptris.h -o /dev/null
# Compiles clean.
```
