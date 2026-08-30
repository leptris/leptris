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

    /* XSLT 3.0 conditional (XPath 2.0+): lazy — evaluate the
     * condition, then only the chosen branch. */
    if (op == XPATH_OP_IF) {
        if (ast->child_count < 3) return NULL;
        struct leptris_xpath_result* cond = evaluate_expr(ctx, ast->children[0]);
        if (!cond) return NULL;
        int truth = xpath_to_boolean(cond);
        xpath_result_free(cond);
        return evaluate_expr(ctx, truth ? ast->children[1]
                                        : ast->children[2]);
    }

    /* XSLT 3.0 `for $v in DOMAIN return EXPR` (XPath 2.0+): iterate
     * the domain, bind $v per iteration, evaluate EXPR; the results
     * join space-separated (the sequence's string form). */
    if (op == XPATH_OP_FOR) {
        if (ast->child_count < 2 || !ast->value) return NULL;
        /* A bare eval context carries no variable set — own a
         * scratch one for the loop binding. */
        XPathVariableSet* scratch = NULL;
        if (!ctx->variable_set) {
            scratch = xpath_variable_set_new();
            if (!scratch) return NULL;
            ctx->variable_set = scratch;
        }
        struct leptris_xpath_result* domain =
            evaluate_expr(ctx, ast->children[0]);
        if (!domain) return NULL;

        char* acc = (char*)malloc(1);
        size_t len = 0, cap = 1;
        if (!acc) { xpath_result_free(domain); return NULL; }
        acc[0] = 0;

        XPathNodeSet* ns =
            (domain->type == XPATH_RESULT_NODESET)
                ? domain->value.nodeset_value : NULL;
        size_t n = ns ? ns->count : 1;
        for (size_t i = 0; i < n; i++) {
            XPathVariable* var = xpath_variable_set_add(
                ctx->variable_set, ast->value, XPATH_VAR_TYPE_NODE_SET);
            if (!var) break;
            XPathNodeSet* one = xpath_nodeset_new();
            if (!one) break;
            if (ns) {
                xpath_nodeset_add(one, ns->nodes[i]);
            } else {
                /* Scalar domain: one iteration with the value. */
                char* sv = xpath_to_string(domain);
                /* Represent as a synthetic single text node. */
                XPathTextNode* tn =
                    (XPathTextNode*)calloc(1, sizeof(*tn));
                if (tn) {
                    tn->node_type = LEPTRIS_NODE_TEXT;
                    tn->content = sv ? sv : (char*)calloc(1, 1);
                    one->owns_synthetic_text = 1;
                    xpath_nodeset_add(one, tn);
                }
            }
            xpath_variable_set_nodeset(var, one);

            struct leptris_xpath_result* item =
                evaluate_expr(ctx, ast->children[1]);
            if (item) {
                char* piece = xpath_to_string(item);
                xpath_result_free(item);
                if (piece && piece[0]) {
                    size_t pl = strlen(piece);
                    int need_sep = (len > 0);
                    while (len + pl + 2 > cap) cap *= 2;
                    char* grown = (char*)realloc(acc, cap);
                    if (grown) {
                        acc = grown;
                        if (need_sep) acc[len++] = ' ';
                        memcpy(acc + len, piece, pl + 1);
                        len += pl;
                    }
                }
                free(piece);
            }
            /* The variable OWNS the nodeset after set_nodeset —
             * remove frees it; do not double-free. */
            xpath_variable_set_remove(ctx->variable_set, ast->value);
        }
        xpath_result_free(domain);
        if (scratch) ctx->variable_set = NULL;
        xpath_variable_set_free(scratch);

        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!result) { free(acc); return NULL; }
        result->value.string_value = acc;
        return result;
    }

    /* XSLT 3.0 range `A to B` (XPath 2.0+): integer sequence as a
     * nodeset of synthetic text nodes — predicates and numeric
     * comparisons then see each member. */
    if (op == XPATH_OP_RANGE) {
        if (ast->child_count < 2) return NULL;
        struct leptris_xpath_result* lo_r =
            evaluate_expr(ctx, ast->children[0]);
        if (!lo_r) return NULL;
        struct leptris_xpath_result* hi_r =
            evaluate_expr(ctx, ast->children[1]);
        if (!hi_r) { xpath_result_free(lo_r); return NULL; }
        long lo = (long)xpath_to_number(lo_r);
        long hi = (long)xpath_to_number(hi_r);
        xpath_result_free(lo_r);
        xpath_result_free(hi_r);

        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) return NULL;
        out->owns_synthetic_text = 1;
        for (long v = lo; v <= hi && v - lo < 100000; v++) {
            char buf[24];
            int l = snprintf(buf, sizeof buf, "%ld", v);
            XPathTextNode* tn = (XPathTextNode*)calloc(1, sizeof(*tn));
            char* content = (char*)malloc((size_t)l + 1);
            if (!tn || !content) { free(tn); free(content); break; }
            memcpy(content, buf, (size_t)l + 1);
            tn->node_type = LEPTRIS_NODE_TEXT;
            tn->content = content;
            xpath_nodeset_add(out, tn);
        }
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
    }

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
    /* XSLT 3.0 conditional (XPath 2.0+): the children were already
     * evaluated above (they are lazy in the real semantics — see the
     * dedicated branch at the top of this function). */
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