"""C bridge for pytaurus.

cffi ABI mode: the cdef below mirrors the public headers
(src/include/taurus/). All handles are opaque pointers; strings
returned by accessors are document-owned and only valid until
taurus_document_free — copy into Python str at the boundary.

The library is resolved from TAURUS_LIB_PATH, then the usual
install names, then the local build directory.
"""

import os

from cffi import FFI

ffi = FFI()

ffi.cdef(
    """
    typedef struct taurus_document* TaurusDocument;
    typedef struct taurus_element*  TaurusElement;
    typedef struct taurus_node*     TaurusNodeRef;
    typedef struct taurus_xpath_result* TaurusXPathResult;

    TaurusDocument taurus_parse_string(const char* xml, size_t len, int* status);
    void           taurus_document_free(TaurusDocument doc);
    TaurusElement  taurus_document_root(TaurusDocument doc);
    char*          taurus_serialize_document(TaurusDocument doc, void* options);
    int            taurus_xinclude_process(TaurusDocument doc, const char* base_path);

    int    taurus_node_get_type(TaurusNodeRef node);
    TaurusNodeRef taurus_node_first_child(TaurusNodeRef node);
    TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node);
    TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node);
    size_t taurus_node_child_count(TaurusNodeRef node);
    TaurusElement  taurus_node_as_element(TaurusNodeRef node);
    TaurusNodeRef  taurus_element_as_node(TaurusElement elem);

    const char* taurus_element_name(TaurusElement elem);
    const char* taurus_element_text(TaurusElement elem);
    TaurusElement taurus_element_first_child_any(TaurusElement elem);
    TaurusElement taurus_element_parent(TaurusElement elem);
    const char* taurus_element_attribute(TaurusElement elem,
                                         const char* name,
                                         const char* default_value);
    TaurusElement taurus_element_next_sibling_any(TaurusElement elem);
    size_t taurus_element_attribute_count(TaurusElement elem);
    size_t taurus_element_child_count(TaurusElement elem);

    const char* taurus_text_node_get_content(TaurusNodeRef node);
    const char* taurus_comment_node_get_content(TaurusNodeRef node);
    const char* taurus_cdata_node_get_content(TaurusNodeRef node);
    const char* taurus_pi_node_get_target(TaurusNodeRef node);
    const char* taurus_pi_node_get_data(TaurusNodeRef node);

    TaurusXPathResult taurus_xpath_eval(TaurusDocument doc,
                                        TaurusElement context,
                                        const char* expression);
    void     taurus_xpath_result_free(TaurusXPathResult result);
    int      taurus_xpath_result_type(TaurusXPathResult result);
    double   taurus_xpath_result_number(TaurusXPathResult result);
    int      taurus_xpath_result_boolean(TaurusXPathResult result);
    char*    taurus_xpath_result_string(TaurusXPathResult result);
    size_t   taurus_xpath_result_count(TaurusXPathResult result);
    TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index);

    void taurus_free_string(char* str);
    """
)


def _load():
    candidates = []
    if os.environ.get("TAURUS_LIB_PATH"):
        candidates.append(os.environ["TAURUS_LIB_PATH"])
    candidates += ["libtaurus.dylib", "libtaurus.so", "taurus.dll"]
    here = os.path.dirname(__file__)
    candidates += [
        os.path.join(here, "..", "..", "..", "build", "src", "libtaurus.dylib"),
        os.path.join(here, "..", "..", "..", "build", "src", "libtaurus.so"),
    ]
    for name in candidates:
        try:
            return ffi.dlopen(name)
        except OSError:
            continue
    raise ImportError(
        "libtaurus not found; build it or set TAURUS_LIB_PATH"
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
