/* evaluator_internal.h - Internal declarations for XPath evaluator
 * Copyright (c) 2024, Ribose Inc.
 *
 * Shared declarations between evaluator implementation files.
 */

#ifndef XPATH_EVALUATOR_INTERNAL_H
#define XPATH_EVALUATOR_INTERNAL_H

#include "evaluator.h"
#include "parser.h"

/* Forward declarations for internal functions */

/* From evaluator_types.c */
int xpath_to_boolean(struct taurus_xpath_result* result);
double xpath_to_number(struct taurus_xpath_result* result);
char* xpath_to_string(struct taurus_xpath_result* result);
char* get_node_text(void* node);

/* Optimized text access for comparisons - returns pointer to internal data
 * DOES NOT allocate memory. Returns pointer to internal string or NULL.
 * For elements, returns first text child's content if available.
 * For attributes, returns the attribute value directly.
 * IMPORTANT: Result is NOT null-terminated if it's element text content.
 * Use get_node_text_len() to get the length. */
const char* get_node_text_direct(void* node, size_t* out_len);

/* Quick length check for node text - avoids full text extraction */
size_t get_node_text_len(void* node);

/* Fast comparison of nodeset's string value with a literal (NO ALLOCATION)
 * PERFORMANCE: O(1) length check + O(n) memcmp for single-node nodesets
 * Returns: 1 if equal, 0 if not equal */
int xpath_nodeset_equals_string(XPathNodeSet* nodeset, const char* str, size_t str_len);

/* Fast check if nodeset's string value matches a boolean (NO ALLOCATION)
 * PERFORMANCE: Just checks if nodeset is non-empty
 * Returns: 1 if nodeset is non-empty (truthy), 0 if empty */
int xpath_nodeset_to_boolean(XPathNodeSet* nodeset);

/* Hash-based nodeset-string comparison with early exit (libxml2 strategy)
 * PERFORMANCE: O(1) hash comparison before O(n) string comparison
 * neq: 0 for equals (=), 1 for not-equals (!=)
 * Returns: 1 if match, 0 if no match */
int xpath_nodeset_equals_string_hash(XPathNodeSet* nodeset, const char* str, int neq);

/* Fast hash of node's text content - NO ALLOCATION
 * Returns hash based on first two characters of node's string value */
unsigned int xpath_node_val_hash(void* node);

/* From evaluator_axes.c */
XPathNodeSet* apply_axis(XPathContext* ctx, TaurusElement node,
                         const char* axis_name, XPathASTNode* test);

/* From evaluator_operators.c */
struct taurus_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast);

/* From evaluator_path.c */
struct taurus_xpath_result* evaluate_location_path(XPathContext* ctx,
                                                   XPathASTNode* path);
struct taurus_xpath_result* evaluate_step(XPathContext* ctx,
                                          XPathASTNode* step,
                                          XPathNodeSet* input);
int matches_node_test(XPathContext* ctx, TaurusElement node,
                     XPathASTNode* test);
XPathNodeSet* apply_predicates(XPathContext* ctx, XPathNodeSet* nodes,
                               XPathASTNode** predicates, size_t pred_count);

/* Main expression evaluator (in evaluator.c) */
struct taurus_xpath_result* evaluate_expr(XPathContext* ctx, XPathASTNode* ast);

/* PERFORMANCE: Direct boolean evaluation - NO ALLOCATION
 * This is the KEY optimization that gives libxml2 its predicate performance.
 * Evaluates expression directly to boolean without creating result objects.
 *
 * is_predicate: 1 if evaluating a predicate (enables position semantics for numbers)
 *
 * Returns: 1 if true, 0 if false, -1 on error
 */
int evaluate_expr_to_boolean(XPathContext* ctx, XPathASTNode* ast, int is_predicate);

/* Namespace support (in evaluator.c) */
const char* xpath_context_resolve_prefix(XPathContext* context,
                                         const char* prefix);

#endif /* XPATH_EVALUATOR_INTERNAL_H */