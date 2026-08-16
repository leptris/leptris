# TODO 82: Python binding (cffi)

**Priority**: P2 (binding — Python ecosystem)
**Status**: Done — `bindings/python/` (pytaurus, cffi ABI mode):
Document.parse/close + context manager, Element navigation with
whitespace-safe sibling walking, generic Node types, typed XPath
results, serialize. 22 pytest specs + parse/free stress + flat-RSS
check green. README quick start added; TODO 82 done 2026-08-16.
**Effort**: M

## Approach: cffi

Python's `cffi` (C Foreign Function Interface) is the natural choice
over `ctypes` for libraries with headers.  cffi can parse the C
headers directly, which keeps the binding in sync with the source.

```python
from cffi import FFI

ffi = FFI()
ffi.cdef("""
    typedef struct taurus_document* TaurusDocument;
    typedef struct taurus_element*  TaurusElement;
    typedef enum { TAURUS_OK = 0, ... } TaurusStatus;

    TaurusDocument taurus_parse_string(const char* xml, size_t len, int* status);
    TaurusElement  taurus_document_root(TaurusDocument doc);
    const char*    taurus_element_name(TaurusElement elem);
    void           taurus_document_free(TaurusDocument doc);
    void           taurus_free_string(char* str);
""")

lib = ffi.dlopen("libtaurus.so")

class Document:
    def __init__(self, xml: str):
        status = ffi.new("int*")
        if isinstance(xml, str):
            xml = xml.encode("utf-8")
        self._ptr = lib.taurus_parse_string(xml, len(xml), status)
        if self._ptr == ffi.NULL:
            raise RuntimeError("parse failed")

    @property
    def root(self):
        return Element(lib.taurus_document_root(self._ptr), self)

    def __del__(self):
        if self._ptr:
            lib.taurus_document_free(self._ptr)
            self._ptr = ffi.NULL


class Element:
    def __init__(self, ptr, doc):
        self._ptr = ptr
        self._doc = doc  # keep alive

    @property
    def name(self) -> str:
        return ffi.string(lib.taurus_element_name(self._ptr)).decode("utf-8")
```

## Packaging

- PyPI package: `pytaurus`.
- `pyproject.toml` with `cffi` as build dep.
- Wheel ships pre-built libtaurus on Linux/macOS; Windows TBD.

## Tests

`test/bindings/python/test_parse.py`:

```python
from pytaurus import Document

doc = Document("<root><item>hi</item></root>")
assert doc.root.name == "root"
del doc  # triggers __del__
```

## Architecture notes

Python's GC pairs well with the pool-ownership model — `__del__` is
a natural place to call `taurus_document_free`.  But finalizers are
non-deterministic; for resource-constrained code, expose explicit
`close()`:

```python
with Document(xml) as doc:
    ...  # released at scope exit
```
