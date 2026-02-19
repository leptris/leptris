/* libtaurus - XPath Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains XPath 1.0 evaluation and result operations.
 */

#ifndef TAURUS_XPATH_XPATH_H
#define TAURUS_XPATH_XPATH_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef TAURUS_API
#  ifdef _WIN32
#    ifdef TAURUS_BUILD_SHARED
#      define TAURUS_API __declspec(dllexport)
#    elif defined(TAURUS_USE_SHARED)
#      define TAURUS_API __declspec(dllimport)
#    else
#      define TAURUS_API
#    endif
#  else
#    define TAURUS_API
#  endif
#endif

/* ============================================================================
 * XPath Evaluation Operations
 * ============================================================================ */

/**
 * Evaluate XPath expression
 *
 * @param doc Document (required)
 * @param context Context element (NULL = document root)
 * @param expression XPath expression string
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 * Thread safety: Not thread-safe. One evaluation per thread.
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 * - All 13 axes: child, descendant, parent, ancestor, sibling, etc.
 * - All 27 functions: string(), count(), position(), etc.
 * - All operators: =, !=, <, <=, >, >=, +, -, *, div, mod, |, and, or
 * - Predicates: [1], [@attr], [position() > 2], etc.
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression
);

/**
 * Evaluate XPath expression with variables
 *
 * @param doc Document to evaluate against
 * @param expression XPath expression string
 * @param variables Variable set (can be NULL)
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 *
 * Variables are referenced in expressions using $name syntax:
 *   taurus_xpath_variable_set_number(vars, "x", 42);
 *   taurus_xpath_eval(doc, "//item[@id = $x]", vars);
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars(
    TaurusDocument doc,
    const char* expression,
    TaurusXPathVariableSet variables
);

/* ============================================================================
 * XPath Pre-Compilation API (Performance Optimization)
 * ============================================================================ */

/**
 * Compile XPath expression for faster repeated evaluation
 *
 * PERFORMANCE: Pre-compile once, evaluate many times.
 * Provides 10-50x speedup for repeated evaluations of the same expression.
 *
 * @param expression XPath expression string
 * @return Compiled expression or NULL on error
 *
 * Memory: Caller must call taurus_xpath_compiled_free() when done
 *
 * Example:
 *   TaurusXPathCompiled compiled = taurus_xpath_compile("//item[@id=$x]");
 *   if (compiled) {
 *       for (each document/context) {
 *           TaurusXPathResult result = taurus_xpath_eval_compiled(doc, ctx, compiled);
 *           // use result
 *           taurus_xpath_result_free(result);
 *       }
 *       taurus_xpath_compiled_free(compiled);
 *   }
 */
TAURUS_API TaurusXPathCompiled taurus_xpath_compile(const char* expression);

/**
 * Evaluate compiled XPath expression
 *
 * @param doc Document (required)
 * @param context Context element (NULL = document root)
 * @param compiled Compiled expression from taurus_xpath_compile()
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 *
 * PERFORMANCE: 10-50x faster than taurus_xpath_eval() for repeated calls.
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_compiled(
    TaurusDocument doc,
    TaurusElement context,
    TaurusXPathCompiled compiled
);

/**
 * Evaluate compiled XPath expression with variables
 *
 * @param doc Document to evaluate against
 * @param compiled Compiled expression
 * @param variables Variable set (can be NULL)
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_compiled_with_vars(
    TaurusDocument doc,
    TaurusXPathCompiled compiled,
    TaurusXPathVariableSet variables
);

/**
 * Free compiled XPath expression
 *
 * @param compiled Compiled expression to free (can be NULL)
 */
TAURUS_API void taurus_xpath_compiled_free(TaurusXPathCompiled compiled);

/* ============================================================================
 * XPath Result Operations
 * ============================================================================ */

/**
 * Get XPath result type
 *
 * @param result XPath result
 * @return Result type or -1 if result is NULL
 */
TAURUS_API TaurusXPathResultType taurus_xpath_result_type(TaurusXPathResult result);

/**
 * Get nodeset size (for NODESET results)
 *
 * @param result XPath result
 * @return Number of nodes or 0 if not a nodeset or result is NULL
 */
TAURUS_API size_t taurus_xpath_result_count(TaurusXPathResult result);

/**
 * Get node from nodeset by index
 *
 * @param result XPath result
 * @param index Node index (0-based)
 * @return Element or NULL if index out of bounds or not a nodeset
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index);

/**
 * Get boolean value (for BOOLEAN results or type conversion)
 *
 * @param result XPath result
 * @return Boolean value (1 = true, 0 = false)
 *
 * Type conversion rules:
 * - BOOLEAN: Direct value
 * - NUMBER: true if non-zero and not NaN
 * - STRING: true if non-empty
 * - NODESET: true if non-empty
 */
TAURUS_API int taurus_xpath_result_boolean(TaurusXPathResult result);

/**
 * Get number value (for NUMBER results or type conversion)
 *
 * @param result XPath result
 * @return Number value (NaN if conversion fails)
 *
 * Type conversion rules:
 * - NUMBER: Direct value
 * - BOOLEAN: 1.0 or 0.0
 * - STRING: Parsed as number (NaN if invalid)
 * - NODESET: First node's string value converted to number
 */
TAURUS_API double taurus_xpath_result_number(TaurusXPathResult result);

/**
 * Get string value (for STRING results or type conversion)
 *
 * @param result XPath result
 * @return String value or NULL if result is NULL
 *
 * Memory: Caller must call taurus_free_string() when done
 *
 * Type conversion rules:
 * - STRING: Direct value
 * - BOOLEAN: "true" or "false"
 * - NUMBER: String representation of number
 * - NODESET: String value of first node (recursive text concatenation)
 */
TAURUS_API char* taurus_xpath_result_string(TaurusXPathResult result);

/**
 * Free XPath result
 *
 * @param result Result to free (can be NULL)
 */
TAURUS_API void taurus_xpath_result_free(TaurusXPathResult result);

/* ============================================================================
 * XPath Variable Operations
 * ============================================================================ */

/**
 * Create a new variable set
 *
 * @return New variable set, or NULL on error
 *
 * Memory: Caller must call taurus_xpath_variable_set_free() when done
 */
TAURUS_API TaurusXPathVariableSet taurus_xpath_variable_set_new(void);

/**
 * Free a variable set
 *
 * @param set Variable set to free (can be NULL)
 */
TAURUS_API void taurus_xpath_variable_set_free(TaurusXPathVariableSet set);

/**
 * Add a boolean variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Boolean value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_boolean(TaurusXPathVariableSet set, const char* name, int value);

/**
 * Add a number variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Number value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_number(TaurusXPathVariableSet set, const char* name, double value);

/**
 * Add a string variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value String value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_string(TaurusXPathVariableSet set, const char* name, const char* value);

/* ============================================================================
 * XPath Custom Function Extension API
 * ============================================================================ */

/**
 * Custom XPath function callback type
 *
 * @param context XPath evaluation context (provides document, context node, etc.)
 * @param argc Number of arguments passed to the function
 * @param argv Array of argument results (already evaluated)
 * @return Function result, or NULL on error
 *
 * Memory: The returned result will be freed by the XPath engine.
 *         Do not free argv elements; they are owned by the engine.
 */
typedef TaurusXPathResult (*TaurusXPathCustomFunction)(
    void* context,
    int argc,
    TaurusXPathResult* argv
);

/**
 * Register a custom XPath function
 *
 * Custom functions can override built-in functions with the same name.
 * The function is registered globally and affects all XPath evaluations.
 *
 * @param name Function name (must be a valid XPath function name)
 * @param func Function callback
 * @return TAURUS_OK on success, error code on failure
 *
 * Thread safety: This function is not thread-safe. Register all custom
 * functions before using XPath in a multi-threaded context.
 *
 * Example:
 *   TaurusXPathResult my_concat(void* ctx, int argc, TaurusXPathResult argv) {
 *       // Custom implementation
 *   }
 *   taurus_xpath_register_custom_function("my-concat", my_concat);
 *   // Now can use: //item[my-concat(@first, @last)]
 */
TAURUS_API TaurusStatus taurus_xpath_register_custom_function(
    const char* name,
    TaurusXPathCustomFunction func
);

/**
 * Unregister a custom XPath function
 *
 * @param name Function name to unregister
 * @return TAURUS_OK on success, error code if function not found
 */
TAURUS_API TaurusStatus taurus_xpath_unregister_custom_function(const char* name);

/**
 * Check if a custom function is registered
 *
 * @param name Function name to check
 * @return 1 if registered, 0 otherwise
 */
TAURUS_API int taurus_xpath_has_custom_function(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_XPATH_XPATH_H */
