# TODO 118 — FFI bindings (Ruby / Python / Rust)

**Priority**: P3
**Status**: Open. Supersedes original TODOs 79-85.

## Why

libleptris is C99 with a stable ABI but no language bindings. To be
usable from Ruby, Python, Rust, etc., we need FFI shims. Each binding
wraps the public API in `src/include/leptris/`.

## Plan

### Ruby (leptris-ruby)

Use [ffi](https://github.com/ffi/ffi) gem (no C extension compilation
needed by users). Map each public function to a Ruby method.

```ruby
require 'ffi'

module Leptris
  extend FFI::Library
  ffi_lib 'libleptris'

  class Document < FFI::AutoPointer; end
  class Element < FFI::Struct; end

  attach_function :leptris_parse_string, [:string, :size_t, :pointer], Document
  attach_function :leptris_document_root, [Document], Element
  attach_function :leptris_element_name, [Element], :string
  # ...
end

doc = Leptris.leptris_parse_string("<r/>", 4, nil)
root = Leptris.leptris_document_root(doc)
puts Leptris.leptris_element_name(root)
```

AutoPointer handles `leptris_document_free` on GC.

### Python (pyleptris)

Use cffi (no C extension compilation). Similar pattern to Ruby.

```python
from cffi import FFI
ffi = FFI()
ffi.cdef("""
    typedef struct leptris_document* LeptrisDocument;
    typedef struct leptris_element* LeptrisElement;
    LeptrisDocument leptris_parse_string(const char*, size_t, int*);
    LeptrisElement leptris_document_root(LeptrisDocument);
    const char* leptris_element_name(LeptrisElement);
    void leptris_document_free(LeptrisDocument);
""")
lib = ffi.dlopen("libleptris.dylib")

st = ffi.new("int*")
doc = lib.leptris_parse_string(b"<r/>", 4, st)
root = lib.leptris_document_root(doc)
print(ffi.string(lib.leptris_element_name(root)))
lib.leptris_document_free(doc)
```

Wrap in a `pyleptris.Document` class with `__enter__`/`__exit__` for RAII.

### Rust (leptris-rs)

Use `bindgen` to generate bindings from the public headers, plus a
safe wrapper crate.

```rust
mod sys {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub struct Document(sys::LeptrisDocument);

impl Document {
    pub fn parse(xml: &str) -> Result<Document, Error> {
        let mut st = sys::LeptrisStatus_LEPTRIS_OK;
        let doc = unsafe {
            sys::leptris_parse_string(xml.as_ptr(), xml.len(), &mut st)
        };
        if doc.is_null() { return Err(Error::from(st)); }
        Ok(Document(doc))
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        unsafe { sys::leptris_document_free(self.0) }
    }
}
```

## Acceptance

Each binding ships:
- parse + free roundtrip
- element traversal
- XPath evaluation
- ASAN-clean under that language's FFI test suite
