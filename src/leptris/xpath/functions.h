/* functions.h - XPath 1.0 function library API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 standard function library.
 * Provides all 27 XPath 1.0 functions with extensible registry.
 */

#ifndef XPATH_FUNCTIONS_H
#define XPATH_FUNCTIONS_H

#include "../leptris_internal.h"
#include "parser.h"

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct xpath_function_registry XPathFunctionRegistry;

/* Document-scoped custom function registry (TODO 148): built lazily
 * per evaluation, freed with the document. */
XPathFunctionRegistry* leptris_xpath_build_custom_registry(
    struct leptris_document* doc);
void leptris_xpath_free_custom_fns(struct leptris_document* doc);

/* XSLT/Schematron bridge registrations — defined in the xslt/sch
 * TUs; the declaration lives here (the registry is an xpath type)
 * so no TU needs a block-scope extern (leptris#1681). */
void xslt_register_bridge_static(XPathFunctionRegistry*, void*);
void xslt_register_ufn_handlers(XPathFunctionRegistry*, void*);
void xslt_register_bridge_handlers(XPathFunctionRegistry*, void*);
void leptris_sch_register_key_bridge(XPathFunctionRegistry*, void*);

/* EXSLT function pack (functions_exslt.c). */
void leptris_exslt_register(XPathFunctionRegistry* registry);

/* XPath 3.1 pack (functions_ext31.c) + helpers shared with the
 * evaluator/xquery TUs. */
void xpath_register_fn31(XPathFunctionRegistry* registry);
char* ctor_member_markup(void* node);
char* xpath_seq_carrier_of(struct leptris_xpath_result* vr);
char* xpath_item_arg_string(struct leptris_xpath_result* vr);
int xpath_array_members_of(const char* content, char*** out,
                           size_t* out_n);
void* xpath_map_builder_new(void);
void xpath_map_builder_add(void* b, const char* k, const char* v);
struct leptris_xpath_result* xpath_map_builder_finish(void* b);

/* ============================================================================
 * Function Handler Type
 * ============================================================================ */

/**
 * XPath function handler
 *
 * @param context Evaluation context
 * @param args Array of pointers to AST nodes (function arguments)
 * @param arg_count Number of arguments
 * @return Result of function evaluation, or NULL on error
 */
typedef struct leptris_xpath_result* (*XPathFunctionHandler)(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
);

/* ============================================================================
 * Function Definition Structure
 * ============================================================================ */

/**
 * Function definition
 */
typedef struct {
    const char* name;           /* Function name */
    XPathFunctionHandler handler; /* Handler function */
    int min_args;               /* Minimum arguments */
    int max_args;               /* Maximum arguments (-1 = unlimited) */
    /* Per-function opaque state. TODO 148 Phase 5: passed back to
     * the handler via the dispatch TLS slot so custom (user) fns
     * can carry a callback + closure. Standard XPath 1.0 fns leave
     * this NULL and ignore the slot. */
    void* user_data;
} XPathFunctionDef;

/* ============================================================================
 * Function Registry Structure
 * ============================================================================ */

/**
 * Function registry
 * Stores registered XPath functions
 */
struct xpath_function_registry {
    XPathFunctionDef* functions;
    size_t count;
    size_t capacity;
};

/* ============================================================================
 * Registry Management
 * ============================================================================ */

/**
 * Create new function registry
 *
 * @return New registry, or NULL on error
 */
XPathFunctionRegistry* xpath_function_registry_new(void);

/**
 * Free function registry
 *
 * @param registry Registry to free
 */
void xpath_function_registry_free(XPathFunctionRegistry* registry);

/**
 * Register a function
 *
 * @param registry Registry to register in
 * @param name Function name
 * @param handler Handler function
 * @param min_args Minimum arguments
 * @param max_args Maximum arguments (-1 for unlimited)
 */
void xpath_function_registry_register(
    XPathFunctionRegistry* registry,
    const char* name,
    XPathFunctionHandler handler,
    int min_args,
    int max_args
);

/* Same, with per-function opaque state (TODO 148): the LAST
 * registration still wins and carries ITS user_data — overriding
 * layers (XSLT bridge) must use this variant so their handler's
 * closure replaces the standard entry's NULL. */
void xpath_function_registry_register_ud(
    XPathFunctionRegistry* registry,
    const char* name,
    XPathFunctionHandler handler,
    int min_args,
    int max_args,
    void* user_data
);

/**
 * Lookup function by name
 *
 * @param registry Registry to search
 * @param name Function name
 * @return Handler function, or NULL if not found
 */
XPathFunctionHandler xpath_function_registry_lookup(
    XPathFunctionRegistry* registry,
    const char* name
);

/**
 * Get function definition by name
 *
 * @param registry Registry to search
 * @param name Function name
 * @return Function definition, or NULL if not found
 */
XPathFunctionDef* xpath_function_registry_get(
    XPathFunctionRegistry* registry,
    const char* name
);

/* Returns the shared standard registry, initializing it on first call.
 * The registry is read-only after init — concurrent reads are safe.
 * Per-eval function registry setup was the largest single perf cost
 * in leptris_xpath_eval (TODO 113). */
XPathFunctionRegistry* xpath_function_registry_get_standard(void);

/* ============================================================================
 * Standard Function Library
 * ============================================================================ */

/**
 * Initialize standard XPath 1.0 functions
 * Registers all 27 XPath 1.0 functions
 *
 * @param registry Registry to initialize
 */
void xpath_function_registry_init_standard(XPathFunctionRegistry* registry);

#endif /* XPATH_FUNCTIONS_H */