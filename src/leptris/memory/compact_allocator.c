/* lib/src/memory/compact_allocator.c - Compact Mode Allocator Implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "compact_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Default block size for compact mode (1 MB = fast for most documents) */
#define DEFAULT_BLOCK_SIZE (1024 * 1024)

/* Alignment for memory allocations */
#define ALIGNMENT 8

/* ============================================================================
 * Compact Allocator Implementation
 * ============================================================================ */

CompactAllocator* compact_allocator_create(size_t initial_block_size) {
    if (initial_block_size == 0) {
        initial_block_size = DEFAULT_BLOCK_SIZE;
    }

    CompactAllocator* alloc = (CompactAllocator*)malloc(sizeof(CompactAllocator));
    if (!alloc) return NULL;

    /* Create first block */
    alloc->first_block = (CompactBlock*)malloc(sizeof(CompactBlock) + initial_block_size);
    if (!alloc->first_block) {
        free(alloc);
        return NULL;
    }

    alloc->first_block->data = (char*)(alloc->first_block + 1);
    alloc->first_block->size = initial_block_size;
    alloc->first_block->used = 0;
    alloc->first_block->next = NULL;

    alloc->current_block = alloc->first_block;
    alloc->block_size = initial_block_size;
    alloc->total_allocated = initial_block_size + sizeof(CompactBlock);

    return alloc;
}

void compact_allocator_destroy(CompactAllocator* alloc) {
    if (!alloc) return;

    /* Free all blocks */
    CompactBlock* block = alloc->first_block;
    while (block) {
        CompactBlock* next = block->next;
        free(block);
        block = next;
    }

    free(alloc);
}

void* compact_alloc(CompactAllocator* alloc, size_t size) {
    if (!alloc || size == 0) return NULL;

    /* Align size to ALIGNMENT */
    size_t aligned_size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    /* Check if current block has space */
    CompactBlock* block = alloc->current_block;
    if (block && (block->used + aligned_size <= block->size)) {
        void* ptr = block->data + block->used;
        block->used += aligned_size;
        return ptr;
    }

    /* Current block full, allocate new block */
    size_t new_block_size = alloc->block_size;
    if (aligned_size > new_block_size) {
        new_block_size = ((aligned_size / DEFAULT_BLOCK_SIZE) + 1) * DEFAULT_BLOCK_SIZE;
    }

    CompactBlock* new_block = (CompactBlock*)malloc(sizeof(CompactBlock) + new_block_size);
    if (!new_block) return NULL;

    new_block->data = (char*)(new_block + 1);
    new_block->size = new_block_size;
    new_block->used = aligned_size;
    new_block->next = NULL;

    /* Add to chain */
    if (block) {
        block->next = new_block;
    }

    alloc->current_block = new_block;
    alloc->total_allocated += new_block_size + sizeof(CompactBlock);

    return new_block->data;
}

void* compact_alloc_aligned(CompactAllocator* alloc, size_t size, size_t alignment) {
    /* For now, just use regular alloc (already aligned to ALIGNMENT) */
    (void)alignment;
    return compact_alloc(alloc, size);
}

void compact_allocator_reset(CompactAllocator* alloc) {
    if (!alloc) return;

    /* Reset all blocks but don't free them */
    CompactBlock* block = alloc->first_block;
    while (block) {
        block->used = 0;
        block = block->next;
    }

    alloc->current_block = alloc->first_block;
}

size_t compact_allocator_usage(CompactAllocator* alloc) {
    if (!alloc) return 0;
    return alloc->total_allocated;
}

size_t compact_allocator_total(CompactAllocator* alloc) {
    if (!alloc) return 0;

    size_t total = 0;
    CompactBlock* block = alloc->first_block;
    while (block) {
        total += block->used;
        block = block->next;
    }
    return total;
}

/* ============================================================================
 * String Storage in Compact Mode
 * ============================================================================ */

/* Store string in compact block and return offset */
uint32_t compact_store_string(CompactAllocator* alloc, const char* str, size_t len) {
    if (!alloc || !str || len == 0) return 0;

    /* Allocate space for string + null terminator */
    char* dest = (char*)compact_alloc(alloc, len + 1);
    if (!dest) return 0;

    /* Copy string and null terminate */
    memcpy(dest, str, len);
    dest[len] = '\0';

    /* Return cumulative offset from first block (not current block) */
    uint32_t offset = 0;
    CompactBlock* block = alloc->first_block;
    while (block) {
        if (block == alloc->current_block) {
            offset += (uint32_t)(dest - block->data);
            break;
        }
        offset += (uint32_t)block->size;
        block = block->next;
    }

    return offset;
}

/* Get string pointer from offset (for internal use) */
static inline const char* compact_get_string(CompactAllocator* alloc, uint32_t offset) {
    CompactBlock* block = alloc->current_block;
    if (!block || offset >= block->size) return NULL;
    return block->data + offset;
}

/* ============================================================================
 * Compact Mode Utilities
 * ============================================================================ */

/* Store string_view in compact block */
uint32_t compact_store_string_view(CompactAllocator* alloc, const char* str, size_t len) {
    return compact_store_string(alloc, str, len);
}

/* Duplicate string from compact block */
char* compact_strdup_compact(CompactAllocator* alloc, uint32_t offset) {
    const char* str = compact_get_string(alloc, offset);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* dup = (char*)compact_alloc(alloc, len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}
