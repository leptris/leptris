/* parser_v3.c - Ultra-Fast 20-Byte Iterative Parser
 * Copyright (c) 2026, Ribose Inc.
 *
 * MAXIMUM PERFORMANCE PARSER
 * Target: 1.0x vs pugixml (50 us for 70 KB file)
 *
 * Key optimizations over v2:
 * 1. 20-byte elements with SEPARATE attribute storage (no type checks)
 * 2. Zero-check ultra-fast allocator (1-2 cycles per allocation)
 * 3. Direct offset returns (no pointer-to-offset conversion)
 * 4. Minimal branching in hot path
 * 5. Inline everything as macros
 */

#include "../dom/compact_element_v3.h"
#include "../memory/ultra_fast_alloc.h"
#include "../simd_helpers.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Parser State for v3
 * ============================================================================ */

/* Fixed stack size for iterative parsing */
#define MAX_STACK_DEPTH 1024

/* Stack entry - just offsets, no extra data */
typedef struct {
    uint32_t elem_off;      /* Current element offset */
    uint32_t last_child;    /* Last child offset for fast linking */
} StackEntryV3;

/* Parser state */
typedef struct {
    const char* pos;        /* Current position */
    const char* end;        /* End of input */
    char* base;             /* String base (same as input for in-place) */
    char* node_base;        /* Node memory base (from allocator) */
    UltraFastAlloc* alloc;  /* Ultra-fast allocator */

    /* Fixed stack */
    StackEntryV3 stack[MAX_STACK_DEPTH];
    size_t stack_depth;
} ParserV3;

/* ============================================================================
 * Inline Character Classification - NO function calls
 * ============================================================================ */

#define IS_SPACE(c)     ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define IS_NAME_START(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || (c) == '_' || (c) == ':')
#define IS_NAME_CHAR(c)  (IS_NAME_START(c) || ((c) >= '0' && (c) <= '9') || (c) == '-' || (c) == '.')

/* ============================================================================
 * Inline Scanning Functions - Force inline
 * ============================================================================ */

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

/* Scan name - returns pointer after name */
static FORCE_INLINE const char* scan_name_v3(const char* p, const char* end) {
    while (p < end && IS_NAME_CHAR(*p)) p++;
    return p;
}

/* Skip whitespace - returns pointer after whitespace */
static FORCE_INLINE const char* skip_ws_v3(const char* p, const char* end) {
    while (p < end && IS_SPACE(*p)) p++;
    return p;
}

/* Check if text is whitespace only */
static FORCE_INLINE int is_ws_only_v3(const char* p, const char* end) {
    while (p < end) {
        if (!IS_SPACE(*p)) return 0;
        p++;
    }
    return 1;
}

/* ============================================================================
 * Stack Operations - Inline
 * ============================================================================ */

#define STACK_PUSH_V3(p, off) do { \
    (p)->stack[(p)->stack_depth].elem_off = (off); \
    (p)->stack[(p)->stack_depth].last_child = 0; \
    (p)->stack_depth++; \
} while(0)

#define STACK_PEEK_V3(p) (&(p)->stack[(p)->stack_depth - 1])

#define STACK_POP_V3(p) do { (p)->stack_depth--; } while(0)

/* ============================================================================
 * Ultra-Fast Allocation - Returns offset directly
 * ============================================================================ */

/* Allocate 20-byte v3 element */
#define ALLOC_ELEM_V3(p) ALLOC_20_OFFSET((p)->alloc)

/* Allocate 16-byte attribute */
#define ALLOC_ATTR_V3(p) ALLOC_16_OFFSET((p)->alloc)

/* Allocate 16-byte text */
#define ALLOC_TEXT_V3(p) ALLOC_16_OFFSET((p)->alloc)

/* Update node base after allocation (in case of growth - shouldn't happen) */
#define UPDATE_BASE(p) (p)->node_base = (p)->alloc->base

/* ============================================================================
 * Attribute Parsing (v3) - Separate from children
 * ============================================================================ */

static uint32_t parse_attr_v3(ParserV3* p) {
    /* Scan name */
    const char* name_start = p->pos;
    p->pos = scan_name_v3(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Null-terminate in place */
    if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

    /* Skip to '=' */
    p->pos = skip_ws_v3(p->pos, p->end);
    if (p->pos >= p->end || *p->pos != '=') return 0;
    p->pos++;

    /* Skip to quote */
    p->pos = skip_ws_v3(p->pos, p->end);
    if (p->pos >= p->end) return 0;
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') return 0;
    p->pos++;

    /* Scan value */
    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) p->pos++;
    size_t value_len = p->pos - value_start;

    /* Null-terminate in place */
    if (p->pos < p->end) {
        ((char*)value_start)[value_len] = '\0';
        p->pos++;  /* Skip closing quote */
    }

    /* Allocate attribute - ZERO checks, direct offset */
    uint32_t attr_off = ALLOC_ATTR_V3(p);
    struct compact_attribute_v3* attr = OFFSET_TO_TYPED(p->node_base, attr_off, struct compact_attribute_v3);

    /* Store offsets */
    attr->name_offset = (uint32_t)(name_start - p->base);
    attr->value_offset = (uint32_t)(value_start - p->base);
    attr->next_attr = 0;
    attr->reserved = 0;

    return attr_off;
}

/* ============================================================================
 * Main Parsing Loop - v3 Iterative
 * ============================================================================ */

static uint32_t parse_v3_main(ParserV3* p) {
    uint32_t root_off = 0;
    int got_root = 0;

    while (p->pos < p->end) {
        /* Skip whitespace */
        p->pos = skip_ws_v3(p->pos, p->end);
        if (p->pos >= p->end) break;

        /* Get current context */
        StackEntryV3* ctx = (p->stack_depth > 0) ? STACK_PEEK_V3(p) : NULL;

        /* Text content (not inside tag) */
        if (*p->pos != '<') {
            if (!ctx) { p->pos++; continue; }

            const char* text_start = p->pos;
            while (p->pos < p->end && *p->pos != '<') p->pos++;

            /* Skip whitespace-only text */
            if (is_ws_only_v3(text_start, p->pos)) continue;

            /* Null-terminate */
            size_t text_len = p->pos - text_start;
            if (p->pos < p->end) ((char*)text_start)[text_len] = '\0';

            /* Allocate text node */
            uint32_t text_off = ALLOC_TEXT_V3(p);
            struct compact_text_v3* text = OFFSET_TO_TYPED(p->node_base, text_off, struct compact_text_v3);

            text->text_offset = (uint32_t)(text_start - p->base);
            text->next_sibling = 0;
            text->text_length = (uint32_t)text_len;
            text->flags = COMPACT_V3_TYPE_TEXT;

            /* Link to parent - use ctx->elem_off which is still valid */
            struct compact_element_v3* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_off, struct compact_element_v3);

            if (ctx->last_child == 0) {
                parent->first_child = text_off;
            } else {
                /* next_sibling is at offset 4 in BOTH element and text */
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child + 4);
                *sibling_ptr = text_off;
            }
            ctx->last_child = text_off;

            continue;
        }

        /* We have '<' */
        p->pos++;
        char c = *p->pos;

        /* Closing tag */
        if (c == '/') {
            p->pos++;
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end && *p->pos == '>') p->pos++;

            STACK_POP_V3(p);
            continue;
        }

        /* Special nodes (PI, comments, CDATA) */
        if (c == '?' || c == '!') {
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end && *p->pos == '>') p->pos++;
            continue;
        }

        /* Parse element name */
        const char* name_start = p->pos;
        p->pos = scan_name_v3(p->pos, p->end);
        size_t name_len = p->pos - name_start;
        if (name_len == 0) continue;

        /* Null-terminate name */
        if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

        /* Allocate element - ZERO checks, direct offset */
        uint32_t elem_off = ALLOC_ELEM_V3(p);
        struct compact_element_v3* elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v3);

        /* Initialize - all fields */
        elem->first_child = 0;
        elem->next_sibling = 0;
        elem->parent = ctx ? ctx->elem_off : 0;
        elem->name_offset = (uint32_t)(name_start - p->base);
        elem->first_attr = 0;

        /* Link to parent or set as root */
        if (ctx) {
            if (ctx->last_child == 0) {
                /* Need fresh parent pointer after potential alloc */
                struct compact_element_v3* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_off, struct compact_element_v3);
                parent->first_child = elem_off;
            } else {
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child + 4);
                *sibling_ptr = elem_off;
            }
            ctx->last_child = elem_off;
        } else if (!got_root) {
            root_off = elem_off;
            got_root = 1;
        }

        /* Parse attributes - into SEPARATE chain */
        p->pos = skip_ws_v3(p->pos, p->end);
        uint32_t last_attr = 0;

        while (p->pos < p->end && *p->pos != '>' && *p->pos != '/') {
            if (IS_SPACE(*p->pos)) {
                p->pos = skip_ws_v3(p->pos, p->end);
                continue;
            }

            if (IS_NAME_START(*p->pos)) {
                uint32_t attr_off = parse_attr_v3(p);
                if (attr_off == 0) break;

                /* Link attribute to element's first_attr */
                if (last_attr == 0) {
                    /* Re-get elem pointer after potential alloc */
                    elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v3);
                    elem->first_attr = attr_off;
                } else {
                    struct compact_attribute_v3* la = OFFSET_TO_TYPED(p->node_base, last_attr, struct compact_attribute_v3);
                    la->next_attr = attr_off;
                }
                last_attr = attr_off;
                continue;
            }

            /* Unknown character - stop parsing attributes */
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

        /* Push to stack if not self-closing */
        if (!self_closing) {
            STACK_PUSH_V3(p, elem_off);
        }
    }

    return root_off;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Forward declaration */
void taurus_document_free(struct taurus_document* doc);

/**
 * Parse XML into v3 20-byte compact elements
 *
 * This is the MAXIMUM PERFORMANCE parser.
 * Target: 1.0x vs pugixml
 *
 * @param xml MUTABLE XML string (will be modified in-place)
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return taurus_document with 20-byte elements, or NULL on error
 */
struct taurus_document* taurus_parse_v3(char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Ensure null-terminated */
    xml[len] = '\0';

    /* Calculate allocation size - 2x input for safety */
    size_t alloc_size = ULTRA_FAST_SIZE_ESTIMATE(len);
    if (alloc_size < ULTRA_FAST_MIN_SIZE) alloc_size = ULTRA_FAST_MIN_SIZE;

    /* Create ultra-fast allocator */
    UltraFastAlloc* alloc = ultra_fast_alloc_create(alloc_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize parser */
    ParserV3 parser;
    parser.pos = xml;
    parser.end = xml + len;
    parser.base = xml;
    parser.node_base = alloc->base;
    parser.alloc = alloc;
    parser.stack_depth = 0;

    /* Skip prolog */
    parser.pos = skip_ws_v3(parser.pos, parser.end);

    /* Skip XML declaration */
    if (parser.pos < parser.end && *parser.pos == '<' &&
        parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
        if (parser.pos < parser.end && *parser.pos == '>') parser.pos++;
        parser.pos = skip_ws_v3(parser.pos, parser.end);
    }

    /* Parse */
    uint32_t root_off = parse_v3_main(&parser);

    if (root_off == 0) {
        ultra_fast_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Create document */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        ultra_fast_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
    doc->compact_alloc = NULL;  /* Using UltraFastAlloc, not CompactSingleAllocator */
    doc->ultra_fast_alloc = alloc;  /* Store ultra-fast allocator */
    doc->compact_base = alloc->base;
    doc->compact_root_offset = root_off;
    doc->is_compact = 1;
    doc->compact_v3 = 1;  /* Mark as v3 (20-byte elements) */
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;
    doc->pool = NULL;

    if (error_out) *error_out = 0;
    return doc;
}
