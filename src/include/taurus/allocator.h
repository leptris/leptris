/* allocator.h - Custom memory allocator interface
 * Copyright (c) 2024, Ribose Inc.
 *
 * This header provides a vtable-based allocator interface that allows
 * users to customize memory management for the Taurus XML parser.
 *
 * Use cases:
 * - Embedded systems with custom memory pools
 * - Memory tracking and debugging
 * - Arena allocators for bulk deallocation
 * - Custom alignment requirements
 *
 * Example usage:
 *
 *   // Create custom allocator
 *   TaurusAllocator my_alloc = {
 *       .alloc = my_malloc,
 *       .realloc = my_realloc,
 *       .free = my_free,
 *       .userdata = my_pool
 *   };
 *
 *   // Set as default allocator
 *   taurus_set_allocator(&my_alloc);
 *
 *   // All Taurus operations now use custom allocator
 *   TaurusDocument doc = taurus_parse_string(xml, len, NULL);
 */

#ifndef TAURUS_ALLOCATOR_H
#define TAURUS_ALLOCATOR_H

#include <stddef.h>

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

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Allocator Vtable
 * ============================================================================ */

/**
 * Memory allocation function type.
 *
 * @param size Number of bytes to allocate
 * @param userdata User-provided context pointer
 * @return Pointer to allocated memory, or NULL on failure
 */
typedef void* (*TaurusAllocFunc)(size_t size, void* userdata);

/**
 * Memory reallocation function type.
 *
 * @param ptr Pointer to previously allocated memory (may be NULL)
 * @param size New size in bytes
 * @param userdata User-provided context pointer
 * @return Pointer to reallocated memory, or NULL on failure
 */
typedef void* (*TaurusReallocFunc)(void* ptr, size_t size, void* userdata);

/**
 * Memory deallocation function type.
 *
 * @param ptr Pointer to memory to free (may be NULL)
 * @param userdata User-provided context pointer
 */
typedef void (*TaurusFreeFunc)(void* ptr, void* userdata);

/**
 * Allocator vtable structure.
 *
 * Contains function pointers for memory operations and an optional
 * userdata pointer that is passed to each function.
 *
 * All function pointers must be non-NULL when passed to taurus_set_allocator().
 */
typedef struct {
    TaurusAllocFunc   alloc;     /* Allocate memory */
    TaurusReallocFunc realloc;   /* Reallocate memory */
    TaurusFreeFunc    free;      /* Free memory */
    void*             userdata;  /* User context passed to all functions */
} TaurusAllocator;

/* ============================================================================
 * Allocator Management API
 * ============================================================================ */

/**
 * Set the global allocator for all Taurus operations.
 *
 * This affects all allocations made after this call, including those
 * for documents, elements, XPath results, and internal structures.
 *
 * IMPORTANT: Set the allocator BEFORE creating any documents or other
 * Taurus objects. Changing the allocator while objects exist may lead
 * to undefined behavior if objects allocated with different allocators
 * interact.
 *
 * @param allocator Pointer to allocator structure (copied internally).
 *                  Pass NULL to restore the default allocator.
 * @return TAURUS_OK on success, TAURUS_ERROR_INVALID_ARG if any
 *         function pointer is NULL (when allocator is non-NULL).
 *
 * Thread Safety: This function is NOT thread-safe. Set the allocator
 * once at program startup before any concurrent access.
 */
TAURUS_API int taurus_set_allocator(const TaurusAllocator* allocator);

/**
 * Get the current allocator.
 *
 * This returns a copy of the current allocator structure. Use this
 * to inspect the current settings or to make modifications before
 * setting a new allocator.
 *
 * @param out_allocator Pointer to structure to fill with current settings.
 *                      Must not be NULL.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if out_allocator is NULL.
 */
TAURUS_API int taurus_get_allocator(TaurusAllocator* out_allocator);

/**
 * Get the default allocator (standard malloc/realloc/free).
 *
 * This returns a pointer to the static default allocator structure.
 * Use this to reset to default or to wrap the default with tracking.
 *
 * @return Pointer to static default allocator structure.
 */
TAURUS_API const TaurusAllocator* taurus_default_allocator(void);

/* ============================================================================
 * Memory Statistics (Optional)
 * ============================================================================ */

/**
 * Memory statistics structure.
 *
 * Filled by taurus_get_memory_stats() when tracking is enabled.
 */
typedef struct {
    size_t current_bytes;     /* Currently allocated bytes */
    size_t peak_bytes;        /* Peak allocated bytes */
    size_t allocation_count;  /* Total number of allocations */
    size_t free_count;        /* Total number of frees */
} TaurusMemoryStats;

/**
 * Enable or disable memory tracking.
 *
 * When enabled, the allocator tracks allocation statistics that can
 * be retrieved with taurus_get_memory_stats(). This adds overhead to
 * each allocation and should typically only be used for debugging.
 *
 * @param enable 1 to enable tracking, 0 to disable.
 * @return TAURUS_OK on success.
 */
TAURUS_API int taurus_set_memory_tracking(int enable);

/**
 * Get memory statistics.
 *
 * Returns statistics for allocations made since tracking was enabled.
 *
 * @param out_stats Pointer to structure to fill with statistics.
 *                  Must not be NULL.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if out_stats is NULL,
 *         TAURUS_ERROR_INVALID_STATE if tracking is not enabled.
 */
TAURUS_API int taurus_get_memory_stats(TaurusMemoryStats* out_stats);

/**
 * Reset memory statistics counters.
 *
 * Resets all counters to zero without disabling tracking.
 *
 * @return TAURUS_OK on success.
 */
TAURUS_API int taurus_reset_memory_stats(void);

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * Allocate memory using the current allocator.
 *
 * This is the internal allocation function used by Taurus. Users
 * can call it directly for custom allocations that should use the
 * same allocator as Taurus.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
TAURUS_API void* taurus_mem_alloc(size_t size);

/**
 * Reallocate memory using the current allocator.
 *
 * @param ptr Pointer to previously allocated memory (may be NULL).
 * @param size New size in bytes.
 * @return Pointer to reallocated memory, or NULL on failure.
 */
TAURUS_API void* taurus_mem_realloc(void* ptr, size_t size);

/**
 * Free memory using the current allocator.
 *
 * @param ptr Pointer to memory to free (may be NULL).
 */
TAURUS_API void taurus_mem_free(void* ptr);

/**
 * Duplicate a string using the current allocator.
 *
 * @param str String to duplicate (may be NULL, returns NULL).
 * @return Pointer to duplicated string, or NULL on failure.
 */
TAURUS_API char* taurus_mem_strdup(const char* str);

/* ============================================================================
 * Per-Document Allocator API (Phase 16)
 * ============================================================================ */

/* Forward declaration */
struct taurus_document;

/**
 * Set a per-document allocator.
 *
 * This allows each document to use a different memory allocator,
 * enabling isolated memory pools for different documents.
 *
 * When set, all allocations for this document (including pool
 * allocations, string allocations, and node allocations) will
 * use this allocator instead of the global allocator.
 *
 * @param doc Document to set allocator for.
 * @param allocator Allocator to use, or NULL to use global allocator.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if doc is NULL.
 *
 * IMPORTANT: Set the allocator BEFORE any allocations are made for
 * the document. Changing the allocator after allocations have been
 * made may lead to undefined behavior.
 *
 * Thread Safety: Not thread-safe. Set the allocator before concurrent
 * access to the document.
 *
 * Example:
 *   TaurusAllocator my_pool = create_arena_allocator(pool_size);
 *   taurus_document_set_allocator(doc, &my_pool);
 *   // All document allocations now use my_pool
 */
TAURUS_API int taurus_document_set_allocator(
    struct taurus_document* doc,
    const TaurusAllocator* allocator
);

/**
 * Get the effective allocator for a document.
 *
 * Returns the document's allocator if set, or the global allocator
 * if no per-document allocator is set.
 *
 * @param doc Document to get allocator for.
 * @return Pointer to effective allocator (never NULL).
 */
TAURUS_API const TaurusAllocator* taurus_document_get_allocator(
    struct taurus_document* doc
);

/**
 * Allocate memory using a document's allocator.
 *
 * This function uses the document's allocator if set, otherwise
 * falls back to the global allocator.
 *
 * @param doc Document to allocate for (may be NULL, uses global).
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
TAURUS_API void* taurus_mem_alloc_for_doc(
    struct taurus_document* doc,
    size_t size
);

/**
 * Free memory using a document's allocator.
 *
 * This function uses the document's allocator if set, otherwise
 * falls back to the global allocator.
 *
 * @param doc Document the memory was allocated for (may be NULL).
 * @param ptr Pointer to memory to free (may be NULL).
 */
TAURUS_API void taurus_mem_free_for_doc(
    struct taurus_document* doc,
    void* ptr
);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ALLOCATOR_H */
