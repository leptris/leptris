/* taurus_api.c - Taurus core API: version, memory hooks, global state
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Core API functions.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include <stdlib.h>

/* ============================================================================
 * Version Constants
 * ============================================================================ */

#define TAURUS_VERSION "0.1.0"
#define TAURUS_VERSION_MAJOR 0
#define TAURUS_VERSION_MINOR 1
#define TAURUS_VERSION_PATCH 0

/* ============================================================================
 * Global State (Thread-Safety Considerations)
 * ============================================================================ */

/* Global default strict mode - used as default for new documents.
 * For thread-safe per-document concurrency, use taurus_document_set_strict().
 * This global is kept for backward compatibility with existing code.
 *
 * THREAD SAFETY: Reading/writing this global is not atomic. In multi-threaded
 * scenarios, use taurus_document_set_strict() after creating a document instead
 * of relying on the global default.
 */
static int g_taurus_strict_mode = 0;  /* Default: lenient mode (pugixml compat) */

/* Global custom allocation functions (NULL = use malloc/free) */
static taurus_allocation_function g_alloc_function = NULL;
static taurus_deallocation_function g_dealloc_function = NULL;

/* ============================================================================
 * Version Information
 * ============================================================================ */

/**
 * Get library version string
 */
TAURUS_API const char* taurus_version(void) {
    return TAURUS_VERSION;
}

/**
 * Get version components
 */
TAURUS_API void taurus_version_components(int* major, int* minor, int* patch) {
    if (major) *major = TAURUS_VERSION_MAJOR;
    if (minor) *minor = TAURUS_VERSION_MINOR;
    if (patch) *patch = TAURUS_VERSION_PATCH;
}

/* ============================================================================
 * Strict Mode (Global Default)
 * ============================================================================ */

/**
 * DEPRECATED: Set strict parsing mode globally
 *
 * This function sets the default strict mode for NEW documents.
 * Existing documents are not affected.
 *
 * For thread safety, use taurus_document_set_strict() after creating a document.
 *
 * @param strict 1 for strict XML 1.0 mode, 0 for lenient (pugixml compat)
 */
TAURUS_API void taurus_set_strict_mode(int strict) {
    g_taurus_strict_mode = (strict != 0);
}

/**
 * Get current global strict parsing mode (internal function)
 *
 * DEPRECATED: This returns the global default, not per-document mode.
 * Use taurus_document_get_strict() for per-document mode.
 *
 * @return Current global strict mode setting
 */
int taurus_get_strict_mode(void) {
    return g_taurus_strict_mode;
}

/* ============================================================================
 * Memory Management Helpers
 * ============================================================================ */

/**
 * Free string returned by libtaurus (Public API)
 */
TAURUS_API void taurus_free_string(char* str) {
    if (str) {
        TAURUS_FREE(str);
    }
}

/* ============================================================================
 * Memory Allocation Hooks (for testing and custom allocators)
 * ============================================================================ */

/**
 * Set custom memory management functions
 */
TAURUS_API void taurus_set_memory_management_functions(taurus_allocation_function alloc_function,
                                                         taurus_deallocation_function dealloc_function) {
    g_alloc_function = alloc_function;
    g_dealloc_function = dealloc_function;
}

/**
 * Get current memory allocation function
 */
TAURUS_API taurus_allocation_function taurus_get_memory_allocation_function(void) {
    return g_alloc_function;
}

/**
 * Get current memory deallocation function
 */
TAURUS_API taurus_deallocation_function taurus_get_memory_deallocation_function(void) {
    return g_dealloc_function;
}

/* Wrapper functions used internally for custom allocation */
void* taurus_alloc_hook(size_t size) {
    if (g_alloc_function) {
        return g_alloc_function(size);
    }
    return malloc(size);
}

void taurus_free_hook(void* ptr) {
    if (g_dealloc_function) {
        g_dealloc_function(ptr);
    } else {
        free(ptr);
    }
}

/* ============================================================================
 * Simplified Quick-Start API
 * ============================================================================ */

/* Note: taurus_parse() is implemented in taurus_parse_api.c */

/**
 * Get root element of document (simplified API)
 */
TAURUS_API TaurusElement taurus_root(TaurusDocument doc) {
    return taurus_document_root(doc);
}

/**
 * Find first child element by name (simplified API)
 */
TAURUS_API TaurusElement taurus_child(TaurusElement parent, const char* name) {
    return taurus_element_find_child(parent, name);
}

/**
 * Get element attribute value (simplified API)
 */
TAURUS_API const char* taurus_attr(TaurusElement elem, const char* name) {
    return taurus_element_attribute(elem, name);
}

/**
 * Get element text content (simplified API)
 */
TAURUS_API const char* taurus_text(TaurusElement elem) {
    return taurus_element_text(elem);
}

/**
 * Free document (simplified API)
 */
TAURUS_API void taurus_free(TaurusDocument doc) {
    taurus_document_free(doc);
}
