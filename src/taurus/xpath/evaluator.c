/* evaluator.c - XPath evaluator implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 evaluator.
 * Converted from Ruby C extension to pure C.
 */

#include "evaluator.h"
#include "evaluator_internal.h"
#include "functions.h"
#include "functions_internal.h"
#include "xpath_variables.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include "../dom/ptr_element.h"  /* For struct ptr_attribute */
#include "lexer.h"
#include "parser.h"
#include "../include/taurus.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

/* Debug logging - Set to 0 to disable */
#define XPATH_DEBUG 0

#if XPATH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[XPath DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Namespace support functions (v0.8.0) */
static void xpath_context_register_namespace(XPathContext* context,
                                             const char* prefix,
                                             const char* uri);
static void xpath_context_init_from_document(XPathContext* context);

/* Function call evaluation */
static struct taurus_xpath_result* evaluate_function_call(XPathContext* ctx,
                                                          XPathASTNode* ast);

/* ============================================================================
 * Context Management
 * ============================================================================ */

XPathContext* xpath_context_new(struct taurus_document* document,
                                TaurusElement context_node) {
    if (!document || !context_node) return NULL;

    XPathContext* context = TAURUS_ALLOC(XPathContext);
    if (!context) return NULL;

    context->document = document;
    context->context_node = context_node;
    context->context_position = 1;
    context->context_size = 1;
    context->error_msg[0] = '\0';
    context->current_predicate_node = NULL;  /* Initialize to NULL */
    context->to_boolean = 0;
    context->max_results = 0;
    context->enable_early_exit = 1;

    /* Initialize namespace support (v0.8.0) */
    context->namespace_mappings = NULL;
    context->namespace_count = 0;
    context->namespace_capacity = 0;

    /* Initialize error context (v1.0.0) */
    context->input = NULL;
    context->input_len = 0;

    /* NOTE: Nodeset pool is now thread-local for better performance.
     * These fields are kept for API compatibility but not used. */
    context->nodeset_pool = NULL;
    context->nodesets_allocated = 0;
    context->nodesets_reused = 0;

    /* Initialize function registry with standard XPath 1.0 functions */
    context->function_registry = xpath_function_registry_new();
    if (context->function_registry) {
        xpath_function_registry_init_standard(
            (XPathFunctionRegistry*)context->function_registry);
    }

    /* Auto-populate namespace mappings from document (v0.8.0) */
    xpath_context_init_from_document(context);

    return context;
}

void xpath_context_free(XPathContext* context) {
    if (!context) return;

    /* Free namespace mappings (v0.8.0) */
    if (context->namespace_mappings) {
        for (size_t i = 0; i < context->namespace_count; i++) {
            TAURUS_FREE(context->namespace_mappings[i].prefix);
            TAURUS_FREE(context->namespace_mappings[i].uri);
        }
        TAURUS_FREE(context->namespace_mappings);
    }

    /* Free function registry */
    if (context->function_registry) {
        xpath_function_registry_free((XPathFunctionRegistry*)context->function_registry);
    }

    /* NOTE: Nodeset pool is now thread-local, not per-context.
     * It will be cleaned up when the thread exits or when
     * xpath_nodeset_cleanup_thread_pool() is called. */

    TAURUS_FREE(context);
}

const char* xpath_context_error(XPathContext* context) {
    if (!context) return "Invalid context";
    return context->error_msg[0] ? context->error_msg : NULL;
}

/* ============================================================================
 * Namespace Support (v0.8.0)
 * ============================================================================ */

/**
 * Register a namespace prefix->URI mapping in the context
 *
 * @param context XPath context
 * @param prefix Namespace prefix (NULL for default namespace)
 * @param uri Namespace URI (required)
 */
void xpath_context_register_namespace(XPathContext* context,
                                      const char* prefix,
                                      const char* uri) {
    if (!context || !uri) return;

    /* Check if prefix already registered - update if found */
    for (size_t i = 0; i < context->namespace_count; i++) {
        int prefix_matches = 0;
        if (!prefix && !context->namespace_mappings[i].prefix) {
            prefix_matches = 1; /* Both NULL (default namespace) */
        } else if (prefix && context->namespace_mappings[i].prefix &&
                   strcmp(prefix, context->namespace_mappings[i].prefix) == 0) {
            prefix_matches = 1; /* Both non-NULL and equal */
        }

        if (prefix_matches) {
            /* Update existing mapping */
            TAURUS_FREE(context->namespace_mappings[i].uri);
            context->namespace_mappings[i].uri = taurus_strdup(uri);
            return;
        }
    }

    /* Add new mapping - grow array if needed */
    if (context->namespace_count >= context->namespace_capacity) {
        size_t new_capacity = context->namespace_capacity == 0 ?
            4 : context->namespace_capacity * 2;
        XPathNamespaceMapping* new_mappings = TAURUS_REALLOC_N(
            context->namespace_mappings,
            XPathNamespaceMapping,
            new_capacity
        );
        if (!new_mappings) return; /* Allocation failed */

        context->namespace_mappings = new_mappings;
        context->namespace_capacity = new_capacity;
    }

    /* Add new mapping */
    context->namespace_mappings[context->namespace_count].prefix =
        prefix ? taurus_strdup(prefix) : NULL;
    context->namespace_mappings[context->namespace_count].uri =
        taurus_strdup(uri);
    context->namespace_count++;
}

/**
 * Resolve namespace prefix to URI (OPTIMIZED with reverse lookup)
 *
 * Strategy: Search from END to START to find most recent registration first.
 * This handles override semantics naturally - child namespace declarations
 * override parent ones because they're registered later.
 *
 * Performance: O(n) worst case, but in practice very fast because:
 * - Most documents have <10 unique namespace prefixes
 * - Recent registrations (local scope) found first
 * - Common prefixes cached by compiler in registers
 *
 * @param context XPath context
 * @param prefix Namespace prefix to resolve (NULL for default namespace)
 * @return Namespace URI, or NULL if not found
 */
const char* xpath_context_resolve_prefix(XPathContext* context,
                                         const char* prefix) {
    if (!context || context->namespace_count == 0) return NULL;

    /* Search BACKWARDS for most recent (local) registration first
     * This implements namespace scope override semantics efficiently */
    for (size_t i = context->namespace_count; i > 0; i--) {
        size_t idx = i - 1;
        XPathNamespaceMapping* mapping = &context->namespace_mappings[idx];

        /* Fast path: Compare prefix pointers first (common case: same string object) */
        if (mapping->prefix == prefix) {
            return mapping->uri;
        }

        /* Both NULL = default namespace match */
        if (!prefix && !mapping->prefix) {
            return mapping->uri;
        }

        /* String comparison only if both non-NULL */
        if (prefix && mapping->prefix && strcmp(prefix, mapping->prefix) == 0) {
            return mapping->uri;
        }
    }

    return NULL; /* Prefix not found */
}

/* Helper: Collect namespaces from element and all descendants recursively
 * MAX_DEPTH prevents infinite recursion due to malformed data or bugs */
#define NAMESPACE_COLLECT_MAX_DEPTH 1000

static void collect_namespaces_recursive_impl(XPathContext* context,
                                         TaurusElement element,
                                         int depth) {
    if (!element || depth > NAMESPACE_COLLECT_MAX_DEPTH) return;

    /* Register namespace from this element (inline in compact mode) */
    const char* ns_uri = taurus_element_get_namespace_uri(element);
    const char* ns_prefix = taurus_element_get_prefix(element);
    if (ns_uri) {
        xpath_context_register_namespace(context, ns_prefix, ns_uri);
    }

    /* Also check attributes for xmlns declarations */
    size_t attr_count = taurus_element_attribute_count(element);
    for (size_t i = 0; i < attr_count; i++) {
        /* Walk the attribute linked list
         * CRITICAL: The actual structure is ptr_attribute, NOT taurus_attribute!
         * They have completely different layouts:
         * - ptr_attribute: name, value, next_attr at offsets 0, 8, 16
         * - taurus_attribute: name_view, value_view... then name, value at offsets 64, 72
         * We MUST use ptr_attribute directly to avoid memory corruption.
         */
        struct ptr_attribute* ptr_attr = (struct ptr_attribute*)taurus_element_get_first_attribute(element);
        for (size_t j = 0; j < i && ptr_attr; j++) {
            ptr_attr = ptr_attr->next_attr;
        }
        if (!ptr_attr) continue;

        /* Get attribute name - use ptr_attr fields directly */
        const char* attr_name = ptr_attr->name;
        size_t attr_name_len = 0;

        if (attr_name) {
            attr_name_len = strlen(attr_name);
        } else if (ptr_attr->name_view_data && ptr_attr->name_view_length > 0) {
            /* Use name_view fields - NOT null-terminated! Use length. */
            attr_name = ptr_attr->name_view_data;
            attr_name_len = ptr_attr->name_view_length;
        } else {
            continue;
        }

        /* Check for xmlns:prefix using direct string comparison */
        if (attr_name_len >= 6 && strncmp(attr_name, "xmlns:", 6) == 0) {
            /* Found xmlns:prefix declaration */
            const char* prefix_str = attr_name + 6;
            size_t prefix_len = attr_name_len - 6;

            /* Get value - use ptr_attr->value directly */
            const char* uri_str = ptr_attr->value;
            char* uri_copy = NULL;

            if (!uri_str && ptr_attr->value_view_data && ptr_attr->value_view_length > 0) {
                /* Need to make a null-terminated copy for registration */
                uri_copy = TAURUS_ALLOC_N(char, ptr_attr->value_view_length + 1);
                if (!uri_copy) continue;
                memcpy(uri_copy, ptr_attr->value_view_data, ptr_attr->value_view_length);
                uri_copy[ptr_attr->value_view_length] = '\0';
                uri_str = uri_copy;
            }
            if (!uri_str) continue;

            /* Make null-terminated copy of prefix */
            char* prefix_copy = TAURUS_ALLOC_N(char, prefix_len + 1);
            if (!prefix_copy) {
                TAURUS_FREE(uri_copy);
                continue;
            }
            memcpy(prefix_copy, prefix_str, prefix_len);
            prefix_copy[prefix_len] = '\0';

            xpath_context_register_namespace(context, prefix_copy, uri_str);
            TAURUS_FREE(prefix_copy);
            TAURUS_FREE(uri_copy);
        } else if (attr_name_len == 5 && strncmp(attr_name, "xmlns", 5) == 0) {
            /* Found xmlns (default namespace) declaration
             *
             * CRITICAL: In XPath 1.0, the default namespace declared in XML does NOT apply
             * to unprefixed element names in XPath expressions! Unprefixed names in XPath
             * are always in NO namespace, even if there's a default xmlns declaration.
             *
             * Therefore, we do NOT register this as a default namespace for XPath context.
             * The default namespace only applies to prefixed names in the XML document.
             *
             * Reference: XPath 1.0 Section 2.3 - "Unprefixed names are not in any namespace"
             * See: https://www.w3.org/TR/xpath/#node-tests
             *
             * For prefixed namespace declarations (xmlns:prefix="uri"), we register the mapping
             * so that prefixed names like prefix:name work in XPath expressions.
             * But for the default namespace, we intentionally do NOT register it here.
             *
             * This means that:
             *   <root xmlns="http://example.com">
             * with XPath "/root" will match the root element (because unprefixed names
             * are in NO namespace in XPath).
             * And XPath "ns1:root" would only match if there's xmlns:ns1="...".
             */
            DEBUG_LOG("  Skipping default namespace declaration (xmlns=...) - not used for unprefixed XPath names");
        }
    }

    /* Recursively collect from children - use compact accessor functions */
    TaurusElement child_elem = taurus_element_get_first_child(element);
    while (child_elem) {
        collect_namespaces_recursive_impl(context, child_elem, depth + 1);
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

/* Public wrapper for collect_namespaces_recursive */
static void collect_namespaces_recursive(XPathContext* context,
                                         TaurusElement element) {
    collect_namespaces_recursive_impl(context, element, 0);
}

/**
 * Initialize XPath context from document
 * Collects all namespace declarations from the document
 */
void xpath_context_init_from_document(XPathContext* context) {
    if (!context || !context->document) return;

    /* Use taurus_document_root() to properly handle compact mode */
    TaurusElement root = taurus_document_root(context->document);
    if (root) {
        collect_namespaces_recursive(context, root);
    }
}

/* ============================================================================
 * Result Management
 * ============================================================================ */

/* ============================================================================
 * PERFORMANCE: Thread-Local Nodeset Pool
 * ============================================================================
 * Thread-local pool for O(1) nodeset allocation. This eliminates malloc
 * overhead for the many short-lived nodesets created during XPath evaluation.
 *
 * How it works:
 * 1. xpath_nodeset_new() checks thread-local pool first
 * 2. xpath_nodeset_free() returns nodesets to thread-local pool
 * 3. Pool has max size to prevent unbounded memory growth
 *
 * Benefits:
 * - No context parameter needed in nodeset_free()
 * - Works automatically with existing code
 * - Thread-safe (each thread has its own pool)
 */

/* Thread-local nodeset pool - no locking needed (one per thread) */
static _Thread_local XPathNodeSet* t_nodeset_pool = NULL;
static _Thread_local size_t t_nodeset_pool_size = 0;
static _Thread_local size_t t_nodesets_allocated = 0;
static _Thread_local size_t t_nodesets_reused = 0;

/* Maximum pool size to prevent unbounded growth */
#define NODESET_POOL_MAX_SIZE 64

/* Flag to mark nodesets that came from the pool (stored in capacity high bit) */
#define NODESET_POOL_FLAG ((size_t)1 << (sizeof(size_t) * 8 - 1))
#define NODESET_IS_POOLED(ns) ((ns)->capacity & NODESET_POOL_FLAG)
#define NODESET_CLEAR_POOL_FLAG(ns) ((ns)->capacity & ~NODESET_POOL_FLAG)
#define NODESET_SET_POOL_FLAG(ns) ((ns)->capacity |= NODESET_POOL_FLAG)

/* Forward declaration */
static void xpath_nodeset_return_to_pool(XPathNodeSet* nodeset);

/* ============================================================================
 * NodeSet Management
 * ============================================================================ */

XPathNodeSet* xpath_nodeset_new(void) {
    /* PERFORMANCE: Check thread-local pool first */
    if (t_nodeset_pool) {
        XPathNodeSet* ns = t_nodeset_pool;
        t_nodeset_pool = ns->next_in_pool;
        t_nodeset_pool_size--;
        t_nodesets_reused++;

        /* Reset nodeset for reuse */
        ns->count = 0;
        ns->owns_attributes = 0;
        ns->owns_namespaces = 0;
        ns->capacity = NODESET_CLEAR_POOL_FLAG(ns);
        ns->next_in_pool = NULL;

        return ns;
    }

    /* Pool empty - allocate new */
    t_nodesets_allocated++;
    return xpath_nodeset_new_with_capacity(4);
}

XPathNodeSet* xpath_nodeset_new_with_capacity(size_t capacity) {
    XPathNodeSet* nodeset = TAURUS_ALLOC(XPathNodeSet);
    if (!nodeset) return NULL;

    nodeset->nodes = NULL;
    nodeset->count = 0;
    nodeset->capacity = 0;
    nodeset->owns_attributes = 0;
    nodeset->owns_namespaces = 0;
    nodeset->next_in_pool = NULL;

    if (capacity > 0) {
        nodeset->nodes = TAURUS_ALLOC_N(void*, capacity);
        if (!nodeset->nodes) {
            TAURUS_FREE(nodeset);
            return NULL;
        }
        /* Initialize all entries to NULL for safety */
        memset(nodeset->nodes, 0, sizeof(void*) * capacity);
        nodeset->capacity = capacity;
    }

    /* Mark as pool-eligible so free() knows to return it to pool */
    NODESET_SET_POOL_FLAG(nodeset);

    return nodeset;
}

void xpath_nodeset_free(XPathNodeSet* nodeset) {
    if (!nodeset) return;

    /* PERFORMANCE: Return to thread-local pool if this nodeset came from it */
    if (NODESET_IS_POOLED(nodeset)) {
        xpath_nodeset_return_to_pool(nodeset);
        return;
    }

    /* Free attribute nodes if we own them */
    if (nodeset->owns_attributes && nodeset->nodes) {
        for (size_t i = 0; i < nodeset->count; i++) {
            void* node = nodeset->nodes[i];
            if (node && XPATH_NODE_TYPE(node) == TAURUS_NODE_ATTRIBUTE) {
                TaurusAttributeNode* attr = (TaurusAttributeNode*)node;
                if (attr->name) TAURUS_FREE(attr->name);
                if (attr->value) TAURUS_FREE(attr->value);
                if (attr->namespace_uri) TAURUS_FREE(attr->namespace_uri);
                TAURUS_FREE(attr);
            }
        }
    }

    /* Free namespace nodes if we own them */
    if (nodeset->owns_namespaces && nodeset->nodes) {
        for (size_t i = 0; i < nodeset->count; i++) {
            void* node = nodeset->nodes[i];
            if (node && XPATH_NODE_TYPE(node) == TAURUS_NODE_NAMESPACE) {
                /* Single allocation: struct + prefix + uri, just free the struct */
                TAURUS_FREE(node);
            }
        }
    }

    if (nodeset->nodes) {
        TAURUS_FREE(nodeset->nodes);
    }
    TAURUS_FREE(nodeset);
}

size_t xpath_nodeset_count(XPathNodeSet* nodeset) {
    return nodeset ? nodeset->count : 0;
}

void* xpath_nodeset_get(XPathNodeSet* nodeset, size_t index) {
    if (!nodeset || index >= nodeset->count) return NULL;
    return nodeset->nodes[index];
}

void xpath_nodeset_add(XPathNodeSet* nodeset, void* node) {
    if (!nodeset || !node) return;

    /* SAFETY: Validate node pointer before adding
     * Stale pointers from previous operations can cause crashes when reallocating */
    if ((uintptr_t)node < 0x1000) {
        /* Clearly invalid pointer - skip */
        return;
    }

    /* Get real capacity (without pool flag) */
    size_t real_capacity = NODESET_CLEAR_POOL_FLAG(nodeset);

    /* SAFETY: Validate nodeset structure before reallocation
     * Corruption in count/capacity can cause heap corruption during realloc */
    if (nodeset->count > real_capacity || real_capacity > 1000000) {
        /* Corrupted structure - skip this addition */
        return;
    }

    /* Grow array if needed */
    if (nodeset->count >= real_capacity) {
        size_t new_capacity = real_capacity == 0 ? 4 : real_capacity * 2;

        /* SAFETY: Check for overflow */
        if (new_capacity > 1000000) {
            return;
        }

        /* SAFETY: Validate nodes pointer before realloc */
        if (nodeset->nodes && (uintptr_t)nodeset->nodes < 0x1000) {
            return;
        }

        void** new_nodes = TAURUS_REALLOC_N(nodeset->nodes, void*, new_capacity);
        if (!new_nodes) return;
        nodeset->nodes = new_nodes;
        /* Set new capacity with pool flag preserved */
        nodeset->capacity = new_capacity | NODESET_POOL_FLAG;
    }

    nodeset->nodes[nodeset->count++] = node;
}

/**
 * Return nodeset to thread-local pool (O(1))
 *
 * Instead of freeing, we add to the free list for reuse.
 * The nodes array is kept allocated for next use.
 */
static void xpath_nodeset_return_to_pool(XPathNodeSet* nodeset) {
    if (!nodeset) return;

    /* Free attribute nodes if we own them (can't pool with owned nodes) */
    if (nodeset->owns_attributes && nodeset->nodes) {
        for (size_t i = 0; i < nodeset->count; i++) {
            void* node = nodeset->nodes[i];
            if (node && XPATH_NODE_TYPE(node) == TAURUS_NODE_ATTRIBUTE) {
                TaurusAttributeNode* attr = (TaurusAttributeNode*)node;
                if (attr->name) TAURUS_FREE(attr->name);
                if (attr->value) TAURUS_FREE(attr->value);
                if (attr->namespace_uri) TAURUS_FREE(attr->namespace_uri);
                TAURUS_FREE(attr);
            }
        }
    }

    /* Free namespace nodes if we own them (can't pool with owned nodes) */
    if (nodeset->owns_namespaces && nodeset->nodes) {
        for (size_t i = 0; i < nodeset->count; i++) {
            void* node = nodeset->nodes[i];
            if (node && XPATH_NODE_TYPE(node) == TAURUS_NODE_NAMESPACE) {
                /* Single allocation: struct + prefix + uri, just free the struct */
                TAURUS_FREE(node);
            }
        }
    }

    /* Don't pool if pool is full */
    if (t_nodeset_pool_size >= NODESET_POOL_MAX_SIZE) {
        /* Pool full - actually free */
        if (nodeset->nodes) {
            TAURUS_FREE(nodeset->nodes);
        }
        TAURUS_FREE(nodeset);
        return;
    }

    /* Clear owns flags - nodes have been freed above */
    nodeset->owns_attributes = 0;
    nodeset->owns_namespaces = 0;

    /* Add to free list using next_in_pool for the linked list */
    nodeset->count = 0;  /* Reset count */
    NODESET_SET_POOL_FLAG(nodeset);
    nodeset->next_in_pool = t_nodeset_pool;
    t_nodeset_pool = nodeset;
    t_nodeset_pool_size++;
}

/**
 * Get nodeset from thread-local pool (O(1) for reused nodesets)
 *
 * If pool has available nodesets, returns one (reset to empty).
 * Otherwise allocates a new nodeset.
 */
XPathNodeSet* xpath_nodeset_new_pooled(XPathContext* ctx) {
    (void)ctx;  /* Context not needed for thread-local pool */

    /* Check thread-local pool for available nodeset */
    if (t_nodeset_pool) {
        XPathNodeSet* ns = t_nodeset_pool;
        t_nodeset_pool = ns->next_in_pool;
        t_nodeset_pool_size--;
        t_nodesets_reused++;

        /* Reset nodeset for reuse */
        ns->count = 0;
        ns->owns_attributes = 0;
        ns->owns_namespaces = 0;
        ns->capacity = NODESET_CLEAR_POOL_FLAG(ns);
        ns->next_in_pool = NULL;
        /* Clear pool flag from capacity but keep the actual capacity */
        ns->capacity = NODESET_CLEAR_POOL_FLAG(ns);

        return ns;
    }

    /* Pool empty - allocate new nodeset */
    t_nodesets_allocated++;
    XPathNodeSet* ns = xpath_nodeset_new();
    if (ns) {
        /* Mark as coming from pool so free() knows to return it */
        NODESET_SET_POOL_FLAG(ns);
    }
    return ns;
}

/**
 * Release nodeset back to context pool (for API compatibility)
 *
 * This function exists for API compatibility with the per-context pool design.
 * Internally it uses the thread-local pool.
 */
void xpath_nodeset_release(XPathNodeSet* nodeset, XPathContext* ctx) {
    (void)ctx;  /* Context not needed for thread-local pool */
    xpath_nodeset_return_to_pool(nodeset);
}

struct taurus_xpath_result* xpath_result_new(XPathResultType type) {
    struct taurus_xpath_result* result = TAURUS_ALLOC(struct taurus_xpath_result);
    if (!result) return NULL;

    result->type = type;

    /* Initialize union based on type */
    switch (type) {
        case XPATH_RESULT_BOOLEAN:
            result->value.boolean_value = 0;
            break;
        case XPATH_RESULT_NUMBER:
            result->value.number_value = 0.0;
            break;
        case XPATH_RESULT_STRING:
            result->value.string_value = NULL;
            break;
        case XPATH_RESULT_NODESET:
            result->value.nodeset_value = NULL;
            break;
    }

    return result;
}

void xpath_result_free(struct taurus_xpath_result* result) {
    if (!result) return;

    switch (result->type) {
        case XPATH_RESULT_STRING:
            if (result->value.string_value) {
                TAURUS_FREE(result->value.string_value);
            }
            break;
        case XPATH_RESULT_NODESET:
            if (result->value.nodeset_value) {
                xpath_nodeset_free(result->value.nodeset_value);
            }
            break;
        case XPATH_RESULT_NUMBER:
        case XPATH_RESULT_BOOLEAN:
            /* No heap allocation to free */
            break;
    }

    TAURUS_FREE(result);
}

/* ============================================================================
 * Expression Evaluation
 * ============================================================================ */

/* Forward declarations for evaluation functions */
extern struct taurus_xpath_result* evaluate_location_path(XPathContext* ctx,
                                                           XPathASTNode* path);
extern struct taurus_xpath_result* evaluate_operator(XPathContext* ctx,
                                                     XPathASTNode* ast);

/**
 * Evaluate function call AST node
 */
static struct taurus_xpath_result* evaluate_function_call(XPathContext* ctx,
                                                          XPathASTNode* ast) {
    if (!ctx || !ast || ast->type != XPATH_AST_FUNCTION_CALL) {
        if (ctx) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                    "Invalid function call node");
        }
        return NULL;
    }

    const char* func_name = ast->value;
    if (!func_name) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                "Function call missing name");
        return NULL;
    }

    /* First, check custom function registry */
    XPathFunctionHandler custom_handler = xpath_custom_function_lookup(func_name);
    if (custom_handler) {
        /* Custom functions accept variable args, so skip validation */
        size_t arg_count = ast->child_count;
        return custom_handler(ctx, ast->children, arg_count);
    }

    /* Get function registry for built-in functions */
    if (!ctx->function_registry) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                "Function registry not initialized");
        return NULL;
    }

    XPathFunctionRegistry* registry = (XPathFunctionRegistry*)ctx->function_registry;

    /* Look up built-in function */
    XPathFunctionDef* func_def = xpath_function_registry_get(registry, func_name);
    if (!func_def) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                "Unknown function: %s", func_name);
        return NULL;
    }

    /* Check argument count */
    size_t arg_count = ast->child_count;
    if ((int)arg_count < func_def->min_args ||
        (func_def->max_args >= 0 && (int)arg_count > func_def->max_args)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                "Function %s expects %d-%d arguments, got %zu",
                func_name, func_def->min_args, func_def->max_args, arg_count);
        return NULL;
    }

    /* Call function handler */
    return func_def->handler(ctx, ast->children, arg_count);
}

/**
 * Evaluate literal AST node (string or number)
 */
static struct taurus_xpath_result* evaluate_literal(XPathContext* ctx,
                                                    XPathASTNode* ast) {
    if (!ctx || !ast) return NULL;

    switch (ast->type) {
        case XPATH_AST_NUMBER: {
            struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
            if (!result) return NULL;
            result->value.number_value = ast->number_value;
            return result;
        }

        case XPATH_AST_STRING: {
            struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
            if (!result) return NULL;
            result->value.string_value = ast->value ? taurus_strdup(ast->value) : taurus_strdup("");
            return result;
        }

        default:
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                    "Not a literal node");
            return NULL;
    }
}

/* ============================================================================
 * PERFORMANCE: Direct Boolean Evaluation (libxml2 Strategy)
 * ============================================================================
 *
 * This is the KEY optimization that gives libxml2 its predicate performance.
 *
 * Instead of:
 *   1. evaluate_expr() -> creates taurus_xpath_result object
 *   2. xpath_to_boolean() -> converts to boolean
 *   3. xpath_result_free() -> frees the object
 *
 * We do:
 *   evaluate_expr_to_boolean() -> returns int directly
 *
 * This eliminates allocation/deallocation for EVERY predicate evaluation.
 * For predicates evaluated N times (once per node), this is N allocations saved.
 *
 * Returns: 1 if true, 0 if false, -1 on error
 */

/* Helper: Evaluate operator directly to boolean (no allocation) */
static int evaluate_operator_to_boolean(XPathContext* ctx, XPathASTNode* ast, int is_predicate) {
    if (!ast || ast->child_count < 2) return -1;

    XPathOperatorType op = (XPathOperatorType)ast->number_value;

    /* Short-circuit evaluation for logical operators */
    if (op == XPATH_OP_AND) {
        int left_bool = evaluate_expr_to_boolean(ctx, ast->children[0], is_predicate);
        if (left_bool <= 0) return left_bool;  /* false or error */
        return evaluate_expr_to_boolean(ctx, ast->children[1], is_predicate);
    }

    if (op == XPATH_OP_OR) {
        int left_bool = evaluate_expr_to_boolean(ctx, ast->children[0], is_predicate);
        if (left_bool < 0) return -1;  /* error */
        if (left_bool > 0) return 1;   /* short-circuit true */
        return evaluate_expr_to_boolean(ctx, ast->children[1], is_predicate);
    }

    /* For comparison operators, we need to evaluate both sides */
    struct taurus_xpath_result* left = evaluate_expr(ctx, ast->children[0]);
    if (!left) return -1;

    struct taurus_xpath_result* right = evaluate_expr(ctx, ast->children[1]);
    if (!right) {
        xpath_result_free(left);
        return -1;
    }

    int result = 0;

    /* Handle nodeset comparisons (most common predicate case) */
    if (left->type == XPATH_RESULT_NODESET || right->type == XPATH_RESULT_NODESET) {
        int is_equality_op = (op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL);
        int neq = (op == XPATH_OP_NOT_EQUAL) ? 1 : 0;

        if (is_equality_op) {
            /* Case 1: nodeset == string */
            if (left->type == XPATH_RESULT_NODESET &&
                right->type == XPATH_RESULT_STRING && right->value.string_value) {
                result = xpath_nodeset_equals_string_hash(
                    left->value.nodeset_value, right->value.string_value, neq);
            }
            /* Case 2: string == nodeset */
            else if (right->type == XPATH_RESULT_NODESET &&
                     left->type == XPATH_RESULT_STRING && left->value.string_value) {
                result = xpath_nodeset_equals_string_hash(
                    right->value.nodeset_value, left->value.string_value, neq);
            }
            /* Case 3: nodeset == nodeset - need full conversion */
            else {
                char* lstr = xpath_to_string(left);
                char* rstr = xpath_to_string(right);
                int cmp = strcmp(lstr ? lstr : "", rstr ? rstr : "");
                result = neq ? (cmp != 0) : (cmp == 0);
                if (lstr) TAURUS_FREE(lstr);
                if (rstr) TAURUS_FREE(rstr);
            }
        } else {
            /* Relational operators - convert to numbers */
            double lval = xpath_to_number(left);
            double rval = xpath_to_number(right);
            switch (op) {
                case XPATH_OP_LESS: result = (lval < rval); break;
                case XPATH_OP_LESS_EQUAL: result = (lval <= rval); break;
                case XPATH_OP_GREATER: result = (lval > rval); break;
                case XPATH_OP_GREATER_EQUAL: result = (lval >= rval); break;
                default: break;
            }
        }
    }
    /* String comparison for equality operators */
    else if ((op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL) &&
             left->type == XPATH_RESULT_STRING &&
             right->type == XPATH_RESULT_STRING) {
        int cmp = strcmp(left->value.string_value ? left->value.string_value : "",
                        right->value.string_value ? right->value.string_value : "");
        result = (op == XPATH_OP_EQUAL) ? (cmp == 0) : (cmp != 0);
    }
    /* Numeric comparison */
    else {
        double lval = xpath_to_number(left);
        double rval = xpath_to_number(right);
        switch (op) {
            case XPATH_OP_EQUAL: result = (lval == rval); break;
            case XPATH_OP_NOT_EQUAL: result = (lval != rval); break;
            case XPATH_OP_LESS: result = (lval < rval); break;
            case XPATH_OP_LESS_EQUAL: result = (lval <= rval); break;
            case XPATH_OP_GREATER: result = (lval > rval); break;
            case XPATH_OP_GREATER_EQUAL: result = (lval >= rval); break;
            default: break;
        }
    }

    xpath_result_free(left);
    xpath_result_free(right);
    return result;
}

/* Helper: Evaluate step directly to boolean (no allocation) */
static int evaluate_step_to_boolean(XPathContext* ctx, XPathASTNode* step) {
    /* Create temporary nodeset with context node */
    XPathNodeSet* current = xpath_nodeset_new();
    if (!current) return -1;

    xpath_nodeset_add(current, ctx->context_node);

    /* Evaluate the step */
    struct taurus_xpath_result* step_result = evaluate_step(ctx, step, current);
    xpath_nodeset_free(current);

    if (!step_result) return -1;

    /* Check if nodeset is non-empty */
    int result = 0;
    if (step_result->type == XPATH_RESULT_NODESET && step_result->value.nodeset_value) {
        result = (step_result->value.nodeset_value->count > 0) ? 1 : 0;
    }

    xpath_result_free(step_result);
    return result;
}

/* Main function: Evaluate expression directly to boolean */
int evaluate_expr_to_boolean(XPathContext* ctx, XPathASTNode* ast, int is_predicate) {
    if (!ctx || !ast) return -1;

    switch (ast->type) {
        case XPATH_AST_OPERATOR:
            return evaluate_operator_to_boolean(ctx, ast, is_predicate);

        case XPATH_AST_STEP:
            return evaluate_step_to_boolean(ctx, ast);

        case XPATH_AST_PATH_EXPR:
        case XPATH_AST_ABSOLUTE_PATH:
        case XPATH_AST_RELATIVE_PATH: {
            /* For paths, evaluate and check if nodeset is non-empty */
            struct taurus_xpath_result* result = evaluate_location_path(ctx, ast);
            if (!result) return -1;

            int bool_result = 0;
            if (result->type == XPATH_RESULT_NODESET && result->value.nodeset_value) {
                bool_result = (result->value.nodeset_value->count > 0) ? 1 : 0;
            }
            xpath_result_free(result);
            return bool_result;
        }

        case XPATH_AST_NUMBER: {
            /* In predicate context, number matches position */
            if (is_predicate) {
                double num = ast->number_value;
                if (num == (double)ctx->context_position) {
                    return 1;
                }
                return 0;
            }
            /* Otherwise, convert to boolean */
            return (ast->number_value != 0.0 && !isnan(ast->number_value)) ? 1 : 0;
        }

        case XPATH_AST_STRING:
            /* String is true if non-empty */
            return (ast->value && ast->value[0] != '\0') ? 1 : 0;

        case XPATH_AST_FUNCTION_CALL: {
            /* Functions need full evaluation */
            struct taurus_xpath_result* result = evaluate_function_call(ctx, ast);
            if (!result) return -1;
            int bool_result = xpath_to_boolean(result);
            xpath_result_free(result);
            return bool_result;
        }

        default:
            /* Fallback: evaluate and convert */
            {
                struct taurus_xpath_result* result = evaluate_expr(ctx, ast);
                if (!result) return -1;
                int bool_result = xpath_to_boolean(result);
                xpath_result_free(result);
                return bool_result;
            }
    }
}

/**
 * Internal expression evaluator
 * Called by various evaluation functions
 */
struct taurus_xpath_result* evaluate_expr(XPathContext* ctx, XPathASTNode* ast) {
    if (!ctx || !ast) return NULL;

    switch (ast->type) {
        case XPATH_AST_STEP:
            /* Bare step (e.g., @id, ., ..) - evaluate as relative path from context node */
            {
                XPathNodeSet* current = xpath_nodeset_new();
                if (!current) return NULL;

                /* Start from context node */
                xpath_nodeset_add(current, ctx->context_node);

                /* Evaluate the step */
                struct taurus_xpath_result* step_result = evaluate_step(ctx, ast, current);
                xpath_nodeset_free(current);

                return step_result;
            }

        case XPATH_AST_PATH_EXPR:
        case XPATH_AST_ABSOLUTE_PATH:
        case XPATH_AST_RELATIVE_PATH:
            return evaluate_location_path(ctx, ast);

        case XPATH_AST_OPERATOR:
            return evaluate_operator(ctx, ast);

        case XPATH_AST_FUNCTION_CALL:
            return evaluate_function_call(ctx, ast);

        case XPATH_AST_VARIABLE_REFERENCE: {
            /* Look up variable in context */
            if (!ctx->variable_set) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                        "Variable '%s' not found (no variable set provided)", ast->value);
                return NULL;
            }

            /* Get variable from variable set */
            const XPathVariable* var = xpath_variable_set_get_const(
                (XPathVariableSet*)ctx->variable_set, ast->value);

            if (!var) {
                snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                        "Undefined variable: %s", ast->value);
                return NULL;
            }

            /* Create result based on variable type */
            XPathVariableType var_type = xpath_variable_type(var);

            switch (var_type) {
                case XPATH_VAR_TYPE_BOOLEAN: {
                    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
                    if (!result) return NULL;
                    result->value.boolean_value = xpath_variable_get_boolean(var);
                    return result;
                }
                case XPATH_VAR_TYPE_NUMBER: {
                    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
                    if (!result) return NULL;
                    result->value.number_value = xpath_variable_get_number(var);
                    return result;
                }
                case XPATH_VAR_TYPE_STRING: {
                    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
                    if (!result) return NULL;
                    const char* str_val = xpath_variable_get_string(var);
                    if (str_val) {
                        result->value.string_value = taurus_strdup(str_val);
                    } else {
                        result->value.string_value = taurus_strdup("");
                    }
                    return result;
                }
                case XPATH_VAR_TYPE_NODE_SET: {
                    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
                    if (!result) return NULL;
                    /* TODO: Implement nodeset variable support */
                    result->value.nodeset_value = xpath_nodeset_new();
                    return result;
                }
                default:
                    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                            "Unsupported variable type for: %s", ast->value);
                    return NULL;
            }
        }

        case XPATH_AST_NUMBER:
        case XPATH_AST_STRING:
            return evaluate_literal(ctx, ast);

        default:
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                    "Unsupported AST node type: %d", ast->type);
            return NULL;
    }
}

/**
 * Main XPath evaluation entry point
 */
struct taurus_xpath_result* xpath_evaluate(XPathContext* context, XPathASTNode* ast) {
    if (!context) return NULL;
    if (!ast) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "NULL AST node");
        return NULL;
    }

    return evaluate_expr(context, ast);
}
