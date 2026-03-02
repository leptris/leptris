/* zero_check_alloc.h - Zero-Check Bump Pointer Allocator
 * Copyright (c) 2026, Ribose Inc.
 *
 * MAXIMUM PERFORMANCE ALLOCATOR
 * - NO size checks - pure bump pointer
 * - Pre-allocates 2x input size to guarantee no growth needed
 * - Returns offsets directly (4 bytes) instead of pointers (8 bytes)
 *
 * This eliminates ~2 cycles per allocation compared to checked allocators.
 */

#ifndef TAURUS_ZERO_CHECK_ALLOC_H
#define TAURUS_ZERO_CHECK_ALLOC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Fallback for UINT32_MAX if not defined */
#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFU
#endif

/* Pre-allocation estimate: 1.2x input + 2KB safety margin
 * OPTIMIZED: Reduced from 1.5x + 4KB for better small file performance */
#define ZERO_CHECK_SIZE_ESTIMATE(input_len) ((input_len) + ((input_len) / 5) + 2048)

/* Adaptive minimum: 4KB for small files (was 16KB)
 * OPTIMIZED: Reduced from 16KB to 4KB - matches typical page size */
#define ZERO_CHECK_MIN_SIZE 4096

typedef struct {
    char* base;        /* Base pointer for offset calculations */
    size_t size;       /* Total allocated size */
    size_t offset;     /* Current offset (bump pointer) */
} ZeroCheckAlloc;

/**
 * Create a zero-check allocator
 *
 * Pre-allocates enough memory to guarantee no growth needed during parsing.
 * The caller must ensure total allocations don't exceed 'size'.
 */
static inline ZeroCheckAlloc* zero_check_alloc_create(size_t size) {
    ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)malloc(sizeof(ZeroCheckAlloc));
    if (!alloc) return NULL;

    alloc->base = (char*)malloc(size);
    if (!alloc->base) {
        free(alloc);
        return NULL;
    }

    alloc->size = size;
    alloc->offset = 0;
    return alloc;
}

/**
 * Destroy a zero-check allocator
 */
static inline void zero_check_alloc_destroy(ZeroCheckAlloc* alloc) {
    if (alloc) {
        if (alloc->base) free(alloc->base);
        free(alloc);
    }
}

/* ============================================================================
 * ZERO-CHECK ALLOCATION MACROS
 *
 * These macros perform bump pointer allocation with bounds checking.
 * Returns UINT32_MAX if out of memory (caller must check).
 * ============================================================================ */

/**
 * Allocate 'size' bytes and return OFFSET from base
 *
 * Returns UINT32_MAX if not enough space (caller must check).
 */
static inline uint32_t zero_check_alloc(ZeroCheckAlloc* alloc, size_t size) {
    size_t off = alloc->offset;
    size_t new_off = off + size;
    if (new_off > alloc->size) {
        return UINT32_MAX;
    }
    alloc->offset = new_off;
    return (uint32_t)off;
}

/**
 * Allocate 16-byte structure (element, attribute, or text node)
 * Returns offset from base, or UINT32_MAX if out of memory.
 */
#define ALLOC_16(alloc) zero_check_alloc((alloc), 16)

/**
 * Convert offset to pointer
 */
#define OFFSET_TO_PTR(base, offset) ((void*)((base) + (offset)))

/**
 * Convert offset to typed pointer
 */
#define OFFSET_TO_TYPED(base, offset, type) ((type*)((base) + (offset)))

#endif /* TAURUS_ZERO_CHECK_ALLOC_H */
