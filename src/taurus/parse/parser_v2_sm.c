/* parser_v2_state_machine.c - 16-BYTE STATE MACHINE PARSER
 * Copyright (c) 2026, Ribose Inc.
 *
 * STATE MACHINE PARSER using computed gotos (GCC/Clash extension)
 *
 * This matches pugixml's approach:
 * 1. Computed gotos eliminate branch misprediction
 * 2. State machine avoids function calls
 * 3. Direct pointer manipulation
 *
 * Target: 1.0x vs pugixml
 */

#include "compact_element_v2.h"
#include "compact_single_alloc.h"
#include "../simd_helpers.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Parser State
 * ============================================================================ */

#define MAX_STACK 1024

typedef struct {
    const char* pos;
    const char* end;
    char* base;        /* String base (mutable) */
    char* node_base;   /* Current node base (may change after realloc) */
    CompactSingleAllocator* alloc;

    /* Stack */
    uint32_t stack[MAX_STACK];
    uint32_t last_child[MAX_STACK];
    int depth;
} ParserSM;

/* ============================================================================
 * Inline Helpers
 * ============================================================================ */

#define IS_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r')
#define IS_NAME(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || \
                     ((c) >= '0' && (c) <= '9') || (c) == '_' || (c) == ':' || \
                     (c) == '-' || (c) == '.')

/* Ultra-fast allocation - NO alignment, NO checks */
#define FAST_ALLOC(p, size) ({ \
    size_t _off = (p)->alloc->offset; \
    (p)->alloc->offset += (size); \
    (void*)((p)->node_base + _off); \
})

#define ALLOC_ELEM(p) ((struct compact_element_v2*)FAST_ALLOC(p, 16))
#define ALLOC_ATTR(p) ((struct compact_attribute_v2*)FAST_ALLOC(p, 16))
#define ALLOC_TEXT(p) ((struct compact_text_v2*)FAST_ALLOC(p, 16))

/* ============================================================================
 * State Machine Parser - Computed Gotos
 * ============================================================================ */

static uint32_t parse_sm(ParserSM* p) {
    /* State labels for computed gotos */
    enum {
        S_SKIP_WS,
        S_TAG_OPEN,
        S_TAG_CLOSE,
        S_TAG_PI,
        S_TAG_COMMENT,
        S_ELEMENT_NAME,
        S_ELEMENT_ATTRS,
        S_ATTR_NAME,
        S_ATTR_VALUE,
        S_TEXT,
        S_DONE
    };

    /* Dispatch table */
    static const void* dispatch[] = {
        [S_SKIP_WS]     = &&state_skip_ws,
        [S_TAG_OPEN]    = &&state_tag_open,
        [S_TAG_CLOSE]   = &&state_tag_close,
        [S_TAG_PI]      = &&state_tag_pi,
        [S_TAG_COMMENT] = &&state_tag_comment,
        [S_ELEMENT_NAME]= &&state_element_name,
        [S_ELEMENT_ATTRS]=&&state_element_attrs,
        [S_ATTR_NAME]   = &&state_attr_name,
        [S_ATTR_VALUE]  = &&state_attr_value,
        [S_TEXT]        = &&state_text,
        [S_DONE]        = &&state_done
    };

    int state = S_SKIP_WS;
    uint32_t root_off = 0;
    int got_root = 0;

    /* Current element context */
    uint32_t elem_off = 0;
    uint32_t last_child_off = 0;

    /* Attribute parsing context */
    const char* attr_name_start = NULL;
    size_t attr_name_len = 0;

    /* Text context */
    const char* text_start = NULL;

    goto *dispatch[state];

state_skip_ws:
    while (p->pos < p->end && IS_SPACE(*p->pos)) p->pos++;
    if (p->pos >= p->end) goto state_done;

    if (*p->pos == '<') {
        p->pos++;
        goto state_tag_open;
    }

    /* Text content */
    text_start = p->pos;
    goto state_text;

state_tag_open:
    if (p->pos >= p->end) goto state_done;

    char c = *p->pos;

    if (c == '/') {
        p->pos++;
        goto state_tag_close;
    }

    if (c == '?') {
        p->pos++;
        goto state_tag_pi;
    }

    if (c == '!') {
        p->pos++;
        goto state_tag_comment;
    }

    /* Element name */
    goto state_element_name;

state_tag_close:
    /* Skip until '>' */
    while (p->pos < p->end && *p->pos != '>') p->pos++;
    if (p->pos < p->end) p->pos++;

    /* Pop stack */
    if (p->depth > 0) {
        p->depth--;
    }

    goto state_skip_ws;

state_tag_pi:
    /* Skip until "?>" */
    while (p->pos < p->end) {
        if (*p->pos == '?' && p->pos + 1 < p->end && *(p->pos + 1) == '>') {
            p->pos += 2;
            break;
        }
        p->pos++;
    }
    goto state_skip_ws;

state_tag_comment:
    /* Skip until ">" (simplified - doesn't handle "-->" correctly) */
    while (p->pos < p->end && *p->pos != '>') p->pos++;
    if (p->pos < p->end) p->pos++;
    goto state_skip_ws;

state_element_name:
    if (p->pos >= p->end) goto state_done;

    const char* name_start = p->pos;
    while (p->pos < p->end && IS_NAME(*p->pos)) p->pos++;
    size_t name_len = p->pos - name_start;
    if (name_len == 0) goto state_skip_ws;

    /* Null-terminate in place */
    if (p->pos < p->end) ((char*)name_start)[name_len] = '\0';

    /* Allocate element */
    struct compact_element_v2* elem = ALLOC_ELEM(p);
    p->node_base = p->alloc->base;  /* Update after potential realloc */
    elem_off = (uint32_t)((char*)elem - p->node_base);

    /* Initialize */
    elem->first_child = 0;
    elem->next_sibling = 0;
    elem->parent = (p->depth > 0) ? p->stack[p->depth - 1] : 0;
    elem->name_offset = (uint32_t)(name_start - p->base);

    /* Link to parent */
    if (p->depth > 0) {
        if (p->last_child[p->depth - 1] == 0) {
            struct compact_element_v2* parent = (struct compact_element_v2*)
                (p->node_base + p->stack[p->depth - 1]);
            parent->first_child = elem_off;
        } else {
            uint32_t* sibling = (uint32_t*)(p->node_base + p->last_child[p->depth - 1] + 4);
            *sibling = elem_off;
        }
        p->last_child[p->depth - 1] = elem_off;
    } else if (!got_root) {
        root_off = elem_off;
        got_root = 1;
    }

    last_child_off = 0;
    goto state_element_attrs;

state_element_attrs:
    /* Skip whitespace */
    while (p->pos < p->end && IS_SPACE(*p->pos)) p->pos++;

    if (p->pos >= p->end) goto state_done;

    c = *p->pos;

    if (c == '>') {
        p->pos++;

        /* Push to stack */
        if (p->depth < MAX_STACK) {
            p->stack[p->depth] = elem_off;
            p->last_child[p->depth] = 0;
            p->depth++;
        }

        goto state_skip_ws;
    }

    if (c == '/') {
        p->pos++;
        if (p->pos < p->end && *p->pos == '>') p->pos++;
        goto state_skip_ws;
    }

    /* Attribute name */
    if (IS_NAME(c)) {
        attr_name_start = p->pos;
        goto state_attr_name;
    }

    /* Unknown - skip */
    p->pos++;
    goto state_element_attrs;

state_attr_name:
    while (p->pos < p->end && IS_NAME(*p->pos)) p->pos++;
    attr_name_len = p->pos - attr_name_start;
    if (attr_name_len == 0) goto state_element_attrs;

    /* Null-terminate */
    if (p->pos < p->end) ((char*)attr_name_start)[attr_name_len] = '\0';

    /* Skip whitespace */
    while (p->pos < p->end && IS_SPACE(*p->pos)) p->pos++;

    /* Expect '=' */
    if (p->pos >= p->end || *p->pos != '=') goto state_element_attrs;
    p->pos++;

    /* Skip whitespace */
    while (p->pos < p->end && IS_SPACE(*p->pos)) p->pos++;

    /* Expect quote */
    if (p->pos >= p->end) goto state_element_attrs;
    char quote = *p->pos;
    if (quote != '"' && quote != '\'') goto state_element_attrs;
    p->pos++;

    goto state_attr_value;

state_attr_value:
    const char* value_start = p->pos;
    while (p->pos < p->end && *p->pos != quote) p->pos++;
    size_t value_len = p->pos - value_start;

    /* Null-terminate */
    if (p->pos < p->end) {
        ((char*)value_start)[value_len] = '\0';
        p->pos++;  /* Skip closing quote */
    }

    /* Allocate attribute */
    struct compact_attribute_v2* attr = ALLOC_ATTR(p);
    p->node_base = p->alloc->base;
    uint32_t attr_off = (uint32_t)((char*)attr - p->node_base);

    attr->name_offset = (uint32_t)(attr_name_start - p->base) | 0x80000000;
    attr->value_offset = (uint32_t)(value_start - p->base);
    attr->next_attr = 0;
    attr->flags = 0;

    /* Link to element */
    struct compact_element_v2* e = (struct compact_element_v2*)(p->node_base + elem_off);
    attr->next_attr = e->first_child;
    e->first_child = attr_off;

    goto state_element_attrs;

state_text:
    while (p->pos < p->end && *p->pos != '<') p->pos++;
    size_t text_len = p->pos - text_start;

    /* Check if whitespace-only */
    int ws_only = 1;
    for (size_t i = 0; i < text_len; i++) {
        if (!IS_SPACE(text_start[i])) {
            ws_only = 0;
            break;
        }
    }

    if (!ws_only && p->depth > 0) {
        /* Null-terminate */
        if (p->pos < p->end) ((char*)text_start)[text_len] = '\0';

        /* Allocate text node */
        struct compact_text_v2* text = ALLOC_TEXT(p);
        p->node_base = p->alloc->base;
        uint32_t text_off = (uint32_t)((char*)text - p->node_base);

        text->text_offset = (uint32_t)(text_start - p->base);
        text->next_sibling = 0;
        text->text_length = (uint32_t)text_len;
        text->flags = COMPACT_V2_TYPE_TEXT;

        /* Link to parent */
        struct compact_element_v2* parent = (struct compact_element_v2*)
            (p->node_base + p->stack[p->depth - 1]);

        if (p->last_child[p->depth - 1] == 0) {
            parent->first_child = text_off;
        } else {
            uint32_t* sibling = (uint32_t*)(p->node_base + p->last_child[p->depth - 1] + 4);
            *sibling = text_off;
        }
        p->last_child[p->depth - 1] = text_off;
    }

    if (p->pos >= p->end) goto state_done;

    /* We have '<' */
    p->pos++;
    goto state_tag_open;

state_done:
    return root_off;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

struct taurus_document* taurus_parse_v2_sm(char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    xml[len] = '\0';

    /* Pre-allocate 2x input size */
    size_t alloc_size = len * 2 + 65536;
    if (alloc_size < 131072) alloc_size = 131072;

    CompactSingleAllocator* alloc = compact_single_alloc_create(alloc_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    ParserSM parser;
    parser.pos = xml;
    parser.end = xml + len;
    parser.base = xml;
    parser.node_base = alloc->base;
    parser.alloc = alloc;
    parser.depth = 0;

    /* Skip prolog */
    while (parser.pos < parser.end && IS_SPACE(*parser.pos)) parser.pos++;

    /* Skip XML declaration */
    if (parser.pos < parser.end && *parser.pos == '<' &&
        parser.pos + 1 < parser.end && parser.pos[1] == '?') {
        while (parser.pos < parser.end && *parser.pos != '>') parser.pos++;
        if (parser.pos < parser.end && *parser.pos == '>') parser.pos++;
        while (parser.pos < parser.end && IS_SPACE(*parser.pos)) parser.pos++;
    }

    uint32_t root_off = parse_sm(&parser);

    if (root_off == 0 || alloc->error) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

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
    doc->compact_v2 = 1;
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;
    doc->pool = NULL;

    if (error_out) *error_out = 0;
    return doc;
}
