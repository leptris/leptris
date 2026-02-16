/* functions_registry.c - XPath custom function registry
 * Copyright (c) 2024, Ribose Inc.
 *
 * Custom function registration support for XPath.
 * The core registry implementation is in functions.c.
 */

#include "functions_internal.h"

/* Forward declaration for custom function type */
typedef TaurusXPathResult (*TaurusXPathCustomFunction)(
    void* context,
    int argc,
    TaurusXPathResult argv
);

/* ============================================================================
 * Global Custom Function Registry
 * ============================================================================ */

/* Global registry for user-defined custom functions */
static XPathFunctionRegistry* g_custom_function_registry = NULL;

/* Initialize custom function registry on first use */
static XPathFunctionRegistry* get_custom_registry(void) {
    if (!g_custom_function_registry) {
        g_custom_function_registry = xpath_function_registry_new();
    }
    return g_custom_function_registry;
}

/* ============================================================================
 * Public Custom Function API
 * ============================================================================ */

/**
 * Register a custom XPath function
 */
TAURUS_API TaurusStatus taurus_xpath_register_custom_function(
    const char* name,
    TaurusXPathCustomFunction func
) {
    if (!name || !func) return TAURUS_ERROR_NULL_ARG;

    XPathFunctionRegistry* registry = get_custom_registry();
    if (!registry) return TAURUS_ERROR_MEMORY;

    /* Check if already registered */
    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            /* Update existing entry */
            registry->functions[i].handler = (XPathFunctionHandler)func;
            return TAURUS_OK;
        }
    }

    /* Add new entry - we use -1 for both min/max args to indicate variable */
    xpath_function_registry_register(registry, name, (XPathFunctionHandler)func, -1, -1);
    return TAURUS_OK;
}

/**
 * Unregister a custom XPath function
 */
TAURUS_API TaurusStatus taurus_xpath_unregister_custom_function(const char* name) {
    if (!name) return TAURUS_ERROR_NULL_ARG;

    XPathFunctionRegistry* registry = g_custom_function_registry;
    if (!registry) return TAURUS_ERROR_NOT_FOUND;

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->functions[i].name, name) == 0) {
            /* Shift remaining entries */
            for (size_t j = i; j < registry->count - 1; j++) {
                registry->functions[j] = registry->functions[j + 1];
            }
            registry->count--;
            return TAURUS_OK;
        }
    }

    return TAURUS_ERROR_NOT_FOUND;
}

/**
 * Check if a custom function is registered
 */
TAURUS_API int taurus_xpath_has_custom_function(const char* name) {
    if (!name) return 0;

    XPathFunctionRegistry* registry = g_custom_function_registry;
    if (!registry) return 0;

    return xpath_function_registry_lookup(registry, name) != NULL;
}

/**
 * Look up a custom function (internal use)
 */
XPathFunctionHandler xpath_custom_function_lookup(const char* name) {
    if (!name || !g_custom_function_registry) return NULL;
    return xpath_function_registry_lookup(g_custom_function_registry, name);
}

/* Note: The standard function registration (xpath_function_registry_init_standard),
 * core registry functions (xpath_function_registry_new, etc.), and supported
 * function lists (taurus_xpath_function_supported, etc.) are implemented in
 * functions.c where the static function handlers are defined.
 */
