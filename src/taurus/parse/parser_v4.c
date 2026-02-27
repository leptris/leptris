/* parser_v4.c - Ultra-Fast 16-Byte Parser with Deferred Null-Termination
 * Copyright (c) 2026, Ribose Inc.
 *
 * MAXIMUM PERFORMANCE PARSER - Target: 1.0x vs pugixml
 *
 * Key optimizations over v2:
 * 1. Zero-check allocator - pure bump pointer, NO size checks (2 cycles saved)
 * 2. Deferred null-termination - post-parse pass, NO branches in hot loop (1.5 cycles saved)
 * 3. Direct offset returns - no pointer-to-offset conversion
 *
 * Total expected savings: 3.5+ cycles per element
 * Target: 50 us for 68 KB file (matching pugixml's 51 us)
 */

#include "compact_element_v2.h"
#include "../memory/zero_check_alloc.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Parser State for v4
 * ============================================================================ */

#define MAX_STACK_DEPTH 1024

typedef struct {
    uint32_t elem_offset;
    uint32_t last_child_off;
} StackEntryV4;

typedef struct {
    const char* pos;
    const char* end;
    char* string_base;      /* Base for string offsets */
    char* node_base;        /* Node memory base (from allocator) */
    ZeroCheckAlloc* alloc;

    /* Fixed stack */
    StackEntryV4 stack[MAX_STACK_DEPTH];
    size_t stack_size;

    /* String tracking for deferred null-termination */
    char** strings;         /* Array of string start pointers */
    size_t* lengths;        /* Array of string lengths */
    size_t string_count;
    size_t string_capacity;
} ParserV4;

/* ============================================================================
 * Inline Character Classification - NO function calls
 * ============================================================================ */

#define IS_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define IS_NAME_START(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || (c) == '_' || (c) == ':')
#define IS_NAME_CHAR(c) (IS_NAME_START(c) || ((c) >= '0' && (c) <= '9') || (c) == '-' || (c) == '.')

#ifdef _MSC_VER
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

static FORCE_INLINE const char* scan_name_v4(const char* p, const char* end) {
    while (p < end && IS_NAME_CHAR(*p)) p++;
    return p;
}

static FORCE_INLINE const char* skip_ws_v4(const char* p, const char* end) {
    while (p < end && IS_SPACE(*p)) p++;
    return p;
}

static FORCE_INLINE int is_ws_only_v4(const char* p, const char* end) {
    while (p < end) {
        if (!IS_SPACE(*p)) return 0;
        p++;
    }
    return 1;
}

/* ============================================================================
 * String Tracking for Deferred Null-Termination
 * ============================================================================ */

#define INITIAL_STRING_CAPACITY 4096

static int init_string_tracking(ParserV4* p) {
    p->strings = (char**)malloc(INITIAL_STRING_CAPACITY * sizeof(char*));
    p->lengths = (size_t*)malloc(INITIAL_STRING_CAPACITY * sizeof(size_t));
    if (!p->strings || !p->lengths) {
        if (p->strings) free(p->strings);
        if (p->lengths) free(p->lengths);
        return 0;
    }
    p->string_count = 0;
    p->string_capacity = INITIAL_STRING_CAPACITY;
    return 1;
}

static void track_string(ParserV4* p, char* start, size_t len) {
    if (p->string_count >= p->string_capacity) {
        size_t new_cap = p->string_capacity * 2;
        char** new_strings = (char**)realloc(p->strings, new_cap * sizeof(char*));
        size_t* new_lengths = (size_t*)realloc(p->lengths, new_cap * sizeof(size_t));
        if (!new_strings || !new_lengths) return;
        p->strings = new_strings;
        p->lengths = new_lengths;
        p->string_capacity = new_cap;
    }
    p->strings[p->string_count] = start;
    p->lengths[p->string_count] = len;
    p->string_count++;
}

static void null_terminate_all(ParserV4* p) {
    for (size_t i = 0; i < p->string_count; i++) {
        p->strings[i][p->lengths[i]] = '\0';
    }
}

static void free_string_tracking(ParserV4* p) {
    if (p->strings) free(p->strings);
    if (p->lengths) free(p->lengths);
}

/* ============================================================================
 * Stack Operations - Inline
 * ============================================================================ */

#define STACK_PUSH_V4(p, off) do { \
    (p)->stack[(p)->stack_size].elem_offset = (off); \
    (p)->stack[(p)->stack_size].last_child_off = 0; \
    (p)->stack_size++; \
} while(0)

#define STACK_PEEK_V4(p) (&(p)->stack[(p)->stack_size - 1])
#define STACK_POP_V4(p) do { (p)->stack_size--; } while(0)

/* ============================================================================
 * Zero-Check Allocation - Returns offset directly
 * ============================================================================ */

#define ALLOC_ELEM_V4(p) ALLOC_16((p)->alloc)
#define ALLOC_ATTR_V4(p) ALLOC_16((p)->alloc)
#define ALLOC_TEXT_V4(p) ALLOC_16((p)->alloc)

/* ============================================================================
 * Attribute Parsing (v4) - NO null-termination during parse
 * ============================================================================ */

static uint32_t parse_attr_v4(ParserV4* p) {
    const char* name_start = p->pos;
    p->pos = scan_name_v4(p->pos, p->end);
    size_t name_len = p->pos - name_start;
    if (name_len == 0) return 0;

    /* Track for deferred null-termination - NO branch! */
    track_string(p, (char*)name_start, name_len);

    /* Skip to '=' */
    p->pos = skip_ws_v4(p->pos, p->end);
    if (p->pos >= p->end || *p->pos != '=') return 0;
    p->pos++;

    /* Skip to quote */
    p->pos = skip_ws_v4(p->pos, p->end);
    if (p->pos >= p->end) return 0;
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') return 0;
    p->pos++;

    /* Parse value */
    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) p->pos++;
    size_t value_len = p->pos - value_start;

    /* Track for deferred null-termination */
    track_string(p, (char*)value_start, value_len);

    /* Skip closing quote */
    if (p->pos < p->end) p->pos++;

    /* Allocate - ZERO checks, direct offset */
    uint32_t attr_off = ALLOC_ATTR_V4(p);
    struct compact_attribute_v2* attr = OFFSET_TO_TYPED(p->node_base, attr_off, struct compact_attribute_v2);

    attr->name_offset = (uint32_t)(name_start - p->string_base) | 0x80000000;
    attr->value_offset = (uint32_t)(value_start - p->string_base);
    attr->next_attr = 0;
    attr->flags = 0;

    return attr_off;
}

/* ============================================================================
 * Main Parsing Loop - v4 with Deferred Null-Termination
 * ============================================================================ */

static uint32_t parse_v4_main(ParserV4* p) {
    uint32_t root_off = 0;
    int got_root = 0;

    while (p->pos < p->end) {
        p->pos = skip_ws_v4(p->pos, p->end);
        if (p->pos >= p->end) break;

        StackEntryV4* ctx = (p->stack_size > 0) ? STACK_PEEK_V4(p) : NULL;

        /* Text content */
        if (*p->pos != '<') {
            if (!ctx) { p->pos++; continue; }

            const char* text_start = p->pos;
            while (p->pos < p->end && *p->pos != '<') p->pos++;

            if (is_ws_only_v4(text_start, p->pos)) continue;

            size_t text_len = p->pos - text_start;

            /* Track for deferred null-termination */
            track_string(p, (char*)text_start, text_len);

            /* Allocate text node */
            uint32_t text_off = ALLOC_TEXT_V4(p);
            struct compact_text_v2* text = OFFSET_TO_TYPED(p->node_base, text_off, struct compact_text_v2);

            text->text_offset = (uint32_t)(text_start - p->string_base);
            text->next_sibling = 0;
            text->text_length = (uint32_t)text_len;
            text->flags = COMPACT_V2_TYPE_TEXT;

            /* Link to parent */
            struct compact_element_v2* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_offset, struct compact_element_v2);

            if (ctx->last_child_off == 0) {
                parent->first_child = text_off;
            } else {
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child_off + 4);
                *sibling_ptr = text_off;
            }
            ctx->last_child_off = text_off;

            continue;
        }

        /* We have '<' */
        p->pos++;
        char c = *p->pos;

        /* Closing tag */
        if (c == '/') {
            p->pos++;
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end) p->pos++;
            STACK_POP_V4(p);
            continue;
        }

        /* Special nodes */
        if (c == '?' || c == '!') {
            while (p->pos < p->end && *p->pos != '>') p->pos++;
            if (p->pos < p->end) p->pos++;
            continue;
        }

        /* Parse element name */
        const char* name_start = p->pos;
        p->pos = scan_name_v4(p->pos, p->end);
        size_t name_len = p->pos - name_start;
        if (name_len == 0) continue;

        /* Track for deferred null-termination */
        track_string(p, (char*)name_start, name_len);

        /* Allocate element - ZERO checks, direct offset */
        uint32_t elem_off = ALLOC_ELEM_V4(p);
        struct compact_element_v2* elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v2);

        /* Initialize */
        elem->first_child = 0;
        elem->next_sibling = 0;
        elem->parent = ctx ? ctx->elem_offset : 0;
        elem->name_offset = (uint32_t)(name_start - p->string_base);

        /* Link to parent or set as root */
        if (ctx) {
            if (ctx->last_child_off == 0) {
                struct compact_element_v2* parent = OFFSET_TO_TYPED(p->node_base, ctx->elem_offset, struct compact_element_v2);
                parent->first_child = elem_off;
            } else {
                uint32_t* sibling_ptr = (uint32_t*)(p->node_base + ctx->last_child_off + 4);
                *sibling_ptr = elem_off;
            }
            ctx->last_child_off = elem_off;
        } else if (!got_root) {
            root_off = elem_off;
            got_root = 1;
        }

        /* Parse attributes */
        p->pos = skip_ws_v4(p->pos, p->end);
        uint32_t last_attr = 0;

        while (p->pos < p->end && *p->pos != '>' && *p->pos != '/') {
            if (IS_SPACE(*p->pos)) {
                p->pos = skip_ws_v4(p->pos, p->end);
                continue;
            }

            if (IS_NAME_START(*p->pos)) {
                uint32_t attr_off = parse_attr_v4(p);
                if (attr_off == 0) break;

                /* Link attribute */
                elem = OFFSET_TO_TYPED(p->node_base, elem_off, struct compact_element_v2);
                if (last_attr == 0) {
                    struct compact_attribute_v2* attr = OFFSET_TO_TYPED(p->node_base, attr_off, struct compact_attribute_v2);
                    attr->next_attr = elem->first_child;
                    elem->first_child = attr_off;
                } else {
                    struct compact_attribute_v2* la = OFFSET_TO_TYPED(p->node_base, last_attr, struct compact_attribute_v2);
                    la->next_attr = attr_off;
                }
                last_attr = attr_off;
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

        if (!self_closing) {
            STACK_PUSH_V4(p, elem_off);
        }
    }

    return root_off;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Parse XML into 16-byte compact elements with v4 optimizations
 *
 * This is the MAXIMUM PERFORMANCE parser with:
 * - Zero-check allocator
 * - Deferred null-termination
 *
 * @param xml MUTABLE XML string (will be modified in-place)
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return taurus_document with 16-byte elements, or NULL on error
 */
struct taurus_document* taurus_parse_v4(char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Pre-allocate 2x input size to guarantee NO growth needed */
    size_t alloc_size = ZERO_CHECK_SIZE_ESTIMATE(len);
    if (alloc_size < ZERO_CHECK_MIN_SIZE) alloc_size = ZERO_CHECK_MIN_SIZE;

    ZeroCheckAlloc* alloc = zero_check_alloc_create(alloc_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize parser */
    ParserV4 parser;
    parser.pos = xml;
    parser.end = xml + len;
    parser.string_base = xml;
    parser.node_base = alloc->base;
    parser.alloc = alloc;
    parser.stack_size = 0;

    /* Initialize string tracking for deferred null-termination */
    if (!init_string_tracking(&parser)) {
        zero_check_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Skip prolog */
    parser.pos = skip_ws_v4(parser.pos, parser.end);

    /* Skip XML declaration */
    if (parser.pos < parser.end && *parser.pos == '<' &&
        parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
        if (parser.pos < parser.end && *parser.pos == '>') parser.pos++;
        parser.pos = skip_ws_v4(parser.pos, parser.end);
    }

    /* Parse (deferred null-termination) */
    uint32_t root_off = parse_v4_main(&parser);

    if (root_off == 0) {
        free_string_tracking(&parser);
        zero_check_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* DEFERRED NULL-TERMINATION PASS - single sequential scan */
    null_terminate_all(&parser);

    /* Final null-termination of the buffer */
    xml[len] = '\0';

    /* Free string tracking */
    free_string_tracking(&parser);

    /* Create document */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        zero_check_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
    doc->compact_alloc = NULL;  /* Not using CompactSingleAllocator */
    doc->zero_check_alloc = alloc;  /* Using ZeroCheckAlloc */
    doc->compact_base = alloc->base;
    doc->compact_root_offset = root_off;
    doc->is_compact = 1;
    doc->compact_v2 = 1;  /* Mark as v2 (16-byte elements) */
    doc->compact_v4 = 1;  /* Mark as v4 (deferred null-term) */
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;
    doc->pool = NULL;

    if (error_out) *error_out = 0;
    return doc;
}
