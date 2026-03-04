/* ptr_element.h - Pointer-Based Element Structures
 * Copyright (c) 2026, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE - Target: 1.2x+ faster than pugixml
 *
 * Key insight from benchmarking:
 * - Offset-based access adds 40-50% overhead
 * - Direct pointers are 1.29-1.45x faster than pugixml
 * - Complete structure for full API support
 *
 * Structure sizes:
 * - ptr_element: 72 bytes (extended for full API + TaurusNode compatibility)
 * - ptr_attribute: 32 bytes (extended for StringView)
 * - ptr_text: 24 bytes
 */

#ifndef TAURUS_PTR_ELEMENT_H
#define TAURUS_PTR_ELEMENT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Forward declaration for StringView support */
struct taurus_string_view;

/* Node type constants (must match TaurusNodeType in taurus_internal.h)
 *
 * IMPORTANT: These values MUST match the legacy TaurusNodeType enum:
 *   TAURUS_NODE_ELEMENT   = 0
 *   TAURUS_NODE_ATTRIBUTE = 1
 *   TAURUS_NODE_TEXT      = 2
 *   TAURUS_NODE_COMMENT   = 3
 *   TAURUS_NODE_CDATA     = 3  (same as comment for our purposes)
 *   TAURUS_NODE_PI        = 4
 *   TAURUS_NODE_DOCTYPE   = 5
 *   TAURUS_NODE_NAMESPACE = 6
 */
#define PTR_NODE_TYPE_ELEMENT   0   /* TAURUS_NODE_ELEMENT */
#define PTR_NODE_TYPE_TEXT      2   /* TAURUS_NODE_TEXT (was 1, fixed to match) */
#define PTR_NODE_TYPE_COMMENT   3   /* TAURUS_NODE_COMMENT */
#define PTR_NODE_TYPE_CDATA     7   /* Unique value for CDATA sections */
#define PTR_NODE_TYPE_PI        4   /* TAURUS_NODE_PI */
#define PTR_NODE_TYPE_DOCTYPE   5   /* TAURUS_NODE_DOCTYPE */

/* ============================================================================
 * Pointer-Based Element Structure (72 bytes)
 * ============================================================================ */

/**
 * Pointer-based element - 72 bytes
 *
 * CRITICAL: First 20 bytes MUST match TaurusNode layout for safe casting:
 * - type (4 bytes)
 * - frozen/version (4 bytes)
 * - next_sibling (8 bytes)
 * - prev_sibling (8 bytes)
 *
 * This allows ptr_element* to be safely cast to TaurusNode*.
 */
struct ptr_element {
    /* === TaurusNode-compatible header (20 bytes) === */
    uint32_t type;                      /* Node type (0=element) - MUST be first */
    uint32_t frozen_version;            /* COW: frozen (1 bit) + version (31 bits) */
    struct ptr_element* next_sibling;   /* Next sibling element */
    struct ptr_element* prev_sibling;   /* Previous sibling for reverse iteration */

    /* === Element-specific fields (52 bytes) === */
    struct ptr_element* first_child;    /* First child element */
    struct ptr_element* last_child;     /* Last child for O(1) append */
    struct ptr_element* parent;         /* Parent element */
    struct ptr_attribute* first_attr;   /* First attribute in linked list */
    const char* name;                   /* Element name (null-terminated) */
    struct taurus_document* document;   /* Document this element belongs to */
    uint16_t child_count;               /* Number of child elements */
    uint8_t attr_count;                 /* Number of attributes */
    uint8_t reserved;                   /* Padding for alignment */
};

/* ============================================================================
 * Pointer-Based Attribute Structure (32 bytes)
 * ============================================================================ */

/**
 * Pointer-based attribute - 32 bytes
 *
 * Extended to support StringView for zero-copy access.
 */
struct ptr_attribute {
    const char* name;                  /* Attribute name (null-terminated) */
    const char* value;                 /* Attribute value (null-terminated) */
    struct ptr_attribute* next_attr;   /* Next attribute in list */
    /* Extended fields for StringView compatibility */
    const char* name_view_data;        /* StringView data pointer */
    size_t name_view_length;           /* StringView length */
    const char* value_view_data;       /* StringView data pointer */
    size_t value_view_length;          /* StringView length */
};

/* ============================================================================
 * Pointer-Based Text Node Structure (24 bytes)
 * ============================================================================ */

/**
 * Pointer-based text node - 24 bytes
 *
 * CRITICAL: First 20 bytes MUST match TaurusNode layout for safe casting.
 */
struct ptr_text {
    /* === TaurusNode-compatible header (20 bytes) === */
    uint32_t type;                      /* Node type - MUST be first */
    uint32_t frozen_version;            /* COW: frozen (1 bit) + version (31 bits) */
    struct ptr_node* next_sibling;      /* Next sibling (can be element or text) */
    struct ptr_node* prev_sibling;      /* Previous sibling */

    /* === Text-specific fields (4 bytes) === */
    const char* text;                   /* Text content (null-terminated) */
    /* Note: length is embedded in the struct, calculated as needed */
};

/* ============================================================================
 * Generic Node Pointer (for mixed child lists)
 * ============================================================================ */

/**
 * Generic node pointer - used for type-safe casting
 *
 * The first field (type) determines the actual type.
 */
struct ptr_node {
    uint32_t type;                      /* MUST be first field for type detection */
    uint32_t frozen_version;            /* COW fields */
    union {
        struct ptr_element elem;
        struct ptr_text text;
    } u;
};

/* Type checking macros */
#define PTR_IS_ELEMENT(node) ((node) && ((struct ptr_node*)(node))->type == PTR_NODE_TYPE_ELEMENT)
#define PTR_IS_TEXT(node) ((node) && ((struct ptr_node*)(node))->type >= PTR_NODE_TYPE_TEXT)
#define PTR_NODE_TYPE(node) ((node) ? ((struct ptr_node*)(node))->type : PTR_NODE_TYPE_ELEMENT)

/* ============================================================================
 * Accessor Macros
 * ============================================================================ */

/**
 * Get element name
 */
#define PTR_ELEMENT_NAME(elem) ((elem) ? (elem)->name : NULL)

/**
 * Get first child element (skip text nodes)
 *
 * All node types share the same first 4 fields for navigation:
 * type, frozen_version, next_sibling, prev_sibling
 */
static inline struct ptr_element* ptr_first_child_element(struct ptr_element* elem) {
    if (!elem) return NULL;
    struct ptr_element* child = elem->first_child;
    while (child && child->type != PTR_NODE_TYPE_ELEMENT) {
        child = (struct ptr_element*)child->next_sibling;
    }
    return child;
}

/**
 * Get next sibling element (skip text nodes)
 *
 * All node types share the same first 4 fields for navigation.
 */
static inline struct ptr_element* ptr_next_sibling_element(struct ptr_element* elem) {
    if (!elem) return NULL;
    struct ptr_element* sibling = elem->next_sibling;
    while (sibling && sibling->type != PTR_NODE_TYPE_ELEMENT) {
        sibling = (struct ptr_element*)sibling->next_sibling;
    }
    return sibling;
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
