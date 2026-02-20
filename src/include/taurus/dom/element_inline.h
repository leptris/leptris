/* element_inline.h - Inline functions for hot path performance
 * Copyright (c) 2024, Ribose Inc.
 *
 * This header provides inline variants of frequently-called functions
 * for performance-critical code paths. These bypass function call overhead
 * and can be inlined by the compiler.
 *
 * Usage:
 *   #include <taurus/dom/element_inline.h>
 *
 *   // Use inline version for hot paths
 *   const char* name = taurus_element_name_inline(elem);
 */

#ifndef TAURUS_DOM_ELEMENT_INLINE_H
#define TAURUS_DOM_ELEMENT_INLINE_H

#include "element.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Inline Element Accessors
 * ============================================================================ */

/**
 * Get element name (inline version)
 *
 * This is an inline variant of taurus_element_name() for hot paths.
 * Directly accesses the element structure for maximum performance.
 *
 * @param elem Element handle
 * @return Element name string or NULL if elem is NULL
 */
static inline const char* taurus_element_name_inline(TaurusElement elem) {
    if (!elem) return NULL;
    return ((struct taurus_element*)elem)->name;
}

/**
 * Get element parent (inline version)
 *
 * This is an inline variant of taurus_element_parent() for hot paths.
 *
 * @param elem Element handle
 * @return Parent element or NULL if elem is NULL or has no parent
 */
static inline TaurusElement taurus_element_parent_inline(TaurusElement elem) {
    if (!elem) return NULL;
    return (TaurusElement)((struct taurus_element*)elem)->parent;
}

/**
 * Get first child element (inline version)
 *
 * This is an inline variant of taurus_element_first_child() for hot paths.
 * Uses the inline children array for O(1) access when available.
 *
 * @param elem Parent element
 * @return First child element or NULL
 */
static inline TaurusElement taurus_element_first_child_inline(TaurusElement elem) {
    if (!elem) return NULL;

    struct taurus_element* e = (struct taurus_element*)elem;

    /* Fast path: use inline array if populated */
    if (e->children[0]) {
        return (TaurusElement)e->children[0];
    }

    /* Fallback to linked list */
    return (TaurusElement)e->first_child;
}

/**
 * Get next sibling element (inline version)
 *
 * This is an inline variant of taurus_element_next_sibling() for hot paths.
 *
 * @param elem Current element
 * @return Next sibling element or NULL
 */
static inline TaurusElement taurus_element_next_sibling_inline(TaurusElement elem) {
    if (!elem) return NULL;

    struct taurus_element* e = (struct taurus_element*)elem;

    /* If we have a parent with inline array, use it for O(1) access */
    if (e->parent && e->parent->child_count <= 4) {
        /* Find ourselves in the array and return next */
        for (int i = 0; i < 3 && e->parent->children[i]; i++) {
            if (e->parent->children[i] == (TaurusNode*)e) {
                return (TaurusElement)e->parent->children[i + 1];
            }
        }
    }

    /* Fallback to linked list */
    TaurusNode* node = e->next_sibling;
    while (node && !TAURUS_NODE_IS_ELEMENT(node)) {
        node = node->next_sibling;
    }
    return (TaurusElement)node;
}

/**
 * Get element text content (inline version for small strings)
 *
 * This is an optimized variant that directly accesses the text field.
 * Only use when you know the element has simple text content.
 *
 * @param elem Element handle
 * @return Text content or NULL
 */
static inline const char* taurus_element_text_inline(TaurusElement elem) {
    if (!elem) return NULL;
    return ((struct taurus_element*)elem)->text;
}

/**
 * Get child count (inline version)
 *
 * This is an inline variant of taurus_element_child_count() for hot paths.
 *
 * @param elem Parent element
 * @return Number of child elements
 */
static inline size_t taurus_element_child_count_inline(TaurusElement elem) {
    if (!elem) return 0;
    return ((struct taurus_element*)elem)->child_count;
}

/**
 * Check if element has children (inline version)
 *
 * Fast check for child existence without counting.
 *
 * @param elem Parent element
 * @return 1 if element has children, 0 otherwise
 */
static inline int taurus_element_has_children_inline(TaurusElement elem) {
    if (!elem) return 0;
    struct taurus_element* e = (struct taurus_element*)elem;
    return (e->first_child != NULL) || (e->children[0] != NULL);
}

/**
 * Get attribute value by name - fast path for elements with ≤4 attributes (inline version)
 *
 * This is an optimized inline version that checks the inline attribute array
 * for O(1) access on elements with 4 or fewer attributes. For elements with
 * more attributes, this falls back to linear search.
 *
 * @param elem Element handle
 * @param name Attribute name to find
 * @return Attribute value string or NULL if not found
 *
 * PERFORMANCE: Use this in hot paths where elements typically have ≤4 attributes.
 * For general use, taurus_element_attribute() provides hash table O(1) lookup.
 */
static inline const char* taurus_element_attribute_inline(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    struct taurus_element* e = (struct taurus_element*)elem;

    /* Fast path: check inline array for first 4 attributes */
    uint8_t count = e->attr_count;
    if (count == 0) return NULL;

    /* Calculate name length once */
    size_t name_len = 0;
    const char* p = name;
    while (*p++) name_len++;

    /* Check inline array (O(1) for ≤4 attributes) */
    uint8_t limit = count < 4 ? count : 4;
    for (uint8_t i = 0; i < limit; i++) {
        struct taurus_attribute* attr = e->attributes_inline[i];
        if (!attr) continue;

        /* Check cached name first (faster string comparison) */
        if (attr->name) {
            const char* an = attr->name;
            const char* nn = name;
            while (*an && *nn && *an == *nn) { an++; nn++; }
            if (*an == *nn) return attr->value;  /* Found! */
        }
        /* Fall back to StringView comparison */
        else if (attr->name_view.length == name_len &&
                 attr->name_view.data &&
                 (attr->name_view.data[0] == name[0]) &&  /* Quick first char check */
                 memcmp(attr->name_view.data, name, name_len) == 0) {
            return attr->value;
        }
    }

    /* For >4 attributes without hash, caller should use full function */
    return NULL;
}

/**
 * Get attribute count (inline version)
 *
 * Fast access to attribute count without function call.
 *
 * @param elem Element handle
 * @return Number of attributes, or 0 if elem is NULL
 */
static inline uint8_t taurus_element_attribute_count_inline(TaurusElement elem) {
    if (!elem) return 0;
    return ((struct taurus_element*)elem)->attr_count;
}

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_DOM_ELEMENT_INLINE_H */
