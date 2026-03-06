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
 *
 * V2: Uses 16-byte compact_element_v2 structures (null-terminated strings)
 */

#ifndef TAURUS_COMPACT_ACCESSOR_H
#define TAURUS_COMPACT_ACCESSOR_H

#include <stdint.h>
#include <stddef.h>
#include "compact_element.h"  /* For struct compact_element_v2 (16-byte) */

/* Forward declarations */
struct taurus_document;
struct taurus_element;

/* ============================================================================
 * Compact Element V2 Accessors - NULL-TERMINATED STRINGS
 * ============================================================================ */

/**
 * Get element name from compact element v2
 * NULL-TERMINATED: Name is null-terminated in v2 format
 */
const char* compact_element_get_name(struct compact_element_v2* elem, struct taurus_document* doc);

/**
 * Get element name length (calculated via strlen for v2)
 */
uint16_t compact_element_get_name_length(struct compact_element_v2* elem);

/**
 * Get element name with length (most efficient)
 */
const char* compact_element_get_name_ex(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    uint16_t* out_length
);

/**
 * Get first child of compact element v2 (skipping attributes)
 */
struct compact_element_v2* compact_element_get_first_child(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/**
 * Get next sibling of compact element v2
 */
struct compact_element_v2* compact_element_get_next_sibling(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/**
 * Get parent of compact element v2
 */
struct compact_element_v2* compact_element_get_parent(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/**
 * Get last child of compact element v2
 */
struct compact_element_v2* compact_element_get_last_child(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/**
 * Get child count of compact element v2 (calculated on demand)
 */
uint16_t compact_element_get_child_count(struct compact_element_v2* elem);

/**
 * Get attribute count of compact element v2
 */
uint16_t compact_element_get_attr_count(struct compact_element_v2* elem);

/**
 * Get first attribute of compact element v2
 */
struct compact_attribute_v2* compact_element_get_first_attr(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/* ============================================================================
 * Compact Attribute V2 Accessors - NULL-TERMINATED STRINGS
 * ============================================================================ */

/**
 * Get attribute name from compact attribute v2
 * NULL-TERMINATED: Reads from xml_buffer
 */
const char* compact_attribute_get_name(
    struct compact_attribute_v2* attr,
    struct taurus_document* doc
);

/**
 * Get attribute name length (calculated via strlen for v2)
 */
uint16_t compact_attribute_get_name_length(struct compact_attribute_v2* attr);

/**
 * Get attribute value from compact attribute v2
 * NULL-TERMINATED: Reads from xml_buffer
 */
const char* compact_attribute_get_value(
    struct compact_attribute_v2* attr,
    struct taurus_document* doc
);

/**
 * Get attribute value length (calculated via strlen for v2)
 */
uint16_t compact_attribute_get_value_length(struct compact_attribute_v2* attr);

/**
 * Find attribute by name in compact element v2
 */
struct compact_attribute_v2* compact_element_find_attr(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
);

/**
 * Get attribute value by name from compact element v2
 */
const char* compact_element_get_attr_value(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
);

/* ============================================================================
 * Child Iteration Helpers - V2
 * ============================================================================ */

/**
 * Get child by index (O(n) walk, but cache-friendly)
 */
struct compact_element_v2* compact_element_get_child_by_index(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    uint16_t index
);

/**
 * Find first child by name - V2
 */
struct compact_element_v2* compact_element_find_child_by_name(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
);

/* ============================================================================
 * Namespace Accessors - V2
 * ============================================================================ */

/**
 * Get namespace URI from compact element v2
 */
const char* compact_element_get_namespace(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/* ============================================================================
 * Document Root Accessor - V2
 * ============================================================================ */

/**
 * Get root compact element v2 from document - COMPACT-ONLY
 */
struct compact_element_v2* compact_document_get_root(struct taurus_document* doc);

/* ============================================================================
 * Pointer-Only Helpers
 * ============================================================================ */

/**
 * Get the compact element v2 from a TaurusElement handle - COMPACT-ONLY
 *
 * Uses the compact_offset field in the element structure to locate
 * the corresponding compact element within the document's compact block.
 */
struct compact_element_v2* compact_from_element(
    struct taurus_element* elem,
    struct taurus_document* doc
);

/**
 * Get element text content from compact element v2
 * Returns NULL if element has no text content.
 */
const char* compact_element_get_text(
    struct compact_element_v2* elem,
    struct taurus_document* doc
);

/* ============================================================================
 * Wrapper Cache Functions
 * ============================================================================ */

/**
 * Get or create a wrapper element for a compact element
 *
 * This function returns a cached wrapper if one exists, or creates a new
 * wrapper and caches it. The wrapper is a ptr_element struct that
 * references the compact element's data via offsets.
 *
 * @param doc Document containing the compact element
 * @param offset Offset of the compact element in the compact block
 * @return Wrapper element, or NULL on error
 */
struct ptr_element* compact_get_or_create_wrapper(
    struct taurus_document* doc,
    uint32_t offset
);

#endif /* TAURUS_COMPACT_ACCESSOR_H */
