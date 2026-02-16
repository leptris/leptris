/* allocator.c - Custom memory allocator implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements the allocator interface with support for:
 * - Custom allocators (vtable-based)
 * - Memory tracking (optional)
 * - Thread-safe statistics
 * - Per-document allocators (Phase 16)
 */

#include "../include/taurus/allocator.h"
#include "../include/taurus/types.h"
#include "taurus_internal.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ============================================================================
 * Internal State
 * ============================================================================ */

/* Global allocator state */
static TaurusAllocator g_allocator;
static int g_allocator_initialized = 0;

/* Memory tracking state */
static int g_tracking_enabled = 0;
static TaurusMemoryStats g_stats = {0};
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Default Allocator Implementation
 * ============================================================================ */

static void* default_alloc(size_t size, void* userdata) {
    (void)userdata;
    return malloc(size);
}

static void* default_realloc(void* ptr, size_t size, void* userdata) {
    (void)userdata;
    return realloc(ptr, size);
}

static void default_free(void* ptr, void* userdata) {
    (void)userdata;
    free(ptr);
}

/* Static default allocator structure */
static const TaurusAllocator g_default_allocator = {
    .alloc = default_alloc,
    .realloc = default_realloc,
    .free = default_free,
    .userdata = NULL
};

/* Initialize global allocator on first use */
static void init_allocator(void) {
    if (!g_allocator_initialized) {
        g_allocator = g_default_allocator;
        g_allocator_initialized = 1;
    }
}

/* ============================================================================
 * Allocator Management API
 * ============================================================================ */

TAURUS_API int taurus_set_allocator(const TaurusAllocator* allocator) {
    init_allocator();

    if (allocator == NULL) {
        /* Reset to default allocator */
        g_allocator = g_default_allocator;
        return TAURUS_OK;
    }

    /* Validate allocator structure */
    if (allocator->alloc == NULL ||
        allocator->realloc == NULL ||
        allocator->free == NULL) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Copy allocator structure */
    g_allocator = *allocator;
    return TAURUS_OK;
}

TAURUS_API int taurus_get_allocator(TaurusAllocator* out_allocator) {
    if (out_allocator == NULL) {
        return TAURUS_ERROR_NULL_ARG;
    }

    init_allocator();
    *out_allocator = g_allocator;
    return TAURUS_OK;
}

TAURUS_API const TaurusAllocator* taurus_default_allocator(void) {
    return &g_default_allocator;
}

/* ============================================================================
 * Memory Tracking
 * ============================================================================ */

TAURUS_API int taurus_set_memory_tracking(int enable) {
    pthread_mutex_lock(&g_stats_mutex);
    g_tracking_enabled = enable;
    if (enable) {
        /* Reset stats when enabling */
        memset(&g_stats, 0, sizeof(g_stats));
    }
    pthread_mutex_unlock(&g_stats_mutex);
    return TAURUS_OK;
}

TAURUS_API int taurus_get_memory_stats(TaurusMemoryStats* out_stats) {
    if (out_stats == NULL) {
        return TAURUS_ERROR_NULL_ARG;
    }

    pthread_mutex_lock(&g_stats_mutex);
    if (!g_tracking_enabled) {
        pthread_mutex_unlock(&g_stats_mutex);
        return TAURUS_ERROR_INVALID_STATE;
    }

    *out_stats = g_stats;
    pthread_mutex_unlock(&g_stats_mutex);
    return TAURUS_OK;
}

TAURUS_API int taurus_reset_memory_stats(void) {
    pthread_mutex_lock(&g_stats_mutex);
    memset(&g_stats, 0, sizeof(g_stats));
    pthread_mutex_unlock(&g_stats_mutex);
    return TAURUS_OK;
}

/* ============================================================================
 * Tracked Allocation Helpers
 * ============================================================================ */

/* Header prepended to each allocation when tracking is enabled */
typedef struct {
    size_t size;
} AllocationHeader;

#define HEADER_SIZE (sizeof(AllocationHeader))
#define ALIGN_HEADER(size) (((size) + HEADER_SIZE + 15) & ~15)  /* 16-byte align */

static void* track_alloc(size_t size) {
    size_t total_size = ALIGN_HEADER(size);
    void* ptr = g_allocator.alloc(total_size, g_allocator.userdata);

    if (ptr) {
        AllocationHeader* header = (AllocationHeader*)ptr;
        header->size = size;

        pthread_mutex_lock(&g_stats_mutex);
        g_stats.current_bytes += size;
        if (g_stats.current_bytes > g_stats.peak_bytes) {
            g_stats.peak_bytes = g_stats.current_bytes;
        }
        g_stats.allocation_count++;
        pthread_mutex_unlock(&g_stats_mutex);

        return (char*)ptr + HEADER_SIZE;
    }
    return NULL;
}

static void* track_realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return track_alloc(size);
    }

    /* Get original size */
    AllocationHeader* old_header = (AllocationHeader*)((char*)ptr - HEADER_SIZE);
    size_t old_size = old_header->size;

    /* Allocate new block */
    size_t total_size = ALIGN_HEADER(size);
    void* new_ptr = g_allocator.alloc(total_size, g_allocator.userdata);

    if (new_ptr) {
        AllocationHeader* header = (AllocationHeader*)new_ptr;
        header->size = size;

        /* Copy old data */
        size_t copy_size = (old_size < size) ? old_size : size;
        memcpy((char*)new_ptr + HEADER_SIZE, ptr, copy_size);

        /* Update stats */
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.current_bytes -= old_size;
        g_stats.current_bytes += size;
        if (g_stats.current_bytes > g_stats.peak_bytes) {
            g_stats.peak_bytes = g_stats.current_bytes;
        }
        g_stats.allocation_count++;
        pthread_mutex_unlock(&g_stats_mutex);

        /* Free old block */
        g_allocator.free(old_header, g_allocator.userdata);

        return (char*)new_ptr + HEADER_SIZE;
    }
    return NULL;
}

static void track_free(void* ptr) {
    if (ptr == NULL) return;

    AllocationHeader* header = (AllocationHeader*)((char*)ptr - HEADER_SIZE);
    size_t size = header->size;

    pthread_mutex_lock(&g_stats_mutex);
    g_stats.current_bytes -= size;
    g_stats.free_count++;
    pthread_mutex_unlock(&g_stats_mutex);

    g_allocator.free(header, g_allocator.userdata);
}

/* ============================================================================
 * Public Allocation Functions
 * ============================================================================ */

TAURUS_API void* taurus_mem_alloc(size_t size) {
    init_allocator();

    if (g_tracking_enabled) {
        return track_alloc(size);
    }

    return g_allocator.alloc(size, g_allocator.userdata);
}

TAURUS_API void* taurus_mem_realloc(void* ptr, size_t size) {
    init_allocator();

    if (g_tracking_enabled) {
        return track_realloc(ptr, size);
    }

    return g_allocator.realloc(ptr, size, g_allocator.userdata);
}

TAURUS_API void taurus_mem_free(void* ptr) {
    if (ptr == NULL) return;

    init_allocator();

    if (g_tracking_enabled) {
        track_free(ptr);
        return;
    }

    g_allocator.free(ptr, g_allocator.userdata);
}

TAURUS_API char* taurus_mem_strdup(const char* str) {
    if (str == NULL) return NULL;

    size_t len = strlen(str);
    char* dup = (char*)taurus_mem_alloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

/* ============================================================================
 * Per-Document Allocator API (Phase 16)
 * ============================================================================ */

/* Forward declaration for document structure */
struct taurus_document;

TAURUS_API int taurus_document_set_allocator(
    struct taurus_document* doc,
    const TaurusAllocator* allocator
) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;

    if (allocator) {
        /* Validate allocator structure */
        if (allocator->alloc == NULL ||
            allocator->realloc == NULL ||
            allocator->free == NULL) {
            return TAURUS_ERROR_INVALID_ARG;
        }
    }

    doc->allocator = (void*)allocator;
    return TAURUS_OK;
}

TAURUS_API const TaurusAllocator* taurus_document_get_allocator(
    struct taurus_document* doc
) {
    init_allocator();

    if (doc && doc->allocator) {
        return (const TaurusAllocator*)doc->allocator;
    }

    /* Fall back to global allocator */
    return &g_allocator;
}

TAURUS_API void* taurus_mem_alloc_for_doc(
    struct taurus_document* doc,
    size_t size
) {
    init_allocator();

    const TaurusAllocator* alloc = taurus_document_get_allocator(doc);

    if (g_tracking_enabled && doc) {
        /* Use tracked allocation with document's allocator */
        size_t total_size = ALIGN_HEADER(size);
        void* ptr = alloc->alloc(total_size, alloc->userdata);

        if (ptr) {
            AllocationHeader* header = (AllocationHeader*)ptr;
            header->size = size;

            pthread_mutex_lock(&g_stats_mutex);
            g_stats.current_bytes += size;
            if (g_stats.current_bytes > g_stats.peak_bytes) {
                g_stats.peak_bytes = g_stats.current_bytes;
            }
            g_stats.allocation_count++;
            pthread_mutex_unlock(&g_stats_mutex);

            return (char*)ptr + HEADER_SIZE;
        }
        return NULL;
    }

    return alloc->alloc(size, alloc->userdata);
}

TAURUS_API void taurus_mem_free_for_doc(
    struct taurus_document* doc,
    void* ptr
) {
    if (ptr == NULL) return;

    init_allocator();

    const TaurusAllocator* alloc = taurus_document_get_allocator(doc);

    if (g_tracking_enabled && doc) {
        /* Use tracked deallocation */
        AllocationHeader* header = (AllocationHeader*)((char*)ptr - HEADER_SIZE);
        size_t size = header->size;

        pthread_mutex_lock(&g_stats_mutex);
        g_stats.current_bytes -= size;
        g_stats.free_count++;
        pthread_mutex_unlock(&g_stats_mutex);

        alloc->free(header, alloc->userdata);
        return;
    }

    alloc->free(ptr, alloc->userdata);
}
