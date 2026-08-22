/* libleptris - XPath Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains XPath 1.0 evaluation and result operations.
 */

#ifndef LEPTRIS_XPATH_XPATH_H
#define LEPTRIS_XPATH_XPATH_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef LEPTRIS_API
#  ifdef LEPTRIS_FOR_BINDGEN
#    define LEPTRIS_API
#  elif defined(_WIN32)
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
       /* Mirrors leptris.h (issue #278): CMake defines
        * LEPTRIS_BUILDING_DLL on the objects that build the DLL. */
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    define LEPTRIS_API __attribute__((visibility("default")))
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
 * Memory: Caller must call leptris_xpath_result_free() when done
 * Thread safety: Not thread-safe. One evaluation per thread.
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 * - All 13 axes: child, descendant, parent, ancestor, sibling, etc.
 * - All 27 functions: string(), count(), position(), etc.
 * - All operators: =, !=, <, <=, >, >=, +, -, *, div, mod, |, and, or
 * - Predicates: [1], [\@attr], [position() > 2], etc.
 */
LEPTRIS_API LeptrisXPathResult leptris_xpath_eval(
    LeptrisDocument doc,
    LeptrisElement context,
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
 * Memory: Caller must call leptris_xpath_result_free() when done
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 *
 * Variables are referenced in expressions using $name syntax:
 *   leptris_xpath_variable_set_number(vars, "x", 42);
 *   leptris_xpath_eval(doc, "//item[@id = $x]", vars);
 */
LEPTRIS_API LeptrisXPathResult leptris_xpath_eval_with_vars(
    LeptrisDocument doc,
    const char* expression,
    LeptrisXPathVariableSet variables
);

/* ============================================================================
 * XPath Result Operations
 * ============================================================================ */

/**
 * Get XPath result type
 *
 * @param result XPath result
 * @return Result type or -1 if result is NULL
 */
LEPTRIS_API LeptrisXPathResultType leptris_xpath_result_type(LeptrisXPathResult result);

/**
 * Get nodeset size (for NODESET results)
 *
 * @param result XPath result
 * @return Number of nodes or 0 if not a nodeset or result is NULL
 */
LEPTRIS_API size_t leptris_xpath_result_count(LeptrisXPathResult result);

/**
 * Get node from nodeset by index
 *
 * @param result XPath result
 * @param index Node index (0-based)
 * @return Element or NULL if index out of bounds or not a nodeset
 *
 * Memory: Element is owned by document. Do not free separately.
 */
LEPTRIS_API LeptrisElement leptris_xpath_result_get(LeptrisXPathResult result, size_t index);

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
LEPTRIS_API int leptris_xpath_result_boolean(LeptrisXPathResult result);

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
LEPTRIS_API double leptris_xpath_result_number(LeptrisXPathResult result);

/**
 * Get string value (for STRING results or type conversion)
 *
 * @param result XPath result
 * @return String value or NULL if result is NULL
 *
 * Memory: Caller must call leptris_free_string() when done
 *
 * Type conversion rules:
 * - STRING: Direct value
 * - BOOLEAN: "true" or "false"
 * - NUMBER: String representation of number
 * - NODESET: String value of first node (recursive text concatenation)
 */
LEPTRIS_API char* leptris_xpath_result_string(LeptrisXPathResult result);

/**
 * Free XPath result
 *
 * @param result Result to free (can be NULL)
 */
LEPTRIS_API void leptris_xpath_result_free(LeptrisXPathResult result);

/* ============================================================================
 * XPath Variable Operations
 * ============================================================================ */

/**
 * Create a new variable set
 *
 * @return New variable set, or NULL on error
 *
 * Memory: Caller must call leptris_xpath_variable_set_free() when done
 */
LEPTRIS_API LeptrisXPathVariableSet leptris_xpath_variable_set_new(void);

/**
 * Free a variable set
 *
 * @param set Variable set to free (can be NULL)
 */
LEPTRIS_API void leptris_xpath_variable_set_free(LeptrisXPathVariableSet set);

/**
 * Add a boolean variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Boolean value
 * @return LEPTRIS_OK on success, error code on failure
 */
LEPTRIS_API LeptrisStatus leptris_xpath_variable_set_boolean(LeptrisXPathVariableSet set, const char* name, int value);

/**
 * Add a number variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Number value
 * @return LEPTRIS_OK on success, error code on failure
 */
LEPTRIS_API LeptrisStatus leptris_xpath_variable_set_number(LeptrisXPathVariableSet set, const char* name, double value);

/**
 * Add a string variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value String value
 * @return LEPTRIS_OK on success, error code on failure
 */
LEPTRIS_API LeptrisStatus leptris_xpath_variable_set_string(LeptrisXPathVariableSet set, const char* name, const char* value);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_XPATH_XPATH_H */
