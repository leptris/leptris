/* compact_accessor.h - Compact Element Accessor Functions
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

#ifndef TAURUS_COMPACT_ACCESSOR_H
#define TAURUS_COMPACT_ACCESSOR_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct taurus_document;
struct taurus_element;
struct compact_element;
struct compact_attribute;

/* ============================================================================
 * Compact Element Accessors
 * ============================================================================ */

/**
 * Get element name from compact element
 */
const char* compact_element_get_name(struct compact_element* elem, struct taurus_document* doc);

/**
 * Get first child of compact element
 */
struct compact_element* compact_element_get_first_child(
    struct compact_element* elem,
    struct taurus_document* doc
);

/**
 * Get next sibling of compact element
 */
struct compact_element* compact_element_get_next_sibling(
    struct compact_element* elem,
    struct taurus_document* doc
);

/**
 * Get parent of compact element
 */
struct compact_element* compact_element_get_parent(
    struct compact_element* elem,
    struct taurus_document* doc
);

/**
 * Get last child of compact element
 */
struct compact_element* compact_element_get_last_child(
    struct compact_element* elem,
    struct taurus_document* doc
);

/**
 * Get child count of compact element
 */
uint16_t compact_element_get_child_count(struct compact_element* elem);

/**
 * Get attribute count of compact element
 */
uint16_t compact_element_get_attr_count(struct compact_element* elem);

/**
 * Get first attribute of compact element
 */
struct compact_attribute* compact_element_get_first_attr(
    struct compact_element* elem,
    struct taurus_document* doc
);

/**
 * Get attribute name from compact attribute
 */
const char* compact_attribute_get_name(
    struct compact_attribute* attr,
    struct taurus_document* doc
);

/**
 * Get attribute value from compact attribute
 */
const char* compact_attribute_get_value(
    struct compact_attribute* attr,
    struct taurus_document* doc
);

/**
 * Find attribute by name in compact element
 */
struct compact_attribute* compact_element_find_attr(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
);

/**
 * Get attribute value by name from compact element
 */
const char* compact_element_get_attr_value(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
);

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
);

/**
 * Find first child by name
 */
struct compact_element* compact_element_find_child_by_name(
    struct compact_element* elem,
    struct taurus_document* doc,
    const char* name
);

/* ============================================================================
 * Namespace Accessors
 * ============================================================================ */

/**
 * Get namespace URI from compact element
 */
const char* compact_element_get_namespace(
    struct compact_element* elem,
    struct taurus_document* doc
);

/* ============================================================================
 * Document Root Accessor
 * ============================================================================ */

/**
 * Get root compact element from document
 */
struct compact_element* compact_document_get_root(struct taurus_document* doc);

/* ============================================================================
 * Dispatch Layer Helpers
 * ============================================================================ */

/**
 * Check if document is in compact mode
 */
int compact_is_compact_mode(struct taurus_document* doc);

/**
 * Get the compact element from a TaurusElement handle
 *
 * Uses the compact_offset field in the element structure to locate
 * the corresponding compact element within the document's compact block.
 */
struct compact_element* compact_from_element(
    struct taurus_element* elem,
    struct taurus_document* doc
);

/**
 * Get element text content from compact element
 * Returns NULL if element has no text content.
 */
const char* compact_element_get_text(
    struct compact_element* elem,
    struct taurus_document* doc
);

#endif /* TAURUS_COMPACT_ACCESSOR_H */
