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

/* Fast inline nodeset_add (TODO 135). Internal-only; callers must
 * guarantee well-formed nodeset. See evaluator.c for the contract. */
void xpath_nodeset_add_fast(struct xpath_nodeset* nodeset, void* node);

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

/* Direct function-call entry for the VM (TODO 120 Phase F).
 * Identical semantics to invoking evaluate_expr on a FUNCTION_CALL
 * AST, but skips the AST-type switch in evaluate_expr. The handler
 * evaluates arguments itself via evaluate_expr, so callers must not
 * pre-evaluate args. */
struct taurus_xpath_result* evaluate_function_call_inline(XPathContext* ctx,
                                                           XPathASTNode* ast);

/* Bytecode VM entry point (TODO 120 Phase D — in vm.c).
 * Compiles the AST to bytecode and runs the VM interpreter.
 * For literals, avoids the AST dispatch overhead.
 * For complex expressions, delegates to evaluate_expr via
 * BC_FALLBACK_EVAL. */
struct taurus_xpath_result* taurus_xpath_vm_eval(XPathASTNode* ast,
                                                  XPathContext* ctx);

/* Run an already-compiled bytecode (TODO 120 Phase F). Avoids the
 * compile cost when the caller has cached the bytecode (typically
 * via xpath_ast_cache_get_bc / xpath_ast_cache_store_bc). */
struct TaurusXPathBytecode;  /* forward; full definition in bytecode.h */
struct taurus_xpath_result* taurus_xpath_vm_run_bc(struct TaurusXPathBytecode* bc,
                                                    XPathContext* ctx);

/* Namespace support (in evaluator.c) */
const char* xpath_context_resolve_prefix(XPathContext* context,
                                         const char* prefix);

#endif /* XPATH_EVALUATOR_INTERNAL_H */