/* compact_single_alloc.h - Single-Block Allocator for Compact DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-block bump-pointer allocator for compact DOM parsing.
 * This is designed for the two-pass parsing approach where we know
 * the exact size needed before allocation.
 *
 * Performance characteristics:
 * - Allocation: O(1) - just increment a pointer
 * - Deallocation: N/A - entire block freed at once
 * - Fragmentation: None - linear allocation
 * - Cache efficiency: Excellent - all data in one block
 */

#ifndef TAURUS_COMPACT_SINGLE_ALLOC_H
#define TAURUS_COMPACT_SINGLE_ALLOC_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * String Interning Hash Entry
 * ============================================================================ */

/**
 * Entry in the string interning hash table
 *
 * Stored within the compact memory block itself.
 */
typedef struct CompactSingleStringEntry {
    uint32_t offset;              /* Offset to string in block */
    uint16_t length;              /* String length */
    uint16_t reserved;            /* Padding */
    uint32_t hash;                /* FNV-1a hash value */
    struct CompactSingleStringEntry* next; /* Collision chain */
} CompactSingleStringEntry;

/* ============================================================================
 * Single-Block Allocator Structure
 * ============================================================================ */

/**
 * Single-block allocator - one allocation for entire DOM
 *
 * Usage:
 * 1. Estimate size with parser_estimate_size()
 * 2. Create allocator with exact size
 * 3. Allocate nodes (elements, attributes, etc.)
 * 4. Allocate strings (with optional interning)
 * 5. Use the block until document is freed
 */
typedef struct {
    char* base;                   /* Base of single memory block */
    size_t size;                  /* Total size allocated */
    size_t offset;                /* Current bump pointer position */
    size_t string_table_offset;   /* Start of string table section */

    /* String interning hash table (within block) */
    CompactSingleStringEntry** string_hash;
    size_t string_hash_size;

    /* Error flag */
    int error;
} CompactSingleAllocator;

/* ============================================================================
 * Allocator Lifecycle
 * ============================================================================ */

/**
 * Create a single-block allocator
 *
 * @param size Total size of memory block to allocate
 * @return Allocator handle, or NULL on failure
 */
CompactSingleAllocator* compact_single_alloc_create(size_t size);

/**
 * Destroy allocator and free all memory
 *
 * @param alloc Allocator to destroy
 */
void compact_single_alloc_destroy(CompactSingleAllocator* alloc);

/* ============================================================================
 * Memory Allocation
 * ============================================================================ */

/**
 * Allocate memory from block (O(1) bump pointer)
 *
 * @param alloc Allocator handle
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if out of space
 */
void* compact_single_alloc(CompactSingleAllocator* alloc, size_t size);

/**
 * Inline allocation for hot paths - avoids function call overhead
 *
 * Use this in performance-critical code paths (like parsing).
 * Falls back to compact_single_alloc() if growth is needed.
 *
 * @param alloc Allocator handle
 * @param size Number of bytes to allocate (will be 8-byte aligned)
 * @return Pointer to allocated memory, or NULL if out of space
 */
static inline void* compact_single_alloc_inline(CompactSingleAllocator* alloc, size_t size) {
    if (!alloc || size == 0 || alloc->error) return NULL;

    /* Align to 8-byte boundary */
    size = (size + 7) & ~7;

    /* Fast path: check if we have space without function call */
    if (alloc->offset + size > alloc->size) {
        /* Slow path: need to grow - use full function */
        return compact_single_alloc(alloc, size);
    }

    /* Fast path: just bump the pointer */
    void* ptr = alloc->base + alloc->offset;
    alloc->offset += size;
    return ptr;
}

/**
 * Allocate and copy a string into the block
 *
 * @param alloc Allocator handle
 * @param str String to copy (need not be null-terminated)
 * @param len Length of string
 * @return Offset of copied string from base (0 on failure)
 */
uint32_t compact_single_alloc_string(CompactSingleAllocator* alloc, const char* str, size_t len);

/**
 * Allocate a null-terminated string (convenience function)
 *
 * @param alloc Allocator handle
 * @param str Null-terminated string to copy
 * @return Offset of copied string from base (0 on failure)
 */
uint32_t compact_single_alloc_cstring(CompactSingleAllocator* alloc, const char* str);

/* ============================================================================
 * String Interning
 * ============================================================================ */

/**
 * Initialize string hash table for interning
 *
 * Must be called after allocator creation, before allocating strings.
 *
 * @param alloc Allocator handle
 * @param bucket_count Number of hash buckets (power of 2)
 * @return 0 on success, -1 on failure
 */
int compact_single_init_string_hash(CompactSingleAllocator* alloc, size_t bucket_count);

/**
 * Find a string in the hash table
 *
 * @param alloc Allocator handle
 * @param str String to find
 * @param len Length of string
 * @return Offset if found, 0 if not found
 */
uint32_t compact_single_find_string(CompactSingleAllocator* alloc, const char* str, size_t len);

/**
 * Add a string to the hash table
 *
 * @param alloc Allocator handle
 * @param str String to add
 * @param len Length of string
 * @param offset Offset of string in block
 */
void compact_single_add_string(CompactSingleAllocator* alloc, const char* str, size_t len, uint32_t offset);

/* ============================================================================
 * Growth Support
 * ============================================================================ */

/**
 * Grow the allocator's memory block
 *
 * This enables single-pass parsing without size estimation.
 * Pointers are INVALIDATED after this call - use offsets instead!
 *
 * @param alloc Allocator handle
 * @param min_needed Minimum additional space needed
 * @return 0 on success, -1 on failure
 */
int compact_single_alloc_grow(CompactSingleAllocator* alloc, size_t min_needed);

/**
 * Ensure allocator has at least 'needed' bytes remaining
 *
 * Grows if necessary. Returns -1 if growth fails.
 *
 * @param alloc Allocator handle
 * @param needed Minimum bytes needed
 * @return 0 on success, -1 on failure
 */
int compact_single_alloc_ensure(CompactSingleAllocator* alloc, size_t needed);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get remaining space in allocator
 */
size_t compact_single_remaining_space(const CompactSingleAllocator* alloc);

/**
 * Get used space in allocator
 */
size_t compact_single_used_space(const CompactSingleAllocator* alloc);

/**
 * Check if allocator has encountered an error
 */
int compact_single_has_error(const CompactSingleAllocator* alloc);

/**
 * Get pointer from offset
 */
void* compact_single_get_ptr(const CompactSingleAllocator* alloc, uint32_t offset);

/**
 * Get offset from pointer
 */
uint32_t compact_single_get_offset(const CompactSingleAllocator* alloc, const void* ptr);

/**
 * Inline version of compact_single_get_offset for hot paths
 * WARNING: Does NOT check bounds - use only when you know ptr is valid
 */
static inline uint32_t compact_single_get_offset_fast(const CompactSingleAllocator* alloc, const void* ptr) {
    return (uint32_t)((const char*)ptr - alloc->base);
}

/* ============================================================================
 * Convenience Macros
 * ============================================================================ */

/**
 * Allocate a typed structure
 */
#define COMPACT_SINGLE_ALLOC(alloc, type) \
    ((type*)compact_single_alloc(alloc, sizeof(type)))

/**
 * Allocate a typed structure using inline fast path
 * Use this in hot paths like parsing for 5-10% performance gain
 */
#define COMPACT_SINGLE_ALLOC_FAST(alloc, type) \
    ((type*)compact_single_alloc_inline(alloc, sizeof(type)))

/**
 * Allocate an array
 */
#define COMPACT_SINGLE_ALLOC_ARRAY(alloc, type, count) \
    ((type*)compact_single_alloc(alloc, sizeof(type) * (count)))

/**
 * Convert offset to typed pointer
 */
#define COMPACT_SINGLE_GET_TYPED(alloc, type, offset) \
    ((type*)compact_single_get_ptr(alloc, offset))

#endif /* TAURUS_COMPACT_SINGLE_ALLOC_H */
