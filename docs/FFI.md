# libtaurus FFI Design

libtaurus is a C99 library with a stable C ABI.  Any language with C
FFI support can call it.  This document describes the contract.

## Principles

1. **Opaque handles.** All public types are pointer typedefs
   (`TaurusDocument`, `TaurusElement`, etc.).  Callers never see struct
   fields — they use accessor functions.

2. **Status via output parameter.** Functions that may fail take a
   `TaurusStatus*` parameter.  Return value is the primary result.

3. **Documented ownership.** Every function that returns a string or
   handle has a `Memory:` comment in the header documenting who frees.

4. **No exceptions across the boundary.** Errors are status codes;
   no `longjmp`, no `setjmp`.

5. **No C-isms.** No varargs, no macros that expand to platform-specific
   attributes (unless `TAURUS_FOR_BINDGEN` is defined — see below).

## Bindgen-friendly mode

Binding generators (bindgen, cffi, ctypes) can't always handle
platform-specific attributes like `__declspec(dllexport)`.  Define
`TAURUS_FOR_BINDGEN` when parsing the headers from a binding tool:

```bash
cc -DTAURUS_FOR_BINDGEN -E src/include/taurus.h
```

This expands `TAURUS_API` to nothing, producing a clean parse.

## ABI stability

The opaque-handle typedefs are pointer-sized.  This is enforced at
compile time via `_Static_assert` in `taurus.h`:

```c
_Static_assert(sizeof(TaurusDocument) == sizeof(void*), "...");
```

Adding a struct field to a public type would silently change its
size and break bindings; the assert catches this at build time.

Enum values are also stable — see `test/abi/test_header_hygiene.cpp`
for the pinned values.

## Memory model

Every byte the parser allocates that ends up referenced by a document
is pool-owned.  `taurus_document_free` releases the pool and everything
reachable from it.  Binding authors only need to call this one
function to release a parsed document.

Strings returned from public functions are either:

- **Document-owned** (e.g., `taurus_element_name` returns a pointer
  into the document's pool).  Do not free; released by
  `taurus_document_free`.
- **Caller-owned** (e.g., `taurus_document_serialize`).  Free with
  `taurus_free_string`.

The contract is documented per-function via `Memory:` comments in the
header.

## Status codes

```c
TAURUS_OK = 0
TAURUS_ERROR_MEMORY = -1
TAURUS_ERROR_PARSE = -2
TAURUS_ERROR_XPATH = -3
TAURUS_ERROR_NULL_ARG = -4
TAURUS_ERROR_INVALID_ARG = -5
TAURUS_ERROR_NOT_FOUND = -6
TAURUS_ERROR_IO = -7
```

Negative on error, zero on success.

## Bindings

Planned bindings (see `TODO.fix/81-*` through `TODO.fix/83-*`):

- **Ruby** via `ruby-ffi` — pure Ruby, no compilation.
- **Python** via `cffi` — header-aware, type-safe.
- **Rust** via `bindgen` + idiomatic wrapper — zero-cost with safety.

The C ABI is the foundation; each binding is a thin wrapper.

## Example FFI call

Minimal C example that any binding would replicate:

```c
#include <taurus.h>

int main(void) {
    const char* xml = "<root>hello</root>";

    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) return 1;

    TaurusElement root = taurus_document_root(doc);
    printf("root: %s\n", taurus_element_name(root));

    taurus_document_free(doc);
    return 0;
}
```

Equivalent Ruby via `ffi`:

```ruby
require 'ffi'

module Taurus
  extend FFI::Library
  ffi_lib 'libtaurus'

  attach_function :taurus_parse_string, [:string, :size_t, :pointer], :pointer
  attach_function :taurus_document_root, [:pointer], :pointer
  attach_function :taurus_element_name,  [:pointer], :string
  attach_function :taurus_document_free, [:pointer], :void
end

status = FFI::MemoryPointer.new(:int)
xml = "<root>hello</root>"
doc = Taurus.taurus_parse_string(xml, xml.bytesize, status)
root = Taurus.taurus_document_root(doc)
puts "root: #{Taurus.taurus_element_name(root)}"
Taurus.taurus_document_free(doc)
```

## Future ABI evolution

When the ABI needs to change:

1. Add a new function with a `_v2` suffix (or similar) rather than
   changing the existing signature.
2. Mark the old function `@deprecated` in docstrings.
3. Remove only at a major version bump (e.g., 1.0 → 2.0).

Symbol versioning on Linux (via linker version script) prevents
older binaries from binding to newer-only symbols.
