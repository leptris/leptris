# TODO 118 — FFI bindings (Ruby / Python / Rust)

**Priority**: P3
**Status**: Open. Supersedes original TODOs 79-85.

## Why

libtaurus is C99 with a stable ABI but no language bindings. To be
usable from Ruby, Python, Rust, etc., we need FFI shims. Each binding
wraps the public API in `src/include/taurus/`.

## Plan

### Ruby (taurus-ruby)

Use [ffi](https://github.com/ffi/ffi) gem (no C extension compilation
needed by users). Map each public function to a Ruby method.

```ruby
require 'ffi'

module Taurus
  extend FFI::Library
  ffi_lib 'libtaurus'

  class Document < FFI::AutoPointer; end
  class Element < FFI::Struct; end

  attach_function :taurus_parse_string, [:string, :size_t, :pointer], Document
  attach_function :taurus_document_root, [Document], Element
  attach_function :taurus_element_name, [Element], :string
  # ...
end

doc = Taurus.taurus_parse_string("<r/>", 4, nil)
root = Taurus.taurus_document_root(doc)
puts Taurus.taurus_element_name(root)
```

AutoPointer handles `taurus_document_free` on GC.

### Python (pytaurus)

Use cffi (no C extension compilation). Similar pattern to Ruby.

```python
from cffi import FFI
ffi = FFI()
ffi.cdef("""
    typedef struct taurus_document* TaurusDocument;
    typedef struct taurus_element* TaurusElement;
    TaurusDocument taurus_parse_string(const char*, size_t, int*);
    TaurusElement taurus_document_root(TaurusDocument);
    const char* taurus_element_name(TaurusElement);
    void taurus_document_free(TaurusDocument);
""")
lib = ffi.dlopen("libtaurus.dylib")

st = ffi.new("int*")
doc = lib.taurus_parse_string(b"<r/>", 4, st)
root = lib.taurus_document_root(doc)
print(ffi.string(lib.taurus_element_name(root)))
lib.taurus_document_free(doc)
```

Wrap in a `pytaurus.Document` class with `__enter__`/`__exit__` for RAII.

### Rust (taurus-rs)

Use `bindgen` to generate bindings from the public headers, plus a
safe wrapper crate.

```rust
mod sys {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub struct Document(sys::TaurusDocument);

impl Document {
    pub fn parse(xml: &str) -> Result<Document, Error> {
        let mut st = sys::TaurusStatus_TAURUS_OK;
        let doc = unsafe {
            sys::taurus_parse_string(xml.as_ptr(), xml.len(), &mut st)
        };
        if doc.is_null() { return Err(Error::from(st)); }
        Ok(Document(doc))
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        unsafe { sys::taurus_document_free(self.0) }
    }
}
```

## Acceptance

Each binding ships:
- parse + free roundtrip
- element traversal
- XPath evaluation
- ASAN-clean under that language's FFI test suite
