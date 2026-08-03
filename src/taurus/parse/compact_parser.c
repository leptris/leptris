/* lib/src/parse/compact_parser.c - Compact Mode Parser Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * COMPACT MODE PARSER: High-performance XML parser that stores DOM in
 * contiguous memory blocks for maximum speed.
 *
 * This is a MINIMAL implementation focused purely on parsing performance.
 * After parsing, the compact DOM is converted to regular Taurus format.
 */

#include "compact_allocator.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../dom/element.h"
#include "../memory/pool.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================================================================
 * Compact DOM Structure (during parsing)
 * ============================================================================ */

typedef struct compact_document {
    CompactAllocator* allocator;
    uint32_t root_offset;      /* Offset to root element (0 if none) */
    uint32_t first_text_offset; /* Offset to first text node */
    int has_error;
    char error_msg[256];
} CompactDocument;

/* ============================================================================
 * Compact Parser State
 * ============================================================================ */

typedef struct {
    const char* input;
    const char* pos;
    const char* end;
    CompactDocument* doc;
    CompactAllocator* alloc;
    int line;
    int column;
    int has_error;
    char error_msg[256];

    /* Temporary storage for attribute offsets during parsing */
    struct {
        uint32_t name_offset;
        uint32_t value_offset;
    } temp_attrs[16];  /* Up to 16 attributes per element */
    int attr_count;
} CompactParser;

/* Forward declarations */
static uint32_t parse_compact_element(CompactParser* p, uint32_t parent_offset);
static uint32_t parse_compact_text(CompactParser* p);
static TaurusElement convert_compact_recursive(CompactDocument* cdoc, uint32_t elem_offset,
                                               TaurusMemoryPool* pool, const char* xml_data);

/* ============================================================================
 * Compact Parser API
 * ============================================================================ */

/* Create compact parser */
static CompactParser* compact_parser_create(const char* xml, size_t len) {
    CompactParser* p = (CompactParser*)malloc(sizeof(CompactParser));
    if (!p) return NULL;

    p->input = xml;
    p->pos = xml;
    p->end = xml + len;
    p->line = 1;
    p->column = 1;
    p->has_error = 0;
    p->error_msg[0] = '\0';

    /* Initialize attribute tracking */
    p->attr_count = 0;
    memset(p->temp_attrs, 0, sizeof(p->temp_attrs));

    /* Create allocator and document */
    p->alloc = compact_allocator_create(1024 * 1024);  /* 1 MB default */
    if (!p->alloc) {
        free(p);
        return NULL;
    }

    p->doc = (CompactDocument*)compact_alloc(p->alloc, sizeof(CompactDocument));
    if (!p->doc) {
        compact_allocator_destroy(p->alloc);
        free(p);
        return NULL;
    }

    p->doc->allocator = p->alloc;
    p->doc->root_offset = 0;
    p->doc->first_text_offset = 0;
    p->doc->has_error = 0;

    return p;
}

/* ============================================================================
 * Compact Parsing Functions (Optimized for Speed)
 * ============================================================================ */

/* Helper: Get cumulative offset from first block to pointer */
static uint32_t get_cumulative_offset(CompactAllocator* alloc, void* ptr) {
    uint32_t offset = 0;
    CompactBlock* block = alloc->first_block;

    while (block) {
        char* block_start = (char*)block + sizeof(CompactBlock);
        char* block_end = block_start + block->size;

        if ((char*)ptr >= block_start && (char*)ptr < block_end) {
            offset += (uint32_t)((char*)ptr - block_start);
            return offset;
        }

        offset += (uint32_t)block->size;
        block = block->next;
    }

    return 0; /* Not found */
}

/* Skip whitespace - FAST */
static inline void compact_skip_whitespace(CompactParser* p) {
    while (p->pos < p->end && isspace((unsigned char)*p->pos)) {
        p->pos++;
    }
}

/* Parse name in compact mode - returns offset */
static uint32_t compact_parse_name(CompactParser* p) {
    const char* start = p->pos;

    /* Fast path: ASCII name validation */
    if (p->pos >= p->end || !isalpha((unsigned char)*p->pos)) {
        return 0;
    }

    /* Consume characters (ASCII fast path) */
    while (p->pos < p->end) {
        unsigned char c = (unsigned char)*p->pos;
        if (c < 0x80) {
            /* ASCII character - simple validation */
            if (!isalnum(c) && c != '_' && c != ':' && c != '-' && c != '.') {
                break;
            }
            p->pos++;
        } else {
            /* Non-ASCII - skip for now (could add UTF-8 support later) */
            break;
        }
    }

    size_t len = p->pos - start;
    if (len == 0) return 0;

    /* Store string in compact block */
    return compact_store_string_view(p->alloc, start, len);
}

/* Parse attribute value in compact mode - returns offset */
static uint32_t compact_parse_attribute_value(CompactParser* p) {
    const char quote = *p->pos;
    if (quote != '"' && quote != '\'') {
        return 0;
    }

    p->pos++; /* Skip opening quote */
    const char* start = p->pos;

    /* FAST: Use memchr to find closing quote */
    const char* end = (const char*)memchr(start, quote, p->end - start);

    size_t len = end ? (size_t)(end - start) : (size_t)(p->end - start);

    /* Move position */
    if (end) {
        p->pos = end + 1;  /* Skip closing quote */
    } else {
        p->pos = p->end;
    }

    /* Store string */
    return compact_store_string_view(p->alloc, start, len);
}

/* Parse attribute in compact mode - returns 1 on success, 0 on failure
 * Stores name_offset and value_offset in out parameters
 */
static int parse_compact_attribute(CompactParser* p, uint32_t* name_offset, uint32_t* value_offset) {
    /* Parse attribute name */
    *name_offset = compact_parse_name(p);
    if (*name_offset == 0) return 0;

    /* Skip '=' */
    compact_skip_whitespace(p);
    if (*p->pos != '=') return 0;
    p->pos++;

    /* Skip whitespace after '=' */
    compact_skip_whitespace(p);

    /* Parse attribute value */
    *value_offset = compact_parse_attribute_value(p);
    if (*value_offset == 0) return 0;

    return 1;
}

/* Parse compact element - returns offset */
static uint32_t parse_compact_element(CompactParser* p, uint32_t parent_offset) {
    if (*p->pos != '<') return 0;
    p->pos++; /* Skip '<' */

    /* Check for closing tag or special nodes */
    if (*p->pos == '/') {
        p->pos++; /* Skip '/' */
        return 0;  /* Should handle closing tags */
    }

    if (*p->pos == '!') {
        /* Comment, CDATA, DOCTYPE - skip for now */
        while (p->pos < p->end && *p->pos != '>') {
            p->pos++;
        }
        if (p->pos < p->end) p->pos++; /* Skip '>' */
        return 0;
    }

    /* Parse element name */
    uint32_t name_offset = compact_parse_name(p);
    if (name_offset == 0) return 0;

    /* Allocate compact element */
    CompactElement* elem = (CompactElement*)compact_alloc(p->alloc, sizeof(CompactElement));
    if (!elem) return 0;

    elem->name_offset = name_offset;
    elem->first_child = 0;
    elem->next_sibling = 0;
    elem->attributes_offset = 0;
    elem->attribute_count = 0;
    elem->flags = COMPACT_NODE_ELEMENT;
    elem->parent_offset = parent_offset;  /* Set parent offset */

    /* Get element offset (cumulative from first block) */
    uint32_t elem_offset = get_cumulative_offset(p->alloc, elem);

    /* Skip whitespace */
    compact_skip_whitespace(p);

    /* Reset attribute tracking for this element */
    p->attr_count = 0;

    /* Parse attributes - collect name/value offsets first */
    uint16_t attr_count = 0;

    while (*p->pos != '>' && *p->pos != '/' && p->pos < p->end) {
        if (isspace((unsigned char)*p->pos)) {
            compact_skip_whitespace(p);
        } else {
            uint32_t name_offset, value_offset;
            if (parse_compact_attribute(p, &name_offset, &value_offset)) {
                if (attr_count < 16) {
                    p->temp_attrs[attr_count].name_offset = name_offset;
                    p->temp_attrs[attr_count].value_offset = value_offset;
                }
                attr_count++;
            }
        }
    }

    /* Allocate attribute array contiguously after all strings are allocated */
    uint32_t first_attr_offset = 0;
    if (attr_count > 0) {
        CompactAttribute* attrs = (CompactAttribute*)compact_alloc(p->alloc, sizeof(CompactAttribute) * attr_count);
        if (!attrs) {
            /* Allocation failed - clean up and return error */
            return 0;
        }

        /* Fill the array with collected offsets */
        for (int i = 0; i < attr_count; i++) {
            attrs[i].name_offset = p->temp_attrs[i].name_offset;
            attrs[i].value_offset = p->temp_attrs[i].value_offset;
        }

        /* Store offset to first attribute (cumulative from first block) */
        first_attr_offset = get_cumulative_offset(p->alloc, attrs);
    }

    /* Store attributes on element */
    elem->attributes_offset = first_attr_offset;
    elem->attribute_count = attr_count;

    /* Handle self-closing tag */
    int self_closing = 0;
    if (*p->pos == '/') {
        self_closing = 1;
        p->pos++;
    }

    if (*p->pos != '>') {
        return 0;  /* Error */
    }
    p->pos++; /* Skip '>' */

    /* Parse children if not self-closing */
    if (!self_closing) {
        uint32_t first_child_offset = 0;
        uint32_t prev_child_offset = 0;

        while (p->pos < p->end) {
            compact_skip_whitespace(p);

            if (*p->pos == '<') {
                /* Check for closing tag */
                if (*(p->pos + 1) == '/') {
                    /* Closing tag - break */
                    break;
                }

                /* Child element node */
                uint32_t child_offset = parse_compact_element(p, elem_offset);
                if (child_offset == 0) break;

                /* Link first child */
                if (first_child_offset == 0) {
                    first_child_offset = child_offset;
                }

                /* Link siblings - find previous child and set its next_sibling */
                if (prev_child_offset != 0) {
                    CompactElement* prev_child = NULL;
                    CompactBlock* block = p->doc->allocator->first_block;
                    uint32_t current_offset = 0;

                    while (block) {
                        if (prev_child_offset < current_offset + block->size) {
                            prev_child = (CompactElement*)(block->data + (prev_child_offset - current_offset));
                            break;
                        }
                        current_offset += block->size;
                        block = block->next;
                    }

                    if (prev_child) {
                        prev_child->next_sibling = child_offset;
                    }
                }

                prev_child_offset = child_offset;
            } else {
                /* Text node */
                uint32_t text_offset = parse_compact_text(p);
                if (text_offset == 0) break;

                /* TODO: Link text nodes as children */
                /* For now, text nodes are not fully linked in the tree structure */
            }
        }

        /* Set first_child in parent element */
        if (first_child_offset != 0) {
            elem->first_child = first_child_offset;
        }
    }

    return elem_offset;
}

/* Parse compact text node - returns offset */
static uint32_t parse_compact_text(CompactParser* p) {
    const char* start = p->pos;

    /* FAST: Find closing tag using memchr */
    const char* end = (const char*)memchr(start, '<', p->end - start);

    if (end) {
        p->pos = end;
    } else {
        p->pos = p->end;
    }

    size_t len = p->pos - start;
    if (len == 0) return 0;

    /* Skip pure whitespace */
    int all_whitespace = 1;
    for (size_t i = 0; i < len; i++) {
        if (!isspace((unsigned char)start[i])) {
            all_whitespace = 0;
            break;
        }
    }

    if (all_whitespace) return 0;  /* Skip whitespace-only nodes */

    /* Allocate compact text node */
    CompactText* text = (CompactText*)compact_alloc(p->alloc, sizeof(CompactText));
    if (!text) return 0;

    /* Store content */
    text->content_offset = compact_store_string_view(p->alloc, start, len);
    text->next_sibling = 0;
    text->parent_offset = 0;

    /* Return cumulative offset from first block */
    return get_cumulative_offset(p->doc->allocator, text);
}

/* ============================================================================
 * Compact to Regular DOM Conversion
 * ============================================================================ */

/* Get string pointer from offset in compact allocator */
static const char* compact_get_string_at_offset(CompactAllocator* alloc, uint32_t offset) {
    if (offset == 0) return NULL;

    /* Search through blocks for this offset */
    CompactBlock* block = alloc->first_block;
    uint32_t current_offset = 0;  /* Cumulative offset across blocks */

    while (block) {
        if (offset < current_offset + block->size) {
            /* Found the block containing this offset */
            return block->data + (offset - current_offset);
        }
        current_offset += block->size;
        block = block->next;
    }
    return NULL;
}

/* Convert compact element to regular element (recursive) */
static TaurusElement convert_compact_recursive(CompactDocument* cdoc, uint32_t elem_offset,
                                               TaurusMemoryPool* pool, const char* xml_data) {
    if (elem_offset == 0) return NULL;

    /* Find the element in the compact blocks */
    CompactElement* celem = NULL;
    CompactBlock* block = cdoc->allocator->first_block;
    uint32_t current_offset = 0;  /* Cumulative offset across blocks */

    while (block) {
        if (elem_offset < current_offset + block->size) {
            /* Found the block containing this element */
            celem = (CompactElement*)(block->data + (elem_offset - current_offset));
            break;
        }
        current_offset += block->size;
        block = block->next;
    }

    if (!celem) {
        return NULL;
    }

    /* Get element name */
    const char* name = compact_get_string_at_offset(cdoc->allocator, celem->name_offset);
    if (!name) {
        return NULL;
    }

    /* CRITICAL: Copy string data to pool!
     * The name pointer points to compact allocator memory which will be freed.
     * We need to pool-allocate the string data so it survives compact allocator destruction. */
    size_t name_len = strlen(name);
    char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
    if (!name_copy) {
        return NULL;
    }
    memcpy(name_copy, name, name_len + 1);  /* Copy null terminator too */

    /* Create StringView pointing to pool-allocated copy */
    TaurusStringView name_view;
    name_view.data = name_copy;
    name_view.length = name_len;

    /* Create element with pool-allocated StringView */
    TaurusElement elem = taurus_element_create_with_view(name_view, pool);
    if (!elem) {
        return NULL;
    }

    /* Convert attributes - array is now contiguous, so simple arithmetic works */
    if (celem->attribute_count > 0 && celem->attributes_offset != 0) {
        /* Find the attribute array in the compact blocks */
        CompactAttribute* cattrs = NULL;
        block = cdoc->allocator->first_block;
        current_offset = 0;

        while (block) {
            if (celem->attributes_offset < current_offset + block->size) {
                cattrs = (CompactAttribute*)(block->data + (celem->attributes_offset - current_offset));
                break;
            }
            current_offset += block->size;
            block = block->next;
        }

        if (cattrs) {
            /* Convert all attributes - they are now contiguous! */
            for (uint16_t i = 0; i < celem->attribute_count; i++) {
                const char* attr_name = compact_get_string_at_offset(cdoc->allocator, cattrs[i].name_offset);
                const char* attr_value = compact_get_string_at_offset(cdoc->allocator, cattrs[i].value_offset);

                if (attr_name && attr_value) {
                    /* CRITICAL: Copy string data to pool!
                     * The pointers point to compact allocator memory which will be freed.
                     * We need to pool-allocate the string data so it survives compact allocator destruction. */
                    size_t name_len = strlen(attr_name);
                    size_t value_len = strlen(attr_value);
                    char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
                    char* value_copy = (char*)taurus_pool_alloc(pool, value_len + 1);
                    if (!name_copy || !value_copy) {
                        continue;  /* Skip this attribute if allocation fails */
                    }
                    memcpy(name_copy, attr_name, name_len + 1);
                    memcpy(value_copy, attr_value, value_len + 1);

                    /* Create StringViews pointing to pool-allocated copies */
                    TaurusStringView name_view;
                    name_view.data = name_copy;
                    name_view.length = name_len;
                    TaurusStringView value_view;
                    value_view.data = value_copy;
                    value_view.length = value_len;

                    /* Add attribute with pool-allocated StringViews */
                    taurus_element_add_attribute(elem, name_view, value_view, pool);
                }
            }
        }
    }

    /* Convert children recursively */
    uint32_t child_offset = celem->first_child;
    while (child_offset != 0) {
        TaurusElement child = convert_compact_recursive(cdoc, child_offset, pool, xml_data);
        if (child) {
            TaurusNode* child_node = TAURUS_ELEMENT_AS_NODE(child);
            taurus_element_append_child_internal(elem, child_node);

            /* Get next sibling from the child element we just converted */
            /* We need to find the child's next_sibling offset */
            CompactElement* cchild = NULL;
            block = cdoc->allocator->first_block;
            current_offset = 0;

            while (block) {
                if (child_offset < current_offset + block->size) {
                    cchild = (CompactElement*)(block->data + (child_offset - current_offset));
                    break;
                }
                current_offset += block->size;
                block = block->next;
            }

            if (cchild) {
                child_offset = cchild->next_sibling;
            } else {
                child_offset = 0;
            }
        } else {
            break;
        }
    }

    return elem;
}

/* Parse document in compact mode */
struct taurus_document* taurus_parse_string_compact(const char* xml, size_t length, int* error_out) {
    if (!xml || length == 0) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Create compact parser */
    CompactParser* parser = compact_parser_create(xml, length);
    if (!parser) {
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Skip leading whitespace */
    compact_skip_whitespace(parser);

    /* Parse root element (parent_offset=0 for root) */
    uint32_t root_offset = parse_compact_element(parser, 0);

    if (root_offset == 0) {
        /* Error or empty document */
        compact_allocator_destroy(parser->doc->allocator);
        free(parser->doc);
        free(parser);
        if (error_out) *error_out = 1;
        return NULL;
    }

    parser->doc->root_offset = root_offset;

    /* Create regular TaurusDocument */
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(struct taurus_document));
    if (!doc) {
        compact_allocator_destroy(parser->doc->allocator);
        free(parser->doc);
        free(parser);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Initialize document */
    memset(doc, 0, sizeof(struct taurus_document));
    doc->pool = taurus_pool_create();
    if (!doc->pool) {
        free(doc);
        compact_allocator_destroy(parser->doc->allocator);
        free(parser->doc);
        free(parser);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Convert compact DOM to regular DOM */
    doc->new_dom_root = convert_compact_recursive(parser->doc, root_offset, doc->pool, xml);

    /* Clean up compact structures */
    compact_allocator_destroy(parser->doc->allocator);

    /* Note: parser->doc was allocated with compact_alloc, so it's already freed */
    free(parser);

    if (!doc->new_dom_root) {
        taurus_pool_destroy(doc->pool);
        free(doc);
        if (error_out) *error_out = 1;
        return NULL;
    }

    /* Set document on root element */
    taurus_element_set_document_tree(doc->new_dom_root, doc);

    if (error_out) *error_out = 0;
    return doc;
}
