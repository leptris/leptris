/* ptr_accessor.h - Pointer-Based Element Accessor Functions
 * Copyright (c) 2026, Ribose Inc.
 *
 * TRIVIAL accessor functions - direct pointer returns!
 *
 * Compare with compact_accessor.h:
 * - compact: return page_base + elem->offset (2 operations)
 * - pointer: return elem->field (1 operation)
 *
 * This 50% reduction in accessor overhead is why ptr_element is faster.
 */

#ifndef TAURUS_PTR_ACCESSOR_H
#define TAURUS_PTR_ACCESSOR_H

#include "ptr_element.h"
#include <stddef.h>

/* Forward declaration */
struct taurus_document;

/* ============================================================================
 * Element Name Accessors
 * ============================================================================ */

/**
 * Get element name - DIRECT pointer return, no calculation!
 */
const char* ptr_element_get_name(struct ptr_element* elem);

/**
 * Get element name length
 */
size_t ptr_element_get_name_length(struct ptr_element* elem);

/* ============================================================================
 * Element Navigation Accessors
 * ============================================================================ */

/**
 * Get first child element (skip text nodes)
 */
struct ptr_element* ptr_element_get_first_child(struct ptr_element* elem);

/**
 * Get next sibling element (skip text nodes)
 */
struct ptr_element* ptr_element_get_next_sibling(struct ptr_element* elem);

/**
 * Get parent element
 */
struct ptr_element* ptr_element_get_parent(struct ptr_element* elem);

/**
 * Get last child element
 */
struct ptr_element* ptr_element_get_last_child(struct ptr_element* elem);

/* ============================================================================
 * Attribute Accessors
 * ============================================================================ */

/**
 * Get attribute name
 */
const char* ptr_attribute_get_name(struct ptr_attribute* attr);

/**
 * Get attribute value
 */
const char* ptr_attribute_get_value(struct ptr_attribute* attr);

/**
 * Get first attribute
 */
struct ptr_attribute* ptr_element_get_first_attr(struct ptr_element* elem);

/**
 * Find attribute by name
 */
struct ptr_attribute* ptr_element_find_attr(struct ptr_element* elem, const char* name);

/**
 * Get attribute value by name
 */
const char* ptr_element_get_attr_value(struct ptr_element* elem, const char* name);

/**
 * Get attribute count
 */
size_t ptr_element_get_attr_count(struct ptr_element* elem);

/* ============================================================================
 * Child Element Accessors
 * ============================================================================ */

/**
 * Get child element count
 */
size_t ptr_element_get_child_count(struct ptr_element* elem);

/**
 * Get child by index (O(n) walk)
 */
struct ptr_element* ptr_element_get_child_by_index(struct ptr_element* elem, size_t index);

/**
 * Find first child by name
 */
struct ptr_element* ptr_element_find_child_by_name(struct ptr_element* elem, const char* name);

/* ============================================================================
 * Text Content Accessors
 * ============================================================================ */

/**
 * Get text content from element
 */
const char* ptr_element_get_text(struct ptr_element* elem);

/* ============================================================================
 * Document Root Accessor
 * ============================================================================ */

/**
 * Get root element from document
 */
struct ptr_element* ptr_document_get_root(struct taurus_document* doc);

/* ============================================================================
 * Node Type Accessor
 * ============================================================================ */

/**
 * Get node type (0=element, 1=text, 2=cdata, 3=comment, 4=pi)
 */
int ptr_node_get_type(struct ptr_node* node);

#endif /* TAURUS_PTR_ACCESSOR_H */
