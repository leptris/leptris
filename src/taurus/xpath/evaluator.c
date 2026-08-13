/* evaluator.c - XPath evaluator implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 evaluator.
 * Converted from Ruby C extension to pure C.
 */

#include "evaluator.h"
#include "evaluator_internal.h"
#include "functions.h"
#include "xpath_variables.h"
#include "../dom/element.h"  /* For TaurusElement structure */
#include "../common/port.h"  /* TAURUS_THREAD_LOCAL */
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
static struct taurus_xpath_result* evaluate_function_call_impl(XPathContext* ctx,
                                                                XPathASTNode* ast);

/* Public alias for the VM (TODO 120 Phase F). The VM dispatches
 * BC_FUNC_CALL by calling this directly, skipping the evaluate_expr
 * AST-type switch. */
struct taurus_xpath_result* evaluate_function_call_inline(XPathContext* ctx,
                                                           XPathASTNode* ast) {
    return evaluate_function_call_impl(ctx, ast);
}

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

    /* Initialize namespace support (v0.8.0). Lazy: flag is set
     * here so the first resolve_prefix call knows to walk the
     * document. The walk itself is deferred (TODO 125). */
    context->namespace_mappings = NULL;
    context->namespace_count = 0;
    context->namespace_capacity = 0;
    context->namespaces_collected = 0;

    /* Initialize variable support (v1.0.1) — must be NULL so the
     * no-vars evaluation path correctly reports "no variable set". */
    context->variable_set = NULL;

    /* Initialize error context (v1.0.0) */
    context->input = NULL;
    context->input_len = 0;

    /* Initialize function registry: use the shared standard singleton
     * (TODO 113 perf). Skip the ~27 alloc + register ops per eval.
     * TODO 148 Phase 5: if the document has custom fns registered,
     * build a per-context registry that merges standard + custom. */
    extern XPathFunctionRegistry* taurus_xpath_build_custom_registry(struct taurus_document*);
    context->function_registry = taurus_xpath_build_custom_registry(document);
    if (!context->function_registry) {
        context->function_registry = xpath_function_registry_get_standard();
    }

    context->current_fn_user_data = NULL;

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

    /* Function registry: usually the shared singleton (TODO 113
     * perf) — do NOT free it. But if the doc had custom XPath
     * functions registered (TODO 148 Phase 5), the context owns a
     * freshly-built per-context registry that needs freeing. */
    extern XPathFunctionRegistry* xpath_function_registry_get_standard(void);
    if (context->function_registry &&
        context->function_registry != xpath_function_registry_get_standard()) {
        xpath_function_registry_free((XPathFunctionRegistry*)context->function_registry);
    }

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
    if (!context) return NULL;

    /* Lazy namespace collection (TODO 125). Walk the document once,
     * on first prefix lookup, then cache. Expressions that never
     * resolve a prefix (the common case: self::*, count(//book),
     * descendant::*[@id], etc.) never pay this cost. */
    if (!context->namespaces_collected) {
        xpath_context_init_from_document(context);
        context->namespaces_collected = 1;
    }

    if (context->namespace_count == 0) return NULL;

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

/* Helper: Collect namespaces from element and all descendants recursively */
static void collect_namespaces_recursive(XPathContext* context,
                                         TaurusElement element) {
    if (!element) return;

    /* Register namespace from this element (inline in compact mode) */
    const char* ns_uri = taurus_element_get_namespace_uri(element);
    const char* ns_prefix = taurus_element_get_prefix(element);
    if (ns_uri) {
        xpath_context_register_namespace(context, ns_prefix, ns_uri);
    }

    /* Also check attributes for xmlns declarations */
    size_t attr_count = taurus_element_attribute_count(element);
    for (size_t i = 0; i < attr_count; i++) {
        /* Walk the attribute linked list */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(element);
        for (size_t j = 0; j < i && attr; j++) {
            attr = attr->next;
        }
        if (!attr) continue;

        /* Check for xmlns:prefix or xmlns using StringView directly
         * SAFETY: Do NOT modify attribute structures during namespace collection!
         * Modifying attr->name or attr->value here would cause memory leaks because:
         * 1. This code might use taurus_sv_to_cstr() (malloc) instead of pool allocation
         * 2. When the document is freed, the pool is freed (including attr structure)
         * 3. But the malloc-allocated strings would leak and corrupt the heap
         *
         * Solution: Use StringView operations directly without lazy conversion */
        TaurusStringView attr_name_view = attr->name_view;

        /* Check for xmlns:prefix using StringView prefix match */
        if (attr_name_view.length >= 6 &&
            attr_name_view.data[0] == 'x' &&
            attr_name_view.data[1] == 'm' &&
            attr_name_view.data[2] == 'l' &&
            attr_name_view.data[3] == 'n' &&
            attr_name_view.data[4] == 's' &&
            attr_name_view.data[5] == ':') {
            /* Found xmlns:prefix declaration */
            TaurusStringView prefix_view = {
                .data = attr_name_view.data + 6,
                .length = attr_name_view.length - 6
            };

            /* Convert prefix StringView to C string for namespace mapping
             * Note: This is stored in XPathContext and freed with context */
            char* prefix = taurus_sv_to_cstr(&prefix_view);
            if (!prefix) continue;

            /* Get value - convert attr value StringView to C string */
            char* uri = taurus_sv_to_cstr(&attr->value_view);
            if (!uri) {
                TAURUS_FREE(prefix);
                continue;
            }

            xpath_context_register_namespace(context, prefix, uri);
            TAURUS_FREE(prefix);  /* Context makes its own copy */
            TAURUS_FREE(uri);     /* Context makes its own copy */
        } else if (attr_name_view.length == 5 &&
                   attr_name_view.data[0] == 'x' &&
                   attr_name_view.data[1] == 'm' &&
                   attr_name_view.data[2] == 'l' &&
                   attr_name_view.data[3] == 'n' &&
                   attr_name_view.data[4] == 's') {
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
        collect_namespaces_recursive(context, child_elem);
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

/**
 * Initialize XPath context from document
 * Collects all namespace declarations from the document
 */
void xpath_context_init_from_document(XPathContext* context) {
    if (!context || !context->document) return;

    TaurusElement root = (TaurusElement)context->document->new_dom_root;
    if (root) {
        collect_namespaces_recursive(context, root);
    }
}

/* ============================================================================
 * Result Management
 * ============================================================================ */

/* ============================================================================
 * NodeSet Management
 * ============================================================================ */

/* Thread-local free-list for XPathNodeSet structs (TODO 159 Phase B).
 * Each taurus_xpath_eval call allocates and frees 2–5 nodesets. The
 * inline_data small-buffer optimisation already eliminates the inner
 * array malloc for small results; this free-list eliminates the struct
 * malloc/free churn too. After warmup, zero heap ops per nodeset.
 *
 * Bounded at NODESET_FREE_LIST_CAP to avoid unbounded growth on
 * pathological queries. The free-list owns XPathNodeSet structs only;
 * spilled nodes arrays are freed before push. */
#define NODESET_FREE_LIST_CAP 64
static TAURUS_THREAD_LOCAL XPathNodeSet* xpath_nodeset_free_list;
static TAURUS_THREAD_LOCAL size_t xpath_nodeset_free_list_count;

/* Thread-local free-list for taurus_xpath_result structs (TODO 162).
 * One result per taurus_xpath_eval call. Pattern matches the nodeset
 * free-list. The struct's value union is reused as the next-pointer
 * slot while on the free-list (smaller than adding a real next field
 * to the struct). */
#define XPATH_RESULT_FREE_LIST_CAP 32
static TAURUS_THREAD_LOCAL struct taurus_xpath_result* xpath_result_free_list;
static TAURUS_THREAD_LOCAL size_t xpath_result_free_list_count;

XPathNodeSet* xpath_nodeset_new(void) {
    return xpath_nodeset_new_with_capacity(XPATH_NODESET_INLINE_CAPACITY);
}

XPathNodeSet* xpath_nodeset_new_with_capacity(size_t capacity) {
    XPathNodeSet* nodeset = NULL;

    /* Fast path: pop from thread-local free-list. The cached structs
     * have count=0 and inline_data already zeroed, so the only setup
     * work is the spill-array allocation when capacity > inline.
     * The next-pointer is stashed in inline_data[0] (a void* slot
     * that is unused while the struct is on the free-list). */
    if (xpath_nodeset_free_list) {
        nodeset = xpath_nodeset_free_list;
        xpath_nodeset_free_list = (XPathNodeSet*)nodeset->inline_data[0];
        xpath_nodeset_free_list_count--;
        nodeset->inline_data[0] = NULL;
    } else {
        nodeset = TAURUS_ALLOC(XPathNodeSet);
        if (!nodeset) return NULL;
        memset(nodeset->inline_data, 0, sizeof(nodeset->inline_data));
    }

    nodeset->count = 0;
    nodeset->owns_attributes = 0;
    nodeset->owns_namespaces = 0;

    /* TODO 113 Phase 2: small-buffer optimization. For capacity ≤ the
     * inline buffer size, point `nodes` at the inline array. This
     * avoids the second heap allocation in the common case where the
     * query result is small (most queries return ≤16 nodes). */
    if (capacity <= XPATH_NODESET_INLINE_CAPACITY) {
        nodeset->nodes = nodeset->inline_data;
        nodeset->capacity = XPATH_NODESET_INLINE_CAPACITY;
    } else if (capacity > 0) {
        nodeset->nodes = TAURUS_ALLOC_N(void*, capacity);
        if (!nodeset->nodes) {
            /* On failure, push back to free-list instead of freeing. */
            nodeset->inline_data[0] = (void*)xpath_nodeset_free_list;
            xpath_nodeset_free_list = nodeset;
            xpath_nodeset_free_list_count++;
            return NULL;
        }
        memset(nodeset->nodes, 0, sizeof(void*) * capacity);
        nodeset->capacity = capacity;
    } else {
        nodeset->nodes = NULL;
        nodeset->capacity = 0;
    }

    return nodeset;
}

void xpath_nodeset_free(XPathNodeSet* nodeset) {
    if (!nodeset) return;

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
                TaurusNamespaceNode* ns = (TaurusNamespaceNode*)node;
                if (ns->prefix) TAURUS_FREE(ns->prefix);
                if (ns->uri) TAURUS_FREE(ns->uri);
                TAURUS_FREE(ns);
            }
        }
    }

    /* Free the spill array unless it's the inline buffer (TODO 113). */
    if (nodeset->nodes && nodeset->nodes != nodeset->inline_data) {
        TAURUS_FREE(nodeset->nodes);
    }

    /* Push struct onto thread-local free-list (TODO 159 Phase B).
     * Cap prevents unbounded growth. inline_data[0] is reused as the
     * next-pointer for the singly-linked free-list; it is reset by
     * xpath_nodeset_new_with_capacity on pop. */
    if (xpath_nodeset_free_list_count < NODESET_FREE_LIST_CAP) {
        nodeset->count = 0;
        nodeset->capacity = 0;
        nodeset->owns_attributes = 0;
        nodeset->owns_namespaces = 0;
        nodeset->nodes = NULL;
        nodeset->inline_data[0] = (void*)xpath_nodeset_free_list;
        xpath_nodeset_free_list = nodeset;
        xpath_nodeset_free_list_count++;
    } else {
        TAURUS_FREE(nodeset);
    }
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

    /* SAFETY: Validate nodeset structure before reallocation
     * Corruption in count/capacity can cause heap corruption during realloc */
    if (nodeset->count > nodeset->capacity || nodeset->capacity > 1000000) {
        /* Corrupted structure - skip this addition */
        return;
    }

    /* Grow array if needed */
    if (nodeset->count >= nodeset->capacity) {
        size_t new_capacity = nodeset->capacity == 0 ? 4 : nodeset->capacity * 2;

        /* SAFETY: Check for overflow */
        if (new_capacity > 1000000) {
            return;
        }

        /* TODO 113 Phase 2: if currently using inline_data, switch
         * to a heap allocation and copy. REALLOC can't be used on
         * the inline buffer (it's part of the struct). */
        if (nodeset->nodes == nodeset->inline_data) {
            void** new_nodes = TAURUS_ALLOC_N(void*, new_capacity);
            if (!new_nodes) return;
            memcpy(new_nodes, nodeset->inline_data,
                   sizeof(void*) * nodeset->count);
            nodeset->nodes = new_nodes;
            nodeset->capacity = new_capacity;
        } else {
            /* SAFETY: Validate nodes pointer before realloc */
            if (nodeset->nodes && (uintptr_t)nodeset->nodes < 0x1000) {
                return;
            }

            void** new_nodes = TAURUS_REALLOC_N(nodeset->nodes, void*, new_capacity);
            if (!new_nodes) return;
            nodeset->nodes = new_nodes;
            nodeset->capacity = new_capacity;
        }
    }

    nodeset->nodes[nodeset->count++] = node;
}

/* Fast inline add (TODO 135). Skips the safety checks that
 * xpath_nodeset_add does. Caller MUST guarantee:
 *   - nodeset is non-NULL and well-formed (count <= capacity)
 *   - node is a valid pointer (not stale, not < 0x1000)
 *   - the element being added is unique within the nodeset (or
 *     the caller is OK with dups; e.g., descendant axis from a
 *     single root can't produce duplicates by tree structure)
 *
 * Used by VM hot paths: vm_apply_axis_descendant, vm_apply_absolute,
 * and the fused axis+predicate handlers. Each call goes from ~30 ns
 * (with checks) to ~5 ns (without).
 *
 * NOT exported — internal use only. Public callers go through
 * xpath_nodeset_add. */
void xpath_nodeset_add_fast(XPathNodeSet* nodeset, void* node) {
    if (nodeset->count >= nodeset->capacity) {
        size_t new_capacity = nodeset->capacity ? nodeset->capacity * 2 : 16;
        if (nodeset->nodes == nodeset->inline_data) {
            void** new_nodes = TAURUS_ALLOC_N(void*, new_capacity);
            if (!new_nodes) return;
            memcpy(new_nodes, nodeset->inline_data,
                   sizeof(void*) * nodeset->count);
            nodeset->nodes = new_nodes;
            nodeset->capacity = new_capacity;
        } else {
            void** new_nodes = TAURUS_REALLOC_N(nodeset->nodes, void*, new_capacity);
            if (!new_nodes) return;
            nodeset->nodes = new_nodes;
            nodeset->capacity = new_capacity;
        }
    }
    nodeset->nodes[nodeset->count++] = node;
}

struct taurus_xpath_result* xpath_result_new(XPathResultType type) {
    struct taurus_xpath_result* result = NULL;

    /* Fast path: pop from thread-local free-list (TODO 162). The
     * value union is reused as the next-pointer slot while the
     * struct is on the free-list — its lifetime is finished and
     * no live value occupies the union. */
    if (xpath_result_free_list) {
        result = xpath_result_free_list;
        xpath_result_free_list = (struct taurus_xpath_result*)
            result->value.nodeset_value;  /* next pointer */
        xpath_result_free_list_count--;
    } else {
        result = TAURUS_ALLOC(struct taurus_xpath_result);
        if (!result) return NULL;
    }

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

    /* Push onto thread-local free-list (TODO 162). Cap prevents
     * unbounded growth. The value union is safe to overwrite with
     * the next-pointer because the live payload has just been
     * released above. */
    if (xpath_result_free_list_count < XPATH_RESULT_FREE_LIST_CAP) {
        result->type = (XPathResultType)0;
        result->value.nodeset_value = (XPathNodeSet*)xpath_result_free_list;
        xpath_result_free_list = result;
        xpath_result_free_list_count++;
    } else {
        TAURUS_FREE(result);
    }
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
static struct taurus_xpath_result* evaluate_function_call_impl(XPathContext* ctx,
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

    /* Get function registry */
    if (!ctx->function_registry) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                "Function registry not initialized");
        return NULL;
    }

    XPathFunctionRegistry* registry = (XPathFunctionRegistry*)ctx->function_registry;

    /* Look up function */
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

    /* Call function handler. TODO 148 Phase 5: save/restore the
     * per-call user_data slot so the handler can read it via the
     * accessor — supports recursion (nested function calls). */
    void* saved_user_data = ctx->current_fn_user_data;
    ctx->current_fn_user_data = func_def->user_data;
    struct taurus_xpath_result* r = func_def->handler(ctx, ast->children, arg_count);
    ctx->current_fn_user_data = saved_user_data;
    return r;
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
            return evaluate_function_call_impl(ctx, ast);

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
                    /* Reference the variable's nodeset by copying the
                     * node pointer array. The nodes themselves are
                     * document-owned (TaurusElement pointers) so they
                     * remain valid for the document's lifetime; the
                     * result owns the new array. Variable storage must
                     * outlive the result (XPath spec guarantees this
                     * within a single eval call). */
                    XPathNodeSet* var_ns = xpath_variable_get_nodeset(var);
                    if (var_ns && var_ns->count > 0) {
                        result->value.nodeset_value = xpath_nodeset_new_with_capacity(var_ns->count);
                        if (!result->value.nodeset_value) return NULL;
                        for (size_t i = 0; i < var_ns->count; i++) {
                            xpath_nodeset_add(result->value.nodeset_value, var_ns->nodes[i]);
                        }
                    } else {
                        result->value.nodeset_value = xpath_nodeset_new();
                    }
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
