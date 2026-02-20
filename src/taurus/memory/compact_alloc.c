/* compact_alloc.c - Compact Memory Allocator for DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-block bump-pointer allocator for compact DOM.
 * This is the key to achieving O(1) allocation with perfect cache locality.
 *
 * Performance characteristics:
 * - Allocation: O(1) - just increment a pointer
 * - Deallocation: N/A - entire block freed at once
 * - Fragmentation: None - linear allocation
 * - Cache efficiency: Excellent - all data in one block
 */

#include "compact_alloc.h"
#include "../taurus_internal.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Compact Allocator Implementation
 * ============================================================================ */

/**
 * Create a compact allocator with pre-allocated block
 *
 * @param size Total size of memory block to allocate
 * @return Allocator handle, or NULL on failure
 */
CompactAllocator* compact_allocator_create(size_t size) {
    if (size == 0) return NULL;

    /* Allocate the main memory block */
    char* block = (char*)TAURUS_ALLOC(size);
    if (!block) return NULL;

    /* Allocate the allocator structure separately (small, persistent) */
    CompactAllocator* alloc = (CompactAllocator*)TAURUS_ALLOC(sizeof(CompactAllocator));
    if (!alloc) {
        TAURUS_FREE(block);
        return NULL;
    }

    /* Initialize allocator */
    memset(block, 0, size);
    alloc->base = block;
    alloc->size = size;
    alloc->offset = 0;
    alloc->string_table_offset = 0;  /* Set after node allocation phase */
    alloc->string_hash = NULL;
    alloc->string_hash_size = 0;
    alloc->error = 0;

    return alloc;
}

/**
 * Destroy compact allocator and free all memory
 *
 * @param alloc Allocator to destroy
 */
void compact_allocator_destroy(CompactAllocator* alloc) {
    if (!alloc) return;

    if (alloc->base) {
        TAURUS_FREE(alloc->base);
    }
    /* Note: string_hash is allocated within the main block, not separately */

    TAURUS_FREE(alloc);
}

/**
 * Allocate memory from compact block (O(1) bump pointer)
 *
 * @param alloc Allocator handle
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if out of space
 */
void* compact_alloc(CompactAllocator* alloc, size_t size) {
    if (!alloc || size == 0 || alloc->error) return NULL;

    /* Align to 8-byte boundary for proper pointer alignment */
    size = (size + 7) & ~7;

    /* Check if we have space */
    if (alloc->offset + size > alloc->size) {
        alloc->error = 1;  /* Mark as out of memory */
        return NULL;
    }

    void* ptr = alloc->base + alloc->offset;
    alloc->offset += size;

    return ptr;
}

/**
 * Allocate and copy a string into the compact block
 *
 * @param alloc Allocator handle
 * @param str String to copy (need not be null-terminated)
 * @param len Length of string
 * @return Offset of copied string from base
 */
uint32_t compact_alloc_string(CompactAllocator* alloc, const char* str, size_t len) {
    if (!alloc || !str || len == 0 || alloc->error) return 0;

    /* Ensure string table section is set up */
    if (alloc->string_table_offset == 0) {
        alloc->string_table_offset = alloc->offset;
    }

    /* Check for string interning (deduplication) */
    if (alloc->string_hash && len >= 3) {
        uint32_t existing = compact_find_string(alloc, str, len);
        if (existing) return existing;  /* Already exists, return existing offset */
    }

    /* Allocate space for string + null terminator */
    size_t total_size = len + 1;

    /* Align string offset */
    size_t aligned_offset = (alloc->offset + 7) & ~7;
    if (aligned_offset + total_size > alloc->size) {
        alloc->error = 1;
        return 0;
    }

    /* Copy string */
    char* dest = alloc->base + aligned_offset;
    memcpy(dest, str, len);
    dest[len] = '\0';

    alloc->offset = aligned_offset + total_size;

    uint32_t offset = (uint32_t)aligned_offset;

    /* Add to hash table for deduplication */
    if (alloc->string_hash && len >= 3) {
        compact_add_string(alloc, str, len, offset);
    }

    return offset;
}

/**
 * Allocate a null-terminated string (convenience function)
 *
 * @param alloc Allocator handle
 * @param str Null-terminated string to copy
 * @return Offset of copied string from base
 */
uint32_t compact_alloc_cstring(CompactAllocator* alloc, const char* str) {
    if (!str) return 0;
    return compact_alloc_string(alloc, str, strlen(str));
}

/* ============================================================================
 * String Interning Hash Table
 * ============================================================================ */

/* FNV-1a hash for string interning */
static uint32_t fnv1a_hash(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

/**
 * Initialize string hash table for interning
 *
 * @param alloc Allocator handle
 * @param bucket_count Number of hash buckets (power of 2)
 * @return 0 on success, -1 on failure
 */
int compact_init_string_hash(CompactAllocator* alloc, size_t bucket_count) {
    if (!alloc || bucket_count == 0) return -1;

    /* Allocate hash table within the compact block */
    size_t hash_size = bucket_count * sizeof(CompactStringEntry*);
    alloc->string_hash = (CompactStringEntry**)compact_alloc(alloc, hash_size);
    if (!alloc->string_hash) return -1;

    alloc->string_hash_size = bucket_count;
    memset(alloc->string_hash, 0, hash_size);

    return 0;
}

/**
 * Find a string in the hash table
 *
 * @param alloc Allocator handle
 * @param str String to find
 * @param len Length of string
 * @return Offset if found, 0 if not found
 */
uint32_t compact_find_string(CompactAllocator* alloc, const char* str, size_t len) {
    if (!alloc || !alloc->string_hash || !str || len == 0) return 0;

    uint32_t hash = fnv1a_hash(str, len);
    uint32_t bucket = hash & (alloc->string_hash_size - 1);

    CompactStringEntry* entry = alloc->string_hash[bucket];
    while (entry) {
        if (entry->length == len) {
            const char* entry_str = alloc->base + entry->offset;
            if (memcmp(entry_str, str, len) == 0) {
                return entry->offset;  /* Found! */
            }
        }
        entry = entry->next;
    }

    return 0;  /* Not found */
}

/**
 * Add a string to the hash table
 *
 * @param alloc Allocator handle
 * @param str String to add
 * @param len Length of string
 * @param offset Offset of string in block
 */
void compact_add_string(CompactAllocator* alloc, const char* str, size_t len, uint32_t offset) {
    if (!alloc || !alloc->string_hash || !str || len == 0 || offset == 0) return;

    uint32_t hash = fnv1a_hash(str, len);
    uint32_t bucket = hash & (alloc->string_hash_size - 1);

    /* Allocate entry within compact block */
    CompactStringEntry* entry = (CompactStringEntry*)compact_alloc(alloc, sizeof(CompactStringEntry));
    if (!entry) return;  /* Out of memory, skip interning */

    entry->offset = offset;
    entry->length = (uint16_t)len;
    entry->hash = hash;
    entry->next = alloc->string_hash[bucket];
    alloc->string_hash[bucket] = entry;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get remaining space in allocator
 */
size_t compact_remaining_space(const CompactAllocator* alloc) {
    if (!alloc) return 0;
    return alloc->size - alloc->offset;
}

/**
 * Get used space in allocator
 */
size_t compact_used_space(const CompactAllocator* alloc) {
    if (!alloc) return 0;
    return alloc->offset;
}

/**
 * Check if allocator has encountered an error
 */
int compact_has_error(const CompactAllocator* alloc) {
    return alloc ? alloc->error : 1;
}

/**
 * Get pointer from offset
 */
void* compact_get_ptr(const CompactAllocator* alloc, uint32_t offset) {
    if (!alloc || offset == 0 || offset >= alloc->size) return NULL;
    return alloc->base + offset;
}

/**
 * Get offset from pointer
 */
uint32_t compact_get_offset(const CompactAllocator* alloc, const void* ptr) {
    if (!alloc || !ptr) return 0;
    const char* p = (const char*)ptr;
    if (p < alloc->base || p >= alloc->base + alloc->size) return 0;
    return (uint32_t)(p - alloc->base);
}
