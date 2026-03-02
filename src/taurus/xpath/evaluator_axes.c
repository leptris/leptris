/* evaluator_axes.c - XPath axis implementations
 * Copyright (c) 2024, Ribose Inc.
 *
 * All 13 XPath 1.0 axes per specification
 */

#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include "../memory/pool.h"  /* For pool allocation optimization */
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

/* Helper: Collect descendants recursively - with depth limit */
#define COLLECT_DESCENDANTS_MAX_DEPTH 1000

static void collect_descendants_impl(XPathContext* ctx,
                               TaurusElement node,
                               XPathNodeSet* result,
                               XPathASTNode* node_test,
                               int depth) {
    if (!node || depth > COLLECT_DESCENDANTS_MAX_DEPTH) return;

    /* Iterate through children using compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        TaurusNode* child_node = (TaurusNode*)child_elem;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            TaurusElement child = child_elem;
            if (matches_node_test(ctx, child, node_test)) {
                xpath_nodeset_add(result, child);
            }
            collect_descendants_impl(ctx, child, result, node_test, depth + 1);
        }
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

static void collect_descendants(XPathContext* ctx,
                               TaurusElement node,
                               XPathNodeSet* result,
                               XPathASTNode* node_test) {
    collect_descendants_impl(ctx, node, result, node_test, 0);
}

/* Helper: Collect descendants or self - with depth limit */
static void collect_descendants_or_self_impl(XPathContext* ctx,
                                       TaurusElement node,
                                       XPathNodeSet* result,
                                       XPathASTNode* node_test,
                                       int depth) {
    if (!node || depth > COLLECT_DESCENDANTS_MAX_DEPTH) return;

    if (matches_node_test(ctx, node, node_test)) {
        xpath_nodeset_add(result, node);
    }

    /* Iterate through children using compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        TaurusNode* child_node = (TaurusNode*)child_elem;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            collect_descendants_or_self_impl(ctx, child_elem, result, node_test, depth + 1);
        }
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

static void collect_descendants_or_self(XPathContext* ctx,
                                       TaurusElement node,
                                       XPathNodeSet* result,
                                       XPathASTNode* node_test) {
    collect_descendants_or_self_impl(ctx, node, result, node_test, 0);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;
    collect_descendants(ctx, node, result, test);
    return result;
}

/* descendant-or-self:: axis */
static XPathNodeSet* axis_descendant_or_self(XPathContext* ctx,
                                            TaurusElement node,
                                            XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;
    collect_descendants_or_self(ctx, node, result, test);
    return result;
}

/* parent:: axis */
static XPathNodeSet* axis_parent(XPathContext* ctx, TaurusElement node,
                                XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    /* OPTIMIZED: Start from the next sibling directly instead of
     * iterating from the first child. This is O(n) -> O(k) where
     * k is the number of following siblings. */
    TaurusElement sibling = taurus_element_get_next_sibling(node);
    while (sibling) {
        if (matches_node_test(ctx, sibling, test)) {
            xpath_nodeset_add(result, sibling);
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    return result;
}

/* preceding-sibling:: axis */
static XPathNodeSet* axis_preceding_sibling(XPathContext* ctx,
                                           TaurusElement node,
                                           XPathASTNode* test) {
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
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

    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) {
        DEBUG_LOG("        EARLY RETURN: result=%p, node=%p", (void*)result, (void*)node);
        return result;
    }

    /* OPTIMIZED: Iterate through attributes directly instead of O(n²) linked list walk
     * This changes from O(n²) to O(n) where n is attribute count */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(node);
    while (attr) {
        /* Sanity check: attribute should point to valid memory */
        if ((uintptr_t)attr < 0x1000) {
            DEBUG_LOG("        SKIPPED: attr has invalid pointer %p", (void*)attr);
            attr = attr->next;
            continue;
        }

        /* SKIP namespace declarations (xmlns, xmlns:*) - per XPath spec they are NOT attributes */
        const char* attr_name = attr->name;
        TaurusStringView* attr_name_view = &attr->name_view;
        int is_ns_decl = 0;
        if (attr_name) {
            if (strcmp(attr_name, "xmlns") == 0 || strncmp(attr_name, "xmlns:", 6) == 0) {
                is_ns_decl = 1;
            }
        } else if (!taurus_sv_is_empty(attr_name_view)) {
            if ((attr_name_view->length == 5 && memcmp(attr_name_view->data, "xmlns", 5) == 0) ||
                (attr_name_view->length > 6 && memcmp(attr_name_view->data, "xmlns:", 6) == 0)) {
                is_ns_decl = 1;
            }
        }
        if (is_ns_decl) {
            DEBUG_LOG("        SKIPPED: namespace declaration");
            attr = attr->next;
            continue;
        }

        DEBUG_LOG("        attr=%p", (void*)attr);
        DEBUG_LOG("        attr->name=%p, attr->value=%p", attr->name, attr->value);

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
            DEBUG_LOG("        NAME test: looking for '%s', matches=%d",
                     test->value ? test->value : "(null)", matches);
        } else if (test && test->type == XPATH_AST_NODE_TEST_ALL) {
            /* Wildcard - matches all attributes */
            matches = 1;
            DEBUG_LOG("        WILDCARD test: matches=%d", matches);
        } else if (!test) {
            /* No test means match all */
            matches = 1;
            DEBUG_LOG("        NO test: matches=%d", matches);
        }

        if (matches) {
            /* Create proper attribute node */
            DEBUG_LOG("        Creating attribute node...");
            TaurusAttributeNode* attr_node = create_attribute_node(attr, node);
            DEBUG_LOG("        attr_node=%p", (void*)attr_node);
            if (attr_node) {
                DEBUG_LOG("        attr_node->node_type=%d (should be 1)",
                         (int)attr_node->node_type);
                DEBUG_LOG("        attr_node->name=%s",
                         attr_node->name ? attr_node->name : "(null)");
                DEBUG_LOG("        attr_node->value=%s",
                         attr_node->value ? attr_node->value : "(null)");
                DEBUG_LOG("        Adding to nodeset...");
                xpath_nodeset_add(result, (void*)attr_node);
                DEBUG_LOG("        Added. Nodeset count now: %zu",
                         xpath_nodeset_count(result));
            } else {
                DEBUG_LOG("        FAILED to create attr_node!");
            }
        }

        attr = attr->next;
    }

    DEBUG_LOG("        Final nodeset count: %zu", xpath_nodeset_count(result));
    DEBUG_LOG("        === axis_attribute END ===");
    return result;
}

/* PERFORMANCE: Single-allocation namespace node creation
 * Allocates struct + prefix string + uri string in one memory block.
 * This reduces 3 heap allocations to 1 per namespace node.
 */
static TaurusNamespaceNode* alloc_namespace_node_single(
    const char* prefix,
    const char* uri,
    TaurusElement owner
) {
    if (!uri) return NULL;

    size_t prefix_len = prefix ? strlen(prefix) + 1 : 0;
    size_t uri_len = strlen(uri) + 1;

    /* Allocate struct + strings in one block */
    size_t total_size = sizeof(TaurusNamespaceNode) + prefix_len + uri_len;
    char* block = (char*)TAURUS_ALLOC_N(char, total_size);
    if (!block) return NULL;

    TaurusNamespaceNode* ns_node = (TaurusNamespaceNode*)block;

    /* Place strings after the struct */
    char* string_area = block + sizeof(TaurusNamespaceNode);

    ns_node->node_type = TAURUS_NODE_NAMESPACE;
    ns_node->owner = owner;

    if (prefix && prefix_len > 0) {
        ns_node->prefix = string_area;
        memcpy(ns_node->prefix, prefix, prefix_len);
        string_area += prefix_len;
    } else {
        ns_node->prefix = NULL;
    }

    ns_node->uri = string_area;
    memcpy(ns_node->uri, uri, uri_len);

    return ns_node;
}

/* Helper: Create namespace node from taurus_namespace */
static TaurusNamespaceNode* create_namespace_node(struct taurus_namespace* ns,
                                                    TaurusElement owner) {
    if (!ns) return NULL;
    return alloc_namespace_node_single(ns->prefix, ns->uri, owner);
}

/* Helper: Check if prefix has already been seen using inline array for small counts */
#define MAX_INLINE_PREFIXES 8

static int is_prefix_seen_inline(const char* seen_prefixes[], size_t seen_count, const char* prefix) {
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
    XPathNodeSet* result = xpath_nodeset_new_pooled(ctx);
    if (!result || !node) return result;

    /* OPTIMIZED: Use inline array for small namespace counts (up to 8)
     * This avoids heap allocation and realloc for the common case.
     * For larger counts, falls back to dynamic allocation. */
    const char* seen_prefixes[MAX_INLINE_PREFIXES];
    const char** dyn_prefixes = NULL;
    const char** prefixes = seen_prefixes;
    size_t seen_count = 0;
    size_t capacity = MAX_INLINE_PREFIXES;

    /* Collect namespaces from element and ancestors */
    TaurusElement current = node;
    while (current) {
        /* Get namespace from element */
        const char* ns_prefix = taurus_element_get_prefix(current);
        const char* ns_uri = taurus_element_get_namespace_uri(current);

        /* Skip if already seen or if no namespace */
        if (ns_uri && !is_prefix_seen_inline(prefixes, seen_count, ns_prefix)) {
            /* Check if matches node test */
            int matches = 0;
            if (!test || test->type == XPATH_AST_NODE_TEST_ALL) {
                matches = 1;  /* Match all */
            } else if (test->type == XPATH_AST_NODE_TEST_NAME) {
                matches = (test->value && ns_prefix &&
                          strcmp(test->value, ns_prefix) == 0);
            }

            if (matches) {
                /* Create namespace node - single allocation for struct + strings */
                TaurusNamespaceNode* ns_node = alloc_namespace_node_single(
                    ns_prefix, ns_uri, current);
                if (ns_node) {
                    xpath_nodeset_add(result, (void*)ns_node);
                }
            }

            /* Add to seen prefixes */
            if (seen_count >= capacity) {
                /* Need to grow - switch to heap allocation */
                size_t new_capacity = capacity * 2;
                const char** new_prefixes = (const char**)malloc(new_capacity * sizeof(const char*));
                if (new_prefixes) {
                    memcpy(new_prefixes, prefixes, seen_count * sizeof(const char*));
                    if (prefixes != seen_prefixes) free(prefixes);
                    prefixes = new_prefixes;
                    dyn_prefixes = new_prefixes;
                    capacity = new_capacity;
                }
            }
            prefixes[seen_count++] = ns_prefix;
        }

        /* Move to parent */
        current = taurus_element_get_parent(current);
    }

    /* Always add implicit xml namespace if not already present */
    if (!is_prefix_seen_inline(prefixes, seen_count, "xml")) {
        int matches = (!test || test->type == XPATH_AST_NODE_TEST_ALL ||
                      (test->type == XPATH_AST_NODE_TEST_NAME &&
                       test->value && strcmp(test->value, "xml") == 0));

        if (matches) {
            /* Single allocation for xml namespace node */
            TaurusNamespaceNode* xml_ns = alloc_namespace_node_single(
                "xml", "http://www.w3.org/XML/1998/namespace", node);
            if (xml_ns) {
                xpath_nodeset_add(result, (void*)xml_ns);
            }
        }
    }

    /* Cleanup */
    if (dyn_prefixes) free(dyn_prefixes);

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

    return xpath_nodeset_new_pooled(ctx);  /* Unknown axis */
}