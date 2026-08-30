/* evaluator_operators.c - XPath operator evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * All XPath 1.0 operators: arithmetic, comparison, logical, union
 */

#include "evaluator_internal.h"
#include "../leptris_internal.h"
#include "../dom/element.h"  /* For LeptrisElement structure */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern char* get_node_text(void* node);

/* ============================================================================
 * Operator Evaluation
 * ============================================================================ */

/* §3.4 relational compare over a double pair. */
static int op_relational_cmp(XPathOperatorType op, double a, double b) {
    switch (op) {
        case XPATH_OP_LESS:          return a <  b;
        case XPATH_OP_LESS_EQUAL:    return a <= b;
        case XPATH_OP_GREATER:       return a >  b;
        case XPATH_OP_GREATER_EQUAL: return a >= b;
        default: return 0;
    }
}

struct leptris_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast) {
    if (!ast || ast->child_count < 1) return NULL;

    XPathOperatorType op = (XPathOperatorType)ast->number_value;

    /* Unary negation */
    if (op == XPATH_OP_NEGATION) {
        struct leptris_xpath_result* operand = evaluate_expr(ctx, ast->children[0]);
        if (!operand) return NULL;
        double value = xpath_to_number(operand);
        xpath_result_free(operand);

        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) result->value.number_value = -value;
        return result;
    }

    /* Binary operators require 2 operands */
    if (ast->child_count < 2) return NULL;

    struct leptris_xpath_result* left = evaluate_expr(ctx, ast->children[0]);
    if (!left) return NULL;

    /* Short-circuit for logical operators */
    if (op == XPATH_OP_AND) {
        if (!xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 0;
            return result;
        }
    } else if (op == XPATH_OP_OR) {
        if (xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 1;
            return result;
        }
    }

    struct leptris_xpath_result* right = evaluate_expr(ctx, ast->children[1]);
    if (!right) {
        xpath_result_free(left);
        return NULL;
    }

    struct leptris_xpath_result* result = NULL;

    /* Arithmetic operators */
    if (op == XPATH_OP_PLUS || op == XPATH_OP_MINUS || op == XPATH_OP_MULTIPLY ||
        op == XPATH_OP_DIV || op == XPATH_OP_MOD) {
        double lval = xpath_to_number(left);
        double rval = xpath_to_number(right);
        result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) {
            switch (op) {
                case XPATH_OP_PLUS: result->value.number_value = lval + rval; break;
                case XPATH_OP_MINUS: result->value.number_value = lval - rval; break;
                case XPATH_OP_MULTIPLY: result->value.number_value = lval * rval; break;
                case XPATH_OP_DIV: result->value.number_value = lval / rval; break;
                case XPATH_OP_MOD: result->value.number_value = fmod(lval, rval); break;
                default: break;
            }
        }
    }
    /* Comparison operators */
    else if (op >= XPATH_OP_EQUAL && op <= XPATH_OP_GREATER_EQUAL) {
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) {
            /* XPath 1.0 spec Section 3.4:
             * - If both are strings: string comparison for = and !=
             * - If one is nodeset: compare nodeset string-values
             * - Otherwise: numeric comparison
             */
            int is_equality_op = (op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL);

            /* Handle nodeset comparisons (§3.4): a nodeset NEVER
             * collapses to its first node — nodeset op nodeset is
             * ANY-PAIR, nodeset op scalar is ANY-NODE. The old
             * to_string shortcut compared first nodes only and broke
             * the distinct-values idiom
             * @name = preceding::G/@name (bug-5-, VM path is the
             * twin in vm.c's comparison op). */
            if (left->type == XPATH_RESULT_NODESET ||
                right->type == XPATH_RESULT_NODESET) {
                int negate = (op == XPATH_OP_NOT_EQUAL);
                struct leptris_xpath_result* other =
                    (left->type == XPATH_RESULT_NODESET) ? right : left;
                XPathNodeSet* ns = (left->type == XPATH_RESULT_NODESET)
                                       ? left->value.nodeset_value
                                       : right->value.nodeset_value;
                int matches = 0;

                if (other->type == XPATH_RESULT_BOOLEAN && is_equality_op) {
                    /* boolean vs nodeset: boolean() both sides. */
                    int lb = xpath_to_boolean(left);
                    int rb = xpath_to_boolean(right);
                    result->value.boolean_value =
                        negate ? (lb != rb) : (lb == rb);
                } else if (other->type == XPATH_RESULT_NODESET) {
                    /* nodeset vs nodeset: any-pair. */
                    XPathNodeSet* on = other->value.nodeset_value;
                    for (size_t i = 0; !matches && ns && i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        for (size_t j = 0; !matches && on && j < on->count; j++) {
                            char* b = get_node_text(on->nodes[j]);
                            if (!b) continue;
                            if (is_equality_op) {
                                matches = negate ? (strcmp(a, b) != 0)
                                                 : (strcmp(a, b) == 0);
                            } else {
                                matches = op_relational_cmp(
                                    op, atof(a), atof(b));
                            }
                            LEPTRIS_FREE(b);
                        }
                        LEPTRIS_FREE(a);
                    }
                    result->value.boolean_value = matches;
                } else if (is_equality_op) {
                    /* nodeset vs scalar: any-node string compare. */
                    char* scalar = xpath_to_string(other);
                    for (size_t i = 0; !matches && ns && scalar &&
                             i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        matches = negate ? (strcmp(a, scalar) != 0)
                                         : (strcmp(a, scalar) == 0);
                        LEPTRIS_FREE(a);
                    }
                    if (scalar) LEPTRIS_FREE(scalar);
                    result->value.boolean_value = matches;
                } else {
                    /* nodeset vs scalar: any-node numeric compare. */
                    double scalar = xpath_to_number(other);
                    for (size_t i = 0; !matches && ns && i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        matches = op_relational_cmp(op, atof(a), scalar);
                        LEPTRIS_FREE(a);
                    }
                    result->value.boolean_value = matches;
                }
            }
            /* String comparison for equality operators when both are strings */
            else if (is_equality_op &&
                     left->type == XPATH_RESULT_STRING &&
                     right->type == XPATH_RESULT_STRING) {
                char* lstr = xpath_to_string(left);
                char* rstr = xpath_to_string(right);
                const char* ls = lstr ? lstr : "";
                const char* rs = rstr ? rstr : "";
                int cmp = strcmp(ls, rs);

                switch (op) {
                    case XPATH_OP_EQUAL: result->value.boolean_value = (cmp == 0); break;
                    case XPATH_OP_NOT_EQUAL: result->value.boolean_value = (cmp != 0); break;
                    default: break;
                }

                if (lstr) LEPTRIS_FREE(lstr);
                if (rstr) LEPTRIS_FREE(rstr);
            }
            /* Numeric comparison (all relational ops and mixed types) */
            else {
                double lval = xpath_to_number(left);
                double rval = xpath_to_number(right);
                switch (op) {
                    case XPATH_OP_EQUAL: result->value.boolean_value = (lval == rval); break;
                    case XPATH_OP_NOT_EQUAL: result->value.boolean_value = (lval != rval); break;
                    case XPATH_OP_LESS: result->value.boolean_value = (lval < rval); break;
                    case XPATH_OP_LESS_EQUAL: result->value.boolean_value = (lval <= rval); break;
                    case XPATH_OP_GREATER: result->value.boolean_value = (lval > rval); break;
                    case XPATH_OP_GREATER_EQUAL: result->value.boolean_value = (lval >= rval); break;
                    default: break;
                }
            }
        }
    }
    /* Logical operators */
    else if (op == XPATH_OP_AND || op == XPATH_OP_OR) {
        int lbool = xpath_to_boolean(left);
        int rbool = xpath_to_boolean(right);
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) {
            result->value.boolean_value = (op == XPATH_OP_AND) ? (lbool && rbool) : (lbool || rbool);
        }
    }
    /* Union operator */
    else if (op == XPATH_OP_UNION) {
        if (left->type != XPATH_RESULT_NODESET || right->type != XPATH_RESULT_NODESET) {
            xpath_result_free(left);
            xpath_result_free(right);
            return NULL;
        }
        result = xpath_result_new(XPATH_RESULT_NODESET);
        if (result) {
            XPathNodeSet* ns = xpath_nodeset_new();
            /* Concatenate both sides; the sort dedups adjacent
             * duplicates (the old per-candidate linear duplicate
             * scan was O(n^2)). */
            for (size_t i = 0; i < xpath_nodeset_count(left->value.nodeset_value); i++) {
                xpath_nodeset_add(ns, xpath_nodeset_get(left->value.nodeset_value, i));
            }
            for (size_t i = 0; i < xpath_nodeset_count(right->value.nodeset_value); i++) {
                xpath_nodeset_add(ns, xpath_nodeset_get(right->value.nodeset_value, i));
            }

            /* CRITICAL: Sort in document order per XPath 1.0 spec
             * (issue #485: pointer order is not document order). */
            xpath_nodeset_sort_doc_order(ctx, ns, 0);

            /* Issue #514: the concatenated entries BORROW the
             * operands' synthetic nodes (attributes, namespaces,
             * EXSLT text). The operand frees below would release
             * them and leave this nodeset with dangling pointers —
             * transfer the ownership flags before they run. */
            ns->owns_attributes =
                left->value.nodeset_value->owns_attributes ||
                right->value.nodeset_value->owns_attributes;
            ns->owns_namespaces =
                left->value.nodeset_value->owns_namespaces ||
                right->value.nodeset_value->owns_namespaces;
            ns->owns_synthetic_text =
                left->value.nodeset_value->owns_synthetic_text ||
                right->value.nodeset_value->owns_synthetic_text;
            left->value.nodeset_value->owns_attributes = 0;
            left->value.nodeset_value->owns_namespaces = 0;
            left->value.nodeset_value->owns_synthetic_text = 0;
            right->value.nodeset_value->owns_attributes = 0;
            right->value.nodeset_value->owns_namespaces = 0;
            right->value.nodeset_value->owns_synthetic_text = 0;

            result->value.nodeset_value = ns;
        }
    }

    xpath_result_free(left);
    xpath_result_free(right);
    return result;
}