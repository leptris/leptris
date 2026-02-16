/* element_text.c - Element text content and subtree utilities
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides text content extraction and subtree analysis for elements:
 * - Recursive text concatenation from all descendant text/CDATA nodes
 * - Efficient two-pass algorithm (calculate length, then copy)
 * - Subtree node counting for bulk allocation planning
 */

#include "element.h"
#include "text.h"
#include "cdata.h"
#include "node.h"
#include "../taurus_memory.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Text Content Extraction Helpers
 * ============================================================================ */

/* Helper function to calculate text length recursively */
static size_t calculate_text_length_recursive(TaurusNode* node) {
    if (!node) return 0;

    size_t len = 0;
    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            if (text->content) {
                len += strlen(text->content);
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if (cdata->content) {
                len += strlen(cdata->content);
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            len += calculate_text_length_recursive(child);
        }

        child = taurus_node_get_next_sibling(child);
    }

    return len;
}

/* Helper function to copy text content recursively */
static void copy_text_content_recursive(TaurusNode* node, char* result, size_t* offset) {
    if (!node || !result || !offset) return;

    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            if (text->content) {
                size_t len = strlen(text->content);
                memcpy(result + *offset, text->content, len);
                *offset += len;
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if (cdata->content) {
                size_t len = strlen(cdata->content);
                memcpy(result + *offset, cdata->content, len);
                *offset += len;
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            copy_text_content_recursive(child, result, offset);
        }

        child = taurus_node_get_next_sibling(child);
    }
}

/* ============================================================================
 * Text Content Public API
 * ============================================================================ */

/* Text content extraction (concatenates ALL text nodes recursively) */
char* taurus_element_get_text_content(TaurusElement elem) {
    if (!elem) return NULL;

    /* First pass: calculate total length needed recursively */
    size_t total_len = calculate_text_length_recursive((TaurusNode*)elem);

    if (total_len == 0) {
        return taurus_strdup("");
    }

    /* Allocate buffer for concatenated text */
    char* result = (char*)taurus_malloc(total_len + 1);
    if (!result) return NULL;

    /* Second pass: copy text content recursively */
    size_t offset = 0;
    copy_text_content_recursive((TaurusNode*)elem, result, &offset);

    result[offset] = '\0';
    return result;
}

/* ============================================================================
 * Subtree Analysis (for bulk allocation planning)
 * ============================================================================ */

/**
 * Count all nodes in subtree (for bulk allocation planning)
 *
 * Recursively traverses element tree and counts all nodes by type.
 * Used to pre-calculate allocation size for bulk operations.
 *
 * @param elem Root element to count
 * @param stats Output structure to fill with counts
 */
void taurus_element_count_subtree(TaurusElement elem, TaurusSubtreeStats* stats) {
    if (!elem || !stats) return;

    /* Initialize counts to zero */
    memset(stats, 0, sizeof(TaurusSubtreeStats));

    /* Count this element */
    stats->element_count = 1;

    /* Count attributes on this element */
    stats->attribute_count = elem->attr_count;

    /* Recursively traverse all children */
    TaurusNode* child = (TaurusNode*)elem->first_child;
    while (child) {
        switch (child->type) {
            case TAURUS_NODE_TYPE_ELEMENT:
                stats->element_count++;
                taurus_element_count_subtree((TaurusElement)child, stats);
                break;

            case TAURUS_NODE_TYPE_TEXT:
                stats->text_count++;
                break;

            case TAURUS_NODE_TYPE_COMMENT:
                stats->comment_count++;
                break;

            case TAURUS_NODE_TYPE_CDATA:
                stats->cdata_count++;
                break;

            case TAURUS_NODE_TYPE_PI:
                stats->pi_count++;
                break;

            default:
                /* Unknown node type, skip */
                break;
        }

        child = taurus_node_get_next_sibling(child);
    }
}

/* ============================================================================
 * Document Tree Operations
 * ============================================================================ */

void taurus_element_set_document_tree(TaurusElement elem, struct taurus_document* doc) {
    if (!elem) return;

    elem->document = doc;

    /* Recursively set document pointer on all element children
     * CRITICAL: Only recurse on element nodes, not text/comment/CDATA/etc.
     * Use generic node navigation to handle mixed content correctly. */
    TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)elem);
    while (child) {
        /* Check if child is an element node before recursing */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            taurus_element_set_document_tree((TaurusElement)child, doc);
        }
        child = taurus_node_get_next_sibling(child);
    }
}
