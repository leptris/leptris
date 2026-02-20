/* parser_two_pass.c - Two-Pass Parser for Compact DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * Two-pass parsing implementation:
 * Pass 1: Estimate document size (using parser_size.c)
 * Pass 2: Allocate single block and parse into it
 *
 * This is the KEY optimization for achieving 1.0x vs pugixml.
 */

#include "parser_size.h"
#include "parser.h"
#include "../memory/compact_single_alloc.h"
#include "../dom/compact_element.h"
#include "../taurus_internal.h"
#include "../simd_helpers.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Two-Pass Parser State
 * ============================================================================ */

typedef struct {
    const char* input;
    const char* pos;
    const char* end;
    CompactSingleAllocator* alloc;
    char* base;
    int has_error;
    char error_msg[256];
} TwoPassParser;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static inline void skip_ws(TwoPassParser* p) {
    p->pos = simd_skip_whitespace(p->pos, p->end);
}

static inline char peek(TwoPassParser* p) {
    return (p->pos < p->end) ? *p->pos : '\0';
}

static inline char advance(TwoPassParser* p) {
    return (p->pos < p->end) ? *p->pos++ : '\0';
}

/* ============================================================================
 * Two-Pass Parsing - Pass 2: Build Compact DOM
 * ============================================================================ */

/* Forward declarations */
static uint32_t parse_compact_elem(TwoPassParser* p, uint32_t parent_offset);
static uint32_t parse_compact_attr(TwoPassParser* p);
static uint32_t parse_compact_text(TwoPassParser* p, uint32_t parent_offset);
static uint32_t parse_compact_cdata(TwoPassParser* p, uint32_t parent_offset);
static uint32_t parse_compact_comment(TwoPassParser* p, uint32_t parent_offset);
static uint32_t parse_compact_pi(TwoPassParser* p, uint32_t parent_offset);

/**
 * Parse a name and store it in compact block
 * Returns offset to stored name (0 on failure)
 */
static uint32_t parse_and_store_name(TwoPassParser* p) {
    const char* start = p->pos;

    /* Use SIMD to find end of name */
    p->pos = simd_scan_name(p->pos, p->end);

    size_t len = p->pos - start;
    if (len == 0) return 0;

    return compact_single_alloc_string(p->alloc, start, len);
}

/**
 * Parse attribute value and store it
 * Returns offset to stored value (0 on failure)
 */
static uint32_t parse_and_store_value(TwoPassParser* p) {
    if (peek(p) != '"' && peek(p) != '\'') return 0;

    char quote = advance(p);
    const char* start = p->pos;

    /* Find closing quote using SIMD */
    const char* end = simd_find_quote_end(p->pos, p->end, quote);

    size_t len = end - start;
    p->pos = end + 1;  /* Skip closing quote */

    return compact_single_alloc_string(p->alloc, start, len);
}

/**
 * Parse a single attribute
 * Returns offset to compact_attribute (0 on failure)
 */
static uint32_t parse_compact_attr(TwoPassParser* p) {
    /* Parse name */
    uint32_t name_off = parse_and_store_name(p);
    if (name_off == 0) return 0;

    skip_ws(p);

    /* Expect '=' */
    if (peek(p) != '=') return 0;
    advance(p);
    skip_ws(p);

    /* Parse value */
    uint32_t value_off = parse_and_store_value(p);
    if (value_off == 0) return 0;

    /* Allocate compact attribute */
    struct compact_attribute* attr = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_attribute);
    if (!attr) return 0;

    attr->name_offset = name_off;
    attr->value_offset = value_off;
    attr->namespace_offset = 0;
    attr->next_attr = 0;
    attr->flags = 0;

    return compact_single_get_offset(p->alloc, attr);
}

/**
 * Parse text content and create compact text node
 * Returns offset to compact_text_node (0 on failure or whitespace-only)
 */
static uint32_t parse_compact_text(TwoPassParser* p, uint32_t parent_offset) {
    const char* start = p->pos;

    /* Find end of text (next '<') */
    while (p->pos < p->end && peek(p) != '<') {
        advance(p);
    }

    size_t len = p->pos - start;
    if (len == 0) return 0;

    /* Check if text is whitespace-only (skip whitespace-only text nodes) */
    int is_whitespace = 1;
    for (size_t i = 0; i < len; i++) {
        if (start[i] != ' ' && start[i] != '\t' && start[i] != '\n' && start[i] != '\r') {
            is_whitespace = 0;
            break;
        }
    }
    if (is_whitespace) return 0;

    /* Store text content */
    uint32_t text_off = compact_single_alloc_string(p->alloc, start, len);
    if (text_off == 0) return 0;

    /* Allocate compact text node */
    struct compact_text_node* text = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_text_node);
    if (!text) return 0;

    text->text_offset = text_off;
    text->text_length = (uint32_t)len;
    text->next_sibling = 0;
    text->flags = COMPACT_NODE_TYPE_TEXT;

    return compact_single_get_offset(p->alloc, text);
}

/**
 * Parse CDATA section <![CDATA[...]]>
 * Returns offset to compact_text_node (0 on failure)
 */
static uint32_t parse_compact_cdata(TwoPassParser* p, uint32_t parent_offset) {
    /* Check for <![CDATA[ */
    if (p->pos + 8 >= p->end) return 0;
    if (strncmp(p->pos, "<![CDATA[", 9) != 0) return 0;

    p->pos += 9;  /* Skip <![CDATA[ */
    const char* start = p->pos;

    /* Find closing ]]> */
    while (p->pos + 2 < p->end) {
        if (p->pos[0] == ']' && p->pos[1] == ']' && p->pos[2] == '>') {
            break;
        }
        advance(p);
    }

    size_t len = p->pos - start;
    if (p->pos + 2 < p->end) {
        p->pos += 3;  /* Skip ]]> */
    }

    if (len == 0) return 0;

    /* Store CDATA content */
    uint32_t text_off = compact_single_alloc_string(p->alloc, start, len);
    if (text_off == 0) return 0;

    /* Allocate compact text node */
    struct compact_text_node* text = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_text_node);
    if (!text) return 0;

    text->text_offset = text_off;
    text->text_length = (uint32_t)len;
    text->next_sibling = 0;
    text->flags = COMPACT_NODE_TYPE_CDATA;

    return compact_single_get_offset(p->alloc, text);
}

/**
 * Parse comment <!--...-->
 * Returns offset to compact_text_node (0 on failure)
 */
static uint32_t parse_compact_comment(TwoPassParser* p, uint32_t parent_offset) {
    /* Check for <!-- */
    if (p->pos + 3 >= p->end) return 0;
    if (strncmp(p->pos, "<!--", 4) != 0) return 0;

    p->pos += 4;  /* Skip <!-- */
    const char* start = p->pos;

    /* Find closing --> */
    while (p->pos + 2 < p->end) {
        if (p->pos[0] == '-' && p->pos[1] == '-' && p->pos[2] == '>') {
            break;
        }
        advance(p);
    }

    size_t len = p->pos - start;
    if (p->pos + 2 < p->end) {
        p->pos += 3;  /* Skip --> */
    }

    /* Store comment content (even if empty) */
    uint32_t text_off = 0;
    if (len > 0) {
        text_off = compact_single_alloc_string(p->alloc, start, len);
    }

    /* Allocate compact text node */
    struct compact_text_node* text = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_text_node);
    if (!text) return 0;

    text->text_offset = text_off;
    text->text_length = (uint32_t)len;
    text->next_sibling = 0;
    text->flags = COMPACT_NODE_TYPE_COMMENT;

    return compact_single_get_offset(p->alloc, text);
}

/**
 * Parse processing instruction <?...?>
 * Returns offset to compact_text_node (0 on failure)
 */
static uint32_t parse_compact_pi(TwoPassParser* p, uint32_t parent_offset) {
    /* Check for <? */
    if (p->pos + 1 >= p->end) return 0;
    if (peek(p) != '?' || *(p->pos + 1) == '?') return 0;

    p->pos += 2;  /* Skip <? */

    /* Parse PI target (name) */
    const char* target_start = p->pos;
    p->pos = simd_scan_name(p->pos, p->end);
    size_t target_len = p->pos - target_start;

    if (target_len == 0) return 0;

    /* Store target */
    uint32_t target_off = compact_single_alloc_string(p->alloc, target_start, target_len);
    if (target_off == 0) return 0;

    /* Skip whitespace before data */
    skip_ws(p);

    /* Find closing ?> */
    const char* data_start = p->pos;
    while (p->pos + 1 < p->end) {
        if (p->pos[0] == '?' && p->pos[1] == '>') {
            break;
        }
        advance(p);
    }

    size_t data_len = p->pos - data_start;
    if (p->pos + 1 < p->end) {
        p->pos += 2;  /* Skip ?> */
    }

    /* Store data */
    uint32_t data_off = 0;
    if (data_len > 0) {
        data_off = compact_single_alloc_string(p->alloc, data_start, data_len);
    }

    /* Allocate compact text node (using text_offset for target, namespace_offset for data) */
    struct compact_text_node* text = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_text_node);
    if (!text) return 0;

    text->text_offset = target_off;
    text->text_length = (uint32_t)target_len;
    text->next_sibling = 0;
    text->flags = COMPACT_NODE_TYPE_PI | (data_off << 16);  /* Store data offset in upper bits */

    return compact_single_get_offset(p->alloc, text);
}

/**
 * Add a child node (element or text) to parent
 * Links siblings properly
 */
static void add_child_node(TwoPassParser* p, uint32_t parent_offset,
                           uint32_t child_offset, uint32_t* last_child_off,
                           int is_element) {
    if (child_offset == 0) return;

    struct compact_element* parent = COMPACT_SINGLE_GET_TYPED(p->alloc, struct compact_element, parent_offset);
    if (!parent) return;

    if (parent->first_child == 0) {
        parent->first_child = child_offset;
    } else if (*last_child_off != 0) {
        /* Link sibling - need to handle both element and text node types */
        void* last_child = COMPACT_SINGLE_GET_TYPED(p->alloc, void, *last_child_off);
        if (last_child) {
            /* Both compact_element and compact_text_node have next_sibling at offset 4 */
            uint32_t* sibling_ptr = (uint32_t*)((char*)last_child + 4);
            *sibling_ptr = child_offset;
        }
    }
    *last_child_off = child_offset;
    if (is_element) {
        parent->child_count++;
    }
}

/**
 * Parse an element and its children
 * Returns offset to compact_element (0 on failure)
 */
static uint32_t parse_compact_elem(TwoPassParser* p, uint32_t parent_offset) {
    if (peek(p) != '<') return 0;
    advance(p);  /* Skip '<' */

    /* Check for closing tag */
    if (peek(p) == '/') return 0;

    /* Check for CDATA section */
    if (p->pos + 8 < p->end && strncmp(p->pos, "![CDATA[", 8) == 0) {
        p->pos--;  /* Back up to include '<' */
        return parse_compact_cdata(p, parent_offset);
    }

    /* Check for comment */
    if (p->pos + 2 < p->end && strncmp(p->pos, "!--", 3) == 0) {
        p->pos--;  /* Back up to include '<' */
        return parse_compact_comment(p, parent_offset);
    }

    /* Check for processing instruction */
    if (peek(p) == '?') {
        p->pos--;  /* Back up to include '<' */
        return parse_compact_pi(p, parent_offset);
    }

    /* Check for DOCTYPE - skip for now */
    if (peek(p) == '!') {
        while (p->pos < p->end && peek(p) != '>') advance(p);
        if (peek(p) == '>') advance(p);
        return 0;  /* Not an element */
    }

    /* Parse element name */
    uint32_t name_off = parse_and_store_name(p);
    if (name_off == 0) return 0;

    /* Allocate compact element */
    struct compact_element* elem = COMPACT_SINGLE_ALLOC(p->alloc, struct compact_element);
    if (!elem) return 0;

    uint32_t elem_off = compact_single_get_offset(p->alloc, elem);

    /* Initialize element */
    elem->first_child = 0;
    elem->next_sibling = 0;
    elem->parent = parent_offset;
    elem->name_offset = name_off;
    elem->namespace_offset = 0;
    elem->first_attr = 0;
    elem->attr_count = 0;
    elem->child_count = 0;
    elem->flags = COMPACT_NODE_TYPE_ELEMENT;

    /* Parse attributes */
    skip_ws(p);
    uint32_t last_attr_off = 0;

    while (peek(p) != '>' && peek(p) != '/' && p->pos < p->end) {
        if (peek(p) == ' ' || peek(p) == '\t' || peek(p) == '\n' || peek(p) == '\r') {
            skip_ws(p);
            continue;
        }

        uint32_t attr_off = parse_compact_attr(p);
        if (attr_off == 0) break;

        if (elem->first_attr == 0) {
            elem->first_attr = attr_off;
        } else {
            /* Link to previous attribute */
            struct compact_attribute* last_attr = COMPACT_SINGLE_GET_TYPED(p->alloc, struct compact_attribute, last_attr_off);
            if (last_attr) last_attr->next_attr = attr_off;
        }
        last_attr_off = attr_off;
        elem->attr_count++;
    }

    /* Check for self-closing */
    int self_closing = 0;
    if (peek(p) == '/') {
        advance(p);
        self_closing = 1;
    }

    if (peek(p) == '>') advance(p);

    if (self_closing) {
        return elem_off;  /* Done with self-closing element */
    }

    /* Parse children */
    uint32_t last_child_off = 0;

    while (p->pos < p->end) {
        skip_ws(p);

        if (peek(p) == '<') {
            /* Check for closing tag */
            if (p->pos + 1 < p->end && *(p->pos + 1) == '/') {
                /* Skip closing tag */
                p->pos += 2;  /* Skip '</' */
                while (p->pos < p->end && peek(p) != '>') advance(p);
                if (peek(p) == '>') advance(p);
                break;  /* Done with this element */
            }

            /* Check for CDATA */
            if (p->pos + 8 < p->end && strncmp(p->pos, "<![CDATA[", 9) == 0) {
                uint32_t cdata_off = parse_compact_cdata(p, elem_off);
                if (cdata_off) {
                    add_child_node(p, elem_off, cdata_off, &last_child_off, 0);
                }
                continue;
            }

            /* Check for comment */
            if (p->pos + 3 < p->end && strncmp(p->pos, "<!--", 4) == 0) {
                uint32_t comment_off = parse_compact_comment(p, elem_off);
                if (comment_off) {
                    add_child_node(p, elem_off, comment_off, &last_child_off, 0);
                }
                continue;
            }

            /* Check for PI */
            if (p->pos + 1 < p->end && *(p->pos + 1) == '?') {
                uint32_t pi_off = parse_compact_pi(p, elem_off);
                if (pi_off) {
                    add_child_node(p, elem_off, pi_off, &last_child_off, 0);
                }
                continue;
            }

            /* Parse child element */
            uint32_t child_off = parse_compact_elem(p, elem_off);
            if (child_off) {
                add_child_node(p, elem_off, child_off, &last_child_off, 1);
            }
        } else {
            /* Text content */
            uint32_t text_off = parse_compact_text(p, elem_off);
            if (text_off) {
                add_child_node(p, elem_off, text_off, &last_child_off, 0);
            }
        }
    }

    return elem_off;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Skip XML declaration <?xml ...?>
 * Returns 1 if skipped, 0 if not found
 */
static int skip_xml_declaration(TwoPassParser* p) {
    if (p->pos + 4 >= p->end) return 0;
    if (strncmp(p->pos, "<?xml", 5) != 0) return 0;

    /* Skip <?xml */
    p->pos += 5;

    /* Find closing ?> */
    while (p->pos + 1 < p->end) {
        if (p->pos[0] == '?' && p->pos[1] == '>') {
            p->pos += 2;
            skip_ws(p);
            return 1;
        }
        p->pos++;
    }
    return 0;
}

/**
 * Skip document-level processing instruction
 * Returns 1 if skipped, 0 if not found
 */
static int skip_document_pi(TwoPassParser* p) {
    if (p->pos + 1 >= p->end) return 0;
    if (p->pos[0] != '<' || p->pos[1] != '?') return 0;

    /* Skip <? */
    p->pos += 2;

    /* Find closing ?> */
    while (p->pos + 1 < p->end) {
        if (p->pos[0] == '?' && p->pos[1] == '>') {
            p->pos += 2;
            skip_ws(p);
            return 1;
        }
        p->pos++;
    }
    return 0;
}

/**
 * Skip DOCTYPE declaration
 * Returns 1 if skipped, 0 if not found
 */
static int skip_doctype(TwoPassParser* p) {
    if (p->pos + 8 >= p->end) return 0;
    if (strncmp(p->pos, "<!DOCTYPE", 9) != 0) return 0;

    /* Skip <!DOCTYPE - find matching > */
    p->pos += 9;
    int depth = 1;
    while (p->pos < p->end && depth > 0) {
        if (*p->pos == '[') depth++;
        else if (*p->pos == ']') depth--;
        else if (*p->pos == '>') depth--;
        p->pos++;
    }
    skip_ws(p);
    return 1;
}

/**
 * Parse XML using two-pass compact allocation
 *
 * @param xml XML string
 * @param len Length of XML string
 * @param error_out Error output (0=success, 1=error)
 * @return TaurusDocument or NULL on error
 */
struct taurus_document* taurus_parse_two_pass(const char* xml, size_t len, int* error_out) {
    if (!xml || len == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* PASS 1: Estimate size */
    DocumentSizeInfo size_info = parser_estimate_size(xml, len);
    size_t total_size = calculate_compact_size(&size_info);

    if (total_size == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Create single-block allocator */
    CompactSingleAllocator* alloc = compact_single_alloc_create(total_size);
    if (!alloc) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize string interning hash table */
    size_t hash_buckets = 64;
    while (hash_buckets < size_info.total_string_bytes / 8 && hash_buckets < 4096) {
        hash_buckets *= 2;
    }
    if (compact_single_init_string_hash(alloc, hash_buckets) != 0) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* PASS 2: Parse into compact block */
    TwoPassParser parser;
    parser.input = xml;
    parser.pos = xml;
    parser.end = xml + len;
    parser.alloc = alloc;
    parser.base = alloc->base;
    parser.has_error = 0;
    parser.error_msg[0] = '\0';

    skip_ws(&parser);

    /* Skip XML declaration if present */
    skip_xml_declaration(&parser);

    /* Skip document-level PIs */
    while (skip_document_pi(&parser)) {
        /* Keep skipping PIs */
    }

    /* Skip DOCTYPE if present */
    skip_doctype(&parser);

    /* Now parse the root element */
    uint32_t root_off = parse_compact_elem(&parser, 0);

    if (root_off == 0 || compact_single_has_error(alloc)) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Create TaurusDocument wrapper */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    memset(doc, 0, sizeof(struct taurus_document));
    doc->compact_alloc = alloc;  /* Store allocator for later freeing */
    doc->compact_base = alloc->base;  /* CRITICAL: Set base pointer for offset resolution */
    doc->compact_root_offset = root_off;
    doc->is_compact = 1;  /* Mark as compact mode */

    /* Create minimal pool for on-demand wrappers only
     * The wrapper will be created by taurus_document_root() when first accessed.
     * This saves memory by not creating wrappers for all elements upfront. */
    doc->pool = taurus_pool_create();
    if (!doc->pool) {
        free(doc);
        compact_single_alloc_destroy(alloc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* NOTE: We do NOT create the wrapper here anymore.
     * The wrapper is created on-demand by taurus_document_root().
     * This is the key optimization for achieving 1.0x vs pugixml. */

    if (error_out) *error_out = 0;
    return doc;
}
