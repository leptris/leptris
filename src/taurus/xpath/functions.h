/* functions.h - XPath 1.0 function library API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C implementation of XPath 1.0 standard function library.
 * Provides all 27 XPath 1.0 functions with extensible registry.
 */

#ifndef XPATH_FUNCTIONS_H
#define XPATH_FUNCTIONS_H

#include "../taurus_internal.h"
#include "parser.h"

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct xpath_function_registry XPathFunctionRegistry;

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
typedef struct taurus_xpath_result* (*XPathFunctionHandler)(
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