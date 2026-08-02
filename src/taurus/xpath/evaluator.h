/* evaluator.h - XPath evaluator API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 evaluator.
 * Provides evaluation of XPath expressions against DOM trees.
 */

#ifndef XPATH_EVALUATOR_H
#define XPATH_EVALUATOR_H

#include "../taurus_internal.h"
#include "parser.h"

/* Forward declaration for TaurusElement */
typedef struct taurus_element* TaurusElement;

/* ============================================================================
 * Types from taurus_internal.h
 * ============================================================================ */

/* XPathContext, XPathNodeSet, XPathResult are defined in taurus_internal.h */

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * Create new XPath evaluation context
 *
 * @param document Document to evaluate against
 * @param context_node Current context node
 * @return New context, or NULL on error
 */
XPathContext* xpath_context_new(struct taurus_document* document,
                                TaurusElement context_node);

/**
 * Free XPath context
 *
 * @param context Context to free
 */
void xpath_context_free(XPathContext* context);

/**
 * Get error message from context
 *
 * @param context Context to query
 * @return Error message, or NULL if no error
 */
const char* xpath_context_error(XPathContext* context);

/* ============================================================================
 * NodeSet Management
 * ============================================================================ */

/**
 * Create new nodeset
 *
 * @return New nodeset, or NULL on error
 */
XPathNodeSet* xpath_nodeset_new(void);

/**
 * Create new nodeset with pre-allocated capacity
 *
 * @param capacity Initial capacity
 * @return New nodeset, or NULL on error
 */
XPathNodeSet* xpath_nodeset_new_with_capacity(size_t capacity);

/**
 * Free nodeset
 *
 * @param nodeset Nodeset to free
 */
void xpath_nodeset_free(XPathNodeSet* nodeset);

/**
 * Get nodeset count
 *
 * @param nodeset Nodeset to query
 * @return Number of nodes in set
 */
size_t xpath_nodeset_count(XPathNodeSet* nodeset);

/**
 * Get node from nodeset (returns typed node pointer)
 *
 * @param nodeset Nodeset to query
 * @param index Index of node (0-based)
 * @return Typed node pointer (void*), or NULL if out of bounds
 * @note Cast to appropriate type using XPATH_NODE_TYPE macro
 */
void* xpath_nodeset_get(XPathNodeSet* nodeset, size_t index);

/**
 * Add typed node to nodeset
 *
 * @param nodeset Nodeset to add to
 * @param node Typed node pointer to add (element or attribute)
 */
void xpath_nodeset_add(XPathNodeSet* nodeset, void* node);

/* ============================================================================
 * Result Management
 * ============================================================================ */

/**
 * Create new XPath result
 *
 * @param type Result type
 * @return New result, or NULL on error
 */
struct taurus_xpath_result* xpath_result_new(XPathResultType type);

/**
 * Free XPath result
 *
 * @param result Result to free
 */
void xpath_result_free(struct taurus_xpath_result* result);

/* ============================================================================
 * Type Conversions (XPath 1.0 spec section 4)
 * ============================================================================ */

/**
 * Convert result to boolean
 *
 * @param result Result to convert
 * @return Boolean value (0 or 1)
 */
int xpath_to_boolean(struct taurus_xpath_result* result);

/**
 * Convert result to number
 *
 * @param result Result to convert
 * @return Number value (NAN if conversion fails)
 */
double xpath_to_number(struct taurus_xpath_result* result);

/**
 * Convert result to string
 *
 * @param result Result to convert
 * @return String value (caller must free), or NULL on error
 */
char* xpath_to_string(struct taurus_xpath_result* result);

/* ============================================================================
 * Evaluation
 * ============================================================================ */

/**
 * Evaluate XPath expression
 *
 * @param context Evaluation context
 * @param ast Parsed AST to evaluate
 * @return Result of evaluation, or NULL on error
 */
struct taurus_xpath_result* xpath_evaluate(XPathContext* context,
                                           XPathASTNode* ast);

#endif /* XPATH_EVALUATOR_H */