/* compact_alloc.h - Compact Memory Allocator for DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-block bump-pointer allocator for compact DOM.
 * This is the key to achieving O(1) allocation with perfect cache locality.
 */

#ifndef TAURUS_COMPACT_ALLOC_H
#define TAURUS_COMPACT_ALLOC_H

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
typedef struct CompactStringEntry {
    uint32_t offset;              /* Offset to string in block */
    uint16_t length;              /* String length */
    uint16_t reserved;            /* Padding */
    uint32_t hash;                /* FNV-1a hash value */
    struct CompactStringEntry* next; /* Collision chain */
} CompactStringEntry;

/* ============================================================================
 * Compact Allocator Structure
 * ============================================================================ */

/**
 * Compact allocator - manages a single memory block for DOM
 *
 * Usage:
 * 1. Create with estimated size
 * 2. Allocate nodes (elements, attributes, etc.)
 * 3. Allocate strings (with optional interning)
 * 4. Use the block until document is freed
 */
typedef struct {
    char* base;                   /* Base of single memory block */
    size_t size;                  /* Total size allocated */
    size_t offset;                /* Current bump pointer position */
    size_t string_table_offset;   /* Start of string table section */

    /* String interning hash table (within block) */
    CompactStringEntry** string_hash;
    size_t string_hash_size;

    /* Error flag */
    int error;
} CompactAllocator;

/* ============================================================================
 * Allocator Lifecycle
 * ============================================================================ */

/**
 * Create a compact allocator with pre-allocated block
 *
 * @param size Total size of memory block to allocate
 * @return Allocator handle, or NULL on failure
 */
CompactAllocator* compact_allocator_create(size_t size);

/**
 * Destroy compact allocator and free all memory
 *
 * @param alloc Allocator to destroy
 */
void compact_allocator_destroy(CompactAllocator* alloc);

/* ============================================================================
 * Memory Allocation
 * ============================================================================ */

/**
 * Allocate memory from compact block (O(1) bump pointer)
 *
 * @param alloc Allocator handle
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if out of space
 */
void* compact_alloc(CompactAllocator* alloc, size_t size);

/**
 * Allocate and copy a string into the compact block
 *
 * @param alloc Allocator handle
 * @param str String to copy (need not be null-terminated)
 * @param len Length of string
 * @return Offset of copied string from base (0 on failure)
 */
uint32_t compact_alloc_string(CompactAllocator* alloc, const char* str, size_t len);

/**
 * Allocate a null-terminated string (convenience function)
 *
 * @param alloc Allocator handle
 * @param str Null-terminated string to copy
 * @return Offset of copied string from base (0 on failure)
 */
uint32_t compact_alloc_cstring(CompactAllocator* alloc, const char* str);

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
int compact_init_string_hash(CompactAllocator* alloc, size_t bucket_count);

/**
 * Find a string in the hash table
 *
 * @param alloc Allocator handle
 * @param str String to find
 * @param len Length of string
 * @return Offset if found, 0 if not found
 */
uint32_t compact_find_string(CompactAllocator* alloc, const char* str, size_t len);

/**
 * Add a string to the hash table
 *
 * @param alloc Allocator handle
 * @param str String to add
 * @param len Length of string
 * @param offset Offset of string in block
 */
void compact_add_string(CompactAllocator* alloc, const char* str, size_t len, uint32_t offset);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get remaining space in allocator
 */
size_t compact_remaining_space(const CompactAllocator* alloc);

/**
 * Get used space in allocator
 */
size_t compact_used_space(const CompactAllocator* alloc);

/**
 * Check if allocator has encountered an error
 */
int compact_has_error(const CompactAllocator* alloc);

/**
 * Get pointer from offset
 */
void* compact_get_ptr(const CompactAllocator* alloc, uint32_t offset);

/**
 * Get offset from pointer
 */
uint32_t compact_get_offset(const CompactAllocator* alloc, const void* ptr);

/* ============================================================================
 * Convenience Macros
 * ============================================================================ */

/**
 * Allocate a typed structure from compact allocator
 */
#define COMPACT_ALLOC(alloc, type) \
    ((type*)compact_alloc(alloc, sizeof(type)))

/**
 * Allocate an array from compact allocator
 */
#define COMPACT_ALLOC_ARRAY(alloc, type, count) \
    ((type*)compact_alloc(alloc, sizeof(type) * (count)))

/**
 * Convert offset to typed pointer
 */
#define COMPACT_GET_TYPED(alloc, type, offset) \
    ((type*)compact_get_ptr(alloc, offset))

#endif /* TAURUS_COMPACT_ALLOC_H */
