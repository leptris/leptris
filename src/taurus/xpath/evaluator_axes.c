/* evaluator_axes.c - XPath axis implementations
 * Copyright (c) 2024, Ribose Inc.
 *
 * All 13 XPath 1.0 axes per specification
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
 * Axis Implementations (All 13 XPath Axes)
 * ============================================================================ */

/* Helper: Collect descendants recursively (TODO 109: walks all node
 * types, not just elements). */
static void collect_descendants(XPathContext* ctx,
                               TaurusElement node,
                               XPathNodeSet* result,
                               XPathASTNode* node_test) {
    if (!node) return;

    /* Walk the raw child list — any node type (text, comment, cdata, pi). */
    TaurusNode* child = taurus_elem_first_child(node);
    while (child) {
        if (matches_node_test(ctx, child, node_test)) {
            xpath_nodeset_add(result, child);
        }
        /* Recurse into element children only (other nodes have no children). */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            collect_descendants(ctx, (TaurusElement)child, result, node_test);
        }
        child = taurus_node_get_next_sibling(child);
    }
}

/* Helper: Collect descendants or self (TODO 109: walks all node types). */
static void collect_descendants_or_self(XPathContext* ctx,
                                       TaurusElement node,
                                       XPathNodeSet* result,
                                       XPathASTNode* node_test) {
    if (!node) return;

    if (matches_node_test(ctx, (TaurusNode*)node, node_test)) {
        xpath_nodeset_add(result, node);
    }

    /* Walk the raw child list — any node type. */
    TaurusNode* child = taurus_elem_first_child(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            collect_descendants_or_self(ctx, (TaurusElement)child, result, node_test);
        } else {
            /* Non-element child: test it directly (no recursion). */
            if (matches_node_test(ctx, child, node_test)) {
                xpath_nodeset_add(result, child);
            }
        }
        child = taurus_node_get_next_sibling(child);
    }
}

/* Helper: Create attribute node from taurus_attribute */
static TaurusAttributeNode* create_attribute_node(struct taurus_attribute* attr,
                                                   TaurusElement owner) {
    if (!attr) return NULL;

    TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
    if (!attr_node) return NULL;

    attr_node->node_type = TAURUS_NODE_ATTRIBUTE;

    /* Handle name - support both cached and StringView-based attributes */
    if (attr->name) {
        /* Use cached name if available */
        attr_node->name = taurus_strdup(attr->name);
    } else if (!taurus_sv_is_empty(&attr->name_view)) {
        /* Convert StringView to C string */
        size_t len = attr->name_view.length;
        char* name_copy = TAURUS_ALLOC_N(char, len + 1);
        if (name_copy) {
            memcpy(name_copy, attr->name_view.data, len);
            name_copy[len] = '\0';
            attr_node->name = name_copy;
        } else {
            attr_node->name = NULL;
        }
    } else {
        attr_node->name = NULL;
    }

    /* Handle value - support both cached and StringView-based attributes */
    if (attr->value) {
        /* Use cached value if available */
        attr_node->value = taurus_strdup(attr->value);
    } else if (!taurus_sv_is_empty(&attr->value_view)) {
        /* Convert StringView to C string */
        size_t len = attr->value_view.length;
        char* value_copy = TAURUS_ALLOC_N(char, len + 1);
        if (value_copy) {
            memcpy(value_copy, attr->value_view.data, len);
            value_copy[len] = '\0';
            attr_node->value = value_copy;
        } else {
            attr_node->value = NULL;
        }
    } else {
        attr_node->value = NULL;
    }

    {
        const char* src_ns = attr_get_namespace_uri(attr);
        attr_node->namespace_uri = src_ns ? taurus_strdup(src_ns) : NULL;
    }
    attr_node->owner = owner;

    return attr_node;
}

/* child:: axis (TODO 109: walks all node types, not just elements).
 * Note: only elements have children. Text/comment/CDATA/PI never have
 * children — return an empty nodeset for non-element context. */
static XPathNodeSet* axis_child(XPathContext* ctx, TaurusNode* node,
                               XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return result;

    /* Walk the raw child list — any node type. */
    TaurusNode* child = taurus_elem_first_child((TaurusElement)node);
    while (child) {
        if (matches_node_test(ctx, child, test)) {
            xpath_nodeset_add(result, child);
        }
        child = taurus_node_get_next_sibling(child);
    }

    return result;
}

/* descendant:: axis */
static XPathNodeSet* axis_descendant(XPathContext* ctx, TaurusElement node,
                                    XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;
    collect_descendants(ctx, node, result, test);
    return result;
}

/* descendant-or-self:: axis */
static XPathNodeSet* axis_descendant_or_self(XPathContext* ctx,
                                            TaurusElement node,
                                            XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;
    collect_descendants_or_self(ctx, node, result, test);
    return result;
}

/* parent:: axis */
static XPathNodeSet* axis_parent(XPathContext* ctx, TaurusElement node,
                                XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    /* Get parent element using compact accessor */
    TaurusElement parent = taurus_element_get_parent(node);

    if (!parent) return result;

    if (matches_node_test(ctx, (TaurusNode*)parent, test)) {
        xpath_nodeset_add(result, parent);
    }

    return result;
}

/* ancestor:: axis */
static XPathNodeSet* axis_ancestor(XPathContext* ctx, TaurusElement node,
                                  XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    TaurusElement current = taurus_element_get_parent(node);
    while (current) {
        if (matches_node_test(ctx, (TaurusNode*)current, test)) {
            xpath_nodeset_add(result, current);
        }
        current = taurus_element_get_parent(current);
    }

    return result;
}

/* ancestor-or-self:: axis */
static XPathNodeSet* axis_ancestor_or_self(XPathContext* ctx,
                                          TaurusElement node,
                                          XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    if (matches_node_test(ctx, (TaurusNode*)node, test)) {
        xpath_nodeset_add(result, node);
    }

    TaurusElement current = taurus_element_get_parent(node);
    while (current) {
        if (matches_node_test(ctx, (TaurusNode*)current, test)) {
            xpath_nodeset_add(result, current);
        }
        current = taurus_element_get_parent(current);
    }

    return result;
}

/* self:: axis */
static XPathNodeSet* axis_self(XPathContext* ctx, TaurusElement node,
                              XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    if (matches_node_test(ctx, (TaurusNode*)node, test)) {
        xpath_nodeset_add(result, node);
    }

    return result;
}

/* following-sibling:: axis */
static XPathNodeSet* axis_following_sibling(XPathContext* ctx,
                                           TaurusElement node,
                                           XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    TaurusElement parent = taurus_element_get_parent(node);
    if (!parent) return result;

    int found = 0;

    /* Iterate through siblings using compact accessor functions */
    TaurusElement sibling = taurus_element_get_first_child(parent);
    while (sibling) {
        if (sibling == node) {
            found = 1;
        } else if (found) {
            if (matches_node_test(ctx, (TaurusNode*)sibling, test)) {
                xpath_nodeset_add(result, sibling);
            }
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    return result;
}

/* preceding-sibling:: axis */
static XPathNodeSet* axis_preceding_sibling(XPathContext* ctx,
                                           TaurusElement node,
                                           XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    TaurusElement parent = taurus_element_get_parent(node);
    if (!parent) return result;

    /* Collect all preceding siblings in forward order using compact accessor functions */
    TaurusElement sibling = taurus_element_get_first_child(parent);
    while (sibling && sibling != node) {
        if (matches_node_test(ctx, (TaurusNode*)sibling, test)) {
            xpath_nodeset_add(result, sibling);
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    /* Reverse the nodeset to match XPath spec (reverse document order) */
    if (result->count > 1) {
        for (size_t i = 0; i < result->count / 2; i++) {
            void* temp = result->nodes[i];
            result->nodes[i] = result->nodes[result->count - 1 - i];
            result->nodes[result->count - 1 - i] = temp;
        }
    }

    return result;
}

/* following:: axis - all nodes after context in document order */
static XPathNodeSet* axis_following(XPathContext* ctx, TaurusElement node,
                                   XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    /* Get following siblings and their descendants */
    TaurusElement parent = taurus_element_get_parent(node);
    if (!parent) return result;

    int found = 0;

    /* Iterate through siblings using compact accessor functions */
    TaurusElement sibling = taurus_element_get_first_child(parent);
    while (sibling) {
        if (sibling == node) {
            found = 1;
        } else if (found) {
            if (matches_node_test(ctx, (TaurusNode*)sibling, test)) {
                xpath_nodeset_add(result, sibling);
            }
            collect_descendants(ctx, sibling, result, test);
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    /* Recursively get parent's following nodes */
    XPathNodeSet* parent_following = axis_following(ctx, parent, test);
    if (parent_following) {
        for (size_t i = 0; i < xpath_nodeset_count(parent_following); i++) {
            xpath_nodeset_add(result, xpath_nodeset_get(parent_following, i));
        }
        xpath_nodeset_free(parent_following);
    }

    return result;
}

/* preceding:: axis - all nodes before context in document order */
static XPathNodeSet* axis_preceding(XPathContext* ctx, TaurusElement node,
                                   XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    /* Get preceding siblings and their descendants */
    TaurusElement parent = taurus_element_get_parent(node);
    if (!parent) return result;

    /* Iterate through siblings using compact accessor functions */
    TaurusElement sibling = taurus_element_get_first_child(parent);
    while (sibling && sibling != node) {
        if (matches_node_test(ctx, (TaurusNode*)sibling, test)) {
            xpath_nodeset_add(result, sibling);
        }
        collect_descendants(ctx, sibling, result, test);
        sibling = taurus_element_get_next_sibling(sibling);
    }

    /* Recursively get parent's preceding nodes */
    XPathNodeSet* parent_preceding = axis_preceding(ctx, parent, test);
    if (parent_preceding) {
        for (size_t i = 0; i < xpath_nodeset_count(parent_preceding); i++) {
            xpath_nodeset_add(result, xpath_nodeset_get(parent_preceding, i));
        }
        xpath_nodeset_free(parent_preceding);
    }

    return result;
}

/* attribute:: axis
 *
 * Returns attributes as proper TaurusAttributeNode structures.
 * These nodes have type TAURUS_NODE_ATTRIBUTE and work with all
 * type conversion functions properly.
 */
static XPathNodeSet* axis_attribute(XPathContext* ctx, TaurusElement node,
                                   XPathASTNode* test) {
    DEBUG_LOG("        === axis_attribute START ===");
    DEBUG_LOG("        node=%p, name=%s", (void*)node, node ? taurus_element_get_name(node) : "(null)");
    DEBUG_LOG("        attr_count=%zu", node ? (size_t)taurus_element_attribute_count(node) : 0);

    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) {
        DEBUG_LOG("        EARLY RETURN: result=%p, node=%p", (void*)result, (void*)node);
        return result;
    }

    /* Iterate through element's attributes using compact accessor functions */
    size_t attr_count = taurus_element_attribute_count(node);
    for (size_t i = 0; i < attr_count; i++) {
        /* Walk the attribute linked list to get the i-th attribute */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(node);
        for (size_t j = 0; j < i && attr; j++) {
            attr = attr->next;
        }

        /* Validate attribute pointer before accessing */
        if (!attr) {
            DEBUG_LOG("        [%zu] SKIPPED: attr is NULL", i);
            continue;
        }

        /* Sanity check: attribute should point to valid memory */
        if ((uintptr_t)attr < 0x1000) {
            DEBUG_LOG("        [%zu] SKIPPED: attr has invalid pointer %p", i, (void*)attr);
            continue;
        }

        DEBUG_LOG("        [%zu] attr=%p", i, (void*)attr);
        DEBUG_LOG("        [%zu] attr->name=%p, attr->value=%p",
                 i, attr->name, attr->value);

        /* Check if attribute matches node test */
        int matches = 0;
        if (test && test->type == XPATH_AST_NODE_TEST_NAME) {
            /* Specific attribute name test - handle both StringView and cached name */
            int name_matches = 0;
            if (attr->name) {
                /* Use cached name if available */
                name_matches = (test->value && strcmp(test->value, attr->name) == 0);
            } else if (!taurus_sv_is_empty(&attr->name_view)) {
                /* Compare with StringView */
                name_matches = taurus_sv_equals_cstr(&attr->name_view, test->value);
            }
            matches = name_matches;
            DEBUG_LOG("        [%zu] NAME test: looking for '%s', matches=%d",
                     i, test->value ? test->value : "(null)", matches);
        } else if (test && test->type == XPATH_AST_NODE_TEST_ALL) {
            /* Wildcard - matches all attributes */
            matches = 1;
            DEBUG_LOG("        [%zu] WILDCARD test: matches=%d", i, matches);
        } else if (!test) {
            /* No test means match all */
            matches = 1;
            DEBUG_LOG("        [%zu] NO test: matches=%d", i, matches);
        }

        if (matches) {
            /* Create proper attribute node */
            DEBUG_LOG("        [%zu] Creating attribute node...", i);
            TaurusAttributeNode* attr_node = create_attribute_node(attr, node);
            DEBUG_LOG("        [%zu] attr_node=%p", i, (void*)attr_node);
            if (attr_node) {
                DEBUG_LOG("        [%zu] attr_node->node_type=%d (should be 1)",
                         i, (int)attr_node->node_type);
                DEBUG_LOG("        [%zu] attr_node->name=%s",
                         i, attr_node->name ? attr_node->name : "(null)");
                DEBUG_LOG("        [%zu] attr_node->value=%s",
                         i, attr_node->value ? attr_node->value : "(null)");
                DEBUG_LOG("        [%zu] Adding to nodeset...", i);
                xpath_nodeset_add(result, (void*)attr_node);
                DEBUG_LOG("        [%zu] Added. Nodeset count now: %zu",
                         i, xpath_nodeset_count(result));
            } else {
                DEBUG_LOG("        [%zu] FAILED to create attr_node!", i);
            }
        }
    }

    DEBUG_LOG("        Final nodeset count: %zu", xpath_nodeset_count(result));
    DEBUG_LOG("        === axis_attribute END ===");
    return result;
}

/* Helper: Check if prefix has already been seen */
static int is_prefix_seen(char** seen_prefixes, size_t seen_count, const char* prefix) {
    for (size_t i = 0; i < seen_count; i++) {
        /* Both NULL = default namespace, already seen */
        if (!prefix && !seen_prefixes[i]) return 1;
        /* One NULL, one not = different */
        if (!prefix || !seen_prefixes[i]) continue;
        /* Both non-NULL, compare strings */
        if (strcmp(prefix, seen_prefixes[i]) == 0) return 1;
    }
    return 0;
}

/* namespace:: axis */
static XPathNodeSet* axis_namespace(XPathContext* ctx, TaurusElement node,
                                   XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    /* Track seen prefixes for deduplication (child overrides parent) */
    char** seen_prefixes = NULL;
    size_t seen_count = 0;
    size_t seen_capacity = 0;

    /* Collect namespaces from element and ancestors using compact accessor functions */
    TaurusElement current = node;
    while (current) {
        /* Get namespace from element (inline in compact mode) */
        const char* ns_prefix = taurus_element_get_prefix(current);
        const char* ns_uri = taurus_element_get_namespace_uri(current);

        /* Skip if already seen (inheritance override) or if no namespace */
        if (ns_uri && !is_prefix_seen(seen_prefixes, seen_count, ns_prefix)) {
            /* Check if matches node test */
            int matches = 0;
            if (!test || test->type == XPATH_AST_NODE_TEST_ALL) {
                matches = 1;  /* Match all */
            } else if (test->type == XPATH_AST_NODE_TEST_NAME) {
                matches = (test->value && ns_prefix &&
                          strcmp(test->value, ns_prefix) == 0);
            }

            if (matches) {
                TaurusNamespaceNode* ns_node = TAURUS_ALLOC(TaurusNamespaceNode);
                if (ns_node) {
                    ns_node->node_type = TAURUS_NODE_NAMESPACE;
                    ns_node->prefix = ns_prefix ? taurus_strdup(ns_prefix) : NULL;
                    ns_node->uri = taurus_strdup(ns_uri);
                    ns_node->owner = current;
                    xpath_nodeset_add(result, (void*)ns_node);
                }
            }

            /* Mark as seen */
            if (seen_count >= seen_capacity) {
                size_t new_cap = seen_capacity == 0 ? 4 : seen_capacity * 2;
                char** new_arr = (char**)realloc(seen_prefixes, new_cap * sizeof(char*));
                if (!new_arr) { free(seen_prefixes); seen_prefixes = NULL; break; }
                seen_prefixes = new_arr;
                seen_capacity = new_cap;
            }
            seen_prefixes[seen_count++] = ns_prefix ? taurus_strdup(ns_prefix) : NULL;
        }

        /* Move to parent using compact accessor */
        current = taurus_element_get_parent(current);
    }

    /* Always add implicit xml namespace if not already present */
    if (!is_prefix_seen(seen_prefixes, seen_count, "xml")) {
        int matches = (!test || test->type == XPATH_AST_NODE_TEST_ALL ||
                      (test->type == XPATH_AST_NODE_TEST_NAME &&
                       test->value && strcmp(test->value, "xml") == 0));

        if (matches) {
            TaurusNamespaceNode* xml_ns = TAURUS_ALLOC(TaurusNamespaceNode);
            if (xml_ns) {
                xml_ns->node_type = TAURUS_NODE_NAMESPACE;
                xml_ns->prefix = taurus_strdup("xml");
                xml_ns->uri = taurus_strdup("http://www.w3.org/XML/1998/namespace");
                xml_ns->owner = node;
                xpath_nodeset_add(result, (void*)xml_ns);
            }
        }
    }

    /* Cleanup seen prefixes */
    for (size_t i = 0; i < seen_count; i++) {
        if (seen_prefixes[i]) free(seen_prefixes[i]);
    }
    free(seen_prefixes);

    /* Mark that result owns namespace nodes for cleanup */
    result->owns_namespaces = 1;

    return result;
}

/* Map an axis name string to its enum (TODO 113 Phase 1 perf).
 * Called once at parse time so apply_axis can switch on the enum
 * instead of strcmp'ing the name on every axis dispatch. */
XPathAxisType xpath_axis_from_name(const char* name) {
    if (!name || !*name) return XPATH_AXIS_CHILD;
    /* First-character dispatch: most axes have a unique first char,
     * collapsing 13 strcmp calls into a single switch + one compare.
     * axis_id is set once at parse time, so this runs ~13× less
     * often than apply_axis itself (TODO 113 Phase 1). */
    switch (name[0]) {
        case 'a':
            if (strcmp(name, "ancestor") == 0) return XPATH_AXIS_ANCESTOR;
            if (strcmp(name, "ancestor-or-self") == 0) return XPATH_AXIS_ANCESTOR_OR_SELF;
            if (strcmp(name, "attribute") == 0) return XPATH_AXIS_ATTRIBUTE;
            break;
        case 'c':
            if (strcmp(name, "child") == 0) return XPATH_AXIS_CHILD;
            break;
        case 'd':
            if (strcmp(name, "descendant") == 0) return XPATH_AXIS_DESCENDANT;
            if (strcmp(name, "descendant-or-self") == 0) return XPATH_AXIS_DESCENDANT_OR_SELF;
            break;
        case 'f':
            if (strcmp(name, "following") == 0) return XPATH_AXIS_FOLLOWING;
            if (strcmp(name, "following-sibling") == 0) return XPATH_AXIS_FOLLOWING_SIBLING;
            break;
        case 'n':
            if (strcmp(name, "namespace") == 0) return XPATH_AXIS_NAMESPACE;
            break;
        case 'p':
            if (strcmp(name, "parent") == 0) return XPATH_AXIS_PARENT;
            if (strcmp(name, "preceding") == 0) return XPATH_AXIS_PRECEDING;
            if (strcmp(name, "preceding-sibling") == 0) return XPATH_AXIS_PRECEDING_SIBLING;
            break;
        case 's':
            if (strcmp(name, "self") == 0) return XPATH_AXIS_SELF;
            break;
    }
    return XPATH_AXIS_CHILD;
}

/* Apply axis dispatcher.
 * TODO 109: accepts TaurusNode* so the descendant-or-self expansion
 * of // can pass non-element nodes through. Most axes only make
 * sense on element context (child, descendant, sibling, etc.); the
 * dispatcher returns an empty nodeset for those if the context is
 * not an element.
 *
 * TODO 113 Phase 1: switches on the pre-computed axis_id from the
 * AST node, avoiding the strcmp chain. Falls back to from_name if
 * axis_id wasn't set (defensive — should always be set by parser). */
XPathNodeSet* apply_axis(XPathContext* ctx, TaurusNode* node,
                         const char* axis_name, XPathASTNode* test) {
    DEBUG_LOG("      === apply_axis: %s ===", axis_name ? axis_name : "(null/child)");

    XPathAxisType axis_id = test ? test->axis_id : XPATH_AXIS_CHILD;
    if (axis_name && *axis_name && axis_id == XPATH_AXIS_CHILD &&
        strcmp(axis_name, "child") != 0) {
        /* Defensive fallback: re-derive from name if axis_id wasn't
         * set to the right value but the name is non-default. */
        axis_id = xpath_axis_from_name(axis_name);
    }

    int element_only = 0;  /* set per-axis below */

    /* Fast path: child is the default axis. Skip the element_only
     * check and dispatch directly. */
    if (axis_id == XPATH_AXIS_CHILD) {
        return axis_child(ctx, node, test);
    }

    /* Most axes require an element context. */
    element_only = (axis_id == XPATH_AXIS_DESCENDANT ||
                    axis_id == XPATH_AXIS_DESCENDANT_OR_SELF ||
                    axis_id == XPATH_AXIS_FOLLOWING_SIBLING ||
                    axis_id == XPATH_AXIS_PRECEDING_SIBLING ||
                    axis_id == XPATH_AXIS_FOLLOWING ||
                    axis_id == XPATH_AXIS_PRECEDING);

    if (element_only && node && node->type != TAURUS_NODE_TYPE_ELEMENT) {
        return xpath_nodeset_new();
    }

    TaurusElement elem = (TaurusElement)node;
    switch (axis_id) {
        case XPATH_AXIS_DESCENDANT:
            return axis_descendant(ctx, elem, test);
        case XPATH_AXIS_DESCENDANT_OR_SELF:
            return axis_descendant_or_self(ctx, elem, test);
        case XPATH_AXIS_PARENT:
            return axis_parent(ctx, elem, test);
        case XPATH_AXIS_ANCESTOR:
            return axis_ancestor(ctx, elem, test);
        case XPATH_AXIS_ANCESTOR_OR_SELF:
            return axis_ancestor_or_self(ctx, elem, test);
        case XPATH_AXIS_SELF:
            return axis_self(ctx, elem, test);
        case XPATH_AXIS_FOLLOWING_SIBLING:
            return axis_following_sibling(ctx, elem, test);
        case XPATH_AXIS_PRECEDING_SIBLING:
            return axis_preceding_sibling(ctx, elem, test);
        case XPATH_AXIS_FOLLOWING:
            return axis_following(ctx, elem, test);
        case XPATH_AXIS_PRECEDING:
            return axis_preceding(ctx, elem, test);
        case XPATH_AXIS_ATTRIBUTE:
            return axis_attribute(ctx, elem, test);
        case XPATH_AXIS_NAMESPACE:
            return axis_namespace(ctx, elem, test);
        default:
            return xpath_nodeset_new();  /* Unknown axis */
    }
}