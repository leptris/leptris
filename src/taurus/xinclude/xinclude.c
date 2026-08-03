/* xinclude/xinclude.c — XInclude 1.0 support.
 *
 * Provides the public XInclude API. The element-classification helpers
 * (is_include_element, is_fallback_element, get_href, get_parse,
 * get_xpointer) are fully implemented — they only inspect element
 * metadata.
 *
 * taurus_xinclude_process is currently a stub returning
 * TAURUS_ERROR_NOT_IMPLEMENTED. Full processing requires recursive
 * href resolution, text/xml parse modes, fallback handling, and
 * xpointer evaluation. See TODO 92.
 *
 * Without this translation unit, the public API in taurus.h
 * (taurus_xinclude_process, taurus_xinclude_is_include_element, etc.)
 * would be declared but undefined — calling them from a consumer
 * would fail at link time.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include <string.h>

#define XINCLUDE_NAMESPACE "http://www.w3.org/2001/XInclude"

static int element_is_in_xinclude_namespace(TaurusElement elem) {
    if (!elem) return 0;
    const char* ns_uri = taurus_element_get_namespace_uri(elem);
    if (!ns_uri) return 0;
    return strcmp(ns_uri, XINCLUDE_NAMESPACE) == 0;
}

static const char* get_attr_value(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;
    return attr->value ? attr->value : "";
}

TAURUS_API TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url) {
    (void)doc;
    (void)base_url;
    /* TODO 92: implement full XInclude processing — href resolution,
     * parse="xml" / parse="text", xi:fallback, xpointer. The
     * element-classification helpers below are useful even without
     * the processor, so they ship now. */
    return TAURUS_ERROR_NOT_IMPLEMENTED;
}

TAURUS_API int taurus_xinclude_is_include_element(TaurusElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = taurus_element_get_name(elem);
    return name && strcmp(name, "include") == 0;
}

TAURUS_API int taurus_xinclude_is_fallback_element(TaurusElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = taurus_element_get_name(elem);
    return name && strcmp(name, "fallback") == 0;
}

TAURUS_API const char* taurus_xinclude_get_href(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "href");
}

TAURUS_API const char* taurus_xinclude_get_parse(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    const char* parse = get_attr_value(include_elem, "parse");
    /* XInclude spec: default parse mode is "xml". */
    return (parse && parse[0]) ? parse : "xml";
}

TAURUS_API const char* taurus_xinclude_get_xpointer(TaurusElement include_elem) {
    if (!taurus_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "xpointer");
}
