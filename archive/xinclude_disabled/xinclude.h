/* lib/src/xinclude/xinclude.h - XInclude 1.0 Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements W3C XInclude 1.0:
 * https://www.w3.org/TR/xinclude/
 */

#ifndef LEPTRIS_XINCLUDE_H
#define LEPTRIS_XINCLUDE_H

#include "../include/leptris.h"

#ifdef __cplusplus
extern "C" {
#endif

/* XInclude namespace */
#define LEPTRIS_XINCLUDE_NS "http://www.w3.org/2001/XInclude"

/* XInclude element names */
#define LEPTRIS_XINCLUDE_INCLUDE "include"
#define LEPTRIS_XINCLUDE_FALLBACK "fallback"

/* XInclude attribute names */
#define LEPTRIS_XINCLUDE_ATTR_HREF "href"
#define LEPTRIS_XINCLUDE_ATTR_PARSE "parse"
#define LEPTRIS_XINCLUDE_ATTR_ENCODING "encoding"
#define LEPTRIS_XINCLUDE_ATTR_XPOINTER "xpointer"

/* Parse modes */
#define LEPTRIS_XINCLUDE_PARSE_XML "xml"
#define LEPTRIS_XINCLUDE_PARSE_TEXT "text"

/**
 * Process XInclude elements in document
 *
 * Replaces all <xi:include> elements with their included content.
 * Follows W3C XInclude 1.0 specification.
 *
 * @param doc Document to process
 * @param base_url Base URL for resolving relative hrefs (can be NULL)
 * @return LEPTRIS_OK on success, error code on failure
 */
LeptrisStatus leptris_xinclude_process(LeptrisDocument doc, const char* base_url);

/**
 * Check if element is an XInclude include element
 */
int leptris_xinclude_is_include_element(LeptrisElement elem);

/**
 * Check if element is an XInclude fallback element
 */
int leptris_xinclude_is_fallback_element(LeptrisElement elem);

/**
 * Get href attribute value from include element
 * Returns NULL if not found or on error
 */
const char* leptris_xinclude_get_href(LeptrisElement include_elem);

/**
 * Get parse attribute value from include element
 * Returns LEPTRIS_XINCLUDE_PARSE_XML by default if not specified
 */
const char* leptris_xinclude_get_parse(LeptrisElement include_elem);

/**
 * Get xpointer attribute value from include element
 * Returns NULL if not specified
 */
const char* leptris_xinclude_get_xpointer(LeptrisElement include_elem);

/**
 * Get encoding attribute value from include element (for parse="text")
 * Returns NULL if not specified
 */
const char* leptris_xinclude_get_encoding(LeptrisElement include_elem);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_XINCLUDE_H */
