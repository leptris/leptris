"""XPath — evaluates XPath 1.0 expressions.

Results are typed: nodeset results yield Element lists; scalar
results convert to native Python types.
"""

from . import _ffi
from .error import LeptrisError


class XPath:
    @staticmethod
    def evaluate(document, context_element, expression):
        ctx = context_element._ptr if context_element is not None else _ffi.ffi.NULL
        result = _ffi.lib.leptris_xpath_eval(document._ptr, ctx, expression.encode("utf-8"))
        if result == _ffi.ffi.NULL:
            raise LeptrisError(f"XPath evaluation failed: {expression!r}")

        try:
            result_type = _ffi.lib.leptris_xpath_result_type(result)
            if result_type == _ffi.XPATH_NODESET:
                count = _ffi.lib.leptris_xpath_result_count(result)
                from .element import Element

                return [
                    Element(_ffi.lib.leptris_xpath_result_get(result, i), document)
                    for i in range(count)
                ]
            if result_type == _ffi.XPATH_NUMBER:
                return _ffi.lib.leptris_xpath_result_number(result)
            if result_type == _ffi.XPATH_STRING:
                ptr = _ffi.lib.leptris_xpath_result_string(result)
                if ptr == _ffi.ffi.NULL:
                    return ""
                value = _ffi.ffi.string(ptr).decode("utf-8")
                _ffi.lib.leptris_free_string(ptr)
                return value
            if result_type == _ffi.XPATH_BOOLEAN:
                return bool(_ffi.lib.leptris_xpath_result_boolean(result))
            return None
        finally:
            _ffi.lib.leptris_xpath_result_free(result)
