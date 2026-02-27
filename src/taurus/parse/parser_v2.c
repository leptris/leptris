/* parser_v2.c - 16-Byte Compact Element Parser
 * Copyright (c) 2026, Ribose Inc.
 *
 * Parser for 16-byte compact elements to match pugixml's memory footprint.
 * This is the key to achieving 1.0x vs pugixml parsing performance.
 */

#include "compact_element_v2.h"
#include "compact_single_alloc.h"
#include "../simd_helpers.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Parser State for v2
 * ============================================================================ */

typedef struct {
    const char* input;         /* Start of input */
    const char* pos;           /* Current position */
    const char* end;           /* End of input */
    char* string_base;         /* Base for string offsets (mutable buffer) */
    char* node_base;           /* Base for node offsets (from allocator) */
    CompactSingleAllocator* alloc;
    uint32_t flags;
    int has_error;
    char error_msg[256];
} ParserV2;

/* Parser flags */
#define PARSER_V2_FAST_MODE     0x01

/* ============================================================================
 * Helper Functions - Force inline for maximum performance
 * ============================================================================ */

/* Direct access macros - no function call overhead */
#define SKIP_WS_V2(p) do { \
    (p)->pos = simd_skip_whitespace((p)->pos, (p)->end); \
} while(0)

#define PEEK_V2(p) ((p)->pos < (p)->end ? *(p)->pos : '\0')

#define ADVANCE_V2(p) do { if ((p)->pos < (p)->end) (p)->pos++; } while(0)

/* Force inline functions for hot path */
#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

/* Cache node base in local variable for hot path */
#define UPDATE_BASE(p) (p)->node_base = (p)->alloc->base

/* Get node base pointer - use direct access for hot path */
#define GET_BASE(p) ((p)->alloc->base)

/* Fast allocation - use inline fast path */
static FORCE_INLINE struct compact_element_v2* alloc_v2_element(ParserV2* p) {
    struct compact_element_v2* elem = COMPACT_SINGLE_ALLOC_FAST(p->alloc, struct compact_element_v2);
    if (elem) UPDATE_BASE(p);
    return elem;
}

static FORCE_INLINE struct compact_attribute_v2* alloc_v2_attr(ParserV2* p) {
    struct compact_attribute_v2* attr = COMPACT_SINGLE_ALLOC_FAST(p->alloc, struct compact_attribute_v2);
    if (attr) UPDATE_BASE(p);
    return attr;
}

/* Convenience macros that use the inline functions */
#define ALLOC_V2_ELEMENT(p) alloc_v2_element(p)
#define ALLOC_V2_ATTR(p) alloc_v2_attr(p)

/* Keep get_node_base_v2 for compatibility */
static FORCE_INLINE char* get_node_base_v2(ParserV2* p) {
    return p->alloc->base;
}

/* Force inline helper functions for hot path */
static FORCE_INLINE void skip_ws_v2(ParserV2* p) {
    p->pos = simd_skip_whitespace(p->pos, p->end);
}

static FORCE_INLINE char peek_v2(ParserV2* p) {
    return (p->pos < p->end) ? *p->pos : '\0';
}

static FORCE_INLINE void advance_v2(ParserV2* p) {
    if (p->pos < p->end) p->pos++;
}

/* ============================================================================
 * Attribute Parsing (v2)
 * ============================================================================ */

static uint32_t parse_attribute_v2(ParserV2* p) {
    /* Parse name */
    const char* name_start = p->pos;
    p->pos = simd_scan_name(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Null-terminate name IN PLACE - no save/restore */
    if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

    /* Skip whitespace */
    skip_ws_v2(p);

    /* Expect '=' */
    if (peek_v2(p) != '=') return 0;
    advance_v2(p);
    skip_ws_v2(p);

    /* Parse value */
    char quote = peek_v2(p);
    if (quote != '"' && quote != '\'') return 0;
    advance_v2(p);

    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) {
        p->pos++;
    }
    size_t value_len = p->pos - value_start;

    /* Null-terminate value IN PLACE */
    char* value_mut = (char*)value_start;
    if (p->pos < p->end) {
        value_mut[value_len] = '\0';
        p->pos++;  /* Skip closing quote */
    }

    /* Allocate 16-byte attribute */
    struct compact_attribute_v2* attr = ALLOC_V2_ATTR(p);
    if (!attr) return 0;

    uint32_t attr_off = COMPACT_V2_PTR_TO_OFFSET(get_node_base_v2(p), attr);

    /* Store offsets (with high bit to mark as attribute) */
    attr->name_offset = (uint32_t)(name_start - p->string_base) | 0x80000000;
    attr->value_offset = (uint32_t)(value_start - p->string_base);
    attr->next_attr = 0;
    attr->flags = 0;

    return attr_off;
}

/* ============================================================================
 * Element Parsing (v2) - 16-byte elements
 * ============================================================================ */

static uint32_t parse_element_v2(ParserV2* p, uint32_t parent_offset) {
    if (peek_v2(p) != '<') return 0;
    advance_v2(p);

    /* Check for closing tag */
    if (peek_v2(p) == '/') return 0;

    /* Check for special nodes */
    char c = peek_v2(p);
    if (c == '?') {
        /* Skip processing instruction */
        p->pos--;  /* Back up to '<' */
        /* For now, skip PIs in fast mode */
        while (p->pos < p->end && *p->pos != '>') p->pos++;
        if (peek_v2(p) == '>') p->pos++;
        return 0;
    }
    if (c == '!') {
        /* Skip comments and CDATA */
        p->pos--;  /* Back up to '<' */
        while (p->pos < p->end && *p->pos != '>') p->pos++;
        if (peek_v2(p) == '>') p->pos++;
        return 0;
    }

    /* Parse element name */
    const char* name_start = p->pos;
    p->pos = simd_scan_name(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Null-terminate name IN PLACE (like pugixml) - no save/restore */
    if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

    /* Allocate 16-byte element */
    struct compact_element_v2* elem = ALLOC_V2_ELEMENT(p);
    if (!elem) return 0;

    char* node_base = get_node_base_v2(p);
    uint32_t elem_off = COMPACT_V2_PTR_TO_OFFSET(node_base, elem);

    /* Initialize 16-byte element */
    elem->first_child = 0;
    elem->next_sibling = 0;
    elem->parent = parent_offset;
    elem->name_offset = (uint32_t)(name_start - p->string_base);

    /* Parse attributes */
    skip_ws_v2(p);
    uint32_t last_attr_off = 0;

    while (peek_v2(p) != '>' && peek_v2(p) != '/' && p->pos < p->end) {
        char c = peek_v2(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            skip_ws_v2(p);
            continue;
        }

        /* Check for attribute */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':') {
            uint32_t attr_off = parse_attribute_v2(p);
            if (attr_off == 0) {
                if (p->flags & PARSER_V2_FAST_MODE) break;
                continue;
            }

            /* Link attribute to element */
            if (last_attr_off == 0) {
                /* First attribute links as first_child with special handling */
                struct compact_attribute_v2* attr =
                    (struct compact_attribute_v2*)(node_base + attr_off);
                attr->next_attr = elem->first_child;
                elem->first_child = attr_off;
            } else {
                struct compact_attribute_v2* last_attr =
                    (struct compact_attribute_v2*)(node_base + last_attr_off);
                last_attr->next_attr = attr_off;
            }
            last_attr_off = attr_off;

            /* Update node_base after potential allocation */
            node_base = get_node_base_v2(p);
            continue;
        }

        /* Unknown character - break in fast mode */
        if (p->flags & PARSER_V2_FAST_MODE) break;
        advance_v2(p);
    }

    /* Check for self-closing */
    int self_closing = 0;
    if (peek_v2(p) == '/') {
        self_closing = 1;
        advance_v2(p);
    }

    if (peek_v2(p) == '>') {
        advance_v2(p);
    }

    /* Update node_base after potential allocation */
    node_base = get_node_base_v2(p);
    elem = (struct compact_element_v2*)(node_base + elem_off);

    /* Parse children if not self-closing */
    uint32_t last_child_off = 0;

    if (!self_closing) {
        while (p->pos < p->end) {
            /* Skip whitespace between elements */
            skip_ws_v2(p);

            if (peek_v2(p) != '<') {
                /* Parse text content */
                const char* text_start = p->pos;
                while (p->pos < p->end && *p->pos != '<') {
                    p->pos++;
                }

                /* Check if text is whitespace-only */
                size_t text_len = p->pos - text_start;
                if (!simd_is_whitespace_only(text_start, p->pos)) {
                    /* Null-terminate text IN PLACE - no save/restore */
                    if (p->pos < p->end) ((char*)text_start)[text_len] = '\0';

                    /* Allocate text node (16 bytes) */
                    struct compact_text_v2* text =
                        (struct compact_text_v2*)compact_single_alloc_inline(p->alloc, sizeof(struct compact_text_v2));
                    if (text) {
                        node_base = GET_BASE(p);

                        uint32_t text_off = compact_single_get_offset_fast(p->alloc, text);
                        text->text_offset = (uint32_t)(text_start - p->string_base);
                        text->next_sibling = 0;
                        text->text_length = (uint32_t)text_len;
                        text->flags = COMPACT_V2_TYPE_TEXT;

                        /* Link to parent - use elem_off directly */
                        elem = (struct compact_element_v2*)(node_base + elem_off);
                        if (elem->first_child == 0) {
                            elem->first_child = text_off;
                        } else if (last_child_off != 0) {
                            /* Link as sibling (offset 4 is next_sibling in both structures) */
                            uint32_t* sibling_ptr = (uint32_t*)(node_base + last_child_off + 4);
                            *sibling_ptr = text_off;
                        }
                        last_child_off = text_off;
                    }
                }
                continue;
            }

            /* Check for closing tag */
            p->pos++;  /* Skip '<' */
            if (peek_v2(p) == '/') {
                /* Closing tag - skip to '>' */
                while (p->pos < p->end && *p->pos != '>') p->pos++;
                if (peek_v2(p) == '>') p->pos++;
                break;
            }

            p->pos--;  /* Back up for element parsing */

            /* Parse child element - get fresh base before recursive call */
            node_base = GET_BASE(p);

            uint32_t child_off = parse_element_v2(p, elem_off);
            if (child_off == 0) continue;

            /* Update base after potential allocation */
            node_base = GET_BASE(p);
            elem = (struct compact_element_v2*)(node_base + elem_off);

            /* Link child to parent */
            if (elem->first_child == 0) {
                elem->first_child = child_off;
            } else if (last_child_off != 0) {
                /* FAST SIBLING LINK: next_sibling is at offset 4 in BOTH structures! */
                uint32_t* sibling_ptr = (uint32_t*)(node_base + last_child_off + 4);
                *sibling_ptr = child_off;
            }
            last_child_off = child_off;
        }
    }

    return elem_off;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Parse XML into 16-byte compact elements
 *
 * This is the fastest parsing mode - matches pugixml's memory footprint.
 *
 * @param xml MUTABLE XML string (will be modified in-place)
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return taurus_document with 16-byte elements, or NULL on error
 */
struct taurus_document* taurus_parse_v2(char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Ensure null-terminated */
    xml[len] = '\0';

    /* Calculate initial allocation size
     * 16-byte elements + 16-byte attributes = much smaller than v1
     * Use 40% of input size + 64KB to minimize reallocs
     * For 70KB file: 28KB + 64KB = 92KB initial */
    size_t initial_size = (len * 4 / 10) + 65536;
    if (initial_size < 65536) initial_size = 65536;

    /* Create allocator */
    CompactSingleAllocator* alloc = compact_single_alloc_create(initial_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize parser */
    ParserV2 parser;
    parser.input = xml;
    parser.pos = xml;
    parser.end = xml + len;
    parser.string_base = xml;
    parser.node_base = alloc->base;
    parser.alloc = alloc;
    parser.flags = PARSER_V2_FAST_MODE;
    parser.has_error = 0;
    parser.error_msg[0] = '\0';

    /* Skip prolog */
    skip_ws_v2(&parser);

    /* Skip XML declaration */
    if (peek_v2(&parser) == '<' && parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
        if (peek_v2(&parser) == '>') parser.pos++;
        skip_ws_v2(&parser);
    }

    /* Parse root element */
    uint32_t root_off = parse_element_v2(&parser, 0);

    if (root_off == 0 || compact_single_has_error(alloc)) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Create document */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
    doc->compact_alloc = alloc;
    doc->compact_base = alloc->base;
    doc->compact_root_offset = root_off;
    doc->is_compact = 1;
    doc->compact_v2 = 1;  /* Mark as v2 (16-byte elements) */
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;
    doc->pool = NULL;

    if (error_out) *error_out = 0;
    return doc;
}
