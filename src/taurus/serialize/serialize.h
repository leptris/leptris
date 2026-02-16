/* lib/src/serialize/serialize.h - XML Serialization
 * Copyright (c) 2024, Ribose Inc.
 *
 * Serializes DOM nodes back to XML string format.
 *
 * CRITICAL RULES:
 * 1. Character-perfect output - must match input exactly
 * 2. No escaping in CDATA - CDATA content is literal
 * 3. Proper escaping elsewhere - text needs <>&"' escaped
 * 4. Document order - traverse tree correctly
 */

#ifndef TAURUS_SERIALIZE_H
#define TAURUS_SERIALIZE_H

#include "../dom/node.h"
#include "../dom/element.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/doctype.h"

/* ============================================================================
 * Serialization Options
 * ============================================================================ */

/* Line ending options */
typedef enum {
    LINE_ENDING_LF,     /* Unix: \n (default) */
    LINE_ENDING_CRLF    /* Windows: \r\n */
} LineEnding;

/* Indent character options */
typedef enum {
    INDENT_CHAR_SPACE,  /* Spaces (default) */
    INDENT_CHAR_TAB     /* Tabs */
} IndentChar;

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Serialize any node to XML string (caller must free) */
char* taurus_serialize_node(TaurusNode* node);

/* Serialize element and children to XML string (caller must free) */
char* taurus_serialize_element(TaurusElement elem);

/* Serialize element with formatting options (caller must free) */
char* taurus_serialize_element_with_options(TaurusElement elem,
                                             int indent_spaces,
                                             IndentChar indent_char,
                                             LineEnding line_ending);

/* Serialize with XML declaration (caller must free) */
char* taurus_serialize_document_with_declaration(TaurusElement root,
                                                   const char* encoding,
                                                   const char* version,
                                                   int standalone,
                                                   int has_bom,
                                                   TaurusDoctypeNode* doctype);

/* ============================================================================
 * Internal Buffer Management
 * ============================================================================ */

/* Dynamic string buffer for building output */
typedef struct {
    char* data;
    size_t size;            /* Current string length */
    size_t capacity;        /* Allocated capacity */
    int indent;             /* Current indentation level (for pretty-printing) */
    int indent_spaces;      /* Number of spaces per indent level (0 = compact) */
    int preserve_whitespace; /* xml:space="preserve" in effect */
    IndentChar indent_char;  /* Character to use for indentation */
    LineEnding line_ending;  /* Line ending style */
} SerializeBuffer;

/* Buffer operations */
SerializeBuffer* buffer_create(int indent_spaces);
SerializeBuffer* buffer_create_with_options(int indent_spaces, IndentChar indent_char, LineEnding line_ending);
void buffer_append(SerializeBuffer* buf, const char* str);
void buffer_append_char(SerializeBuffer* buf, char c);
void buffer_append_len(SerializeBuffer* buf, const char* str, size_t len);
void buffer_append_indent(SerializeBuffer* buf);
void buffer_append_newline(SerializeBuffer* buf);
char* buffer_to_string(SerializeBuffer* buf);
void buffer_free(SerializeBuffer* buf);

/* ============================================================================
 * Internal Serialization Functions
 * ============================================================================ */

void serialize_node_internal(TaurusNode* node, SerializeBuffer* buf);
void serialize_element_internal(TaurusElement elem, SerializeBuffer* buf, int is_root);
void serialize_text_internal(TaurusTextNode* text, SerializeBuffer* buf);
void serialize_comment_internal(TaurusCommentNode* comment, SerializeBuffer* buf);
void serialize_cdata_internal(TaurusCDATANode* cdata, SerializeBuffer* buf);
void serialize_pi_internal(TaurusPINode* pi, SerializeBuffer* buf);
void serialize_doctype_internal(TaurusDoctypeNode* doctype, SerializeBuffer* buf);

#endif /* TAURUS_SERIALIZE_H */