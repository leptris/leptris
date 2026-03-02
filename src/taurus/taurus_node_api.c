/* taurus_node_api.c - Taurus Node API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Low-level Node API.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "dom/element.h"
#include "dom/node.h"
#include "dom/text.h"
#include "dom/comment.h"
#include "dom/cdata.h"
#include "dom/pi.h"

/* ============================================================================
 * Node Type and Navigation
 * ============================================================================ */

/**
 * Get node type
 */
TAURUS_API int taurus_node_get_type(TaurusNodeRef node) {
    if (!node) return 0; /* TAURUS_NODE_TYPE_ELEMENT */
    return (int)node->type;
}

/**
 * Get first child node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_first_child(TaurusNodeRef node) {
    if (!node) return NULL;
    /* Use internal function which handles compact mode */
    return taurus_node_first_child_internal(node);
}

/**
 * Get last child node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_last_child(TaurusNodeRef node) {
    if (!node) return NULL;
    /* Use internal function which handles compact mode */
    return taurus_node_last_child_internal(node);
}

/**
 * Get next sibling node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    return (TaurusNodeRef)taurus_node_get_next_sibling(node);
}

/**
 * Get previous sibling node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == 0) { /* TAURUS_NODE_TYPE_ELEMENT */
        TaurusElement elem = (TaurusElement)node;
        TaurusElement parent = elem->parent;
        if (!parent) return NULL;

        TaurusNodeRef prev = NULL;
        TaurusNodeRef child = (TaurusNodeRef)parent->first_child;
        while (child && child != node) {
            prev = child;
            child = (TaurusNodeRef)taurus_node_get_next_sibling(child);
        }
        return prev;
    }
    return NULL;
}

/**
 * Get child count (all node types)
 */
TAURUS_API size_t taurus_node_child_count(TaurusNodeRef node) {
    if (!node) return 0;
    return taurus_node_child_count_internal(node);
}

/**
 * Cast node to element (if node is an element)
 */
TAURUS_API TaurusElement taurus_node_as_element(TaurusNodeRef node) {
    if (!node || node->type != 0) return NULL;
    return (TaurusElement)node;
}

/* ============================================================================
 * Node Content Accessors
 * ============================================================================ */

/**
 * Get text content from text node
 */
TAURUS_API const char* taurus_text_node_get_content(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == 1) { /* TAURUS_NODE_TYPE_TEXT */
        return ((TaurusTextNode*)node)->content;
    }
    if (node->type == 3) { /* TAURUS_NODE_TYPE_CDATA */
        return ((TaurusCDATANode*)node)->content;
    }
    return NULL;
}

/**
 * Get comment content
 */
TAURUS_API const char* taurus_comment_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != 2) return NULL;
    return ((TaurusCommentNode*)node)->content;
}

/**
 * Get CDATA content
 */
TAURUS_API const char* taurus_cdata_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != 3) return NULL;
    return ((TaurusCDATANode*)node)->content;
}

/**
 * Get processing instruction target
 */
TAURUS_API const char* taurus_pi_node_get_target(TaurusNodeRef node) {
    if (!node || node->type != 4) return NULL;
    return ((TaurusPINode*)node)->target;
}

/**
 * Get processing instruction data
 */
TAURUS_API const char* taurus_pi_node_get_data(TaurusNodeRef node) {
    if (!node || node->type != 4) return NULL;
    return ((TaurusPINode*)node)->data;
}
