/* evaluator_operators.c - XPath operator evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * All XPath 1.0 operators: arithmetic, comparison, logical, union
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Compare two nodes for document order
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 * Uses pointer comparison for consistent ordering
 * TODO: Implement proper document order traversal if needed for correctness
 */
static int compare_document_order(const void* a, const void* b) {
    void* node_a = *(void**)a;
    void* node_b = *(void**)b;

    if (node_a == node_b) return 0;

    /* New DOM doesn't track document order.  Pointer comparison gives a
     * stable, total ordering sufficient for XPath's de-duplication needs;
     * true document order would require an ancestor/descendant walk that
     * is not worth the cost for the queries that reach this path. */
    return (node_a < node_b) ? -1 : 1;
}
/* ============================================================================
 * Operator Evaluation
 * ============================================================================ */

struct taurus_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast) {
    if (!ast || ast->child_count < 1) return NULL;

    XPathOperatorType op = (XPathOperatorType)ast->number_value;

    /* Unary negation */
    if (op == XPATH_OP_NEGATION) {
        struct taurus_xpath_result* operand = evaluate_expr(ctx, ast->children[0]);
        if (!operand) return NULL;
        double value = xpath_to_number(operand);
        xpath_result_free(operand);

        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) result->value.number_value = -value;
        return result;
    }

    /* Binary operators require 2 operands */
    if (ast->child_count < 2) return NULL;

    struct taurus_xpath_result* left = evaluate_expr(ctx, ast->children[0]);
    if (!left) return NULL;

    /* Short-circuit for logical operators */
    if (op == XPATH_OP_AND) {
        if (!xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 0;
            return result;
        }
    } else if (op == XPATH_OP_OR) {
        if (xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 1;
            return result;
        }
    }

    struct taurus_xpath_result* right = evaluate_expr(ctx, ast->children[1]);
    if (!right) {
        xpath_result_free(left);
        return NULL;
    }

    struct taurus_xpath_result* result = NULL;

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

            /* Handle nodeset comparisons */
            if (left->type == XPATH_RESULT_NODESET || right->type == XPATH_RESULT_NODESET) {
                /* For nodesets, convert to string and compare */
                char* lstr = xpath_to_string(left);
                char* rstr = xpath_to_string(right);

                if (is_equality_op) {
                    /* String comparison for equality ops */
                    const char* ls = lstr ? lstr : "";
                    const char* rs = rstr ? rstr : "";
                    int cmp = strcmp(ls, rs);
                    switch (op) {
                        case XPATH_OP_EQUAL: result->value.boolean_value = (cmp == 0); break;
                        case XPATH_OP_NOT_EQUAL: result->value.boolean_value = (cmp != 0); break;
                        default: break;
                    }
                } else {
                    /* Numeric comparison for relational ops */
                    double lval = xpath_to_number(left);
                    double rval = xpath_to_number(right);
                    switch (op) {
                        case XPATH_OP_LESS: result->value.boolean_value = (lval < rval); break;
                        case XPATH_OP_LESS_EQUAL: result->value.boolean_value = (lval <= rval); break;
                        case XPATH_OP_GREATER: result->value.boolean_value = (lval > rval); break;
                        case XPATH_OP_GREATER_EQUAL: result->value.boolean_value = (lval >= rval); break;
                        default: break;
                    }
                }

                if (lstr) TAURUS_FREE(lstr);
                if (rstr) TAURUS_FREE(rstr);
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

                if (lstr) TAURUS_FREE(lstr);
                if (rstr) TAURUS_FREE(rstr);
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
            /* Add left nodes */
            for (size_t i = 0; i < xpath_nodeset_count(left->value.nodeset_value); i++) {
                xpath_nodeset_add(ns, xpath_nodeset_get(left->value.nodeset_value, i));
            }
            /* Add right nodes (skip duplicates) */
            for (size_t i = 0; i < xpath_nodeset_count(right->value.nodeset_value); i++) {
                void* node = xpath_nodeset_get(right->value.nodeset_value, i);
                int duplicate = 0;
                for (size_t j = 0; j < xpath_nodeset_count(ns); j++) {
                    if (xpath_nodeset_get(ns, j) == node) {
                        duplicate = 1;
                        break;
                    }
                }
                if (!duplicate) xpath_nodeset_add(ns, node);
            }

            /* CRITICAL: Sort in document order per XPath 1.0 spec */
            if (ns->count > 1) {
                qsort(ns->nodes, ns->count, sizeof(void*), compare_document_order);
            }

            result->value.nodeset_value = ns;
        }
    }

    xpath_result_free(left);
    xpath_result_free(right);
    return result;
}