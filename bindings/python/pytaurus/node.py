"""Node — wraps TaurusNodeRef for generic tree traversal.

Covers every node type (element, text, comment, CDATA, PI, doctype);
Element is the typed view for element nodes.
"""

from . import _ffi


class Node:
    def __init__(self, _ptr, document):
        self._ptr = _ptr
        self._document = document

    @property
    def type(self) -> int:
        return _ffi.lib.taurus_node_get_type(self._ptr)

    def is_element(self) -> bool:
        return self.type == _ffi.NODE_ELEMENT

    def is_text(self) -> bool:
        return self.type == _ffi.NODE_TEXT

    def is_comment(self) -> bool:
        return self.type == _ffi.NODE_COMMENT

    def is_cdata(self) -> bool:
        return self.type == _ffi.NODE_CDATA

    def is_pi(self) -> bool:
        return self.type == _ffi.NODE_PI

    @property
    def content(self):
        t = self.type
        if t == _ffi.NODE_TEXT:
            getter = _ffi.lib.taurus_text_node_get_content
        elif t == _ffi.NODE_COMMENT:
            getter = _ffi.lib.taurus_comment_node_get_content
        elif t == _ffi.NODE_CDATA:
            getter = _ffi.lib.taurus_cdata_node_get_content
        else:
            return None
        value = getter(self._ptr)
        return _ffi.ffi.string(value).decode("utf-8") if value != _ffi.ffi.NULL else ""

    @property
    def first_child(self):
        ptr = _ffi.lib.taurus_node_first_child(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        return Node(ptr, self._document)

    @property
    def next_sibling(self):
        ptr = _ffi.lib.taurus_node_next_sibling(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        return Node(ptr, self._document)

    @property
    def previous_sibling(self):
        ptr = _ffi.lib.taurus_node_previous_sibling(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        return Node(ptr, self._document)

    @property
    def child_count(self) -> int:
        return _ffi.lib.taurus_node_child_count(self._ptr)

    def as_element(self):
        if not self.is_element():
            return None
        ptr = _ffi.lib.taurus_node_as_element(self._ptr)
        if ptr == _ffi.ffi.NULL:
            return None
        from .element import Element

        return Element(ptr, self._document)

    def __repr__(self):
        return f"<pytaurus.Node type={self.type}>"
