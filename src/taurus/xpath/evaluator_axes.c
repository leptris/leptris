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

/* Helper: Collect descendants recursively */
static void collect_descendants(XPathContext* ctx,
                               TaurusElement node,
                               XPathNodeSet* result,
                               XPathASTNode* node_test) {
    if (!node) return;

    /* Iterate through children using compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        TaurusNode* child_node = (TaurusNode*)child_elem;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            TaurusElement child = child_elem;
            if (matches_node_test(ctx, child, node_test)) {
                xpath_nodeset_add(result, child);
            }
            collect_descendants(ctx, child, result, node_test);
        }
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

/* Helper: Collect descendants or self */
static void collect_descendants_or_self(XPathContext* ctx,
                                       TaurusElement node,
                                       XPathNodeSet* result,
                                       XPathASTNode* node_test) {
    if (!node) return;

    if (matches_node_test(ctx, node, node_test)) {
        xpath_nodeset_add(result, node);
    }

    /* Iterate through children using compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        TaurusNode* child_node = (TaurusNode*)child_elem;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            collect_descendants_or_self(ctx, child_elem, result, node_test);
        }
        child_elem = taurus_element_get_next_sibling(child_elem);
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

    attr_node->namespace_uri = attr->namespace_uri ? taurus_strdup(attr->namespace_uri) : NULL;
    attr_node->owner = owner;

    return attr_node;
}

/* Helper: Create text node from element text content */
static XPathTextNode* create_text_node(TaurusElement elem) {
    if (!elem) return NULL;

    /* Get text content from element */
    const char* text = taurus_element_get_text_content(elem);
    if (!text || text[0] == '\0') return NULL;

    XPathTextNode* text_node = TAURUS_ALLOC(XPathTextNode);
    if (!text_node) return NULL;

    text_node->node_type = TAURUS_NODE_TEXT;
    text_node->content = taurus_strdup(text);
    text_node->owner = elem;

    if (!text_node->content) {
        TAURUS_FREE(text_node);
        return NULL;
    }

    return text_node;
}

/* Helper: Check if node test is for text nodes */
static int is_text_node_test(XPathASTNode* test) {
    if (!test) return 0;
    if (test->type != XPATH_AST_NODE_TEST_TYPE) return 0;
    if (!test->value) return 0;
    return (strcmp(test->value, "text") == 0);
}

/* child:: axis */
static XPathNodeSet* axis_child(XPathContext* ctx, TaurusElement node,
                               XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new();
    if (!result || !node) return result;

    /* Check if this is a text() node test */
    int text_test = is_text_node_test(test);

    /* Iterate through children using compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        TaurusNode* child_node = (TaurusNode*)child_elem;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            TaurusElement child = child_elem;
            if (matches_node_test(ctx, child, test)) {
                xpath_nodeset_add(result, child);
            }
        }
        child_elem = taurus_element_get_next_sibling(child_elem);
    }

    /* For text() node test, also add text content of the current element */
    if (text_test) {
        XPathTextNode* text_node = create_text_node(node);
        if (text_node) {
            xpath_nodeset_add(result, text_node);
        }
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

    if (matches_node_test(ctx, parent, test)) {
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
        if (matches_node_test(ctx, current, test)) {
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

    if (matches_node_test(ctx, node, test)) {
        xpath_nodeset_add(result, node);
    }

    TaurusElement current = taurus_element_get_parent(node);
    while (current) {
        if (matches_node_test(ctx, current, test)) {
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

    if (matches_node_test(ctx, node, test)) {
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
            if (matches_node_test(ctx, sibling, test)) {
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
        if (matches_node_test(ctx, sibling, test)) {
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
            if (matches_node_test(ctx, sibling, test)) {
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
        if (matches_node_test(ctx, sibling, test)) {
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

/* Apply axis dispatcher */
XPathNodeSet* apply_axis(XPathContext* ctx, TaurusElement node,
                         const char* axis_name, XPathASTNode* test) {
    DEBUG_LOG("      === apply_axis: %s ===", axis_name ? axis_name : "(null/child)");
    if (!axis_name) {
        DEBUG_LOG("        Using default 'child' axis");
        return axis_child(ctx, node, test);
    }

    if (strcmp(axis_name, "child") == 0) {
        DEBUG_LOG("        Using 'child' axis");
        return axis_child(ctx, node, test);
    }
    if (strcmp(axis_name, "descendant") == 0) {
        DEBUG_LOG("        Using 'descendant' axis");
        return axis_descendant(ctx, node, test);
    }
    if (strcmp(axis_name, "descendant-or-self") == 0) {
        DEBUG_LOG("        Using 'descendant-or-self' axis");
        return axis_descendant_or_self(ctx, node, test);
    }
    if (strcmp(axis_name, "parent") == 0) return axis_parent(ctx, node, test);
    if (strcmp(axis_name, "ancestor") == 0) return axis_ancestor(ctx, node, test);
    if (strcmp(axis_name, "ancestor-or-self") == 0)
        return axis_ancestor_or_self(ctx, node, test);
    if (strcmp(axis_name, "self") == 0) return axis_self(ctx, node, test);
    if (strcmp(axis_name, "following-sibling") == 0)
        return axis_following_sibling(ctx, node, test);
    if (strcmp(axis_name, "preceding-sibling") == 0)
        return axis_preceding_sibling(ctx, node, test);
    if (strcmp(axis_name, "following") == 0) return axis_following(ctx, node, test);
    if (strcmp(axis_name, "preceding") == 0) return axis_preceding(ctx, node, test);
    if (strcmp(axis_name, "attribute") == 0) return axis_attribute(ctx, node, test);
    if (strcmp(axis_name, "namespace") == 0) return axis_namespace(ctx, node, test);

    return xpath_nodeset_new();  /* Unknown axis */
}