/* compact_accessor.c - Compact Element Accessor Functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides accessor functions that work directly with compact elements.
 * This is the key to achieving pugixml-level performance - no conversion
 * to legacy format, just direct access to compact data.
 *
 * Architecture:
 * - Compact elements use 4-byte offsets instead of 8-byte pointers
 * - All data is in a single contiguous memory block (cache efficiency)
 * - No individual allocations = O(1) operations
 */

#include "compact_accessor.h"
#include "compact_element.h"
#include "element.h"
#include "../memory/compact_single_alloc.h"
#include "../taurus_internal.h"
#include <string.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Get the compact allocator from a document
 */
static inline CompactSingleAllocator* get_compact_alloc(struct taurus_document* doc) {
    return doc ? (CompactSingleAllocator*)doc->compact_alloc : NULL;
}

/**
 * Get the compact base pointer from a document
 */
static inline char* get_compact_base(struct taurus_document* doc) {
    CompactSingleAllocator* alloc = get_compact_alloc(doc);
    return alloc ? alloc->base : NULL;
}

/* ============================================================================
 * Compact Element Accessors
 * ============================================================================ */

/**
 * Get element name from compact element
 */
const char* compact_element_get_name(struct compact_element* elem, struct taurus_document* doc) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return (const char*)COMPACT_OFFSET_TO_PTR(base, elem->name_offset);
}

/**
 * Get first child of compact element
 */
struct compact_element* compact_element_get_first_child(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return COMPACT_FIRST_CHILD(base, elem);
}

/**
 * Get next sibling of compact element
 */
struct compact_element* compact_element_get_next_sibling(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return COMPACT_NEXT_SIBLING(base, elem);
}

/**
 * Get parent of compact element
 */
struct compact_element* compact_element_get_parent(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return COMPACT_PARENT(base, elem);
}

/**
 * Get last child of compact element
 */
struct compact_element* compact_element_get_last_child(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc || elem->child_count == 0) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* Walk to last child */
    struct compact_element* child = COMPACT_FIRST_CHILD(base, elem);
    struct compact_element* last = child;

    while (child) {
        last = child;
        child = COMPACT_NEXT_SIBLING(base, child);
    }

    return last;
}

/**
 * Get child count of compact element
 */
uint16_t compact_element_get_child_count(struct compact_element* elem) {
    return elem ? elem->child_count : 0;
}

/**
 * Get attribute count of compact element
 */
uint16_t compact_element_get_attr_count(struct compact_element* elem) {
    return elem ? elem->attr_count : 0;
}

/**
 * Get first attribute of compact element
 */
struct compact_attribute* compact_element_get_first_attr(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return COMPACT_FIRST_ATTR(base, elem);
}

/**
 * Get attribute name from compact attribute
 */
const char* compact_attribute_get_name(
    struct compact_attribute* attr,
    struct taurus_document* doc
) {
    if (!attr || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return (const char*)COMPACT_OFFSET_TO_PTR(base, attr->name_offset);
}

/**
 * Get attribute value from compact attribute
 */
const char* compact_attribute_get_value(
    struct compact_attribute* attr,
    struct taurus_document* doc
) {
    if (!attr || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return (const char*)COMPACT_OFFSET_TO_PTR(base, attr->value_offset);
}

/**
 * Find attribute by name in compact element
 */
struct compact_attribute* compact_element_find_attr(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
) {
    if (!elem || !doc || !name) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    struct compact_attribute* attr = COMPACT_FIRST_ATTR(base, elem);

    while (attr) {
        const char* attr_name = (const char*)COMPACT_OFFSET_TO_PTR(base, attr->name_offset);
        if (attr_name && strcmp(attr_name, name) == 0) {
            return attr;
        }
        attr = (struct compact_attribute*)COMPACT_OFFSET_TO_PTR(base, attr->next_attr);
    }

    return NULL;
}

/**
 * Get attribute value by name from compact element
 */
const char* compact_element_get_attr_value(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
) {
    struct compact_attribute* attr = compact_element_find_attr(elem, doc, name);
    return compact_attribute_get_value(attr, doc);
}

/* ============================================================================
 * Child Iteration Helpers
 * ============================================================================ */

/**
 * Get child by index (O(n) walk, but cache-friendly)
 */
struct compact_element* compact_element_get_child_by_index(
    struct compact_element* elem,
    struct taurus_document* doc,
    uint16_t index
) {
    if (!elem || !doc || index >= elem->child_count) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    struct compact_element* child = COMPACT_FIRST_CHILD(base, elem);
    for (uint16_t i = 0; i < index && child; i++) {
        child = COMPACT_NEXT_SIBLING(base, child);
    }

    return child;
}

/**
 * Find first child by name
 */
struct compact_element* compact_element_find_child_by_name(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
) {
    if (!elem || !doc || !name) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    struct compact_element* child = COMPACT_FIRST_CHILD(base, elem);
    while (child) {
        const char* child_name = (const char*)COMPACT_OFFSET_TO_PTR(base, child->name_offset);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = COMPACT_NEXT_SIBLING(base, child);
    }

    return NULL;
}

/* ============================================================================
 * Namespace Accessors
 * ============================================================================ */

/**
 * Get namespace URI from compact element
 */
const char* compact_element_get_namespace(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    if (!(elem->flags & COMPACT_HAS_NAMESPACE)) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return (const char*)COMPACT_OFFSET_TO_PTR(base, elem->namespace_offset);
}

/* ============================================================================
 * Document Root Accessor
 * ============================================================================ */

/**
 * Get root compact element from document
 */
struct compact_element* compact_document_get_root(struct taurus_document* doc) {
    if (!doc || !doc->is_compact || doc->compact_root_offset == 0) return NULL;

    CompactSingleAllocator* alloc = get_compact_alloc(doc);
    if (!alloc) return NULL;

    return COMPACT_SINGLE_GET_TYPED(alloc, struct compact_element, doc->compact_root_offset);
}

/* ============================================================================
 * Dispatch Layer Helpers
 * ============================================================================ */

/**
 * Check if document is in compact mode
 */
int compact_is_compact_mode(struct taurus_document* doc) {
    return doc && doc->is_compact;
}

/**
 * Get the compact element from a TaurusElement handle
 *
 * Uses the compact_offset field in the element structure to locate
 * the corresponding compact element within the document's compact block.
 */
struct compact_element* compact_from_element(
    struct taurus_element* elem,
    struct taurus_document* doc
) {
    if (!elem || !compact_is_compact_mode(doc)) return NULL;

    CompactSingleAllocator* alloc = get_compact_alloc(doc);
    if (!alloc) return NULL;

    /* Use compact_offset from element structure
     * If offset is 0, this is the root element */
    uint32_t offset = elem->compact_offset;
    if (offset == 0) {
        /* Root element - use document's root offset */
        offset = doc->compact_root_offset;
    }

    if (offset == 0) return NULL;

    return COMPACT_SINGLE_GET_TYPED(alloc, struct compact_element, offset);
}

/**
 * Get element text content from compact element
 * Returns NULL if element has no text content.
 *
 * Note: Current compact format doesn't store text content separately.
 * This is a placeholder for future implementation.
 */
const char* compact_element_get_text(
    struct compact_element* elem,
    struct taurus_document* doc
) {
    /* TODO: Implement text content retrieval from compact format */
    (void)elem;
    (void)doc;
    return NULL;
}
