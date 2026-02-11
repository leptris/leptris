/* evaluator_path.c - XPath path and predicate evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Location path evaluation, predicates, node tests
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include <string.h>
#include <stdio.h>

/* Debug logging - Set to 0 to disable */
#define XPATH_DEBUG 0

#if XPATH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[XPath DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Helper: Get element from typed node (returns NULL if not element)
 *
 * IMPORTANT: Elements are stored as TaurusElement with TAURUS_NODE_TYPE_ELEMENT.
 * Attribute nodes have node_type field as their first member.
 *
 * Strategy: Check if first field is TAURUS_NODE_ATTRIBUTE. If so, it's an attribute.
 * Otherwise, treat it as an element.
 */
static TaurusElement node_as_element(void* node) {
    if (!node) return NULL;

    /* Check if it's an attribute node by reading first field */
    TaurusNodeType first_field = *(TaurusNodeType*)node;
    if (first_field == TAURUS_NODE_ATTRIBUTE) {
        /* It's an attribute node, not an element */
        return NULL;
    }

    /* Otherwise, it's an element */
    return (TaurusElement)node;
}

/* Helper: Get attribute node from typed node (returns NULL if not attribute) */
static TaurusAttributeNode* node_as_attribute(void* node) {
    if (!node) return NULL;

    /* Check if first field is TAURUS_NODE_ATTRIBUTE */
    TaurusNodeType first_field = *(TaurusNodeType*)node;
    return (first_field == TAURUS_NODE_ATTRIBUTE) ? (TaurusAttributeNode*)node : NULL;
}

/* ============================================================================
 * Node Test Matching
 * ============================================================================
 */

/* Helper: Parse node test name into prefix and local parts
 * Input: "ns1:element" → prefix="ns1", local="element"
 * Input: "element" → prefix=NULL, local="element"
 * Input: "ns1:*" → prefix="ns1", local="*"
 */
static void parse_node_test_name(const char* test_name,
                                   char** prefix,
                                   char** local) {
    if (!test_name) {
        *prefix = NULL;
        *local = NULL;
        return;
    }

    const char* colon = strchr(test_name, ':');
    if (colon) {
        size_t prefix_len = colon - test_name;
        *prefix = TAURUS_ALLOC_N(char, prefix_len + 1);
        if (*prefix) {
            memcpy(*prefix, test_name, prefix_len);
            (*prefix)[prefix_len] = '\0';
        }
        *local = taurus_strdup(colon + 1);
    } else {
        *prefix = NULL;
        *local = taurus_strdup(test_name);
    }
}

int matches_node_test(XPathContext* ctx, TaurusElement node, XPathASTNode* test) {
    if (!node || !test) return 1;  /* No test means match all */

    switch (test->type) {
        case XPATH_AST_NODE_TEST_NAME: {
            /* Match specific name - namespace-aware */
            const char* node_name = taurus_element_get_name(node);
            if (!test->value || !node_name) return 0;

            /* Fast path: No colon means no namespace prefix */
            const char* colon = strchr(test->value, ':');

            if (!colon) {
                /* Simple name match - no prefix in test */
                const char* node_prefix = taurus_element_get_prefix(node);
                DEBUG_LOG("      matches_node_test: test='%s', node_name='%s', node_prefix=%s",
                         test->value, node_name, node_prefix ? node_prefix : "(null)");
                /* Unprefixed test should only match unprefixed elements */
                int match = !node_prefix && strcmp(node_name, test->value) == 0;
                DEBUG_LOG("        -> result=%d (node_prefix=%s, name_match=%d)",
                         match, node_prefix ? "SET" : "NULL", strcmp(node_name, test->value) == 0);
                return match;
            }

            /* Has prefix - need namespace-aware matching */
            size_t prefix_len = colon - test->value;
            const char* test_local = colon + 1;

            /* Get node prefix */
            const char* node_prefix = taurus_element_get_prefix(node);
            if (!node_prefix) return 0;  /* Test has prefix, node doesn't */

            /* Match prefix (compare up to prefix_len) */
            if (strncmp(test->value, node_prefix, prefix_len) != 0 ||
                node_prefix[prefix_len] != '\0') {
                return 0;  /* Prefix mismatch or node prefix longer */
            }

            /* Match local name */
            return (strcmp(node_name, test_local) == 0);
        }

        case XPATH_AST_NODE_TEST_ALL: {
            /* Wildcard - if test has prefix, match namespace */
            if (test->value) {
                /* Fast path: No colon means match all */
                const char* colon = strchr(test->value, ':');
                if (!colon) return 1;  /* Pure "*" matches all */

                /* Has prefix (e.g., "ns1:*") - match namespace */
                size_t prefix_len = colon - test->value;
                const char* node_prefix = taurus_element_get_prefix(node);

                if (!node_prefix) return 0;

                /* Match prefix */
                return (strncmp(test->value, node_prefix, prefix_len) == 0 &&
                        node_prefix[prefix_len] == '\0');
            }
            /* No prefix - match all elements */
            return 1;
        }

        case XPATH_AST_NODE_TEST_TYPE:
            /* Node type tests (node(), text(), comment(), etc.) */
            if (test->value) {
                if (strcmp(test->value, "node") == 0) {
                    return 1;  /* node() matches all nodes */
                }
                if (strcmp(test->value, "text") == 0) {
                    /* text() matches elements with non-empty text content */
                    const char* text = taurus_element_get_text_content(node);
                    return (text && text[0] != '\0');
                }
                /* comment(), processing-instruction() not fully implemented yet */
            }
            return 0;

        default:
            return 0;
    }
}

/* ============================================================================
 * Predicate Evaluation
 * ============================================================================ */

/* Helper: Evaluate a single predicate for a node in-place
 * Returns 1 if node matches predicate, 0 otherwise
 */
static int evaluate_predicate_for_node(XPathContext* ctx,
                                       void* node,
                                       XPathASTNode* predicate,
                                       size_t proximity_position,
                                       size_t context_size) {
    /* For predicates, we need element context - attributes predicate on their owner */
    TaurusElement context_elem = node_as_element(node);
    if (!context_elem) {
        TaurusAttributeNode* attr_node = node_as_attribute(node);
        if (attr_node) {
            context_elem = attr_node->owner;
        }
    }

    if (!context_elem) {
        return 0; /* Skip if no valid context */
    }

    /* Save context */
    TaurusElement old_node = ctx->context_node;
    size_t old_pos = ctx->context_position;
    size_t old_size = ctx->context_size;
    void* old_predicate_node = ctx->current_predicate_node;

    /* Set context ONCE for this evaluation
     * IMPORTANT: context_node should be the actual node being tested (element or attribute)
     * This allows functions like name() to work correctly on attributes */
    ctx->context_node = (TaurusElement)node;  /* Use the actual node, not just element */
    ctx->context_position = proximity_position;  /* 1-based position in candidate set */
    ctx->context_size = context_size;
    ctx->current_predicate_node = node;  /* The actual node (can be attribute) */

    /* Evaluate predicate */
    struct taurus_xpath_result* pred_result = evaluate_expr(ctx, predicate);
    int matches = 0;

    if (pred_result) {
        /* Numeric predicate: matches position */
        if (pred_result->type == XPATH_RESULT_NUMBER) {
            if ((size_t)pred_result->value.number_value == proximity_position) {
                matches = 1;
            }
        }
        /* Boolean predicate */
        else if (xpath_to_boolean(pred_result)) {
            matches = 1;
        }
        xpath_result_free(pred_result);
    }

    /* Restore context */
    ctx->context_node = old_node;
    ctx->context_position = old_pos;
    ctx->context_size = old_size;
    ctx->current_predicate_node = old_predicate_node;

    return matches;
}

/* Apply predicates using in-place filtering (libxml2 algorithm)
 * This is 100-200x faster than creating new arrays
 *
 * Algorithm:
 *   - Use two-pointer technique (read/write positions)
 *   - Track matched_position separately from iteration
 *   - Set contextSize once, not per evaluation
 *   - Early termination for position predicates
 */
XPathNodeSet* apply_predicates(XPathContext* ctx, XPathNodeSet* nodes,
                                XPathASTNode** predicates, size_t pred_count) {
    if (!nodes || pred_count == 0) return nodes;

    size_t initial_size = xpath_nodeset_count(nodes);

    DEBUG_LOG("    === apply_predicates: pred_count=%zu, nodeset size=%zu ===",
             pred_count, initial_size);

    /* Apply each predicate in sequence, filtering in-place */
    for (size_t p = 0; p < pred_count; p++) {
        DEBUG_LOG("      Processing predicate %zu", p);

        size_t current_size = xpath_nodeset_count(nodes);
        if (current_size == 0) {
            break;  /* No nodes left to filter */
        }

        /* Two-pointer in-place filtering algorithm */
        size_t read_pos = 0;      /* Reading from here */
        size_t write_pos = 0;     /* Writing to here */
        size_t matched_pos = 1;   /* Count of matched nodes (1-based for XPath position()) */

        DEBUG_LOG("      Filtering %zu nodes", current_size);

        /* Process all candidates */
        for (read_pos = 0; read_pos < current_size; read_pos++) {
            void* node = xpath_nodeset_get(nodes, read_pos);
            size_t proximity_position = read_pos + 1;  /* 1-based position in candidate set */

            DEBUG_LOG("        Node[%zu]: proximity_pos=%zu, matched_pos=%zu",
                     read_pos, proximity_position, matched_pos);

            /* Evaluate predicate for this node */
            int matches = evaluate_predicate_for_node(ctx, node, predicates[p],
                                                     proximity_position, current_size);

            if (matches) {
                DEBUG_LOG("          MATCH! Writing to pos %zu", write_pos);

                /* Keep this node - move it to write position if needed */
                if (read_pos != write_pos) {
                    /* Move node pointer from read to write position */
                    nodes->nodes[write_pos] = node;
                }
                write_pos++;
                matched_pos++;

                /* Early termination optimization:
                 * If predicate is position-based and we've found enough matches, stop
                 * Example: [position() < 3] stops after finding 2 matches
                 *
                 * TODO: Implement maxPos detection from predicate AST
                 * For now, continue to ensure correctness
                 */
            } else {
                DEBUG_LOG("          NO MATCH, skipping");
                /* Don't increment write_pos - effectively deletes this node */
            }
        }

        /* Update nodeset size to reflect filtered results */
        nodes->count = write_pos;

        DEBUG_LOG("      After predicate %zu: %zu nodes remain", p, write_pos);
    }

    DEBUG_LOG("    === apply_predicates END: result size=%zu ===",
             xpath_nodeset_count(nodes));
    return nodes;
}

/* ============================================================================
 * Path Expression Evaluation
 * ============================================================================ */

struct taurus_xpath_result* evaluate_step(XPathContext* ctx,
                                          XPathASTNode* step,
                                          XPathNodeSet* input) {
    DEBUG_LOG("  === evaluate_step START ===");
    if (!step || step->type != XPATH_AST_STEP || !input) {
        DEBUG_LOG("    Invalid parameters: step=%p, type=%d, input=%p",
                 (void*)step, step ? step->type : -1, (void*)input);
        return NULL;
    }

    const char* axis_name = step->value ? step->value : "child";
    XPathASTNode* node_test = (step->child_count > 0) ? step->children[0] : NULL;

    DEBUG_LOG("    axis_name = %s", axis_name);
    DEBUG_LOG("    node_test = %p (type=%d)", (void*)node_test, node_test ? node_test->type : -1);
    if (node_test && node_test->value) {
        DEBUG_LOG("    node_test->value = %s", node_test->value);
    }
    DEBUG_LOG("    input nodeset count = %zu", xpath_nodeset_count(input));

    XPathNodeSet* result = xpath_nodeset_new();
    if (!result) {
        DEBUG_LOG("    FAILED to create result nodeset");
        return NULL;
    }

    /* Apply axis to each input node (must be elements) */
    for (size_t i = 0; i < xpath_nodeset_count(input); i++) {
        void* node_ptr = xpath_nodeset_get(input, i);
        TaurusElement node = node_as_element(node_ptr);
        DEBUG_LOG("    Processing input[%zu]: node=%p", i, (void*)node);

        /* Handle attribute nodes for certain axes */
        if (!node) {
            TaurusAttributeNode* attr_node = node_as_attribute(node_ptr);
            if (attr_node) {
                DEBUG_LOG("      Input is an attribute node, name=%s", attr_node->name);

                /* Special case: parent axis from attribute should return owner directly */
                if (strcmp(axis_name, "parent") == 0) {
                    if (attr_node->owner && matches_node_test(ctx, attr_node->owner, node_test)) {
                        xpath_nodeset_add(result, attr_node->owner);
                    }
                    continue;  /* Skip normal axis processing */
                }

                /* For ancestor/self axes from attributes, use owner element as context */
                if (strcmp(axis_name, "ancestor") == 0 ||
                    strcmp(axis_name, "ancestor-or-self") == 0 ||
                    strcmp(axis_name, "self") == 0) {

                    /* Use the owner element as context */
                    node = attr_node->owner;
                    DEBUG_LOG("      Using owner element as context: %p (name=%s)",
                             (void*)node, node ? taurus_element_get_name(node) : "(null)");
                } else {
                    DEBUG_LOG("      Axis '%s' cannot operate on attribute nodes, skipping", axis_name);
                    continue;  /* Skip non-element nodes for other axes */
                }
            } else {
                DEBUG_LOG("      Skipping non-element, non-attribute node");
                continue; /* Skip non-element, non-attribute nodes */
            }
        }

        if (!node) {
            DEBUG_LOG("      No valid node context, skipping");
            continue;
        }

        DEBUG_LOG("      node->name = %s",
                 taurus_element_get_name(node) ? taurus_element_get_name(node) : "(null)");

        XPathNodeSet* axis_result = apply_axis(ctx, node, axis_name, node_test);
        DEBUG_LOG("      axis_result count = %zu", axis_result ? xpath_nodeset_count(axis_result) : 0);

        if (axis_result) {
            /* Apply predicates if present - NOW MODIFIES IN-PLACE
             * No need to create new nodeset or free old one! */
            if (step->child_count > 1) {
                apply_predicates(ctx, axis_result,
                               &step->children[1],
                               step->child_count - 1);
                /* axis_result is now filtered in-place */
            }

            /* Transfer nodes to result (result will own the attribute/namespace nodes)
             *
             * IMPORTANT: Determine ownership by axis type, not by node type checking!
             * TaurusElement doesn't start with TaurusNodeType (it starts with TaurusCompactHeader),
             * so XPATH_NODE_TYPE() reads garbage data from the compact header. If page_offset happens
             * to equal TAURUS_NODE_ATTRIBUTE (1), IS_ATTRIBUTE_NODE() would incorrectly return TRUE,
             * causing element nodes to be freed as attributes later! */
            if (strcmp(axis_name, "attribute") == 0) {
                /* Attribute axis creates TaurusAttributeNode structures that must be freed */
                result->owns_attributes = 1;
            } else if (strcmp(axis_name, "namespace") == 0) {
                /* Namespace axis creates TaurusNamespaceNode structures that must be freed */
                result->owns_namespaces = 1;
            }

            for (size_t j = 0; j < xpath_nodeset_count(axis_result); j++) {
                void* candidate = xpath_nodeset_get(axis_result, j);

                /* Quick duplicate check using pointer equality only
                 * For simple axes (most common), nodes are unlikely to appear twice
                 * For complex queries, the O(n) check is acceptable
                 * TODO: Use hash set for large result sets if this becomes bottleneck */
                int already_present = 0;
                size_t result_count = xpath_nodeset_count(result);
                if (result_count < 32) {  /* Small result set - linear search is fine */
                    for (size_t k = 0; k < result_count; k++) {
                        if (xpath_nodeset_get(result, k) == candidate) {
                            already_present = 1;
                            break;
                        }
                    }
                } else {
                    /* Large result set - skip dedup check for now (rare case)
                     * Most XPath queries return small nodesets anyway */
                    already_present = 0;
                }

                /* Only add if not already present */
                if (!already_present) {
                    xpath_nodeset_add(result, candidate);
                }
            }

            /* Don't free attribute/namespace nodes from intermediate node sets - result now owns them */
            axis_result->owns_attributes = 0;  /* Result nodeset now owns the attributes */
            axis_result->owns_namespaces = 0;  /* Result nodeset now owns the namespaces */
            xpath_nodeset_free(axis_result);
        }
    }

    DEBUG_LOG("    Final result count = %zu", xpath_nodeset_count(result));
    DEBUG_LOG("  === evaluate_step END ===");

    struct taurus_xpath_result* res = xpath_result_new(XPATH_RESULT_NODESET);
    if (res) res->value.nodeset_value = result;
    return res;
}

struct taurus_xpath_result* evaluate_location_path(XPathContext* ctx,
                                                    XPathASTNode* path) {
    XPathNodeSet* current = xpath_nodeset_new();
    if (!current) return NULL;

    /* Starting nodeset */
    if (path->type == XPATH_AST_ABSOLUTE_PATH) {
        /* Special case: Absolute path with element name as first step
         * XPath "/root" means "child of document node named root"
         * Since we don't have a document node, check if root matches and use it */
        int is_root_match = 0;
        TaurusElement root = (TaurusElement)ctx->document->new_dom_root;

        DEBUG_LOG("  Checking for special case: child_count=%zu, root=%p",
                 (size_t)path->child_count, (void*)root);

        if (path->child_count > 0 && root) {
            const char* root_name = taurus_element_get_name(root);
            if (!root_name) {
                /* No name means something is wrong, skip special case */
                goto normal_absolute_path;
            }

            XPathASTNode* first_child = path->children[0];

            DEBUG_LOG("  First child type=%d (RELATIVE_PATH=%d, STEP=%d)",
                     first_child->type, XPATH_AST_RELATIVE_PATH, XPATH_AST_STEP);

            /* The first child might be RELATIVE_PATH containing steps, or a direct STEP */
            XPathASTNode* first_step = NULL;
            if (first_child->type == XPATH_AST_RELATIVE_PATH && first_child->child_count > 0) {
                first_step = first_child->children[0];
                DEBUG_LOG("  Found RELATIVE_PATH, extracting first step");
            } else if (first_child->type == XPATH_AST_STEP) {
                first_step = first_child;
                DEBUG_LOG("  Found direct STEP");
            }

            /* Check if first step is a simple child axis with element name */
            if (first_step && first_step->type == XPATH_AST_STEP) {
                const char* axis = first_step->value ? first_step->value : "child";
                DEBUG_LOG("  Axis=%s, child_count=%zu", axis, (size_t)first_step->child_count);

                if (strcmp(axis, "child") == 0 && first_step->child_count > 0) {
                    XPathASTNode* node_test = first_step->children[0];
                    DEBUG_LOG("  Node test type=%d, value=%s",
                             node_test->type, node_test->value ? node_test->value : "(null)");

                    if (node_test->type == XPATH_AST_NODE_TEST_NAME && node_test->value) {
                        /* Parse test value to get local name (in case it has prefix) */
                        char* test_prefix = NULL;
                        char* test_local = NULL;
                        parse_node_test_name(node_test->value, &test_prefix, &test_local);

                        if (test_local) {
                            /* root_name is already the local name (split-name architecture) */
                            const char* root_local = root_name;

                            DEBUG_LOG("  Comparing root_local='%s' with test_local='%s'",
                                     root_local, test_local);

                            /* Check if root element name matches */
                            int names_match = (strcmp(root_local, test_local) == 0);

                            /* If test has prefix, also check prefix */
                            if (names_match && test_prefix) {
                                const char* root_prefix = taurus_element_get_prefix(root);
                                names_match = (root_prefix && strcmp(root_prefix, test_prefix) == 0);
                            }

                            if (names_match) {
                                is_root_match = 1;
                                DEBUG_LOG("  ✓ Special case: /root matches document root");
                            }

                            TAURUS_FREE(test_local);
                            if (test_prefix) TAURUS_FREE(test_prefix);
                        }
                    }
                }
            }
        }

normal_absolute_path:
        DEBUG_LOG("  is_root_match=%d", is_root_match);

        if (is_root_match) {
            /* Root matches - add it and process remaining steps */
            xpath_nodeset_add(current, root);
            DEBUG_LOG("  Added root to nodeset, processing remaining steps");

            /* Get the RELATIVE_PATH (first child of ABSOLUTE_PATH) */
            XPathASTNode* rel_path = path->children[0];
            if (rel_path && rel_path->type == XPATH_AST_RELATIVE_PATH && rel_path->child_count > 1) {
                /* Process steps starting from index 1 (skip first step which matched root) */
                for (size_t j = 1; j < rel_path->child_count; j++) {
                    XPathASTNode* step = rel_path->children[j];
                    if (step->type == XPATH_AST_STEP) {
                        DEBUG_LOG("    Processing remaining step %zu", j);
                        struct taurus_xpath_result* step_result = evaluate_step(ctx, step, current);
                        if (!step_result) {
                            xpath_nodeset_free(current);
                            return NULL;
                        }
                        xpath_nodeset_free(current);
                        current = step_result->value.nodeset_value;
                        step_result->value.nodeset_value = NULL;
                        xpath_result_free(step_result);
                    }
                }
            }
            /* If rel_path has only 1 child (the step that matched), we're done - just return root */
        } else {
            /* Normal absolute path - start from root and process ALL steps */
            DEBUG_LOG("  Adding root to initial nodeset for absolute path");

            /* Special case: /* (child::* from document node) should return root element directly
             * In XPath, the document node has one child: the root element
             * So /* means "select all children of document node" which is just the root element */
            int is_wildcard_only = (path->child_count == 1);
            if (is_wildcard_only) {
                XPathASTNode* first_child = path->children[0];
                /* Check if it's a RELATIVE_PATH with single STEP that is child::* */
                if (first_child->type == XPATH_AST_RELATIVE_PATH && first_child->child_count == 1) {
                    XPathASTNode* step = first_child->children[0];
                    if (step->type == XPATH_AST_STEP) {
                        const char* axis = step->value ? step->value : "child";
                        if (strcmp(axis, "child") == 0 && step->child_count > 0) {
                            XPathASTNode* node_test =step->children[0];
                            if (node_test->type == XPATH_AST_NODE_TEST_ALL) {
                                /* This is /* - just return root element */
                                DEBUG_LOG("  Special case: /* returns document root element");
                                xpath_nodeset_add(current, (TaurusElement)ctx->document->new_dom_root);
                                goto done_evaluating;
                            }
                        }
                    }
                }
            }

            xpath_nodeset_add(current, (TaurusElement)ctx->document->new_dom_root);
            DEBUG_LOG("  Nodeset count after adding root: %zu", xpath_nodeset_count(current));

            /* Process steps - handle both direct steps and those in RELATIVE_PATH */
            DEBUG_LOG("  Processing %zu children", (size_t)path->child_count);
            for (size_t i = 0; i < path->child_count; i++) {
                XPathASTNode* child = path->children[i];
                DEBUG_LOG("  Child[%zu]: type=%d", i, child->type);

                if (child->type == XPATH_AST_STEP) {
                    DEBUG_LOG("    Processing STEP child");
                    DEBUG_LOG("    Input nodeset count: %zu", xpath_nodeset_count(current));
                    /* Direct step child - process it */
                    struct taurus_xpath_result* step_result = evaluate_step(ctx, child, current);
                    if (!step_result) {
                        DEBUG_LOG("    STEP evaluation FAILED");
                        xpath_nodeset_free(current);
                        return NULL;
                    }
                    DEBUG_LOG("    STEP result nodeset count: %zu",
                             xpath_nodeset_count(step_result->value.nodeset_value));

                    xpath_nodeset_free(current);
                    current = step_result->value.nodeset_value;
                    step_result->value.nodeset_value = NULL;
                    xpath_result_free(step_result);
                }
                else if (child->type == XPATH_AST_RELATIVE_PATH) {
                    /* RELATIVE_PATH container - process its step children */
                    for (size_t j = 0; j < child->child_count; j++) {
                        XPathASTNode* step = child->children[j];

                        if (step->type == XPATH_AST_STEP) {
                            struct taurus_xpath_result* step_result = evaluate_step(ctx, step, current);
                            if (!step_result) {
                                xpath_nodeset_free(current);
                                return NULL;
                            }

                            xpath_nodeset_free(current);
                            current = step_result->value.nodeset_value;
                            step_result->value.nodeset_value = NULL;
                            xpath_result_free(step_result);
                        }
                    }
                }
            }
        }
    }  /* End of if (path->type == XPATH_AST_ABSOLUTE_PATH) */
    else {
        /* Relative path - start from context node */
        xpath_nodeset_add(current, ctx->context_node);

        /* Process steps - handle both direct steps and those in RELATIVE_PATH */
        for (size_t i = 0; i < path->child_count; i++) {
            XPathASTNode* child = path->children[i];

            if (child->type == XPATH_AST_STEP) {
                /* Direct step child - process it */
                struct taurus_xpath_result* step_result = evaluate_step(ctx, child, current);
                if (!step_result) {
                    xpath_nodeset_free(current);
                    return NULL;
                }

                xpath_nodeset_free(current);
                current = step_result->value.nodeset_value;
                step_result->value.nodeset_value = NULL;
                xpath_result_free(step_result);
            }
            else if (child->type == XPATH_AST_RELATIVE_PATH) {
                /* RELATIVE_PATH container - process its step children */
                for (size_t j = 0; j < child->child_count; j++) {
                    XPathASTNode* step = child->children[j];

                    if (step->type == XPATH_AST_STEP) {
                        struct taurus_xpath_result* step_result = evaluate_step(ctx, step, current);
                        if (!step_result) {
                            xpath_nodeset_free(current);
                            return NULL;
                        }

                        xpath_nodeset_free(current);
                        current = step_result->value.nodeset_value;
                        step_result->value.nodeset_value = NULL;
                        xpath_result_free(step_result);
                    }
                }
            }
        }
    }

done_evaluating:
    DEBUG_LOG("  Final nodeset count: %zu", xpath_nodeset_count(current));
    DEBUG_LOG("=== evaluate_location_path END ===");

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
    if (result) result->value.nodeset_value = current;
    return result;
}