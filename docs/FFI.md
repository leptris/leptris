# libleptris FFI Design

libleptris is a C99 library with a stable C ABI.  Any language with C
FFI support can call it.  This document describes the contract.

## Principles

1. **Opaque handles.** All public types are pointer typedefs
   (`LeptrisDocument`, `LeptrisElement`, etc.).  Callers never see struct
   fields — they use accessor functions.

2. **Status via output parameter.** Functions that may fail take a
   `LeptrisStatus*` parameter.  Return value is the primary result.

3. **Documented ownership.** Every function that returns a string or
   handle has a `Memory:` comment in the header documenting who frees.

4. **No exceptions across the boundary.** Errors are status codes;
   no `longjmp`, no `setjmp`.

5. **No C-isms.** No varargs, no macros that expand to platform-specific
   attributes (unless `LEPTRIS_FOR_BINDGEN` is defined — see below).

## Bindgen-friendly mode

Binding generators (bindgen, cffi, ctypes) can't always handle
platform-specific attributes like `__declspec(dllexport)`.  Define
`LEPTRIS_FOR_BINDGEN` when parsing the headers from a binding tool:

```bash
cc -DLEPTRIS_FOR_BINDGEN -E src/include/leptris.h
```

This expands `LEPTRIS_API` to nothing, producing a clean parse.

## ABI stability

The opaque-handle typedefs are pointer-sized.  This is enforced at
compile time via `_Static_assert` in `leptris.h`:

```c
_Static_assert(sizeof(LeptrisDocument) == sizeof(void*), "...");
```

Adding a struct field to a public type would silently change its
size and break bindings; the assert catches this at build time.

Enum values are also stable — see `test/abi/test_header_hygiene.cpp`
for the pinned values.

## Memory model

Every byte the parser allocates that ends up referenced by a document
is pool-owned.  `leptris_document_free` releases the pool and everything
reachable from it.  Binding authors only need to call this one
function to release a parsed document.

Strings returned from public functions are either:

- **Document-owned** (e.g., `leptris_element_name` returns a pointer
  into the document's pool).  Do not free; released by
  `leptris_document_free`.
- **Caller-owned** (e.g., `leptris_document_serialize`).  Free with
  `leptris_free_string`.

The contract is documented per-function via `Memory:` comments in the
header.

## Status codes

```c
LEPTRIS_OK = 0
LEPTRIS_ERROR_MEMORY = -1
LEPTRIS_ERROR_PARSE = -2
LEPTRIS_ERROR_XPATH = -3
LEPTRIS_ERROR_NULL_ARG = -4
LEPTRIS_ERROR_INVALID_ARG = -5
LEPTRIS_ERROR_NOT_FOUND = -6
LEPTRIS_ERROR_IO = -7
```

Negative on error, zero on success.

## Bindings

- **Ruby** via `ruby-ffi` — pure Ruby, no compilation. **Shipped**
  (TODO 118): `Leptris::Document.parse`, XPath, serialize, and the
  full SAX surface — `Leptris::SAX.parse(xml, handlers)` (all 11
  event callbacks) and `Leptris::SAX::Parser` for incremental
  `feed()` streaming. See `bindings/ruby/` and the Ruby SAX
  section of the README.
- **Python** via `cffi` — header-aware, type-safe. Planned.
- **Rust** via `bindgen` + idiomatic wrapper — zero-cost with
  safety. Planned.

The C ABI is the foundation; each binding is a thin wrapper.

### Binding the SAX callbacks (Ruby pattern)

C hands the user's `void* user_data` back on every callback. The
Ruby binding passes the address of a small unique `FFI::MemoryPointer`
as `user_data` and maps that address to the Ruby handler object in
a registry — the Ruby object is never exposed to C, so the GC can
move it freely; the registry entry (and with it the anchor and the
`FFI::Function` wrappers) lives exactly as long as C can fire
callbacks. `characters` delivers `pointer, length` — the text is
NOT NUL-terminated and must be read with the length; entities
arrive expanded (XML 1.0 2.4/3.3.3).

## Example FFI call

Minimal C example that any binding would replicate:

```c
#include <leptris.h>

int main(void) {
    const char* xml = "<root>hello</root>";

    LeptrisStatus status;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (!doc) return 1;

    LeptrisElement root = leptris_document_root(doc);
    printf("root: %s\n", leptris_element_name(root));

    leptris_document_free(doc);
    return 0;
}
```

Equivalent Ruby via `ffi`:

```ruby
require 'ffi'

module Leptris
  extend FFI::Library
  ffi_lib 'libleptris'

  attach_function :leptris_parse_string, [:string, :size_t, :pointer], :pointer
  attach_function :leptris_document_root, [:pointer], :pointer
  attach_function :leptris_element_name,  [:pointer], :string
  attach_function :leptris_document_free, [:pointer], :void
end

status = FFI::MemoryPointer.new(:int)
xml = "<root>hello</root>"
doc = Leptris.leptris_parse_string(xml, xml.bytesize, status)
root = Leptris.leptris_document_root(doc)
puts "root: #{Leptris.leptris_element_name(root)}"
Leptris.leptris_document_free(doc)
```

## Future ABI evolution

When the ABI needs to change:

1. Add a new function with a `_v2` suffix (or similar) rather than
   changing the existing signature.
2. Mark the old function `@deprecated` in docstrings.
3. Remove only at a major version bump (e.g., 1.0 → 2.0).

Symbol versioning on Linux (via linker version script) prevents
older binaries from binding to newer-only symbols.
