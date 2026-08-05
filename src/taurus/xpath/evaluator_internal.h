/* evaluator_internal.h - Internal declarations for XPath evaluator
 * Copyright (c) 2024, Ribose Inc.
 *
 * Shared declarations between evaluator implementation files.
 */

#ifndef XPATH_EVALUATOR_INTERNAL_H
#define XPATH_EVALUATOR_INTERNAL_H

#include "evaluator.h"
#include "parser.h"
#include "../dom/node.h"  /* TaurusNode for matches_node_test (TODO 109) */

/* Forward declarations for internal functions */

/* From evaluator_types.c */
int xpath_to_boolean(struct taurus_xpath_result* result);
double xpath_to_number(struct taurus_xpath_result* result);
char* xpath_to_string(struct taurus_xpath_result* result);
char* get_node_text(void* node);

/* From evaluator_axes.c */
XPathNodeSet* apply_axis(XPathContext* ctx, TaurusNode* node,
                         const char* axis_name, XPathASTNode* test);
XPathAxisType xpath_axis_from_name(const char* name);

/* From evaluator_operators.c */
struct taurus_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast);

/* From evaluator_path.c */
struct taurus_xpath_result* evaluate_location_path(XPathContext* ctx,
                                                   XPathASTNode* path);
struct taurus_xpath_result* evaluate_step(XPathContext* ctx,
                                          XPathASTNode* step,
                                          XPathNodeSet* input);
int matches_node_test(XPathContext* ctx, TaurusNode* node,
                     XPathASTNode* test);
XPathNodeSet* apply_predicates(XPathContext* ctx, XPathNodeSet* nodes,
                               XPathASTNode** predicates, size_t pred_count);

/* Main expression evaluator (in evaluator.c) */
struct taurus_xpath_result* evaluate_expr(XPathContext* ctx, XPathASTNode* ast);

/* Namespace support (in evaluator.c) */
const char* xpath_context_resolve_prefix(XPathContext* context,
                                         const char* prefix);

#endif /* XPATH_EVALUATOR_INTERNAL_H */