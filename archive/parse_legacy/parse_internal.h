/* parse_internal.h - Internal parser function declarations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * INTERNAL HEADER - Not part of public API
 * Shared between parse modules (parse_document.c, parse_element.c, parse_content.c)
 */

#ifndef LEPTRIS_PARSE_INTERNAL_H
#define LEPTRIS_PARSE_INTERNAL_H

#include "leptris_parse.h"
#include "leptris_internal.h"
#include "parse_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================
 * STRING HELPERS (from leptris_parse.c)
 * ================================================================== */

/* Duplicate string from buffer with length (NOT null-terminated in source)
 * Caller must free returned string with leptris_free() */
char *leptris_strndup(const char *str, size_t len);

/* Extract prefix and local name from qualified name "prefix:local"
 * Returns 0 if no prefix, 1 if prefix found, -1 on error
 * Modifies qname buffer in place (replaces ':' with '\0')
 * Caller must free qname after use */
int extract_prefix_and_local(char *qname, char **prefix, char **local_name);

/* Extract just local name from qualified name (for end tag comparison)
 * Returns pointer into qname buffer (after ':' or whole name if no prefix) */
const char *extract_local_name_only(const char *qname);

/* ==================================================================
 * CONTENT PROCESSING (from parse_content.c)
 * ================================================================== */

/* Expand entity reference &name; or &#number; or &#xhex;
 * Returns expanded string (MUST be freed by caller) or NULL if unknown */
char* expand_entity_reference(const char* ref, size_t len);

/* Add text content to element (creates or appends to text_content field)
 * Automatically expands entity references during addition */
void add_text_to_element(struct leptris_element *elem, const char *text, size_t len);

/* Process namespace declarations from attribute stack
 * Extracts xmlns and xmlns:prefix attributes and creates namespace objects */
void process_namespace_declarations(struct leptris_element *elem, LeptrisAttrStack *attr_stack);

/* Set element namespace from prefix or parent inheritance
 * Resolves prefix to URI using namespace chain */
void set_element_namespace(struct leptris_element *elem, const char *prefix,
                           struct leptris_element *parent);

/* Add non-xmlns attributes to element
 * Filters out namespace declarations and creates attribute objects */
void add_attributes_to_element(struct leptris_element *elem, LeptrisAttrStack *attr_stack,
                               StringInternTable *intern_table);

/* ==================================================================
 * PROCESSING INSTRUCTION PARSING (from parse_document.c)
 * ================================================================== */

/* Parse processing instruction <?target data?>
 * Assumes positioned after '<?'
 * Returns newly created PI or NULL on error */
struct leptris_processing_instruction *parse_processing_instruction(LeptrisParseContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_PARSE_INTERNAL_H */