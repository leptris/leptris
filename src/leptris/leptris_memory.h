/* libleptris - Memory management
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Internal memory management functions
 */

#ifndef LEPTRIS_MEMORY_H
#define LEPTRIS_MEMORY_H

#include "leptris_internal.h"

/* ============================================================================
 * Document Management
 * ============================================================================ */

/**
 * Create new document
 * @return Document or NULL on allocation failure
 */
struct leptris_document* leptris_document_new(void);

/**
 * Free document and all elements (internal implementation)
 * @param doc Document to free
 */
void leptris_document_free_internal(struct leptris_document* doc);

/* ============================================================================
 * Element Management
 * ============================================================================ */

/**
 * Create new element
 * @param name Element name (will be copied)
 * @return Element or NULL on allocation failure
 */
struct leptris_element* leptris_element_new(const char* name);

/**
 * Free element (non-recursive, doesn't free children)
 * @param elem Element to free
 */
void leptris_element_free_shallow(struct leptris_element* elem);

/**
 * Free element and entire subtree recursively
 * @param elem Element to free
 */
void leptris_element_free_tree(struct leptris_element* elem);

/**
 * Add child element to parent
 * @param parent Parent element
 * @param child Child element
 * @return 0 on success, -1 on allocation failure
 */
int leptris_element_add_child(struct leptris_element* parent, struct leptris_element* child);

/**
 * Add namespace declaration to element
 * @param elem Element
 * @param ns Namespace (ownership transferred to element)
 * @return 0 on success, -1 on allocation failure
 */
int leptris_element_add_namespace(struct leptris_element* elem, struct leptris_namespace* ns);

/* ============================================================================
 * Attribute Management
 * ============================================================================ */

/**
 * Create new attribute
 * @param name Attribute name (will be copied)
 * @param value Attribute value (will be copied, can be NULL)
 * @return Attribute or NULL on allocation failure
 */
struct leptris_attribute* leptris_attribute_new(const char* name, const char* value);

/**
 * Free attribute
 * @param attr Attribute to free
 */
void leptris_attribute_free(struct leptris_attribute* attr);

/* ============================================================================
 * Namespace Management
 * ============================================================================ */

/**
 * Create new namespace
 * @param prefix Namespace prefix (will be copied, NULL for default namespace)
 * @param uri Namespace URI (will be copied, required)
 * @return Namespace or NULL on allocation failure
 */
struct leptris_namespace* leptris_namespace_new(const char* prefix, const char* uri);

/**
 * Free namespace (non-recursive, doesn't free next)
 * @param ns Namespace to free
 */
void leptris_namespace_free_single(struct leptris_namespace* ns);

/**
 * Free namespace chain (recursive, frees entire linked list)
 * @param ns First namespace in chain
 */
void leptris_namespace_free_chain(struct leptris_namespace* ns);

/**
 * Find namespace by prefix in element (with inheritance)
 * @param elem Element to start search from
 * @param prefix Prefix to find (NULL for default namespace)
 * @return Namespace or NULL if not found
 */
struct leptris_namespace* leptris_namespace_find(struct leptris_element* elem, const char* prefix);

/* ============================================================================
 * XPath Memory Management
 * ============================================================================ */

/**
 * Create new XPath nodeset
 * @return Nodeset or NULL on allocation failure
 */
XPathNodeSet* leptris_xpath_nodeset_new(void);

/**
 * Create new XPath nodeset with initial capacity
  * @param capacity Initial capacity
 * @return Nodeset or NULL on allocation failure
 */
XPathNodeSet* leptris_xpath_nodeset_new_with_capacity(size_t capacity);

/**
 * Add node to nodeset
 * @param nodeset Nodeset
 * @param node Element to add
 * @return 0 on success, -1 on allocation failure
 */
int leptris_xpath_nodeset_add(XPathNodeSet* nodeset, struct leptris_element* node);

/**
 * Free nodeset (doesn't free the elements themselves)
 * @param nodeset Nodeset to free
 */
void leptris_xpath_nodeset_free(XPathNodeSet* nodeset);

/**
 * Create new XPath result
 * @param type Result type
 * @return Result or NULL on allocation failure
 */
struct leptris_xpath_result* leptris_xpath_result_new(XPathResultType type);

/**
 * Free XPath result (frees owned data)
 * @param result Result to free
 */
void leptris_xpath_result_free_internal(struct leptris_xpath_result* result);

/* ============================================================================
 * Processing Instruction Management
 * ============================================================================ */

/**
 * Create new processing instruction
 * @param target PI target (will be copied)
 * @param data PI data (will be copied)
 * @return Processing instruction or NULL on allocation failure
 */
struct leptris_processing_instruction* leptris_pi_new(const char* target, const char* data);

/* leptris_pi_free / leptris_pi_free_chain for doc-level PIs were declared
 * here historically but never implemented (doc-level PIs are malloc'd/
 * freed inline in direct_parse.c and leptris.c). The names collide with
 * the tree-node version in dom/pi.h under the amalgamation build (TODO 170).
 * Dead declarations removed. */

#endif /* LEPTRIS_MEMORY_H */