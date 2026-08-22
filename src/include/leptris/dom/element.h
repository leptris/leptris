/* libleptris - Element Query Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains element query operations (read-only access to elements).
 * For element modification operations, see element_modify.h
 */

#ifndef LEPTRIS_DOM_ELEMENT_H
#define LEPTRIS_DOM_ELEMENT_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef LEPTRIS_API
#  ifdef LEPTRIS_FOR_BINDGEN
#    define LEPTRIS_API
#  elif defined(_WIN32)
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
       /* Mirrors leptris.h (issue #278): CMake defines
        * LEPTRIS_BUILDING_DLL on the objects that build the DLL. */
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif

/* ============================================================================
 * Element Information Operations
 * ============================================================================ */

/**
 * Get element name
 *
 * @param elem Element
 * @return Element name or NULL if elem is NULL
 *
 * Memory: String is owned by element. Do not free or modify.
 */
LEPTRIS_API const char* leptris_element_name(LeptrisElement elem);

/**
 * Get element text content (concatenation of all text nodes)
 *
 * @param elem Element
 * @return Text content, or "" if elem is NULL or has no text
 *
 * Memory: String is owned by the document. Do not free or modify. It stays
 * valid until leptris_document_free(). When the element's only child is a text
 * or CDATA node the node's own storage is returned; mixed content is
 * concatenated into the document pool.
 */
LEPTRIS_API const char* leptris_element_text(LeptrisElement elem);

/**
 * Get element text content as integer
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid integer
 * @return Text content as integer, or default_value
 *
 * Converts element text content to int using strtol(). Supports decimal and hexadecimal (0x) formats.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
LEPTRIS_API int leptris_element_text_int(LeptrisElement elem, int default_value);

/**
 * Get element text content as unsigned integer
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid integer
 * @return Text content as unsigned integer, or default_value
 *
 * Converts element text content to unsigned int using strtoul(). Supports decimal and hexadecimal (0x) formats.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
LEPTRIS_API unsigned int leptris_element_text_uint(LeptrisElement elem, unsigned int default_value);

/**
 * Get element text content as double
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid number
 * @return Text content as double, or default_value
 *
 * Converts element text content to double using strtod().
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
LEPTRIS_API double leptris_element_text_double(LeptrisElement elem, double default_value);

/**
 * Get element text content as float
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid number
 * @return Text content as float, or default_value
 *
 * Converts element text content to float using strtof().
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
LEPTRIS_API float leptris_element_text_float(LeptrisElement elem, float default_value);

/**
 * Get element text content as boolean
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid boolean
 * @return Text content as boolean (1 for true, 0 for false), or default_value
 *
 * Parses text content as boolean. Accepts: "true", "1" (case-insensitive) for true;
 * "false", "0" (case-insensitive) for false.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
LEPTRIS_API int leptris_element_text_bool(LeptrisElement elem, int default_value);

/* ============================================================================
 * Attribute Query Operations
 * ============================================================================ */

/**
 * Get attribute value by name
 *
 * @param elem Element
 * @param name Attribute name
 * @return Attribute value or NULL if not found
 *
 * Memory: String is owned by element. Do not free or modify.
 */
LEPTRIS_API const char* leptris_element_attribute(LeptrisElement elem, const char* name);

/**
 * Get attribute value as integer
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as integer, or default_value if not found
 *
 * Converts attribute value to int using atoi(). Returns default_value
 * if attribute doesn't exist or value is empty.
 */
LEPTRIS_API int leptris_element_attribute_int(LeptrisElement elem, const char* name, int default_value);

/**
 * Get attribute value as double
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as double, or default_value if not found
 *
 * Converts attribute value to double using atof(). Returns default_value
 * if attribute doesn't exist or value is empty.
 */
LEPTRIS_API double leptris_element_attribute_double(LeptrisElement elem, const char* name, double default_value);

/**
 * Get attribute value as boolean
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as boolean, or default_value if not found
 *
 * Returns true if attribute exists and value is "true" or "1" (case-insensitive).
 * Returns false if attribute exists and value is "false" or "0" (case-insensitive).
 * Returns default_value if attribute doesn't exist.
 */
LEPTRIS_API int leptris_element_attribute_bool(LeptrisElement elem, const char* name, int default_value);

/**
 * Get attribute value as unsigned integer
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as unsigned int, or default_value if not found
 *
 * Converts attribute value to unsigned int using strtoul(). Returns default_value
 * if attribute doesn't exist or value is empty/invalid.
 */
LEPTRIS_API unsigned int leptris_element_attribute_uint(LeptrisElement elem, const char* name, unsigned int default_value);

/**
 * Get attribute value as float
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as float, or default_value if not found
 *
 * Converts attribute value to float using strtof(). Returns default_value
 * if attribute doesn't exist or value is empty/invalid.
 */
LEPTRIS_API float leptris_element_attribute_float(LeptrisElement elem, const char* name, float default_value);

/**
 * Get attribute value as string with default
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value, or default_value if not found
 *
 * Memory: String is owned by element. Do not free or modify.
 * The default_value string must remain valid for the duration of use.
 */
LEPTRIS_API const char* leptris_element_attribute_string(LeptrisElement elem, const char* name, const char* default_value);

/* ============================================================================
 * Child Element Query Operations
 * ============================================================================ */

/**
 * Get number of child elements
 *
 * @param elem Element
 * @return Number of children or 0 if elem is NULL
 */
LEPTRIS_API size_t leptris_element_child_count(LeptrisElement elem);

/**
 * Get child element by index
 *
 * @param elem Element
 * @param index Child index (0-based)
 * @return Child element or NULL if index out of bounds
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_child(LeptrisElement elem, size_t index);

/**
 * Get parent element
 *
 * @param elem Element
 * @return Parent element or NULL if elem is root or NULL
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_parent(LeptrisElement elem);

/**
 * Get root element from any element in the document
 *
 * @param elem Any element in the document
 * @return Root element of the document, or NULL if elem is NULL or not in a document
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_root(LeptrisElement elem);

/**
 * Get text value of first child text node
 *
 * @param elem Element
 * @return Text content of first child text node, or NULL if no text child
 *
 * Returns the text content of the first child text node.
 * If the element has no children or the first child is not a text node, returns NULL.
 *
 * Memory: String is owned by element. Do not free or modify.
 */
LEPTRIS_API const char* leptris_element_child_value(LeptrisElement elem);

/**
 * Get hash value of element for comparison
 *
 * @param elem Element
 * @return Hash value based on element name and attributes
 *
 * Computes a hash value for the element based on its name and attributes.
 * This can be used for quick comparison between elements.
 * Returns 0 if elem is NULL.
 */
LEPTRIS_API size_t leptris_element_hash_value(LeptrisElement elem);

/* ============================================================================
 * Element Search Operations
 * ============================================================================ */

/**
 * Find first child element with given tag name
 *
 * @param elem Element to search in
 * @param name Tag name to find
 * @return First matching child element or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_find_child(LeptrisElement elem, const char* name);

/**
 * Find first child with given name and attribute value
 *
 * @param elem Element to search in
 * @param child_name Child tag name (NULL to match any tag)
 * @param attr_name Attribute name to check
 * @param attr_value Attribute value to match
 * @return First matching child element or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_find_child_by_attr(LeptrisElement elem,
                                                             const char* child_name,
                                                             const char* attr_name,
                                                             const char* attr_value);

/**
 * Get next sibling element with specified name
 *
 * @param elem Element to start from
 * @param name Element name to find (NULL to get next sibling regardless of name)
 * @return Next sibling with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_next_sibling(LeptrisElement elem, const char* name);

/**
 * Get previous sibling element with specified name
 *
 * @param elem Element to start from
 * @param name Element name to find (NULL to get previous sibling regardless of name)
 * @return Previous sibling with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_previous_sibling(LeptrisElement elem, const char* name);

/**
 * Get first child element with specified name
 *
 * @param elem Element to search in
 * @param name Element name to find (NULL to get first child regardless of name)
 * @return First child with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_first_child(LeptrisElement elem, const char* name);

/**
 * Get last child element with specified name
 *
 * @param elem Element to search in
 * @param name Element name to find (NULL to get last child regardless of name)
 * @return Last child with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_last_child(LeptrisElement elem, const char* name);

/**
 * Get first child element regardless of name
 *
 * @param elem Element to search in
 * @return First child element or NULL if elem has no children
 *
 * Convenience function that returns the first child element
 * regardless of its name. Same as leptris_element_first_child(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_first_child_any(LeptrisElement elem);

/**
 * Get last child element regardless of name
 *
 * @param elem Parent element
 * @return Last child element or NULL if elem has no children
 *
 * Convenience function that returns the last child element
 * regardless of its name.
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_last_child_any(LeptrisElement elem);

/**
 * Get next sibling element regardless of name
 *
 * @param elem Element to start from
 * @return Next sibling element or NULL if elem is last child
 *
 * Convenience function that returns the next sibling element
 * regardless of its name. Same as leptris_element_next_sibling(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_next_sibling_any(LeptrisElement elem);

/**
 * Get previous sibling element regardless of name
 *
 * @param elem Current element
 * @return Previous sibling element, or NULL if not found
 *
 * Convenience function that returns the previous sibling element
 * regardless of its name. Same as leptris_element_previous_sibling(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_element_previous_sibling_any(LeptrisElement elem);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DOM_ELEMENT_H */
