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

/* Pre-allocation estimate: 2x input + 64KB safety margin */
#define ZERO_CHECK_SIZE_ESTIMATE(input_len) ((input_len) * 2 + 65536)
#define ZERO_CHECK_MIN_SIZE 131072  /* 128KB minimum */

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
 * These macros perform PURE bump pointer allocation with NO checks.
 * This saves ~2 cycles per allocation (branch + comparison).
 *
 * IMPORTANT: The allocator MUST be pre-sized large enough to hold all data.
 * ============================================================================ */

/**
 * Allocate 'size' bytes and return OFFSET from base
 *
 * NO bounds checking - caller must ensure pre-allocation is sufficient.
 */
#define ZERO_CHECK_ALLOC(alloc, size) ({ \
    size_t _off = (alloc)->offset; \
    (alloc)->offset += (size); \
    (uint32_t)_off; \
})

/**
 * Allocate 16-byte structure (element, attribute, or text node)
 * Returns offset from base.
 */
#define ALLOC_16(alloc) ZERO_CHECK_ALLOC(alloc, 16)

/**
 * Convert offset to pointer
 */
#define OFFSET_TO_PTR(base, offset) ((void*)((base) + (offset)))

/**
 * Convert offset to typed pointer
 */
#define OFFSET_TO_TYPED(base, offset, type) ((type*)((base) + (offset)))

#endif /* TAURUS_ZERO_CHECK_ALLOC_H */
