/* ultra_fast_alloc.h - Zero-Check Bump Pointer Allocator
 * Copyright (c) 2026, Ribose Inc.
 *
 * ULTRA-FAST ALLOCATOR DESIGN:
 *
 * KEY INSIGHT: The compact_single_alloc_inline() has overhead from:
 * 1. Error flag check (alloc->error)
 * 2. Alignment calculation ((size + 7) & ~7)
 * 3. Size check (offset + size > size)
 * 4. Potential slow path call to compact_single_alloc()
 *
 * This adds ~2-3 cycles per allocation overhead.
 *
 * SOLUTION: Pure bump pointer with NO checks.
 * - Trust pre-allocation (2x input size)
 * - Return offsets directly, not pointers
 * - Single instruction allocation
 *
 * Performance target: 1-2 cycles per allocation
 */

#ifndef TAURUS_ULTRA_FAST_ALLOC_H
#define TAURUS_ULTRA_FAST_ALLOC_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Ultra-Fast Allocator Structure
 * ============================================================================ */

/**
 * Ultra-fast allocator - minimal structure for maximum speed
 *
 * Usage:
 * 1. Estimate size (2x input size recommended)
 * 2. Create allocator with ultra_fast_alloc_create()
 * 3. Allocate with ALLOC_OFFSET_* macros
 * 4. Use offsets for all navigation
 * 5. Free with ultra_fast_alloc_destroy()
 */
typedef struct {
    char* base;        /* Base of memory block */
    size_t offset;     /* Current bump pointer (ALSO the next offset to use) */
} UltraFastAlloc;

/* ============================================================================
 * Creation and Destruction
 * ============================================================================ */

/**
 * Create ultra-fast allocator
 *
 * @param size Size of memory block to allocate
 * @return Allocator handle, or NULL on failure
 */
static inline UltraFastAlloc* ultra_fast_alloc_create(size_t size) {
    UltraFastAlloc* alloc = (UltraFastAlloc*)malloc(sizeof(UltraFastAlloc));
    if (!alloc) return NULL;

    alloc->base = (char*)malloc(size);
    if (!alloc->base) {
        free(alloc);
        return NULL;
    }

    alloc->offset = 0;
    return alloc;
}

/**
 * Destroy allocator and free memory
 */
static inline void ultra_fast_alloc_destroy(UltraFastAlloc* alloc) {
    if (alloc) {
        if (alloc->base) {
            free(alloc->base);
        }
        free(alloc);
    }
}

/* ============================================================================
 * Ultra-Fast Allocation Macros
 * ============================================================================ */

/**
 * Allocate bytes and return OFFSET directly
 *
 * This is the HOT PATH allocation - ZERO checks, ZERO branches.
 * Trust that pre-allocation was sufficient.
 *
 * Usage:
 *   uint32_t offset = ALLOC_OFFSET(alloc, 16);
 *   char* ptr = alloc->base + offset;
 */
#define ALLOC_OFFSET(alloc, size) ({ \
    size_t _off = (alloc)->offset; \
    (alloc)->offset += (size); \
    (uint32_t)_off; \
})

/**
 * Allocate 16-byte aligned structure and return OFFSET
 *
 * All v3 structures are 16 or 20 bytes, so we align to 16.
 * This ensures cache-line alignment for structures.
 */
#define ALLOC_OFFSET_ALIGNED(alloc, size) ({ \
    size_t _off = (alloc)->offset; \
    size_t _aligned = ((_off + 15) & ~15); \
    (alloc)->offset = _aligned + (size); \
    (uint32_t)_aligned; \
})

/**
 * Allocate 16-byte structure (element, attribute, text)
 * Returns OFFSET directly, not pointer.
 */
#define ALLOC_16_OFFSET(alloc) ALLOC_OFFSET(alloc, 16)

/**
 * Allocate 20-byte structure (v3 element)
 * Returns OFFSET directly, not pointer.
 */
#define ALLOC_20_OFFSET(alloc) ALLOC_OFFSET_ALIGNED(alloc, 20)

/**
 * Get pointer from offset
 * Only use when you need to write to the structure.
 */
#define OFFSET_TO_PTR(base, off) ((void*)((char*)(base) + (off)))

/**
 * Get typed pointer from offset
 */
#define OFFSET_TO_TYPED(base, off, type) ((type*)((char*)(base) + (off)))

/* ============================================================================
 * Convenience Functions
 * ============================================================================ */

/**
 * Get current offset (for snapshot/restore)
 */
static inline size_t ultra_fast_alloc_get_offset(const UltraFastAlloc* alloc) {
    return alloc->offset;
}

/**
 * Restore offset (for rollback)
 */
static inline void ultra_fast_alloc_set_offset(UltraFastAlloc* alloc, size_t offset) {
    alloc->offset = offset;
}

/**
 * Get remaining space (for debugging only)
 */
static inline size_t ultra_fast_alloc_remaining(const UltraFastAlloc* alloc, size_t total_size) {
    return total_size - alloc->offset;
}

/* ============================================================================
 * Pre-Allocation Size Estimation
 * ============================================================================ */

/**
 * Estimate allocation size from input size
 *
 * Strategy: Over-allocate 2x to avoid any growth during parsing.
 * This is conservative but ensures zero checks in hot path.
 *
 * For XML:
 * - Average element: 20 bytes (v3)
 * - Average attribute: 16 bytes
 * - Average element has ~2-3 attributes
 * - Total: ~50-70 bytes per element
 * - Input text: ~50-100 bytes per element
 *
 * Ratio: ~0.7x input size for structures
 * With 2x margin: ~1.5x input size
 *
 * We use 2x for safety margin.
 */
#define ULTRA_FAST_SIZE_ESTIMATE(input_len) ((input_len) * 2 + 65536)

/**
 * Minimum allocation size (64 KB)
 */
#define ULTRA_FAST_MIN_SIZE 65536

#endif /* TAURUS_ULTRA_FAST_ALLOC_H */
