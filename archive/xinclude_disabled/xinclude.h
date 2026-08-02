/* lib/src/xinclude/xinclude.h - XInclude 1.0 Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements W3C XInclude 1.0:
 * https://www.w3.org/TR/xinclude/
 */

#ifndef TAURUS_XINCLUDE_H
#define TAURUS_XINCLUDE_H

#include "../include/taurus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* XInclude namespace */
#define TAURUS_XINCLUDE_NS "http://www.w3.org/2001/XInclude"

/* XInclude element names */
#define TAURUS_XINCLUDE_INCLUDE "include"
#define TAURUS_XINCLUDE_FALLBACK "fallback"

/* XInclude attribute names */
#define TAURUS_XINCLUDE_ATTR_HREF "href"
#define TAURUS_XINCLUDE_ATTR_PARSE "parse"
#define TAURUS_XINCLUDE_ATTR_ENCODING "encoding"
#define TAURUS_XINCLUDE_ATTR_XPOINTER "xpointer"

/* Parse modes */
#define TAURUS_XINCLUDE_PARSE_XML "xml"
#define TAURUS_XINCLUDE_PARSE_TEXT "text"

/**
 * Process XInclude elements in document
 *
 * Replaces all <xi:include> elements with their included content.
 * Follows W3C XInclude 1.0 specification.
 *
 * @param doc Document to process
 * @param base_url Base URL for resolving relative hrefs (can be NULL)
 * @return TAURUS_OK on success, error code on failure
 */
TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url);

/**
 * Check if element is an XInclude include element
 */
int taurus_xinclude_is_include_element(TaurusElement elem);

/**
 * Check if element is an XInclude fallback element
 */
int taurus_xinclude_is_fallback_element(TaurusElement elem);

/**
 * Get href attribute value from include element
 * Returns NULL if not found or on error
 */
const char* taurus_xinclude_get_href(TaurusElement include_elem);

/**
 * Get parse attribute value from include element
 * Returns TAURUS_XINCLUDE_PARSE_XML by default if not specified
 */
const char* taurus_xinclude_get_parse(TaurusElement include_elem);

/**
 * Get xpointer attribute value from include element
 * Returns NULL if not specified
 */
const char* taurus_xinclude_get_xpointer(TaurusElement include_elem);

/**
 * Get encoding attribute value from include element (for parse="text")
 * Returns NULL if not specified
 */
const char* taurus_xinclude_get_encoding(TaurusElement include_elem);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_XINCLUDE_H */
