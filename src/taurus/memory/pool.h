/* lib/src/memory/pool.h - Page-based Memory Pool Allocator
 * Copyright (c) 2024, Ribose Inc.
 *
 * High-performance memory pool for DOM nodes using bump-pointer allocation.
 * Based on pugixml's memory allocator design (32KB pages).
 *
 * Key Features:
 * - Fast O(1) bump-pointer allocation (no malloc per node)
 * - Page-based design (32KB pages, ~1000x fewer malloc calls)
 * - Batch deallocation (free entire pool at once)
 * - Zero overhead for small allocations
 * - String interning/deduplication for memory savings
 */

#ifndef TAURUS_MEMORY_POOL_H
#define TAURUS_MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>
#include "../common/types_internal.h"   /* Single source for TaurusMemoryPool */
#include "../common/string_view.h"     /* Full TaurusStringView definition */

/* Forward-declared hook types — full typedefs are in taurus/types.h.
 * Defining them here too would create a redefinition warning. */
typedef void* (*taurus_allocation_function)(size_t size);
typedef void  (*taurus_deallocation_function)(void* ptr);

/* ============================================================================
 * String Interning Structures (for deduplication)
 * ============================================================================ */

/**
 * Hash table entry for string interning
 *
 * Stores a mapping from StringView (key) to cached NULL-terminated string.
 * Multiple entries with the same hash are chained together.
 */
typedef struct string_hash_entry {
    char* key_data;               /* Pool-allocated copy of key (NOT const - we own it!) */
    size_t key_length;            /* StringView length */
    char* cached_string;          /* Pool-allocated NULL-terminated string */
    struct string_hash_entry* next;  /* Collision chain */
} StringHashEntry;

/**
 * Hash table for string interning
 *
 * Maps StringViews to cached NULL-terminated strings, avoiding duplicate
 * allocations for identical strings (e.g., repeated element names).
 */
typedef struct {
    StringHashEntry** buckets;    /* Array of bucket pointers */
    size_t bucket_count;          /* Number of buckets (power of 2) */
    size_t entry_count;           /* Total entries in table */
    size_t cache_hits;            /* Deduplication successes */
    size_t cache_misses;          /* New strings allocated */
} StringHashTable;

/* Internal memory page structure (forward declaration) */
typedef struct memory_page MemoryPage;

/**
 * Oversized-allocation tracking node
 *
 * When taurus_pool_alloc() is asked for more bytes than fit in a single
 * page, the request is satisfied via taurus_alloc_hook() directly.  The
 * result is recorded on a side list (first_big_alloc) so that
 * taurus_pool_destroy() can free it.  Without this list, oversized
 * allocations (e.g., a single 10 KB attribute value) would leak on every
 * parse — see TODO 06.
 */
typedef struct taurus_big_alloc {
    struct taurus_big_alloc* next;  /* Singly-linked list */
    void* ptr;                      /* The oversized allocation */
    size_t size;                    /* Byte count (for stats) */
} TaurusBigAlloc;

/**
 * Memory pool structure
 *
 * Manages pages and allocation. Now public so that string_view.c can access
 * the string_cache field for deduplication.
 */
struct taurus_memory_pool {
    MemoryPage* first_page;       /* Head of page list */
    MemoryPage* current_page;     /* Current page for allocation */
    size_t page_count;            /* Number of pages (for statistics) */
    StringHashTable* string_cache; /* Hash table for string interning */
    int strict_mode;              /* Strict entity validation mode */
    size_t page_size;             /* Page size for this pool */
    void* page_base;              /* Base pointer for compact pointer decoding */

    /* Per-pool allocator hooks (TODO 74) — if non-NULL, override the
     * thread-default globals.  When set, every page and oversized
     * allocation goes through these instead. */
    taurus_allocation_function  alloc_hook;
    taurus_deallocation_function dealloc_hook;

    /* Oversized allocations — freed in taurus_pool_destroy alongside pages. */
    TaurusBigAlloc* first_big_alloc;            /* Head of side list */
    TaurusBigAlloc** last_big_alloc_link;       /* O(1) append target */
};

/* TaurusMemoryPool typedef comes from common/types_internal.h
 * (included above).  No local redefinition — see TODO 12. */

/* ============================================================================
 * Pool Lifecycle
 * ============================================================================ */

/**
 * Create a new memory pool with default page size
 *
 * Returns: New pool, or NULL on allocation failure
 */
TaurusMemoryPool* taurus_pool_create(void);

/**
 * Create a new memory pool with specified page size
 *
 * @param page_size Size of each memory page (must be >= 4096)
 * Returns: New pool, or NULL on allocation failure
 */
TaurusMemoryPool* taurus_pool_create_with_page_size(size_t page_size);

/**
 * Destroy pool and free all allocated memory
 *
 * Frees all pages in the pool. All pointers allocated from this pool
 * become invalid after this call.
 *
 * @param pool Pool to destroy (NULL is safe)
 */
void taurus_pool_destroy(TaurusMemoryPool* pool);

/**
 * Get the base pointer for compact pointer decoding
 *
 * Returns the pointer to the first page's data area, which should be used
 * as the base for encoding/decoding compact pointers.
 *
 * @param pool Pool to get base pointer from
 * @return Pointer to first page's data area, or NULL if pool is NULL
 */
void* taurus_pool_get_base(TaurusMemoryPool* pool);

/* ============================================================================
 * Allocation
 * ============================================================================ */

/**
 * Allocate memory from pool
 *
 * Fast O(1) bump-pointer allocation. Memory is 8-byte aligned.
 * Memory is NOT initialized (like malloc, not calloc).
 *
 * @param pool  Pool to allocate from (must not be NULL)
 * @param size  Bytes to allocate (must be > 0)
 *
 * Returns: Pointer to allocated memory, or NULL on failure
 *
 * Performance:
 * - Average case: O(1) pointer bump (no malloc)
 * - Worst case: O(1) new page allocation every 32KB
 */
void* taurus_pool_alloc(TaurusMemoryPool* pool, size_t size);

/**
 * Allocate zeroed memory from pool
 *
 * Like taurus_pool_alloc() but initializes memory to zero.
 *
 * @param pool  Pool to allocate from (must not be NULL)
 * @param size  Bytes to allocate (must be > 0)
 *
 * Returns: Pointer to zeroed memory, or NULL on failure
 */
void* taurus_pool_calloc(TaurusMemoryPool* pool, size_t size);

/**
 * Allocate multiple items contiguously from pool
 *
 * Allocates an array of items in a single contiguous block. This improves
 * cache locality for related structures (e.g., all elements in a subtree).
 *
 * @param pool      Pool to allocate from (must not be NULL)
 * @param item_size Size of each item in bytes
 * @param count     Number of items to allocate
 *
 * Returns: Pointer to first item, or NULL on failure
 *
 * Performance: 1 allocation instead of 'count' allocations, better cache locality
 */
void* taurus_pool_alloc_batch(TaurusMemoryPool* pool, size_t item_size, size_t count);

/* ============================================================================
 * String Functions (for zero-copy parsing)
 * ============================================================================ */

/**
 * Store in-place string pointer (zero-copy)
 *
 * Returns the string pointer directly without copying. Used for in-place
 * parsing where strings are already NULL-terminated in the XML buffer.
 * The pool tracks the buffer for cleanup.
 *
 * @param pool  Pool that owns the buffer containing the string
 * @param str   Pointer to NULL-terminated string in buffer
 *
 * Returns: str (same pointer, no allocation)
 */
char* taurus_pool_strdup_inplace(TaurusMemoryPool* pool, char* str);

/**
 * Duplicate string into pool memory
 *
 * Copies string into pool-allocated memory. Used for non-writable parsing
 * where we need to preserve the original buffer.
 *
 * @param pool  Pool to allocate from
 * @param str   Source string to copy (must not be NULL)
 *
 * Returns: Pointer to pool-allocated copy, or NULL on failure
 */
char* taurus_pool_strdup(TaurusMemoryPool* pool, const char* str);

/* ============================================================================
 * Statistics (for debugging/profiling)
 * ============================================================================ */

/**
 * Get total bytes allocated across all pages
 *
 * @param pool Pool to query
 * @return Total bytes allocated (includes page overhead)
 */
size_t taurus_pool_total_size(TaurusMemoryPool* pool);

/**
 * Get bytes currently in use (across pages + oversized allocs)
 *
 * Distinct from taurus_pool_total_size() which reports capacity; this
 * reports actual usage.  Useful for waste-ratio reporting.
 *
 * @param pool Memory pool
 * @return Bytes in use, or 0 if pool is NULL
 */
size_t taurus_pool_used_size(TaurusMemoryPool* pool);

/**
 * Get number of pages in pool
 *
 * @param pool Pool to query
 * @return Number of allocated pages
 */
size_t taurus_pool_page_count(TaurusMemoryPool* pool);

/* ============================================================================
 * String Interning (for zero-copy parsing with deduplication)
 * ============================================================================ */

/**
 * Create hash table for string interning
 *
 * Creates a hash table with the specified number of buckets. All memory
 * for the table and its entries is allocated from the pool.
 *
 * @param pool         Pool to allocate from
 * @param bucket_count Number of buckets (must be power of 2, e.g., 128)
 *
 * Returns: New hash table, or NULL on allocation failure
 *
 * Note: Table is automatically freed when pool is destroyed
 */
StringHashTable* taurus_hash_table_create(TaurusMemoryPool* pool, size_t bucket_count);

/**
 * Intern a string (lookup or allocate)
 *
 * Looks up the StringView in the hash table. If found, returns the cached
 * string. If not found, allocates a new NULL-terminated string from the pool,
 * inserts it into the hash table, and returns it.
 *
 * @param pool Pool with hash table
 * @param sv   StringView to intern
 *
 * Returns: Cached or newly allocated NULL-terminated string
 *
 * Performance:
 * - Cache hit: O(1) hash lookup + O(1) string comparison
 * - Cache miss: O(1) pool allocation + O(1) hash insert
 */
char* taurus_pool_intern_string(TaurusMemoryPool* pool,
                                 const struct taurus_string_view* sv);

/**
 * Destroy hash table
 *
 * Called automatically by taurus_pool_destroy(). Do not call directly.
 *
 * @param table Hash table to destroy
 */
void taurus_hash_table_destroy(StringHashTable* table);

/* ============================================================================
 * Generic Hash Table Functions (for attribute lookup)
 * ============================================================================ */

/**
 * Get value from hash table by string key
 *
 * Generic hash table lookup for storing arbitrary pointers (e.g., attributes).
 * Reuses StringHashTable structure but stores void* instead of strings.
 *
 * @param table Hash table to search
 * @param key   C string key
 * @param len   Length of key
 * @return Stored value (void*), or NULL if not found
 */
void* taurus_hash_table_get(StringHashTable* table, const char* key, size_t len);

/**
 * Set value in hash table by string key
 *
 * @param table Hash table
 * @param key   C string key (will be copied)
 * @param len   Length of key
 * @param value Value to store (void*)
 * @param pool  Memory pool for allocating entries
 * @return 1 on success, 0 on failure
 */
int taurus_hash_table_set(StringHashTable* table, const char* key, size_t len, void* value, TaurusMemoryPool* pool);

/**
 * Remove entry from hash table by string key
 *
 * NOTE: For pool-allocated entries, this only removes from chain but doesn't free memory.
 * Memory will be freed when pool is destroyed.
 *
 * @param table Hash table
 * @param key   C string key
 * @param len   Length of key
 * @return 1 if found and removed, 0 if not found
 */
int taurus_hash_table_remove(StringHashTable* table, const char* key, size_t len);

/**
 * Iterator callback signature for taurus_hash_table_for_each.
 *
 * Returns 1 to continue iteration, 0 to stop early.
 */
typedef int (*TaurusHashTableIterator)(const char* key, size_t key_len,
                                        void* value, void* user_data);

/**
 * Walk every entry in the hash table, invoking `iter` on each.
 * If `iter` returns 0 for any entry, iteration stops.
 *
 * Used by the DTD validator (Phase 3 of TODO 91) to scan all
 * attribute declarations without knowing keys in advance.
 */
void taurus_hash_table_for_each(StringHashTable* table,
                                TaurusHashTableIterator iter,
                                void* user_data);

#endif /* TAURUS_MEMORY_POOL_H */