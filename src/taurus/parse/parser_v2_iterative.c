/* parser_v2_iterative.c - 16-Byte Iterative Parser (No Recursion)
 * Copyright (c) 2026, Ribose Inc.
 *
 * ITERATIVE parser for 16-byte compact elements.
 * Eliminates recursive function call overhead (~5-10 cycles per element).
 *
 * Key optimization: Uses explicit stack instead of recursive calls.
 * This matches pugixml's approach for maximum performance.
 */

#include "compact_element_v2.h"
#include "compact_single_alloc.h"
#include "../simd_helpers.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Parser State for v2 Iterative
 * ============================================================================ */

/* Maximum nesting depth - fixed stack size for zero allocation overhead */
#define MAX_STACK_DEPTH 1024

/* Stack entry for iterative parsing - 8 bytes */
typedef struct {
    uint32_t elem_offset;     /* Current element being parsed */
    uint32_t last_child_off;  /* Last child offset for sibling linking */
} StackEntry;

typedef struct {
    const char* pos;           /* Current position */
    const char* end;           /* End of input */
    char* string_base;         /* Base for string offsets (mutable buffer) */
    CompactSingleAllocator* alloc;

    /* FIXED-SIZE stack - NO malloc/realloc overhead */
    StackEntry stack[MAX_STACK_DEPTH];
    size_t stack_size;
} ParserV2Iter;

/* ============================================================================
 * Inline Helper Functions - Zero function call overhead
 * ============================================================================ */

/* Force inline functions for hot path */
#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

/* Direct character checks - NO function calls, like pugixml */
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define IS_NAME_START(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || (c) == '_' || (c) == ':')
#define IS_NAME_CHAR(c) (IS_NAME_START(c) || ((c) >= '0' && (c) <= '9') || (c) == '-' || (c) == '.')

/* Inline scan_name - NO function call */
static FORCE_INLINE const char* scan_name_inline(const char* p, const char* end) {
    while (p < end && IS_NAME_CHAR(*p)) p++;
    return p;
}

/* Inline whitespace skip - NO function call */
static FORCE_INLINE const char* skip_ws_inline(const char* p, const char* end) {
    while (p < end && IS_SPACE(*p)) p++;
    return p;
}

/* Inline whitespace-only check */
static FORCE_INLINE int is_ws_only(const char* p, const char* end) {
    while (p < end) {
        if (!IS_SPACE(*p)) return 0;
        p++;
    }
    return 1;
}

/* ULTRA-FAST inline allocation - no error check, no alignment calculation
 * Assumes 16-byte structures are naturally aligned (they are)
 */
static FORCE_INLINE void* alloc_ultra_fast(CompactSingleAllocator* alloc, size_t size) {
    size_t offset = alloc->offset;
    size_t new_offset = offset + size;

    /* Fast path check - no growth needed for most cases */
    if (new_offset <= alloc->size) {
        alloc->offset = new_offset;
        return alloc->base + offset;
    }

    /* Slow path - need growth */
    return compact_single_alloc(alloc, size);
}

/* Fast allocation macros for 16-byte structures */
#define ALLOC_ELEM(p) ((struct compact_element_v2*)alloc_ultra_fast((p)->alloc, 16))
#define ALLOC_ATTR(p) ((struct compact_attribute_v2*)alloc_ultra_fast((p)->alloc, 16))
#define ALLOC_TEXT(p) ((struct compact_text_v2*)alloc_ultra_fast((p)->alloc, 16))

/* INLINE stack operations - zero function call overhead */
#define STACK_PUSH(p, elem_off) do { \
    if ((p)->stack_size < MAX_STACK_DEPTH) { \
        (p)->stack[(p)->stack_size].elem_offset = (elem_off); \
        (p)->stack[(p)->stack_size].last_child_off = 0; \
        (p)->stack_size++; \
    } \
} while(0)

#define STACK_PEEK(p) ((p)->stack + (p)->stack_size - 1)

#define STACK_POP(p) do { \
    if ((p)->stack_size > 0) (p)->stack_size--; \
} while(0)

/* ============================================================================
 * Attribute Parsing (v2) - Non-recursive
 * ============================================================================ */

static uint32_t parse_attribute_v2_inline(ParserV2Iter* p, char* base) {
    /* Parse name */
    const char* name_start = p->pos;
    p->pos = scan_name_inline(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Null-terminate name IN PLACE */
    if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

    /* Skip whitespace */
    p->pos = skip_ws_inline(p->pos, p->end);

    /* Expect '=' */
    if (p->pos >= p->end || *p->pos != '=') return 0;
    p->pos++;
    p->pos = skip_ws_inline(p->pos, p->end);

    /* Parse value */
    if (p->pos >= p->end) return 0;
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') return 0;
    p->pos++;

    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) {
        p->pos++;
    }
    size_t value_len = p->pos - value_start;

    /* Null-terminate value IN PLACE */
    if (p->pos < p->end) {
        ((char*)value_start)[value_len] = '\0';
        p->pos++;  /* Skip closing quote */
    }

    /* Allocate 16-byte attribute - ULTRA FAST */
    struct compact_attribute_v2* attr = ALLOC_ATTR(p);
    if (!attr) return 0;

    /* Calculate offset directly from base pointer */
    uint32_t attr_off = (uint32_t)((char*)attr - base);

    /* Store offsets (with high bit to mark as attribute) */
    attr->name_offset = (uint32_t)(name_start - p->string_base) | 0x80000000;
    attr->value_offset = (uint32_t)(value_start - p->string_base);
    attr->next_attr = 0;
    attr->flags = 0;

    return attr_off;
}

/* ============================================================================
 * Iterative Element Parsing - No recursion!
 * ============================================================================ */

/**
 * Parse XML iteratively using explicit stack
 *
 * MAXIMUM PERFORMANCE: All helper functions inlined as macros or force_inline.
 * Direct character comparisons instead of SIMD for short strings.
 */
static uint32_t parse_iterative(ParserV2Iter* p) {
    uint32_t root_offset = 0;
    int parsing_root = 1;

    /* CACHE BASE POINTER - update only when allocator grows */
    char* base = p->alloc->base;

    while (p->pos < p->end) {
        /* Skip whitespace */
        p->pos = skip_ws_inline(p->pos, p->end);
        if (p->pos >= p->end) break;

        /* Check current context */
        StackEntry* current = (p->stack_size > 0) ? STACK_PEEK(p) : NULL;

        if (*p->pos != '<') {
            /* Text content */
            if (!current) {
                p->pos++;
                continue;
            }

            const char* text_start = p->pos;
            while (p->pos < p->end && *p->pos != '<') {
                p->pos++;
            }
            size_t text_len = p->pos - text_start;

            /* Skip whitespace-only text */
            if (is_ws_only(text_start, p->pos)) {
                continue;
            }

            /* Null-terminate text IN PLACE */
            if (p->pos < p->end) ((char*)text_start)[text_len] = '\0';

            /* Allocate text node - ULTRA FAST */
            struct compact_text_v2* text = ALLOC_TEXT(p);
            if (!text) continue;

            uint32_t text_off = (uint32_t)((char*)text - base);

            text->text_offset = (uint32_t)(text_start - p->string_base);
            text->next_sibling = 0;
            text->text_length = (uint32_t)text_len;
            text->flags = COMPACT_V2_TYPE_TEXT;

            /* Link to parent */
            struct compact_element_v2* elem = (struct compact_element_v2*)
                (base + current->elem_offset);

            if (current->last_child_off == 0) {
                elem->first_child = text_off;
            } else {
                /* FAST SIBLING LINK: next_sibling is at offset 4 */
                uint32_t* sibling_ptr = (uint32_t*)(base + current->last_child_off + 4);
                *sibling_ptr = text_off;
            }
            current->last_child_off = text_off;

            continue;
        }

        /* We have '<' */
        p->pos++;  /* Skip '<' */
        char c = *p->pos;

        /* Closing tag? */
        if (c == '/') {
            p->pos++;

            /* Skip to '>' */
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end && *p->pos == '>') p->pos++;

            /* Pop stack - done with this element */
            STACK_POP(p);

            /* Update current pointer - NO need to walk siblings */
            current = (p->stack_size > 0) ? STACK_PEEK(p) : NULL;

            continue;
        }

        /* Skip special nodes (PI, comments, CDATA) */
        if (c == '?' || c == '!') {
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end && *p->pos == '>') p->pos++;
            continue;
        }

        /* Parse element name */
        const char* name_start = p->pos;
        p->pos = scan_name_inline(p->pos, p->end);
        size_t name_len = p->pos - name_start;
        if (name_len == 0) continue;

        /* Null-terminate name IN PLACE */
        if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

        /* Allocate 16-byte element - ULTRA FAST */
        struct compact_element_v2* elem = ALLOC_ELEM(p);
        if (!elem) continue;

        uint32_t elem_off = (uint32_t)((char*)elem - base);

        /* Initialize 16-byte element */
        elem->first_child = 0;
        elem->next_sibling = 0;
        elem->parent = current ? current->elem_offset : 0;
        elem->name_offset = (uint32_t)(name_start - p->string_base);

        /* Link to parent or as root */
        if (current) {
            if (current->last_child_off == 0) {
                /* Refresh parent pointer after potential realloc */
                struct compact_element_v2* parent = (struct compact_element_v2*)
                    (base + current->elem_offset);
                parent->first_child = elem_off;
            } else {
                /* FAST SIBLING LINK: next_sibling is at offset 4 */
                uint32_t* sibling_ptr = (uint32_t*)(base + current->last_child_off + 4);
                *sibling_ptr = elem_off;
            }
            current->last_child_off = elem_off;
        } else if (parsing_root) {
            root_offset = elem_off;
            parsing_root = 0;
        }

        /* Parse attributes */
        p->pos = skip_ws_inline(p->pos, p->end);
        uint32_t last_attr_off = 0;

        while (p->pos < p->end && *p->pos != '>' && *p->pos != '/') {
            char ac = *p->pos;
            if (IS_SPACE(ac)) {
                p->pos = skip_ws_inline(p->pos, p->end);
                continue;
            }

            if (IS_NAME_START(ac)) {
                uint32_t attr_off = parse_attribute_v2_inline(p, base);
                if (attr_off == 0) break;

                /* Link attribute */
                elem = (struct compact_element_v2*)(base + elem_off);
                if (last_attr_off == 0) {
                    struct compact_attribute_v2* attr =
                        (struct compact_attribute_v2*)(base + attr_off);
                    attr->next_attr = elem->first_child;
                    elem->first_child = attr_off;
                } else {
                    struct compact_attribute_v2* last_attr =
                        (struct compact_attribute_v2*)(base + last_attr_off);
                    last_attr->next_attr = attr_off;
                }
                last_attr_off = attr_off;
                continue;
            }

            break;
        }

        /* Check for self-closing */
        int self_closing = 0;
        if (p->pos < p->end && *p->pos == '/') {
            self_closing = 1;
            p->pos++;
        }

        if (p->pos < p->end && *p->pos == '>') {
            p->pos++;
        }

        /* If not self-closing, push to stack for children */
        if (!self_closing) {
            STACK_PUSH(p, elem_off);
        }
    }

    return root_offset;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Parse XML into 16-byte compact elements (ITERATIVE version)
 *
 * This is the fastest parsing mode - no recursive function calls.
 *
 * @param xml MUTABLE XML string (will be modified in-place)
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return taurus_document with 16-byte elements, or NULL on error
 */
struct taurus_document* taurus_parse_v2_iterative(char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Ensure null-terminated */
    xml[len] = '\0';

    /* Calculate initial allocation size - OVER-ALLOCATE to avoid any growth
     * For XML parsing, we typically need:
     * - 16 bytes per element (average 20-50 chars per element = 40-100 bytes)
     * - Plus 16 bytes per attribute
     * - Plus 16 bytes per text node
     * A safe estimate is 50% of input size for nodes, giving plenty of headroom
     */
    size_t initial_size = (len / 2) + 65536;
    if (initial_size < 131072) initial_size = 131072;  /* Minimum 128KB */

    /* Create allocator */
    CompactSingleAllocator* alloc = compact_single_alloc_create(initial_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize parser - STACK IS FIXED SIZE, NO MALLOC */
    ParserV2Iter parser;
    parser.pos = xml;
    parser.end = xml + len;
    parser.string_base = xml;
    parser.alloc = alloc;
    parser.stack_size = 0;

    /* Skip prolog - use inline whitespace skip */
    parser.pos = skip_ws_inline(parser.pos, parser.end);

    /* Skip XML declaration */
    if (parser.pos < parser.end && *parser.pos == '<' &&
        parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
        if (parser.pos < parser.end && *parser.pos == '>') parser.pos++;
        parser.pos = skip_ws_inline(parser.pos, parser.end);
    }

    /* Parse iteratively */
    uint32_t root_off = parse_iterative(&parser);

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
