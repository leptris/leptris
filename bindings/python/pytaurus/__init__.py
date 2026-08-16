"""pytaurus — Python bindings for libtaurus.

Usage:

    from pytaurus import Document

    doc = Document.parse("<root><item>hi</item></root>")
    print(doc.root.name)

Requires libtaurus on the library search path (or TAURUS_LIB_PATH).
"""

__version__ = "0.1.0"

from .document import Document
from .element import Element
from .error import TaurusError
from .node import Node
from .xpath import XPath

__all__ = ["Document", "Element", "Node", "XPath", "TaurusError"]
