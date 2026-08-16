"""Document — wraps TaurusDocument.

The document owns the entire DOM tree and memory pool. Release it
with close() (or the context manager); __del__ is a last-resort
safety net for CPython refcounting, not a contract.
"""

from . import _ffi
from .error import TaurusError


class Document:
    def __init__(self, _ptr):
        self._ptr = _ptr
        self._freed = False

    @classmethod
    def parse(cls, xml):
        if isinstance(xml, str):
            xml = xml.encode("utf-8")
        if not isinstance(xml, (bytes, bytearray, memoryview)):
            raise TypeError("xml must be str or bytes")
        xml = bytes(xml)
        status = _ffi.ffi.new("int*")
        ptr = _ffi.lib.taurus_parse_string(xml, len(xml), status)
        if ptr == _ffi.ffi.NULL:
            raise TaurusError(f"parse failed (status={status[0]})")
        return cls(ptr)

    @property
    def root(self):
        ptr = _ffi.lib.taurus_document_root(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        from .element import Element

        return Element(ptr, self)

    def serialize(self) -> str:
        result = _ffi.lib.taurus_serialize_document(self._ptr, _ffi.ffi.NULL)
        if result == _ffi.ffi.NULL:
            return ""
        value = _ffi.ffi.string(result).decode("utf-8")
        _ffi.lib.taurus_free_string(result)
        return value

    def process_xinclude(self, base_url=None):
        base = base_url.encode("utf-8") if base_url is not None else _ffi.ffi.NULL
        rc = _ffi.lib.taurus_xinclude_process(self._ptr, base)
        if rc != 0:
            raise TaurusError("XInclude processing failed")
        return self

    def xpath(self, expression, context=None):
        from .xpath import XPath

        return XPath.evaluate(self, context, expression)

    def close(self):
        if not self._freed:
            _ffi.lib.taurus_document_free(self._ptr)
            self._freed = True
            self._ptr = _ffi.ffi.NULL

    @property
    def closed(self) -> bool:
        return self._freed

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass
