"""Element — wraps LeptrisElement.

Elements are owned by their parent Document; they are never freed
directly. Element objects keep a reference to the Document so the
tree cannot outlive its pool.
"""

from . import _ffi


class Element:
    def __init__(self, _ptr, document):
        self._ptr = _ptr
        self._document = document

    @property
    def document(self):
        return self._document

    @property
    def name(self) -> str:
        value = _ffi.lib.leptris_element_name(self._ptr)
        return _ffi.ffi.string(value).decode("utf-8") if value != _ffi.ffi.NULL else ""

    @property
    def text(self) -> str:
        value = _ffi.lib.leptris_element_text(self._ptr)
        return _ffi.ffi.string(value).decode("utf-8") if value != _ffi.ffi.NULL else ""

    def attribute(self, name: str, default=None):
        value = _ffi.lib.leptris_element_attribute(
            self._ptr, name.encode("utf-8")
        )
        if value == _ffi.ffi.NULL:
            return default
        return _ffi.ffi.string(value).decode("utf-8")

    __getitem__ = attribute

    def attributes(self):
        """Yield (name, value) for every attribute in document order.

        Handle-based iteration — O(n) total where index-based access
        re-walks the list per call.
        """
        attr = _ffi.lib.leptris_element_first_attribute(self._ptr)
        while attr != _ffi.ffi.NULL:
            name = _ffi.ffi.string(
                _ffi.lib.leptris_attribute_get_name(attr)
            ).decode("utf-8")
            value = _ffi.ffi.string(
                _ffi.lib.leptris_attribute_get_value(self._ptr, attr)
            ).decode("utf-8")
            yield (name, value)
            attr = _ffi.lib.leptris_attribute_next(attr)

    @property
    def attribute_count(self) -> int:
        return _ffi.lib.leptris_element_attribute_count(self._ptr)

    @property
    def child_count(self) -> int:
        return _ffi.lib.leptris_element_child_count(self._ptr)

    @property
    def parent(self):
        ptr = _ffi.lib.leptris_element_parent(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        return Element(ptr, self._document)

    @property
    def first_child_element(self):
        ptr = _ffi.lib.leptris_element_first_child_any(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        return Element(ptr, self._document)

    @property
    def next_sibling_element(self):
        # The node-level sibling chain interleaves text nodes, so
        # walk until the next element (or the end of the chain).
        node = _ffi.lib.leptris_node_next_sibling(_ffi.lib.leptris_element_as_node(self._ptr))
        while node != _ffi.ffi.NULL:
            elem = _ffi.lib.leptris_node_as_element(node)
            if elem != _ffi.ffi.NULL:
                return Element(elem, self._document)
            node = _ffi.lib.leptris_node_next_sibling(node)
        return None

    def child_elements(self):
        child = self.first_child_element
        while child is not None:
            yield child
            child = child.next_sibling_element

    def to_node(self):
        from .node import Node

        return Node(_ffi.lib.leptris_element_as_node(self._ptr), self._document)

    def xpath(self, expression):
        return self._document.xpath(expression, context=self)

    def __iter__(self):
        return self.child_elements()

    def __repr__(self):
        return f"<pyleptris.Element {self.name!r}>"
