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

#ifndef LEPTRIS_MEMORY_POOL_H
#define LEPTRIS_MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>
#include "../../include/leptris/types.h"  /* Allocator hook typedefs + opaque types */
#include "../common/types_internal.h"     /* Single source for LeptrisMemoryPool */
#include "../common/string_view.h"       /* Full LeptrisStringView definition */
#include "arena.h"                       /* Contiguous arena mode (TODO 183) */

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
 * When leptris_pool_alloc() is asked for more bytes than fit in a single
 * page, the request is satisfied via leptris_alloc_hook() directly.  The
 * result is recorded on a side list (first_big_alloc) so that
 * leptris_pool_destroy() can free it.  Without this list, oversized
 * allocations (e.g., a single 10 KB attribute value) would leak on every
 * parse — see TODO 06.
 */
typedef struct leptris_big_alloc {
    struct leptris_big_alloc* next;  /* Singly-linked list */
    void* ptr;                      /* The oversized allocation */
    size_t size;                    /* Byte count (for stats) */
} LeptrisBigAlloc;

/**
 * Memory pool structure
 *
 * Manages pages and allocation. Now public so that string_view.c can access
 * the string_cache field for deduplication.
 */
struct leptris_memory_pool {
    MemoryPage* first_page;       /* Head of page list */
    MemoryPage* current_page;     /* Current page for allocation */
    size_t page_count;            /* Number of pages (for statistics) */
    StringHashTable* string_cache; /* Hash table for string interning */
    int strict_mode;              /* Strict entity validation mode */
    size_t page_size;             /* Page size for this pool */
    void* page_base;              /* Base pointer for compact pointer decoding */

    /* When set, the first page is allocated INLINE with the pool
     * struct (single malloc for both). leptris_pool_destroy must
     * skip freeing the first page in this case — the pool-struct
     * free at the end of destroy reclaims both. Saves one malloc
     * per parse (TODO 154). */
    int first_page_inline;

    /* Per-pool allocator hooks (TODO 74) — if non-NULL, override the
     * thread-default globals.  When set, every page and oversized
     * allocation goes through these instead. */
    leptris_allocation_function  alloc_hook;
    leptris_deallocation_function dealloc_hook;

    /* Oversized allocations — freed in leptris_pool_destroy alongside pages. */
    LeptrisBigAlloc* first_big_alloc;            /* Head of side list */
    LeptrisBigAlloc** last_big_alloc_link;       /* O(1) append target */

    /* Arena mode (TODO 183 Phase 2). When non-NULL, every allocation
     * routes to this single contiguous arena instead of pages: pool
     * allocs are guaranteed within [arena->base, +size), and overflow
     * is a hard NULL (never a scattered malloc) — the property tree
     * compact-pointer edges need. Page fields are unused in this mode.
     * arena_owned: destroy the arena with the pool (parser-owned
     * arenas set 0 when the caller manages lifetime itself). */
    LeptrisArena* arena;
    int arena_owned;
};

/* LeptrisMemoryPool typedef comes from common/types_internal.h
 * (included above).  No local redefinition — see TODO 12. */

/* ============================================================================
 * Pool Lifecycle
 * ============================================================================ */

/**
 * Create a new memory pool with default page size
 *
 * Returns: New pool, or NULL on allocation failure
 */
LeptrisMemoryPool* leptris_pool_create(void);

/**
 * Create a new memory pool with specified page size
 *
 * @param page_size Size of each memory page (must be >= 4096)
 * Returns: New pool, or NULL on allocation failure
 */
LeptrisMemoryPool* leptris_pool_create_with_page_size(size_t page_size);

/* Arena-backed mode (TODO 183 Phase 2).
 *
 * All allocations route to `arena` — a single contiguous malloc —
 * so every pointer the pool hands out lies within
 * [base, base + size) and exhaustion is a hard NULL (no fallback
 * malloc, no scattered pages). `owns_arena`: when 1, pool destroy
 * frees the arena; when 0, the caller owns the arena's lifetime
 * (used when the parser pre-sizes one arena and hands it to the
 * pool). The pool API is unchanged for callers. */
LeptrisMemoryPool* leptris_pool_create_arena_backed(LeptrisArena* arena,
                                                   int owns_arena);

/* Nonzero when the pool routes to an arena (diagnostics + parser
 * capacity planning). */
int leptris_pool_is_arena_backed(const LeptrisMemoryPool* pool);

/**
 * Destroy pool and free all allocated memory
 *
 * Frees all pages in the pool. All pointers allocated from this pool
 * become invalid after this call.
 *
 * @param pool Pool to destroy (NULL is safe)
 */
void leptris_pool_destroy(LeptrisMemoryPool* pool);

/**
 * Get the base pointer for compact pointer decoding
 *
 * Returns the pointer to the first page's data area, which should be used
 * as the base for encoding/decoding compact pointers.
 *
 * @param pool Pool to get base pointer from
 * @return Pointer to first page's data area, or NULL if pool is NULL
 */
void* leptris_pool_get_base(LeptrisMemoryPool* pool);

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
void* leptris_pool_alloc(LeptrisMemoryPool* pool, size_t size);

/**
 * Allocate zeroed memory from pool
 *
 * Like leptris_pool_alloc() but initializes memory to zero.
 *
 * @param pool  Pool to allocate from (must not be NULL)
 * @param size  Bytes to allocate (must be > 0)
 *
 * Returns: Pointer to zeroed memory, or NULL on failure
 */
void* leptris_pool_calloc(LeptrisMemoryPool* pool, size_t size);

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
void* leptris_pool_alloc_batch(LeptrisMemoryPool* pool, size_t item_size, size_t count);

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
char* leptris_pool_strdup_inplace(LeptrisMemoryPool* pool, char* str);

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
char* leptris_pool_strdup(LeptrisMemoryPool* pool, const char* str);

/* Allocate a node struct plus an associated content buffer.
 *
 * When the combined size fits in a pool page, both pieces land in
 * the same page (contiguous, cache-friendly) — the fast path used
 * for typical small nodes.
 *
 * When the combined size would exceed the pool's page size, the
 * struct stays in a normal pool page (so it remains within ±2GB of
 * other pool-resident nodes — required for int32_t compact-pointer
 * offsets, see TODO 90 Phase 2b) and the content goes into a
 * separate oversized allocation referenced via raw pointer.
 *
 * Without this split, a 5MB text node forces an oversized
 * allocation for struct+content; on systems where malloc places
 * oversized requests far from small ones (e.g. macOS), the parent
 * element's int32_t first_child_off silently overflows to 0 and
 * the child is dropped from the tree without any error.
 *
 * @param pool         Pool to allocate from
 * @param struct_size  Size of the node struct (e.g. sizeof(LeptrisTextNode))
 * @param content_size Size of the content buffer (excluding NUL)
 * @param content_out  Out-param: pointer to the NUL-terminated content
 *                     buffer. Caller must memcpy content in and set NUL.
 *
 * Returns: pointer to the struct (NULL on failure). */
void* leptris_pool_alloc_node_with_content(LeptrisMemoryPool* pool,
                                           size_t struct_size,
                                           size_t content_size,
                                           char** content_out);

/* ============================================================================
 * Statistics (for debugging/profiling)
 * ============================================================================ */

/**
 * Get total bytes allocated across all pages
 *
 * @param pool Pool to query
 * @return Total bytes allocated (includes page overhead)
 */
size_t leptris_pool_total_size(LeptrisMemoryPool* pool);

/**
 * Get bytes currently in use (across pages + oversized allocs)
 *
 * Distinct from leptris_pool_total_size() which reports capacity; this
 * reports actual usage.  Useful for waste-ratio reporting.
 *
 * @param pool Memory pool
 * @return Bytes in use, or 0 if pool is NULL
 */
size_t leptris_pool_used_size(LeptrisMemoryPool* pool);

/**
 * Get number of pages in pool
 *
 * @param pool Pool to query
 * @return Number of allocated pages
 */
size_t leptris_pool_page_count(LeptrisMemoryPool* pool);

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
StringHashTable* leptris_hash_table_create(LeptrisMemoryPool* pool, size_t bucket_count);

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
char* leptris_pool_intern_string(LeptrisMemoryPool* pool,
                                 const struct leptris_string_view* sv);

/**
 * Destroy hash table
 *
 * Called automatically by leptris_pool_destroy(). Do not call directly.
 *
 * @param table Hash table to destroy
 */
void leptris_hash_table_destroy(StringHashTable* table);

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
void* leptris_hash_table_get(StringHashTable* table, const char* key, size_t len);

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
int leptris_hash_table_set(StringHashTable* table, const char* key, size_t len, void* value, LeptrisMemoryPool* pool);

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
int leptris_hash_table_remove(StringHashTable* table, const char* key, size_t len);

/**
 * Iterator callback signature for leptris_hash_table_for_each.
 *
 * Returns 1 to continue iteration, 0 to stop early.
 */
typedef int (*LeptrisHashTableIterator)(const char* key, size_t key_len,
                                        void* value, void* user_data);

/**
 * Walk every entry in the hash table, invoking `iter` on each.
 * If `iter` returns 0 for any entry, iteration stops.
 *
 * Used by the DTD validator (Phase 3 of TODO 91) to scan all
 * attribute declarations without knowing keys in advance.
 */
void leptris_hash_table_for_each(StringHashTable* table,
                                LeptrisHashTableIterator iter,
                                void* user_data);

#endif /* LEPTRIS_MEMORY_POOL_H */