/* evaluator_integrated.h - Integrated predicate evaluation for XPath
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-pass axis traversal with inline predicate evaluation.
 * This matches libxml2's xmlXPathNodeCollectAndTest() performance pattern.
 *
 * TRADITIONAL TWO-PASS:
 *   axis() → nodeset → apply_predicates() → filtered nodeset
 *
 * INTEGRATED SINGLE-PASS:
 *   axis_integrated() → traverse + test + predicate → filtered nodeset
 *   (no intermediate nodeset allocation)
 */

#ifndef XPATH_EVALUATOR_INTEGRATED_H
#define XPATH_EVALUATOR_INTEGRATED_H

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/ptr_element.h"  /* For struct ptr_attribute */

/* ============================================================================
 * Integrated Predicate Evaluation
 * ============================================================================ */

/* PERFORMANCE: Inline predicate evaluation - NO ALLOCATION
 *
 * Evaluates a predicate directly for a node without creating intermediate
 * result objects. This is the key optimization that gives libxml2 its
 * predicate performance.
 *
 * ctx: XPath evaluation context
 * node: The node to test (element or attribute)
 * predicate: The predicate AST node
 * position: 1-based position in the axis traversal
 *
 * Returns: 1 if predicate matches, 0 if not, -1 on error
 */
static inline int evaluate_predicate_inline(
    XPathContext* ctx,
    void* node,
    XPathASTNode* predicate,
    size_t position
);

/* ============================================================================
 * XPATH_TEST_HIT Macro
 * ============================================================================ */

/* PERFORMANCE: Test-Hit macro for integrated axis traversal
 *
 * This macro implements the core optimization pattern from libxml2:
 * 1. Test if node matches node test
 * 2. If match, evaluate predicates inline (no allocation)
 * 3. If all predicates pass, add to result
 * 4. If to_bool=1 and we have a match, early exit
 *
 * This eliminates the intermediate nodeset allocation that was the
 * performance bottleneck in the two-pass approach.
 *
 * Parameters:
 *   ctx: XPath evaluation context
 *   node: The node being tested
 *   result: The result nodeset to add matching nodes to
 *   predicates: Array of predicate AST nodes
 *   pred_count: Number of predicates
 *   position: 1-based position in traversal (for [n] predicates)
 *   to_bool: Whether this is a boolean context (enables early exit)
 *   matched: Output variable set to 1 if node was added, 0 otherwise
 *
 * Usage:
 *   int matched;
 *   XPATH_TEST_HIT(ctx, child, result, preds, n_preds, pos, 0, matched);
 *   if (matched && to_bool) return result;  // Early exit
 */
#define XPATH_TEST_HIT(ctx, node, result, predicates, pred_count, \
                       position, to_bool, matched) \
    do { \
        (matched) = 0; \
        int _all_match = 1; \
        for (size_t _p = 0; _p < (pred_count) && _all_match; _p++) { \
            int _pred_result = evaluate_predicate_inline( \
                (ctx), (node), (predicates)[_p], (position)); \
            if (_pred_result <= 0) { \
                _all_match = 0; \
            } \
        } \
        if (_all_match) { \
            xpath_nodeset_add((result), (node)); \
            (matched) = 1; \
        } \
    } while(0)

/* Same as XPATH_TEST_HIT but with early return for boolean context */
#define XPATH_TEST_HIT_EARLY_EXIT(ctx, node, result, predicates, pred_count, \
                                   position, to_bool, matched) \
    do { \
        (matched) = 0; \
        int _all_match = 1; \
        for (size_t _p = 0; _p < (pred_count) && _all_match; _p++) { \
            int _pred_result = evaluate_predicate_inline( \
                (ctx), (node), (predicates)[_p], (position)); \
            if (_pred_result <= 0) { \
                _all_match = 0; \
            } \
        } \
        if (_all_match) { \
            xpath_nodeset_add((result), (node)); \
            (matched) = 1; \
        } \
    } while(0)

/* For void functions - sets early_exit flag instead of returning */
#define XPATH_TEST_HIT_VOID(ctx, node, result, predicates, pred_count, \
                            position, to_bool, matched, early_exit) \
    do { \
        (matched) = 0; \
        int _all_match = 1; \
        for (size_t _p = 0; _p < (pred_count) && _all_match; _p++) { \
            int _pred_result = evaluate_predicate_inline( \
                (ctx), (node), (predicates)[_p], (position)); \
            if (_pred_result <= 0) { \
                _all_match = 0; \
            } \
        } \
        if (_all_match) { \
            xpath_nodeset_add((result), (node)); \
            (matched) = 1; \
            if (to_bool) { \
                *(early_exit) = 1; \
            } \
        } \
    } while(0)

/* ============================================================================
 * Integrated Axis Functions
 * ============================================================================ */

/* PERFORMANCE: Integrated child axis with inline predicate evaluation
 *
 * Combines axis traversal, node test, and predicate evaluation into
 * a single pass. No intermediate nodeset allocation.
 *
 * Returns: Nodeset with matching nodes (may be empty)
 */
XPathNodeSet* axis_child_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
);

/* PERFORMANCE: Integrated attribute axis with inline predicate evaluation
 *
 * Critical for [@attr='value'] patterns - checks attribute directly
 * without creating attribute nodes for non-matching attributes.
 */
XPathNodeSet* axis_attribute_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
);

/* PERFORMANCE: Integrated descendant axis with inline predicate evaluation */
XPathNodeSet* axis_descendant_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
);

/* PERFORMANCE: Integrated descendant-or-self axis with inline predicate evaluation */
XPathNodeSet* axis_descendant_or_self_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
);

/* ============================================================================
 * Step Evaluation Entry Point
 * ============================================================================ */

/* PERFORMANCE: Integrated step evaluation - single-pass predicate handling
 *
 * Main entry point for integrated evaluation. Checks if predicates are
 * simple enough for integrated processing, then dispatches to appropriate
 * integrated axis function, or falls back to traditional two-pass.
 *
 * Returns: Result nodeset, or NULL on error
 */
XPathNodeSet* evaluate_step_integrated(
    XPathContext* ctx,
    XPathASTNode* step,
    XPathNodeSet* input,
    int to_bool
);

/* Check if predicates can be evaluated using integrated path
 *
 * Returns: 1 if all predicates are simple, 0 if complex predicates present
 */
int can_use_integrated_evaluation(
    XPathASTNode** predicates,
    size_t pred_count
);

/* ============================================================================
 * Inline Predicate Evaluation Implementation
 * ============================================================================ */

/* Fast path for simple positional predicates [n]
 * Returns: 1 if position matches, 0 if not, -1 if not applicable
 */
static inline int evaluate_positional_predicate_inline(
    XPathASTNode* predicate,
    size_t position
) {
    /* Check for simple number predicate [1], [2], etc. */
    if (predicate->type == XPATH_AST_NUMBER) {
        double pred_val = predicate->number_value;
        /* Position must be integer and match */
        if (pred_val == (int)pred_val && pred_val > 0) {
            return (position == (size_t)pred_val) ? 1 : 0;
        }
    }

    /* Check for pre-classified positional predicate */
    if (predicate->pred_type == XPATH_PRED_SIMPLE_POSITIONAL) {
        int pred_pos = predicate->pred_position;
        if (pred_pos > 0) {
            return (position == (size_t)pred_pos) ? 1 : 0;
        }
    }

    return -1;  /* Not a simple positional predicate */
}

/* Fast path for simple attribute existence predicates [@attr]
 * Returns: 1 if attribute exists, 0 if not, -1 if not applicable
 */
static inline int evaluate_attr_exists_predicate_inline(
    TaurusElement elem,
    XPathASTNode* predicate
) {
    /* Must be a step with attribute axis */
    if (predicate->type != XPATH_AST_STEP) return -1;
    if (!predicate->value || strcmp(predicate->value, "attribute") != 0) return -1;
    if (predicate->child_count < 1) return -1;

    /* Check for additional predicates on the step (only node test allowed) */
    if (predicate->child_count > 1) return -1;

    XPathASTNode* test = predicate->children[0];
    if (!test) return -1;

    /* Get attribute name from test */
    const char* attr_name = NULL;
    if (test->type == XPATH_AST_NODE_TEST_NAME) {
        attr_name = test->value;
    } else if (test->type == XPATH_AST_NODE_TEST_ALL) {
        /* [@*] - any attribute - check if element has any attributes */
        struct ptr_element* element = (struct ptr_element*)elem;
        return (element->first_attr != NULL) ? 1 : 0;
    } else {
        return -1;
    }

    if (!attr_name || !elem) return -1;

    /* PERFORMANCE: Direct attribute lookup without function call */
    struct ptr_element* element = (struct ptr_element*)elem;
    struct ptr_attribute* attr = element->first_attr;

    /* Precompute first two chars for quick filter */
    char c0 = attr_name[0];
    char c1 = attr_name[1];

    while (attr) {
        /* Two-char filter before strcmp */
        if (attr->name && attr->name[0] == c0 &&
            (c1 == '\0' || attr->name[1] == c1) &&
            strcmp(attr->name, attr_name) == 0) {
            return 1;  /* Attribute exists */
        }
        attr = attr->next_attr;
    }

    return 0;  /* Attribute doesn't exist */
}

/* Fast path for attribute comparison predicates [@attr='value']
 * This is the MOST COMMON predicate pattern in XPath queries.
 *
 * Returns: 1 if matches, 0 if doesn't match, -1 if not applicable
 */
static inline int evaluate_attr_compare_predicate_inline(
    XPathContext* ctx,
    TaurusElement elem,
    XPathASTNode* predicate
) {
    /* Must be an operator */
    if (predicate->type != XPATH_AST_OPERATOR) return -1;
    if (predicate->child_count != 2) return -1;

    XPathOperatorType op = (XPathOperatorType)predicate->number_value;
    if (op != XPATH_OP_EQUAL && op != XPATH_OP_NOT_EQUAL) return -1;

    XPathASTNode* left = predicate->children[0];
    XPathASTNode* right = predicate->children[1];

    /* Find which is the step and which is the string literal */
    XPathASTNode* step = NULL;
    XPathASTNode* str_lit = NULL;

    if (left->type == XPATH_AST_STEP && right->type == XPATH_AST_STRING) {
        step = left;
        str_lit = right;
    } else if (right->type == XPATH_AST_STEP && left->type == XPATH_AST_STRING) {
        step = right;
        str_lit = left;
    } else {
        return -1;  /* Not the pattern we're optimizing */
    }

    /* Step must have attribute axis and name test, no additional predicates */
    if (step->child_count < 1) return -1;
    if (!step->value || strcmp(step->value, "attribute") != 0) return -1;

    /* Check for name test (not wildcard) */
    XPathASTNode* test_node = step->children[0];
    if (!test_node || test_node->type != XPATH_AST_NODE_TEST_NAME) return -1;

    /* Check no additional predicates on the step */
    if (step->child_count > 1) return -1;

    const char* attr_name = test_node->value;
    const char* lit_value = str_lit->value;

    if (!attr_name || !lit_value || !elem) return -1;

    /* PERFORMANCE: Direct attribute lookup */
    struct ptr_element* element = (struct ptr_element*)elem;
    struct ptr_attribute* attr = element->first_attr;

    /* Precompute first two chars for quick filter */
    char c0 = attr_name[0];
    char c1 = attr_name[1];

    const char* attr_value = NULL;
    while (attr) {
        /* Two-char filter before strcmp */
        if (attr->name && attr->name[0] == c0 &&
            (c1 == '\0' || attr->name[1] == c1) &&
            strcmp(attr->name, attr_name) == 0) {
            attr_value = attr->value;
            break;
        }
        attr = attr->next_attr;
    }

    /* Compare attribute value with literal */
    int cmp_result;
    if (attr_value == NULL) {
        /* Attribute doesn't exist - compare with empty string */
        cmp_result = (lit_value[0] == '\0') ? 0 : 1;
    } else {
        cmp_result = strcmp(attr_value, lit_value);
    }

    /* Return result based on operator type */
    if (op == XPATH_OP_EQUAL) {
        return (cmp_result == 0) ? 1 : 0;
    } else {  /* XPATH_OP_NOT_EQUAL */
        return (cmp_result != 0) ? 1 : 0;
    }
}

/* Inline predicate evaluation dispatcher
 *
 * Attempts fast paths first, falls back to full evaluation if needed.
 */
static inline int evaluate_predicate_inline(
    XPathContext* ctx,
    void* node,
    XPathASTNode* predicate,
    size_t position
) {
    if (!predicate || !node) return 0;

    /* Get element context - predicates are evaluated on elements */
    TaurusElement elem = NULL;
    TaurusNodeType node_type = *(TaurusNodeType*)node;

    if (node_type == TAURUS_NODE_ELEMENT) {
        elem = (TaurusElement)node;
    } else if (node_type == TAURUS_NODE_ATTRIBUTE) {
        TaurusAttributeNode* attr = (TaurusAttributeNode*)node;
        elem = attr->owner;
    }

    if (!elem) return 0;

    /* FAST PATH 1: Simple positional predicate [n] */
    int pos_result = evaluate_positional_predicate_inline(predicate, position);
    if (pos_result >= 0) return pos_result;

    /* FAST PATH 2: Attribute existence predicate [@attr] */
    int attr_exists_result = evaluate_attr_exists_predicate_inline(elem, predicate);
    if (attr_exists_result >= 0) return attr_exists_result;

    /* FAST PATH 3: Attribute comparison predicate [@attr='value'] */
    int attr_cmp_result = evaluate_attr_compare_predicate_inline(ctx, elem, predicate);
    if (attr_cmp_result >= 0) return attr_cmp_result;

    /* FALLBACK: Use full predicate evaluation via direct boolean */
    /* Save context */
    TaurusElement old_node = ctx->context_node;
    size_t old_pos = ctx->context_position;
    size_t old_size = ctx->context_size;
    void* old_predicate_node = ctx->current_predicate_node;

    /* Set context for predicate evaluation */
    ctx->context_node = (TaurusElement)node;
    ctx->context_position = position;
    ctx->context_size = 0;  /* Unknown in integrated mode */
    ctx->current_predicate_node = node;

    /* Evaluate predicate to boolean */
    int matches = evaluate_expr_to_boolean(ctx, predicate, 1);

    /* Restore context */
    ctx->context_node = old_node;
    ctx->context_position = old_pos;
    ctx->context_size = old_size;
    ctx->current_predicate_node = old_predicate_node;

    return (matches > 0) ? 1 : 0;
}

#endif /* XPATH_EVALUATOR_INTEGRATED_H */
