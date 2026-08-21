"""C bridge for pyleptris.

cffi ABI mode: the cdef below mirrors the public headers
(src/include/leptris/). All handles are opaque pointers; strings
returned by accessors are document-owned and only valid until
leptris_document_free — copy into Python str at the boundary.

The library is resolved from LEPTRIS_LIB_PATH, then the usual
install names, then the local build directory.
"""

import os

from cffi import FFI

ffi = FFI()

ffi.cdef(
    """
    typedef struct leptris_document* LeptrisDocument;
    typedef struct leptris_element*  LeptrisElement;
    typedef struct leptris_node*     LeptrisNodeRef;
    typedef struct leptris_attribute* LeptrisAttribute;
    typedef struct leptris_xpath_result* LeptrisXPathResult;

    LeptrisDocument leptris_parse_string(const char* xml, size_t len, int* status);
    void           leptris_document_free(LeptrisDocument doc);
    LeptrisElement  leptris_document_root(LeptrisDocument doc);
    char*          leptris_serialize_document(LeptrisDocument doc, void* options);
    int            leptris_xinclude_process(LeptrisDocument doc, const char* base_path);

    int    leptris_node_get_type(LeptrisNodeRef node);
    LeptrisNodeRef leptris_node_first_child(LeptrisNodeRef node);
    LeptrisNodeRef leptris_node_next_sibling(LeptrisNodeRef node);
    LeptrisNodeRef leptris_node_previous_sibling(LeptrisNodeRef node);
    size_t leptris_node_child_count(LeptrisNodeRef node);
    LeptrisElement  leptris_node_as_element(LeptrisNodeRef node);
    LeptrisNodeRef  leptris_element_as_node(LeptrisElement elem);

    const char* leptris_element_name(LeptrisElement elem);
    const char* leptris_element_text(LeptrisElement elem);
    LeptrisElement leptris_element_first_child_any(LeptrisElement elem);
    LeptrisElement leptris_element_parent(LeptrisElement elem);
    const char* leptris_element_attribute(LeptrisElement elem,
                                         const char* name);
    LeptrisElement leptris_element_next_sibling_any(LeptrisElement elem);
    LeptrisAttribute leptris_element_first_attribute(LeptrisElement elem);
    LeptrisAttribute leptris_attribute_next(LeptrisAttribute attr);
    const char* leptris_attribute_get_name(LeptrisAttribute attr);
    const char* leptris_attribute_get_value(LeptrisElement elem,
                                            LeptrisAttribute attr);
    size_t leptris_element_attribute_count(LeptrisElement elem);
    size_t leptris_element_child_count(LeptrisElement elem);

    const char* leptris_text_node_get_content(LeptrisNodeRef node);
    const char* leptris_comment_node_get_content(LeptrisNodeRef node);
    const char* leptris_cdata_node_get_content(LeptrisNodeRef node);
    const char* leptris_pi_node_get_target(LeptrisNodeRef node);
    const char* leptris_pi_node_get_data(LeptrisNodeRef node);

    LeptrisXPathResult leptris_xpath_eval(LeptrisDocument doc,
                                        LeptrisElement context,
                                        const char* expression);
    void     leptris_xpath_result_free(LeptrisXPathResult result);
    int      leptris_xpath_result_type(LeptrisXPathResult result);
    double   leptris_xpath_result_number(LeptrisXPathResult result);
    int      leptris_xpath_result_boolean(LeptrisXPathResult result);
    char*    leptris_xpath_result_string(LeptrisXPathResult result);
    size_t   leptris_xpath_result_count(LeptrisXPathResult result);
    LeptrisElement leptris_xpath_result_get(LeptrisXPathResult result, size_t index);

    void leptris_free_string(char* str);
    """
)


def _load():
    candidates = []
    if os.environ.get("LEPTRIS_LIB_PATH"):
        candidates.append(os.environ["LEPTRIS_LIB_PATH"])
    candidates += ["libleptris.dylib", "libleptris.so", "leptris.dll"]
    here = os.path.dirname(__file__)
    candidates += [
        os.path.join(here, "..", "..", "..", "build", "src", "libleptris.dylib"),
        os.path.join(here, "..", "..", "..", "build", "src", "libleptris.so"),
    ]
    for name in candidates:
        try:
            return ffi.dlopen(name)
        except OSError:
            continue
    raise ImportError(
        "libleptris not found; build it or set LEPTRIS_LIB_PATH"
    )


lib = _load()

NODE_ELEMENT = 0
NODE_TEXT = 1
NODE_COMMENT = 2
NODE_CDATA = 3
NODE_PI = 4
NODE_DOCTYPE = 5

XPATH_NODESET = 0
XPATH_BOOLEAN = 1
XPATH_NUMBER = 2
XPATH_STRING = 3
