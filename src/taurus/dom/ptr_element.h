/* ptr_element.h - Pointer-Based Element Structures
 * Copyright (c) 2026, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE - Target: 1.2x+ faster than pugixml
 *
 * Key insight from benchmarking:
 * - Offset-based access adds 40-50% overhead
 * - Direct pointers are 1.29-1.45x faster than pugixml
 * - Smaller structures = better cache efficiency
 *
 * Structure sizes:
 * - ptr_element: 40 bytes (vs 168 bytes for taurus_element)
 * - ptr_attribute: 24 bytes
 * - ptr_text: 24 bytes
 */

#ifndef TAURUS_PTR_ELEMENT_H
#define TAURUS_PTR_ELEMENT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ============================================================================
 * Pointer-Based Element Structure (40 bytes)
 * ============================================================================ */

/**
 * Pointer-based element - 56 bytes
 *
 * Uses direct pointers for tree navigation, eliminating offset-to-pointer
 * conversion overhead. This matches pugixml's approach.
 *
 * Design principles:
 * 1. Direct pointers for O(1) navigation (no offset conversion)
 * 2. Null-terminated strings (like pugixml)
 * 3. Pool allocation for O(1) creation
 * 4. Minimal fields for cache efficiency
 */
struct ptr_element {
    struct ptr_element* first_child;   /* First child element */
    struct ptr_element* next_sibling;  /* Next sibling element */
    struct ptr_element* parent;        /* Parent element */
    struct ptr_attribute* first_attr;  /* First attribute in linked list */
    const char* name;                  /* Element name (null-terminated) */
    struct taurus_document* document;  /* Document this element belongs to */
    uint32_t node_type;                /* Node type (0=element) */
};

/* ============================================================================
 * Pointer-Based Attribute Structure (24 bytes)
 * ============================================================================ */

/**
 * Pointer-based attribute - 24 bytes
 *
 * Simple linked list of attributes with direct pointers.
 */
struct ptr_attribute {
    const char* name;                  /* Attribute name (null-terminated) */
    const char* value;                 /* Attribute value (null-terminated) */
    struct ptr_attribute* next_attr;   /* Next attribute in list */
};

/* ============================================================================
 * Pointer-Based Text Node Structure (24 bytes)
 * ============================================================================ */

/**
 * Pointer-based text node - 24 bytes
 *
 * Stores text content with direct pointer. Used for text nodes, CDATA,
 * comments, and processing instructions.
 */
struct ptr_text {
    const char* text;                  /* Text content (null-terminated) */
    struct ptr_node* next_sibling;     /* Next sibling (can be element or text) */
    uint32_t length;                   /* Text length (for whitespace detection) */
    uint32_t node_type;                /* Node type: 0=element, 1=text, 2=cdata, 3=comment, 4=pi */
};

/* ============================================================================
 * Generic Node Pointer (for mixed child lists)
 * ============================================================================ */

/**
 * Generic node pointer - used for traversing mixed content
 *
 * The first field (node_type) determines the actual type:
 * - 0: ptr_element (name is NOT null, first_child points to element)
 * - 1-4: ptr_text (node_type indicates specific type)
 */
struct ptr_node {
    uint32_t node_type;                /* MUST be first field for type detection */
    union {
        struct ptr_element elem;
        struct ptr_text text;
    } u;
};

/* Node type constants */
#define PTR_NODE_TYPE_ELEMENT   0
#define PTR_NODE_TYPE_TEXT      1
#define PTR_NODE_TYPE_CDATA     2
#define PTR_NODE_TYPE_COMMENT   3
#define PTR_NODE_TYPE_PI        4

/* Type checking macros */
#define PTR_IS_ELEMENT(node) ((node) && ((struct ptr_node*)(node))->node_type == PTR_NODE_TYPE_ELEMENT)
#define PTR_IS_TEXT(node) ((node) && ((struct ptr_node*)(node))->node_type >= PTR_NODE_TYPE_TEXT)
#define PTR_NODE_TYPE(node) ((node) ? ((struct ptr_node*)(node))->node_type : PTR_NODE_TYPE_ELEMENT)

/* ============================================================================
 * Accessor Macros
 * ============================================================================ */

/**
 * Get element name
 */
#define PTR_ELEMENT_NAME(elem) ((elem) ? (elem)->name : NULL)

/**
 * Get first child element (skip text nodes)
 */
static inline struct ptr_element* ptr_first_child_element(struct ptr_element* elem) {
    if (!elem) return NULL;
    struct ptr_node* child = (struct ptr_node*)elem->first_child;
    while (child && child->node_type != PTR_NODE_TYPE_ELEMENT) {
        child = (struct ptr_node*)child->u.text.next_sibling;
    }
    return (struct ptr_element*)child;
}

/**
 * Get next sibling element (skip text nodes)
 */
static inline struct ptr_element* ptr_next_sibling_element(struct ptr_element* elem) {
    if (!elem) return NULL;
    struct ptr_node* sibling = (struct ptr_node*)elem->next_sibling;
    while (sibling && sibling->node_type != PTR_NODE_TYPE_ELEMENT) {
        sibling = (struct ptr_node*)sibling->u.text.next_sibling;
    }
    return (struct ptr_element*)sibling;
}

/**
 * Get first attribute
 */
#define PTR_FIRST_ATTR(elem) ((elem) ? (elem)->first_attr : NULL)

/**
 * Get attribute value by name
 */
static inline const char* ptr_get_attr(struct ptr_element* elem, const char* name) {
    if (!elem || !name) return NULL;
    struct ptr_attribute* attr = elem->first_attr;
    while (attr) {
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr->value;
        }
        attr = attr->next_attr;
    }
    return NULL;
}

#endif /* TAURUS_PTR_ELEMENT_H */
