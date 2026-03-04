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
#include "ptr_element.h"
#include "../taurus_memory.h"
#include <stdlib.h>
#include <string.h>

/* Maximum recursion depth to prevent stack overflow */
#define MAX_TEXT_RECURSION_DEPTH 256

/* ============================================================================
 * Text Content Extraction Helpers
 * ============================================================================ */

/* Helper function to calculate text length recursively with depth limit */
static size_t calculate_text_length_recursive_impl(TaurusNode* node, int depth) {
    if (!node || depth > MAX_TEXT_RECURSION_DEPTH) return 0;

    size_t len = 0;
    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            /* Use ptr_text structure which has 'text' field */
            struct ptr_text* text = (struct ptr_text*)child;
            if (text->text) {
                len += strlen(text->text);
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            /* CDATA also uses ptr_text structure */
            struct ptr_text* cdata = (struct ptr_text*)child;
            if (cdata->text) {
                len += strlen(cdata->text);
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            len += calculate_text_length_recursive_impl(child, depth + 1);
        }

        child = taurus_node_get_next_sibling(child);
    }

    return len;
}

/* Wrapper function for backward compatibility */
static size_t calculate_text_length_recursive(TaurusNode* node) {
    return calculate_text_length_recursive_impl(node, 0);
}

/* Helper function to copy text content recursively with depth limit */
static void copy_text_content_recursive_impl(TaurusNode* node, char* result, size_t* offset, int depth) {
    if (!node || !result || !offset || depth > MAX_TEXT_RECURSION_DEPTH) return;

    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            /* Use ptr_text structure which has 'text' field */
            struct ptr_text* text = (struct ptr_text*)child;
            if (text->text) {
                size_t len = strlen(text->text);
                memcpy(result + *offset, text->text, len);
                *offset += len;
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            /* CDATA also uses ptr_text structure */
            struct ptr_text* cdata = (struct ptr_text*)child;
            if (cdata->text) {
                size_t len = strlen(cdata->text);
                memcpy(result + *offset, cdata->text, len);
                *offset += len;
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            copy_text_content_recursive_impl(child, result, offset, depth + 1);
        }

        child = taurus_node_get_next_sibling(child);
    }
}

/* Wrapper function for backward compatibility */
static void copy_text_content_recursive(TaurusNode* node, char* result, size_t* offset) {
    copy_text_content_recursive_impl(node, result, offset, 0);
}

/* ============================================================================
 * Text Content Public API
 * ============================================================================ */

/* Text content extraction (concatenates ALL text nodes recursively)
 * NOTE: Implementation moved to element.c to avoid offset issues with ptr_node union.
 * The implementation in element.c correctly casts to ptr_text directly.
 */

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
