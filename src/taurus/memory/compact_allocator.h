/* lib/src/memory/compact_allocator.h - Compact Mode Allocator
 * Copyright (c) 2024, Ribose Inc.
 *
 * COMPACT MODE: High-performance parsing mode that stores DOM nodes
 * and strings in contiguous memory blocks for maximum speed.
 *
 * This is a MINIMAL implementation focused purely on parsing performance.
 * Documents can be converted to full mode when modification is needed.
 */

#ifndef TAURUS_COMPACT_ALLOCATOR_H
#define TAURUS_COMPACT_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

/* No TaurusStringView forward decl: nothing in this header references
 * it.  Callers that need it include common/string_view.h directly. */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Compact Memory Block
 * ============================================================================ */

typedef struct compact_block {
    char* data;              /* Start of block */
    size_t size;            /* Total block size */
    size_t used;            /* Bytes used (bump allocator) */
    struct compact_block* next; /* Next block for chaining */
} CompactBlock;

/* ============================================================================
 * Compact Allocator
 * ============================================================================ */

typedef struct {
    CompactBlock* current_block;   /* Current block for allocations */
    CompactBlock* first_block;      /* First block (for freeing all) */
    size_t block_size;             /* Size of new blocks (default: 1MB) */
    size_t total_allocated;        /* Total bytes allocated */
} CompactAllocator;

/* Create compact allocator */
CompactAllocator* compact_allocator_create(size_t initial_block_size);

/* Destroy compact allocator and free all blocks */
void compact_allocator_destroy(CompactAllocator* alloc);

/* Allocate memory from current block (returns NULL if full, requiring new block) */
void* compact_alloc(CompactAllocator* alloc, size_t size);

/* Allocate with alignment */
void* compact_alloc_aligned(CompactAllocator* alloc, size_t size, size_t alignment);

/* Reset allocator (keeps blocks, resets used pointer) */
void compact_allocator_reset(CompactAllocator* alloc);

/* Get total memory usage */
size_t compact_allocator_usage(CompactAllocator* alloc);

/* Get total allocated memory */
size_t compact_allocator_total(CompactAllocator* alloc);

/* ============================================================================
 * String Storage in Compact Mode
 * ============================================================================ */

/* Store string in compact block and return offset */
uint32_t compact_store_string(CompactAllocator* alloc, const char* str, size_t len);

/* Store string_view in compact block and return offset */
uint32_t compact_store_string_view(CompactAllocator* alloc, const char* str, size_t len);

/* ============================================================================
 * Compact DOM Structures
 * ============================================================================ */

/* Compact element - Minimal structure for fast parsing
 * All strings are stored as offsets into the memory block
 * instead of pointers, reducing memory and improving cache locality.
 */
typedef struct compact_element {
    uint32_t name_offset;      /* Offset to element name string in block */
    uint32_t first_child;      /* Offset to first child (0 if none) */
    uint32_t next_sibling;      /* Offset to next sibling (0 if none) */
    uint32_t attributes_offset;/* Offset to attributes array (0 if none) */
    uint16_t attribute_count;   /* Number of attributes */
    uint16_t flags;            /* Flags (type, etc.) */
    uint32_t parent_offset;     /* Offset to parent element (0 if root) */
} CompactElement;

/* Compact attribute - Minimal attribute storage */
typedef struct compact_attribute {
    uint32_t name_offset;      /* Offset to attribute name */
    uint32_t value_offset;     /* Offset to attribute value */
} CompactAttribute;

/* Compact text node */
typedef struct compact_text {
    uint32_t content_offset;    /* Offset to text content */
    uint32_t next_sibling;      /* Offset to next sibling (0 if none) */
    uint32_t parent_offset;     /* Offset to parent element */
} CompactText;

/* Compact node types */
#define COMPACT_NODE_ELEMENT  1
#define COMPACT_NODE_TEXT    2
#define COMPACT_NODE_COMMENT 3
#define COMPACT_NODE_PI      4

/* ============================================================================
 * API for Compact Parsing
 * ============================================================================ */

/* Parse document in compact mode - returns regular TaurusDocument
 * (converts from compact to regular format internally) */
struct taurus_document;
struct taurus_document* taurus_parse_string_compact(const char* xml, size_t length, int* error_out);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_COMPACT_ALLOCATOR_H */
