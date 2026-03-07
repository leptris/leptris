/* evaluator_integrated.c - Integrated predicate evaluation implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-pass axis traversal with inline predicate evaluation.
 * Matches libxml2's xmlXPathNodeCollectAndTest() performance pattern.
 */

#include "evaluator_integrated.h"
#include "../dom/element.h"
#include "../dom/ptr_element.h"
#include <string.h>

/* Debug logging - Set to 0 to disable */
#define XPATH_DEBUG 0

#if XPATH_DEBUG
#include <stdio.h>
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[XPath INT] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * Predicate Classification
 * ============================================================================ */

/* Helper: Check if AST contains last() or position() function calls
 * These functions require context_size which is not available in integrated mode
 */
static int ast_uses_context_size_functions(XPathASTNode* ast) {
    if (!ast) return 0;

    /* Check for function calls to last() or position() */
    if (ast->type == XPATH_AST_FUNCTION_CALL) {
        if (ast->value) {
            if (strcmp(ast->value, "last") == 0 || strcmp(ast->value, "position") == 0) {
                return 1;
            }
        }
    }

    /* Recursively check children */
    for (size_t i = 0; i < ast->child_count; i++) {
        if (ast_uses_context_size_functions(ast->children[i])) {
            return 1;
        }
    }

    return 0;
}

/* Check if a predicate can be evaluated inline
 * Returns: 1 if simple predicate, 0 if complex
 */
static int is_simple_predicate(XPathASTNode* predicate) {
    if (!predicate) return 0;

    /* Reject predicates that use last() or position() functions
     * These require context_size which is not known in integrated mode */
    if (ast_uses_context_size_functions(predicate)) {
        return 0;
    }

    /* Check pre-computed classification */
    if (predicate->pred_type == XPATH_PRED_SIMPLE_POSITIONAL ||
        predicate->pred_type == XPATH_PRED_SIMPLE_ATTR_EXISTS ||
        predicate->pred_type == XPATH_PRED_SIMPLE_ATTR_COMPARE) {
        return 1;
    }

    /* Check for simple number [1], [2], etc. */
    if (predicate->type == XPATH_AST_NUMBER) {
        return 1;
    }

    /* Check for [@attr] pattern - single step with attribute axis */
    if (predicate->type == XPATH_AST_STEP) {
        if (predicate->value && strcmp(predicate->value, "attribute") == 0) {
            /* Attribute step with only a node test (no additional predicates) */
            if (predicate->child_count == 1) {
                XPathASTNode* test = predicate->children[0];
                if (test && (test->type == XPATH_AST_NODE_TEST_NAME ||
                             test->type == XPATH_AST_NODE_TEST_ALL)) {
                    return 1;
                }
            }
        }
    }

    /* Check for [@attr='value'] pattern - equality operator with attribute step and string */
    if (predicate->type == XPATH_AST_OPERATOR && predicate->child_count == 2) {
        XPathOperatorType op = (XPathOperatorType)predicate->number_value;
        if (op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL) {
            XPathASTNode* left = predicate->children[0];
            XPathASTNode* right = predicate->children[1];

            /* Check if one side is attribute step and other is string literal */
            int has_attr_step = 0;
            int has_string_lit = 0;

            if (left->type == XPATH_AST_STEP && left->value &&
                strcmp(left->value, "attribute") == 0) {
                has_attr_step = 1;
            }
            if (right->type == XPATH_AST_STEP && right->value &&
                strcmp(right->value, "attribute") == 0) {
                has_attr_step = 1;
            }
            if (left->type == XPATH_AST_STRING) has_string_lit = 1;
            if (right->type == XPATH_AST_STRING) has_string_lit = 1;

            if (has_attr_step && has_string_lit) {
                return 1;
            }
        }
    }

    return 0;  /* Complex predicate */
}

int can_use_integrated_evaluation(
    XPathASTNode** predicates,
    size_t pred_count
) {
    if (pred_count == 0) return 1;  /* No predicates is trivially simple */

    for (size_t i = 0; i < pred_count; i++) {
        if (!is_simple_predicate(predicates[i])) {
            DEBUG_LOG("    Predicate %zu is complex, using fallback", i);
            return 0;
        }
    }

    DEBUG_LOG("    All %zu predicates are simple, using integrated evaluation", pred_count);
    return 1;
}

/* ============================================================================
 * Helper: Node Test Matching (Inline Version)
 * ============================================================================ */

/* Inline version of matches_node_test for performance */
static inline int matches_node_test_inline(
    TaurusElement node,
    XPathASTNode* test
) {
    if (!node || !test) return 1;  /* No test means match all */

    switch (test->type) {
        case XPATH_AST_NODE_TEST_NAME: {
            const char* node_name = taurus_element_get_name(node);
            if (!test->value || !node_name) return 0;

            /* Fast path: No colon means no namespace prefix */
            const char* colon = strchr(test->value, ':');
            if (!colon) {
                const char* node_prefix = taurus_element_get_prefix(node);
                return (!node_prefix && strcmp(node_name, test->value) == 0);
            }

            /* Has prefix - namespace-aware matching */
            size_t prefix_len = colon - test->value;
            const char* test_local = colon + 1;
            const char* node_prefix = taurus_element_get_prefix(node);
            if (!node_prefix) return 0;

            if (strncmp(test->value, node_prefix, prefix_len) != 0 ||
                node_prefix[prefix_len] != '\0') {
                return 0;
            }

            const char* node_local = strchr(node_name, ':');
            node_local = node_local ? node_local + 1 : node_name;
            return (strcmp(node_local, test_local) == 0);
        }

        case XPATH_AST_NODE_TEST_ALL: {
            if (test->value) {
                const char* colon = strchr(test->value, ':');
                if (!colon) return 1;

                size_t prefix_len = colon - test->value;
                const char* node_prefix = taurus_element_get_prefix(node);
                if (!node_prefix) return 0;
                return (strncmp(test->value, node_prefix, prefix_len) == 0 &&
                        node_prefix[prefix_len] == '\0');
            }
            return 1;
        }

        case XPATH_AST_NODE_TEST_TYPE:
            if (test->value) {
                if (strcmp(test->value, "node") == 0) return 1;
                if (strcmp(test->value, "text") == 0) {
                    const char* text = taurus_element_get_text_content(node);
                    return (text && text[0] != '\0');
                }
            }
            return 0;

        default:
            return 0;
    }
}

/* ============================================================================
 * Integrated Axis: Child
 * ============================================================================ */

XPathNodeSet* axis_child_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
) {
    DEBUG_LOG("    axis_child_integrated: node=%p, pred_count=%zu, to_bool=%d",
             (void*)node, pred_count, to_bool);

    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    size_t position = 0;  /* 1-based position for predicates */

    /* Iterate through children */
    TaurusElement child = taurus_element_get_first_child(node);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            position++;

            /* Test node */
            if (matches_node_test_inline(child, test)) {
                /* Apply predicates inline */
                int matched;
                XPATH_TEST_HIT(ctx, child, result, predicates,
                               pred_count, position, to_bool, matched);

                /* Early exit for boolean context */
                if (matched && to_bool) {
                    DEBUG_LOG("      Early exit: found match at position %zu", position);
                    return result;
                }
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    DEBUG_LOG("    axis_child_integrated: found %zu nodes", result->count);
    return result;
}

/* ============================================================================
 * Integrated Axis: Attribute
 * ============================================================================ */

/* Helper: Check if attribute matches test (inline) */
static inline int attr_matches_test_inline(
    struct ptr_attribute* attr,
    XPathASTNode* test
) {
    if (!test) return 1;  /* No test = match all */

    const char* attr_name = attr->name;
    size_t attr_name_len = 0;

    if (!attr_name && attr->name_view_data && attr->name_view_length > 0) {
        attr_name = attr->name_view_data;
        attr_name_len = attr->name_view_length;
    } else if (attr_name) {
        attr_name_len = strlen(attr_name);
    }

    if (test->type == XPATH_AST_NODE_TEST_NAME) {
        if (!attr_name || !test->value) return 0;

        if (attr_name_len > 0) {
            size_t test_len = strlen(test->value);
            return (test_len == attr_name_len &&
                    memcmp(attr_name, test->value, test_len) == 0);
        } else {
            return strcmp(attr_name, test->value) == 0;
        }
    } else if (test->type == XPATH_AST_NODE_TEST_ALL) {
        return 1;
    }

    return 0;
}

XPathNodeSet* axis_attribute_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
) {
    DEBUG_LOG("    axis_attribute_integrated: node=%p, pred_count=%zu",
             (void*)node, pred_count);

    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    struct ptr_element* element = (struct ptr_element*)node;
    struct ptr_attribute* attr = element->first_attr;

    /* If no predicates, just collect matching attributes */
    if (pred_count == 0) {
        while (attr) {
            /* Skip namespace declarations */
            const char* attr_name = attr->name;
            if (attr_name && (strcmp(attr_name, "xmlns") == 0 ||
                             strncmp(attr_name, "xmlns:", 6) == 0)) {
                attr = attr->next_attr;
                continue;
            }

            if (attr_matches_test_inline(attr, test)) {
                /* Create attribute node - need to allocate since we return them */
                TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                if (attr_node) {
                    attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                    attr_node->name = attr->name ? taurus_strdup(attr->name) : NULL;
                    attr_node->value = attr->value ? taurus_strdup(attr->value) : NULL;
                    attr_node->namespace_uri = NULL;
                    attr_node->owner = (TaurusElement)node;
                    xpath_nodeset_add(result, attr_node);
                    result->owns_attributes = 1;
                }
            }
            attr = attr->next_attr;
        }
        return result;
    }

    /* With predicates: evaluate inline */
    size_t position = 0;

    while (attr) {
        /* Skip namespace declarations */
        const char* attr_name = attr->name;
        if (attr_name && (strcmp(attr_name, "xmlns") == 0 ||
                         strncmp(attr_name, "xmlns:", 6) == 0)) {
            attr = attr->next_attr;
            continue;
        }

        if (attr_matches_test_inline(attr, test)) {
            position++;

            /* Create attribute node for predicate evaluation */
            TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
            if (!attr_node) {
                attr = attr->next_attr;
                continue;
            }

            attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
            attr_node->name = attr->name ? taurus_strdup(attr->name) : NULL;
            attr_node->value = attr->value ? taurus_strdup(attr->value) : NULL;
            attr_node->namespace_uri = NULL;
            attr_node->owner = (TaurusElement)node;

            /* Apply predicates inline */
            int matched;
            XPATH_TEST_HIT(ctx, attr_node, result, predicates,
                           pred_count, position, to_bool, matched);

            /* If not matched, free the attribute node we created */
            if (!matched) {
                if (attr_node->name) TAURUS_FREE(attr_node->name);
                if (attr_node->value) TAURUS_FREE(attr_node->value);
                TAURUS_FREE(attr_node);
            } else {
                result->owns_attributes = 1;
                /* Early exit for boolean context */
                if (to_bool) {
                    return result;
                }
            }
        }
        attr = attr->next_attr;
    }

    DEBUG_LOG("    axis_attribute_integrated: found %zu nodes", result->count);
    return result;
}

/* ============================================================================
 * Integrated Axis: Descendant (recursive helper)
 * ============================================================================ */

static void collect_descendants_integrated_impl(
    XPathContext* ctx,
    TaurusElement node,
    XPathNodeSet* result,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool,
    size_t* position,
    int* early_exit,
    int depth
) {
    if (!node || depth > 1000 || *early_exit) return;

    TaurusElement child = taurus_element_get_first_child(node);
    while (child && !*early_exit) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            (*position)++;

            if (matches_node_test_inline(child, test)) {
                int matched;
                XPATH_TEST_HIT_VOID(ctx, child, result, predicates,
                                     pred_count, *position, to_bool, matched, early_exit);
                if (matched && to_bool) {
                    *early_exit = 1;
                    return;
                }
            }

            if (!*early_exit) {
                /* Recurse into children */
                collect_descendants_integrated_impl(ctx, child, result, test,
                                                    predicates, pred_count, to_bool,
                                                    position, early_exit, depth + 1);
            }
        }
        child = taurus_element_get_next_sibling(child);
    }
}

XPathNodeSet* axis_descendant_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
) {
    DEBUG_LOG("    axis_descendant_integrated: node=%p, pred_count=%zu",
             (void*)node, pred_count);

    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    size_t position = 0;
    int early_exit = 0;

    collect_descendants_integrated_impl(ctx, node, result, test,
                                        predicates, pred_count, to_bool,
                                        &position, &early_exit, 0);

    DEBUG_LOG("    axis_descendant_integrated: found %zu nodes", result->count);
    return result;
}

/* ============================================================================
 * Integrated Axis: Descendant-or-Self
 * ============================================================================ */

static void collect_descendants_or_self_integrated_impl(
    XPathContext* ctx,
    TaurusElement node,
    XPathNodeSet* result,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool,
    size_t* position,
    int* early_exit,
    int depth
) {
    if (!node || depth > 1000 || *early_exit) return;

    /* First, test self */
    (*position)++;
    if (matches_node_test_inline(node, test)) {
        int matched;
        XPATH_TEST_HIT_VOID(ctx, node, result, predicates,
                             pred_count, *position, to_bool, matched, early_exit);
        if (matched && to_bool) {
            *early_exit = 1;
            return;
        }
    }

    if (!*early_exit) {
        /* Then recurse into children */
        TaurusElement child = taurus_element_get_first_child(node);
        while (child && !*early_exit) {
            TaurusNode* child_node = (TaurusNode*)child;
            if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
                collect_descendants_or_self_integrated_impl(ctx, child, result, test,
                                                            predicates, pred_count, to_bool,
                                                            position, early_exit, depth + 1);
            }
            child = taurus_element_get_next_sibling(child);
        }
    }
}

XPathNodeSet* axis_descendant_or_self_integrated(
    XPathContext* ctx,
    TaurusElement node,
    XPathASTNode* test,
    XPathASTNode** predicates,
    size_t pred_count,
    int to_bool
) {
    DEBUG_LOG("    axis_descendant_or_self_integrated: node=%p, pred_count=%zu",
             (void*)node, pred_count);

    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    size_t position = 0;
    int early_exit = 0;

    collect_descendants_or_self_integrated_impl(ctx, node, result, test,
                                                predicates, pred_count, to_bool,
                                                &position, &early_exit, 0);

    DEBUG_LOG("    axis_descendant_or_self_integrated: found %zu nodes", result->count);
    return result;
}

/* ============================================================================
 * Integrated Step Evaluation
 * ============================================================================ */

XPathNodeSet* evaluate_step_integrated(
    XPathContext* ctx,
    XPathASTNode* step,
    XPathNodeSet* input,
    int to_bool
) {
    DEBUG_LOG("  === evaluate_step_integrated START ===");

    if (!step || step->type != XPATH_AST_STEP || !input) {
        DEBUG_LOG("    Invalid parameters");
        return NULL;
    }

    const char* axis_name = step->value ? step->value : "child";
    XPathASTNode* node_test = (step->child_count > 0) ? step->children[0] : NULL;

    /* Extract predicates (all children after the first which is the node test) */
    XPathASTNode** predicates = NULL;
    size_t pred_count = 0;

    if (step->child_count > 1) {
        pred_count = step->child_count - 1;
        predicates = &step->children[1];
    }

    DEBUG_LOG("    axis=%s, pred_count=%zu", axis_name, pred_count);

    /* Check if we can use integrated evaluation */
    if (!can_use_integrated_evaluation(predicates, pred_count)) {
        DEBUG_LOG("    Complex predicates, falling back to traditional evaluation");
        return NULL;  /* Signal to use fallback */
    }

    /* Create result nodeset */
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result) return NULL;

    /* Process each input node */
    for (size_t i = 0; i < xpath_nodeset_count(input); i++) {
        void* node_ptr = xpath_nodeset_get(input, i);
        TaurusElement node = NULL;

        /* Handle element nodes */
        TaurusNodeType node_type = *(TaurusNodeType*)node_ptr;
        if (node_type == TAURUS_NODE_ELEMENT) {
            node = (TaurusElement)node_ptr;
        } else if (node_type == TAURUS_NODE_ATTRIBUTE) {
            /* Attributes only support certain axes */
            TaurusAttributeNode* attr = (TaurusAttributeNode*)node_ptr;
            if (strcmp(axis_name, "parent") == 0) {
                if (attr->owner && matches_node_test_inline(attr->owner, node_test)) {
                    xpath_nodeset_add(result, attr->owner);
                }
                continue;
            }
            node = attr->owner;  /* Use owner for self/ancestor axes */
        }

        if (!node) continue;

        /* Dispatch to appropriate integrated axis function */
        XPathNodeSet* axis_result = NULL;

        if (strcmp(axis_name, "child") == 0) {
            axis_result = axis_child_integrated(ctx, node, node_test,
                                                predicates, pred_count, to_bool);
        } else if (strcmp(axis_name, "attribute") == 0) {
            axis_result = axis_attribute_integrated(ctx, node, node_test,
                                                    predicates, pred_count, to_bool);
        } else if (strcmp(axis_name, "descendant") == 0) {
            axis_result = axis_descendant_integrated(ctx, node, node_test,
                                                      predicates, pred_count, to_bool);
        } else if (strcmp(axis_name, "descendant-or-self") == 0) {
            axis_result = axis_descendant_or_self_integrated(ctx, node, node_test,
                                                              predicates, pred_count, to_bool);
        } else {
            /* Axis not implemented for integrated evaluation - use fallback */
            DEBUG_LOG("    Axis '%s' not implemented for integrated, using fallback", axis_name);
            xpath_nodeset_free(result);
            return NULL;
        }

        /* Merge results (with duplicate checking) */
        if (axis_result) {
            for (size_t j = 0; j < xpath_nodeset_count(axis_result); j++) {
                void* candidate = xpath_nodeset_get(axis_result, j);

                /* Quick duplicate check using pointer equality only
                 * For simple axes (most common), nodes are unlikely to appear twice */
                int already_present = 0;
                size_t result_count = xpath_nodeset_count(result);
                if (result_count < 32) {  /* Small result set - linear search is fine */
                    for (size_t k = 0; k < result_count; k++) {
                        if (xpath_nodeset_get(result, k) == candidate) {
                            already_present = 1;
                            break;
                        }
                    }
                }

                if (!already_present) {
                    xpath_nodeset_add(result, candidate);
                }
            }
            /* Don't free attribute nodes - result now owns them */
            if (strcmp(axis_name, "attribute") == 0) {
                axis_result->owns_attributes = 0;
            }
            xpath_nodeset_free(axis_result);

            /* Early exit check */
            if (to_bool && xpath_nodeset_count(result) > 0) {
                DEBUG_LOG("    Early exit: found match");
                return result;
            }
        }
    }

    DEBUG_LOG("  === evaluate_step_integrated END: %zu nodes ===", result->count);
    return result;
}
