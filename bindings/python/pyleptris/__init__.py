"""pyleptris — Python bindings for libleptris.

Usage:

    from pyleptris import Document

    doc = Document.parse("<root><item>hi</item></root>")
    print(doc.root.name)

Requires libleptris on the library search path (or LEPTRIS_LIB_PATH).
"""

__version__ = "1.1.0"

from .document import Document
from .element import Element
from .error import LeptrisError
from .node import Node
from .xpath import XPath

__all__ = ["Document", "Element", "Node", "XPath", "LeptrisError"]
