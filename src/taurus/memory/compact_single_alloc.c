/* compact_single_alloc.c - Single-Block Allocator for Compact DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * Single-block bump-pointer allocator for compact DOM.
 * This is the key to achieving O(1) allocation with perfect cache locality.
 */

#include "compact_single_alloc.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Compact Allocator Implementation
 * ============================================================================ */

/**
 * Create a single-block allocator
 */
CompactSingleAllocator* compact_single_alloc_create(size_t size) {
    if (size == 0) return NULL;

    /* Allocate the main memory block */
    char* block = (char*)malloc(size);
    if (!block) return NULL;

    /* Allocate the allocator structure separately (small, persistent) */
    CompactSingleAllocator* alloc = (CompactSingleAllocator*)malloc(sizeof(CompactSingleAllocator));
    if (!alloc) {
        free(block);
        return NULL;
    }

    /* Initialize allocator */
    memset(block, 0, size);
    alloc->base = block;
    alloc->size = size;
    alloc->offset = 0;
    alloc->string_table_offset = 0;
    alloc->string_hash = NULL;
    alloc->string_hash_size = 0;
    alloc->error = 0;

    return alloc;
}

/**
 * Destroy allocator and free all memory
 */
void compact_single_alloc_destroy(CompactSingleAllocator* alloc) {
    if (!alloc) return;

    if (alloc->base) {
        free(alloc->base);
    }

    free(alloc);
}

/**
 * Allocate memory from block (O(1) bump pointer)
 */
void* compact_single_alloc(CompactSingleAllocator* alloc, size_t size) {
    if (!alloc || size == 0 || alloc->error) return NULL;

    /* Align to 8-byte boundary for proper pointer alignment */
    size = (size + 7) & ~7;

    /* Check if we have space */
    if (alloc->offset + size > alloc->size) {
        alloc->error = 1;
        return NULL;
    }

    void* ptr = alloc->base + alloc->offset;
    alloc->offset += size;

    return ptr;
}

/**
 * Allocate and copy a string into the block
 */
uint32_t compact_single_alloc_string(CompactSingleAllocator* alloc, const char* str, size_t len) {
    if (!alloc || !str || len == 0 || alloc->error) return 0;

    /* Ensure string table section is set up */
    if (alloc->string_table_offset == 0) {
        alloc->string_table_offset = alloc->offset;
    }

    /* Check for string interning (deduplication) */
    if (alloc->string_hash && len >= 3) {
        uint32_t existing = compact_single_find_string(alloc, str, len);
        if (existing) return existing;
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
        compact_single_add_string(alloc, str, len, offset);
    }

    return offset;
}

/**
 * Allocate a null-terminated string (convenience function)
 */
uint32_t compact_single_alloc_cstring(CompactSingleAllocator* alloc, const char* str) {
    if (!str) return 0;
    return compact_single_alloc_string(alloc, str, strlen(str));
}

/* ============================================================================
 * String Interning Hash Table
 * ============================================================================ */

/* FNV-1a hash for string interning */
static uint32_t fnv1a_hash_single(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;
    }
    return hash;
}

/**
 * Initialize string hash table for interning
 */
int compact_single_init_string_hash(CompactSingleAllocator* alloc, size_t bucket_count) {
    if (!alloc || bucket_count == 0) return -1;

    /* Allocate hash table within the compact block */
    size_t hash_size = bucket_count * sizeof(CompactSingleStringEntry*);
    alloc->string_hash = (CompactSingleStringEntry**)compact_single_alloc(alloc, hash_size);
    if (!alloc->string_hash) return -1;

    alloc->string_hash_size = bucket_count;
    memset(alloc->string_hash, 0, hash_size);

    return 0;
}

/**
 * Find a string in the hash table
 */
uint32_t compact_single_find_string(CompactSingleAllocator* alloc, const char* str, size_t len) {
    if (!alloc || !alloc->string_hash || !str || len == 0) return 0;

    uint32_t hash = fnv1a_hash_single(str, len);
    uint32_t bucket = hash & (alloc->string_hash_size - 1);

    CompactSingleStringEntry* entry = alloc->string_hash[bucket];
    while (entry) {
        if (entry->length == len) {
            const char* entry_str = alloc->base + entry->offset;
            if (memcmp(entry_str, str, len) == 0) {
                return entry->offset;
            }
        }
        entry = entry->next;
    }

    return 0;
}

/**
 * Add a string to the hash table
 */
void compact_single_add_string(CompactSingleAllocator* alloc, const char* str, size_t len, uint32_t offset) {
    if (!alloc || !alloc->string_hash || !str || len == 0 || offset == 0) return;

    uint32_t hash = fnv1a_hash_single(str, len);
    uint32_t bucket = hash & (alloc->string_hash_size - 1);

    /* Allocate entry within compact block */
    CompactSingleStringEntry* entry = (CompactSingleStringEntry*)compact_single_alloc(alloc, sizeof(CompactSingleStringEntry));
    if (!entry) return;

    entry->offset = offset;
    entry->length = (uint16_t)len;
    entry->hash = hash;
    entry->next = alloc->string_hash[bucket];
    alloc->string_hash[bucket] = entry;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

size_t compact_single_remaining_space(const CompactSingleAllocator* alloc) {
    if (!alloc) return 0;
    return alloc->size - alloc->offset;
}

size_t compact_single_used_space(const CompactSingleAllocator* alloc) {
    if (!alloc) return 0;
    return alloc->offset;
}

int compact_single_has_error(const CompactSingleAllocator* alloc) {
    return alloc ? alloc->error : 1;
}

void* compact_single_get_ptr(const CompactSingleAllocator* alloc, uint32_t offset) {
    if (!alloc || offset == 0 || offset >= alloc->size) return NULL;
    return alloc->base + offset;
}

uint32_t compact_single_get_offset(const CompactSingleAllocator* alloc, const void* ptr) {
    if (!alloc || !ptr) return 0;
    const char* p = (const char*)ptr;
    if (p < alloc->base || p >= alloc->base + alloc->size) return 0;
    return (uint32_t)(p - alloc->base);
}
