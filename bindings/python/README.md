# pytaurus — Python bindings for libtaurus

`pytaurus` wraps the libtaurus C API (XML 1.0 parsing, XPath 1.0)
using `cffi` in ABI mode — the `cdef` in `pytaurus/_ffi.py` mirrors
the public headers in `src/include/taurus/`.

## Requirements

- Python 3.8+
- `cffi` (`pip install cffi`)
- libtaurus as a shared library (`libtaurus.dylib` / `libtaurus.so`)
  on the loader path, or pointed to by `TAURUS_LIB_PATH`. For a
  development checkout:

```bash
cmake -B build -S . -DTAURUS_BUILD_SHARED=ON
cmake --build build --target taurus_shared
export TAURUS_LIB_PATH=$PWD/build/src/libtaurus.dylib
```

## Quick start

```python
from pytaurus import Document

doc = Document.parse("<library><book id='1'>Ulysses</book></library>")

doc.root.name                     # "library"
book = doc.root.first_child_element
book.name                         # "book"
book.attribute("id")              # "1"
book.text                         # "Ulysses"

doc.xpath("count(//book)")        # 1.0
[e.text for e in doc.xpath("//book")]   # ["Ulysses"]

doc.close()   # or: with Document.parse(xml) as doc: ...
```

## Layout

- `pytaurus/_ffi.py` — cdef + shared-library loading (single source
  of the C surface, mirroring the Ruby binding's `lib/taurus.rb`)
- `pytaurus/document.py`, `element.py`, `node.py`, `xpath.py`,
  `error.py` — typed wrappers
- `tests/` — pytest suite (run: `pytest` with `TAURUS_LIB_PATH` set)

## Memory model

The `Document` owns the whole tree and its pool. Accessor strings
are copied into Python `str` at the boundary, so nothing depends on
document lifetime after a call returns. Elements keep a reference to
their `Document`, so the pool cannot be freed while any wrapper is
alive. Prefer explicit `close()` / the context manager; `__del__` is
a refcounting safety net, not a contract.
