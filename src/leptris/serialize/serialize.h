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

#ifndef LEPTRIS_SERIALIZE_H
#define LEPTRIS_SERIALIZE_H

#include "../dom/node.h"
#include "../dom/element.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/doctype.h"

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Serialize any node to XML string (caller must free) */
char* leptris_serialize_node(LeptrisNode* node);

/* Serialize element and children to XML string (caller must free) */
char* leptris_serialize_element(LeptrisElement elem);

/* Serialize with XML declaration (caller must free) */
char* leptris_serialize_document_with_declaration(LeptrisElement root,
                                                   const char* encoding,
                                                   const char* version,
                                                   int standalone,
                                                   int has_bom,
                                                   LeptrisDoctypeNode* doctype);

/* ============================================================================
 * Internal Buffer Management
 * ============================================================================ */

/* Dynamic string buffer for building output.
 *
 * Tagged so the forward declaration `struct SerializeBuffer;` in
 * dom/node.h resolves to the same type — TODO 43/29.
 *
 * Invariants:
 *   size <= capacity
 *   data != NULL (after buffer_create success)
 *   alloc_failed is sticky — once set, subsequent appends become no-ops.
 *     Callers that care can check buffer_has_error() before using the
 *     result; callers that don't will silently truncate, which is the
 *     historical behavior.
 */
typedef struct SerializeBuffer {
    char* data;
    size_t size;       /* Current string length */
    size_t capacity;   /* Allocated capacity */
    int indent;        /* Current indentation level (for pretty-printing) */
    int indent_spaces; /* Number of spaces per indent level (0 = compact) */
    int indent_text;   /* #129: formatter also owns text/mixed whitespace */
    int at_line_start; /* last emitted byte was a newline (dedups
                        * consecutive newlines under indent_text) */
    /* §16.1 cdata-section-elements: QNames whose text children emit
     * as CDATA. Set by the serialization entry points. */
    const char* const* cdata_names;
    size_t cdata_count;
    /* §16.2 method="html": newline-per-block-element layout governed
     * by the HTML element table (html_elem_flags). */
    int html_method;
    int alloc_failed;  /* Sticky realloc-failure flag (TODO 08) */
} SerializeBuffer;

/* HTML element flags (§16.2, libxml2 html40ElementTable parity).
 * Returns 0 for unknown elements: they never take part in the HTML
 * indent decisions (no line breaks around or inside them). */
#define HTML_F_EMPTY   1   /* void element: <br>, <img>, … */
#define HTML_F_INLINE  2   /* inline content model: <span>, <b>, … */
#define HTML_F_BLOCK   4   /* known block element: <div>, <p>, … */
#define HTML_F_RAW     8   /* rawtext content: <script>, <style>, … */
int html_elem_flags(const char* name, size_t len);

/* The typedef name `SerializeBuffer` is already declared above as
 * part of `typedef struct SerializeBuffer { ... } SerializeBuffer;`
 * — the struct tag and the typedef share a name, which lets the
 * forward declaration `struct SerializeBuffer;` in dom/node.h resolve
 * to the same type.  No additional typedef needed (TODO 43). */

/* Buffer operations */
SerializeBuffer* buffer_create(int indent_spaces);
void buffer_append(SerializeBuffer* buf, const char* str);
void buffer_append_char(SerializeBuffer* buf, char c);
void buffer_append_len(SerializeBuffer* buf, const char* str, size_t len);
void buffer_append_indent(SerializeBuffer* buf);
void buffer_append_newline(SerializeBuffer* buf);
char* buffer_to_string(SerializeBuffer* buf);
void buffer_free(SerializeBuffer* buf);
int  buffer_has_error(SerializeBuffer* buf);

/* ============================================================================
 * Internal Serialization Functions
 * ============================================================================ */

/* Extended serialization settings (issue #568): everything the
 * XSLT layer needs beyond the ABI-frozen public options. Passed
 * only through leptris_document_serialize_ex — never by growing
 * the public struct. */
typedef struct {
    const char* const* cdata_elements;   /* §16.1 cdata-section-elements */
    size_t cdata_element_count;
    int html_method;                     /* §16.2 method="html" */
    int indent_text;                     /* #129: indent text/mixed too */
} LeptrisSerializeExtended;

LEPTRIS_API char* leptris_document_serialize_ext(
    struct leptris_document* doc,
    const LeptrisSerializeOptions* options,
    const LeptrisSerializeExtOptions* ext);

char* leptris_document_serialize_ex(struct leptris_document* doc,
                                    const LeptrisSerializeOptions* options,
                                    const LeptrisSerializeExtended* extended);

void serialize_node_internal(LeptrisNode* node, SerializeBuffer* buf);
void serialize_element_internal(LeptrisElement elem, SerializeBuffer* buf, int is_root);
void serialize_text_internal(LeptrisTextNode* text, SerializeBuffer* buf);
void serialize_comment_internal(LeptrisCommentNode* comment, SerializeBuffer* buf);
void serialize_cdata_internal(LeptrisCDATANode* cdata, SerializeBuffer* buf);
void serialize_pi_internal(LeptrisPINode* pi, SerializeBuffer* buf);
void serialize_doctype_internal(LeptrisDoctypeNode* doctype, SerializeBuffer* buf);

#endif /* LEPTRIS_SERIALIZE_H */