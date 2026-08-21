/* lib/src/memory/pool.c - Page-based Memory Pool Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Based on pugixml's memory allocator design:
 * - 32KB pages (reduces malloc calls by ~1000x)
 * - Bump-pointer allocation (O(1), no malloc per allocation)
 * - Batch deallocation (free all pages at once)
 */

#include "../common/string_view.h"  /* Full LeptrisStringView definition */
#include "pool.h"                    /* Pool API */
#include "../leptris_internal.h"      /* Custom allocation hooks */
#include "../dom/compact.h"          /* LEPTRIS_COMPACT_ALIGNMENT for assertions */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Default page size (same as pugixml) - 32KB provides good balance:
 * - Large enough to amortize malloc overhead
 * - Small enough to avoid wasting memory
 * - Typical XML elements use ~100 bytes, so ~320 elements per page
 */
#define LEPTRIS_POOL_PAGE_SIZE_DEFAULT 32768

/* Minimum page size for small files */
#define LEPTRIS_POOL_PAGE_SIZE_MIN 4096

/* Alignment for all allocations (same as pugixml) */
#define LEPTRIS_POOL_ALIGNMENT 8

/* Align size up to nearest multiple of LEPTRIS_POOL_ALIGNMENT */
#define ALIGN_SIZE(size) (((size) + LEPTRIS_POOL_ALIGNMENT - 1) & ~(LEPTRIS_POOL_ALIGNMENT - 1))

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* Memory page - holds actual memory for allocations (dynamic size).
 * The typedef `MemoryPage` is forward-declared in pool.h; this is the
 * struct body.  No redefinition here (TODO 12). */
struct memory_page {
    struct memory_page* next;    /* Linked list of pages */
    size_t page_size;            /* Total size of this page */
    size_t busy_size;            /* Bytes used in this page */
    char data[1];                /* Flexible array member - actual memory */
};

/* Note: struct leptris_memory_pool is now defined in pool.h */

/* ============================================================================
 * Pool Lifecycle
 * ============================================================================ */

/* Helper to allocate a new page */
static MemoryPage* allocate_new_page(size_t page_size) {
    /* Allocate page structure + data in one allocation */
    size_t total_size = sizeof(MemoryPage) - 1 + page_size;
    MemoryPage* page = (MemoryPage*)leptris_alloc_hook(total_size);
    if (!page) return NULL;

    page->next = NULL;
    page->page_size = page_size;
    page->busy_size = 0;

    return page;
}

LeptrisMemoryPool* leptris_pool_create(void) {
    return leptris_pool_create_with_page_size(LEPTRIS_POOL_PAGE_SIZE_DEFAULT);
}

/* Arena-backed mode (TODO 183 Phase 2). The pool struct is a plain
 * hook-allocated block — no pages, no inline first page. */
LeptrisMemoryPool* leptris_pool_create_arena_backed(LeptrisArena* arena,
                                                   int owns_arena) {
    if (!arena) return NULL;
    LeptrisMemoryPool* pool =
        (LeptrisMemoryPool*)leptris_alloc_hook(sizeof(LeptrisMemoryPool));
    if (!pool) return NULL;
    pool->first_page = NULL;
    pool->current_page = NULL;
    pool->page_count = 0;
    pool->string_cache = NULL;
    pool->strict_mode = 0;
    pool->page_size = 0;
    pool->page_base = arena->base;
    pool->first_page_inline = 0;
    pool->alloc_hook = NULL;
    pool->dealloc_hook = NULL;
    pool->first_big_alloc = NULL;
    pool->last_big_alloc_link = &pool->first_big_alloc;
    pool->arena = arena;
    pool->arena_owned = owns_arena;
    return pool;
}

int leptris_pool_is_arena_backed(const LeptrisMemoryPool* pool) {
    return pool && pool->arena != NULL;
}

LeptrisMemoryPool* leptris_pool_create_with_hooks(
    size_t page_size,
    leptris_allocation_function alloc,
    leptris_deallocation_function dealloc) {
    LeptrisMemoryPool* pool = leptris_pool_create_with_page_size(page_size);
    if (pool) {
        pool->alloc_hook   = alloc;
        pool->dealloc_hook = dealloc;
    }
    return pool;
}

LeptrisMemoryPool* leptris_pool_create_with_page_size(size_t page_size) {
    /* Validate page size */
    if (page_size < LEPTRIS_POOL_PAGE_SIZE_MIN) {
        page_size = LEPTRIS_POOL_PAGE_SIZE_MIN;
    }

    /* Round up to alignment */
    page_size = (page_size + LEPTRIS_POOL_ALIGNMENT - 1) & ~(LEPTRIS_POOL_ALIGNMENT - 1);

    /* Allocate pool struct + first page header + first page data in
     * ONE malloc. Cuts per-parse malloc count from 2 to 1 (TODO 154).
     *
     * Layout:
     *   [ LeptrisMemoryPool | MemoryPage header | page data ... ]
     *
     * The pool's `first_page_inline` flag tells leptris_pool_destroy
     * to free the pool struct (which owns everything) and skip the
     * first-page free (which would otherwise double-free). */
    size_t header_size = sizeof(LeptrisMemoryPool) + sizeof(struct memory_page) - 1;
    /* Keep pool struct and page header aligned so the page data
     * starts on an 8-byte boundary. */
    header_size = (header_size + LEPTRIS_POOL_ALIGNMENT - 1) &
                  ~(LEPTRIS_POOL_ALIGNMENT - 1);
    size_t total_size = header_size + page_size;
    char* block = (char*)leptris_alloc_hook(total_size);
    if (!block) return NULL;

    LeptrisMemoryPool* pool = (LeptrisMemoryPool*)block;
    struct memory_page* page = (struct memory_page*)(block + sizeof(LeptrisMemoryPool));
    page->next = NULL;
    page->page_size = page_size;
    page->busy_size = 0;

    pool->first_page = page;
    pool->current_page = page;
    pool->page_count = 1;
    pool->string_cache = NULL;
    pool->strict_mode = 0;
    pool->page_size = page_size;
    pool->page_base = page->data;  /* Initialize page_base for compact pointer decoding */
    pool->first_page_inline = 1;
    pool->arena = NULL;
    pool->arena_owned = 0;

    /* No oversized allocations yet — list starts empty. */
    pool->first_big_alloc = NULL;
    pool->last_big_alloc_link = &pool->first_big_alloc;

    /* Pre-allocate additional pages for better cache locality
     * PERFORMANCE: Only pre-allocate for larger page sizes (>= 8KB)
     * to avoid wasting memory on small documents. For 4KB pages used with
     * small files, the single page is typically sufficient. */
    if (page_size >= 8192) {
        MemoryPage* next_page = allocate_new_page(page_size);
        if (next_page) {
            page->next = next_page;
            pool->page_count++;
            /* current_page must always point at the LAST page in the
             * chain so that add_new_page()'s `current_page->next = page`
             * appends rather than overwrites an existing link (which
             * would orphan the pre-allocated page). */
            pool->current_page = next_page;
        }
    }

    return pool;
}

void leptris_pool_destroy(LeptrisMemoryPool* pool) {
    if (!pool) return;

    /* Arena mode: everything handed out during PARSE lives inside the
     * single arena allocation. Post-parse mutation overflow went to
     * tracked extension blocks — free those, then the arena (when
     * owned) and the pool struct. */
    if (pool->arena) {
        LeptrisBigAlloc* big = pool->first_big_alloc;
        while (big) {
            LeptrisBigAlloc* next = big->next;
            leptris_free_hook(big->ptr);
            leptris_free_hook(big);
            big = next;
        }
        if (pool->arena_owned) {
            leptris_arena_destroy(pool->arena);
        }
        leptris_free_hook(pool);
        return;
    }

    /* Oversized allocations come from leptris_alloc_hook directly and
     * are tracked on the side list.  Free the user data, then the
     * tracking node (which itself is pool-allocated and would be freed
     * with the pages below — but freeing it explicitly here is harmless
     * because double-free is avoided via the singly-linked walk). */
    LeptrisBigAlloc* big = pool->first_big_alloc;
    while (big) {
        LeptrisBigAlloc* next = big->next;
        leptris_free_hook(big->ptr);
        /* big is pool-allocated; freed with the page walk below. */
        big = next;
    }

    /* Free pages. The first page may be inline with the pool struct
     * (when first_page_inline is set) — in that case, skip freeing
     * it directly; the pool-struct free below reclaims both. */
    int first_inline = pool->first_page_inline;
    MemoryPage* page = pool->first_page;
    if (page) {
        /* Skip the first page if it's inline. */
        if (first_inline) {
            page = page->next;
        }
        while (page) {
            MemoryPage* next = page->next;
            leptris_free_hook(page);
            page = next;
        }
    }

    /* When the first page was inline, freeing the pool struct frees
     * both the pool and the first page (they share one malloc). */
    leptris_free_hook(pool);
}

void* leptris_pool_get_base(LeptrisMemoryPool* pool) {
    if (!pool) return NULL;
    return pool->page_base;
}

/* ============================================================================
 * Allocation - Fast Bump-Pointer Implementation
 * ============================================================================ */

/* Allocate a new page and add to pool
 *
 * Called when current page is full. This is the "slow path" but still O(1).
 */
static MemoryPage* add_new_page(LeptrisMemoryPool* pool, size_t page_size) {
    MemoryPage* page = allocate_new_page(page_size);
    if (!page) return NULL;

    /* Add to end of list */
    pool->current_page->next = page;
    pool->current_page = page;
    pool->page_count++;

    return page;
}

/* Allocate an oversized request that doesn't fit in a standard page.
 * The bytes come from leptris_alloc_hook directly; we record the result
 * on pool->first_big_alloc so leptris_pool_destroy can free it. */
static void* pool_alloc_oversized(LeptrisMemoryPool* pool, size_t size) {
    void* ptr = leptris_alloc_hook(size);
    if (!ptr) return NULL;

    /* Tracking node: small, fits in a page.  Allocated from the pool
     * so it is automatically accounted for in page_count and freed
     * with the page walk. */
    LeptrisBigAlloc* node = (LeptrisBigAlloc*)
        leptris_pool_alloc(pool, sizeof(LeptrisBigAlloc));
    if (!node) {
        leptris_free_hook(ptr);
        return NULL;
    }
    node->ptr  = ptr;
    node->size = size;
    node->next = NULL;
    *pool->last_big_alloc_link = node;
    pool->last_big_alloc_link  = &node->next;
    return ptr;
}

/* Arena-mode extension: a request that doesn't fit in the sized span
 * (post-parse mutation growth — element_create/append after the parse
 * sized the arena from the document) is satisfied by a tracked malloc
 * block, restoring the pool's never-fail contract. Parse-time
 * allocations stay inside the contiguous span; extensions only exist
 * for overflow. The tracking node itself comes from leptris_alloc_hook
 * directly (NOT a recursive pool_alloc) so a near-full arena can't
 * loop. Freed by leptris_pool_destroy via the big-alloc list. */
static void* pool_alloc_arena_extension(LeptrisMemoryPool* pool, size_t size) {
    void* ptr = leptris_alloc_hook(ALIGN_SIZE(size));
    if (!ptr) return NULL;
    LeptrisBigAlloc* node =
        (LeptrisBigAlloc*)leptris_alloc_hook(sizeof(LeptrisBigAlloc));
    if (!node) {
        leptris_free_hook(ptr);
        return NULL;
    }
    node->ptr  = ptr;
    node->size = size;
    node->next = NULL;
    *pool->last_big_alloc_link = node;
    pool->last_big_alloc_link  = &node->next;
    return ptr;
}

void* leptris_pool_alloc(LeptrisMemoryPool* pool, size_t size) {
    if (!pool || size == 0) return NULL;

    /* Arena mode: parse-time allocations live in the one contiguous
     * span. Overflow (post-parse mutation growth) extends via a
     * tracked block — the pool never hard-fails, matching the page
     * pool's contract; only the SPAN's contiguity is parse-scoped. */
    if (pool->arena) {
        void* p = leptris_arena_alloc(pool->arena, size);
        if (p) return p;
        return pool_alloc_arena_extension(pool, size);
    }

    /* Align size to 8 bytes for performance and alignment requirements */
    size = ALIGN_SIZE(size);

    /* Check if allocation fits in current page (FAST PATH) */
    MemoryPage* page = pool->current_page;
    if (page->busy_size + size <= page->page_size) {
        /* Fast bump-pointer allocation - just increment offset */
        void* ptr = page->data + page->busy_size;

        /* CRITICAL: Verify alignment for compact pointer encoding
         * Compact pointers assume 4-byte aligned bases. If this assertion fails,
         * it will cause bus errors and memory corruption in the DOM. */
        assert(((uintptr_t)ptr & (LEPTRIS_COMPACT_ALIGNMENT - 1)) == 0 &&
               "Pool allocation not 4-byte aligned - will cause compact pointer corruption!");

        page->busy_size += size;
        return ptr;
    }

    /* Current page full - allocate new page (SLOW PATH, but still O(1)) */
    size_t page_size = page->page_size;  /* Use same page size as current page */
    if (size > page_size) {
        /* Allocation too large for standard page: record on the
         * oversized list so the pool can free it later. */
        return pool_alloc_oversized(pool, size);
    }

    page = add_new_page(pool, page_size);
    if (!page) return NULL;

    /* Allocate from new page */
    void* ptr = page->data;
    page->busy_size = size;
    return ptr;
}

void* leptris_pool_calloc(LeptrisMemoryPool* pool, size_t size) {
    void* ptr = leptris_pool_alloc(pool, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* Allocate multiple items contiguously from pool
 * Returns pointer to first item, or NULL on failure
 */
void* leptris_pool_alloc_batch(LeptrisMemoryPool* pool, size_t item_size, size_t count) {
    if (!pool || item_size == 0 || count == 0) return NULL;

    /* Arena mode: one bump for the whole batch — contiguous by
     * construction. Overflow extends (mutation growth). */
    if (pool->arena) {
        void* p = leptris_arena_alloc(pool->arena, item_size * count);
        if (p) return p;
        return pool_alloc_arena_extension(pool, item_size * count);
    }

    /* Align item size */
    item_size = ALIGN_SIZE(item_size);

    /* Calculate total size needed */
    size_t total_size = item_size * count;

    /* Check if fits in current page */
    MemoryPage* page = pool->current_page;
    if (page && page->busy_size + total_size <= page->page_size) {
        /* Fast path: allocate from current page */
        void* ptr = page->data + page->busy_size;
        page->busy_size += total_size;

        /* Verify alignment */
        assert(((uintptr_t)ptr & (LEPTRIS_COMPACT_ALIGNMENT - 1)) == 0 &&
               "Pool batch allocation not 4-byte aligned!");

        return ptr;
    }

    /* Fallback: allocate using standard alloc for each item
     * Not ideal, but maintains correctness
     */
    if (total_size > page->page_size) {
        /* Too large for single page — track on oversized list. */
        return pool_alloc_oversized(pool, total_size);
    }

    /* Allocate from new page */
    page = add_new_page(pool, page->page_size);
    if (!page) return NULL;

    void* ptr = page->data;
    page->busy_size = total_size;
    return ptr;
}

/* Allocate a node struct + content buffer with the split-on-oversize
 * behavior documented in the header (TODO 90 Phase 2b silent-drop
 * bug fix). */
void* leptris_pool_alloc_node_with_content(LeptrisMemoryPool* pool,
                                           size_t struct_size,
                                           size_t content_size,
                                           char** content_out) {
    if (!pool || struct_size == 0 || !content_out) return NULL;

    /* Arena mode: struct + content contiguous in one bump — including
     * oversized content. This is strictly better than the page-mode
     * split (which parks big content in a separate oversized malloc);
     * here the content stays inside the span, so nothing an edge could
     * point at ever leaves it. Overflow extends via two pool allocs. */
    if (pool->arena) {
        void* node = leptris_arena_alloc_node_with_content(
            pool->arena, struct_size, content_size, content_out);
        if (node) return node;
        char* struct_ptr = (char*)leptris_pool_alloc(pool, struct_size);
        if (!struct_ptr) return NULL;
        char* content = (char*)leptris_pool_alloc(pool, content_size + 1);
        if (!content) return NULL;
        *content_out = content;
        return struct_ptr;
    }

    size_t aligned_struct = ALIGN_SIZE(struct_size);
    size_t total = aligned_struct + ALIGN_SIZE(content_size + 1);

    if (total <= pool->page_size) {
        /* Fast path: struct + content in a single pool bump. Both
         * end up in the same page, cache-friendly and within ±2GB
         * of every other pool-resident node. */
        char* memory = (char*)leptris_pool_alloc(pool, total);
        if (!memory) return NULL;
        *content_out = memory + aligned_struct;
        return memory;
    }

    /* Oversize path: keep the struct pool-resident (so int32_t
     * offsets from the parent element stay valid) and put the
     * content in its own oversized allocation. The content is
     * referenced via raw pointer; the distance between the content
     * buffer and the struct is unconstrained — only the struct's
     * address matters for compact-pointer offsets. */
    char* struct_ptr = (char*)leptris_pool_alloc(pool, aligned_struct);
    if (!struct_ptr) return NULL;

    char* content = (char*)leptris_pool_alloc(pool, content_size + 1);
    if (!content) {
        /* struct_ptr stays in the pool; reclaimed at pool destroy.
         * Caller sees NULL and aborts; no leak. */
        return NULL;
    }

    *content_out = content;
    return struct_ptr;
}

/* ============================================================================
 * String Functions - Zero-Copy Support
 * ============================================================================ */

char* leptris_pool_strdup_inplace(LeptrisMemoryPool* pool, char* str) {
    if (!pool || !str) return NULL;

    /* Zero-copy: just return the pointer directly
     * The string is already NULL-terminated in the XML buffer
     * Pool will track the buffer for cleanup
     */
    return str;
}

char* leptris_pool_strdup(LeptrisMemoryPool* pool, const char* str) {
    if (!pool || !str) return NULL;

    /* Calculate string length */
    size_t len = strlen(str);

    /* Allocate from pool */
    char* copy = (char*)leptris_pool_alloc(pool, len + 1);
    if (!copy) return NULL;

    /* Copy string including NULL terminator */
    memcpy(copy, str, len + 1);

    return copy;
}

/* ============================================================================
 * String Interning - Hash Table Implementation
 * ============================================================================ */

/* Forward declaration — defined below; both leptris_pool_intern_string
 * and leptris_hash_table_set call it.  See TODO 36. */
static int hash_table_maybe_grow(StringHashTable* table, LeptrisMemoryPool* pool);

/**
 * FNV-1a hash function for StringView
 *
 * Fast, simple, well-distributed.  See TODO 09 for why the previous
 * "pointer looks like ASCII text" heuristic was removed — the only
 * legitimate runtime check is NULL.
 */
static uint32_t hash_string_view(const LeptrisStringView* sv) {
    if (!sv || sv->length == 0 || sv->data == NULL) return 0;

    uint32_t hash = 2166136261u;  /* FNV offset basis */
    for (size_t i = 0; i < sv->length; i++) {
        hash ^= (uint8_t)sv->data[i];
        hash *= 16777619u;  /* FNV prime */
    }
    return hash;
}

/**
 * Create hash table for string interning
 */
StringHashTable* leptris_hash_table_create(LeptrisMemoryPool* pool, size_t bucket_count) {
    if (!pool || bucket_count == 0) return NULL;

    /* Allocate table structure from pool */
    StringHashTable* table = (StringHashTable*)leptris_pool_alloc(pool, sizeof(StringHashTable));
    if (!table) return NULL;

    /* Allocate buckets array from pool */
    table->buckets = (StringHashEntry**)leptris_pool_alloc(pool, sizeof(StringHashEntry*) * bucket_count);
    if (!table->buckets) return NULL;

    /* Initialize buckets to NULL */
    for (size_t i = 0; i < bucket_count; i++) {
        table->buckets[i] = NULL;
    }

    table->bucket_count = bucket_count;
    table->entry_count = 0;
    table->cache_hits = 0;
    table->cache_misses = 0;

    return table;
}

/**
 * Intern a string (lookup or allocate)
 *
 * This is the core deduplication function. It checks if we've seen this
 * string before. If yes, return the cached version. If no, allocate and cache it.
 */
char* leptris_pool_intern_string(LeptrisMemoryPool* pool, const LeptrisStringView* sv) {
    if (!pool || !sv || sv->length == 0 || sv->data == NULL) {
        return NULL;
    }

    /* If no hash table, fall back to direct allocation (no deduplication) */
    if (!pool->string_cache) {
        char* str = (char*)leptris_pool_alloc(pool, sv->length + 1);
        if (!str) return NULL;
        memcpy(str, sv->data, sv->length);
        str[sv->length] = '\0';
        return str;
    }

    StringHashTable* table = pool->string_cache;

    /* 1. Hash the StringView */
    uint32_t hash = hash_string_view(sv);
    size_t bucket_index = hash % table->bucket_count;

    /* 2. Lookup in hash table */
    StringHashEntry* entry = table->buckets[bucket_index];
    while (entry) {
        /* Compare key_length first (fast integer comparison) */
        if (entry->key_length == sv->length) {
            /* Compare key_data (memcmp - only if lengths match) */
            if (memcmp(entry->key_data, sv->data, sv->length) == 0) {
                /* Cache hit! Return existing string */
                table->cache_hits++;
                return entry->cached_string;
            }
        }
        entry = entry->next;
    }

    /* 3. Cache miss - allocate new string */
    table->cache_misses++;

    /* Allocate NULL-terminated string from pool */
    char* new_string = (char*)leptris_pool_alloc(pool, sv->length + 1);
    if (!new_string) return NULL;
    memcpy(new_string, sv->data, sv->length);
    new_string[sv->length] = '\0';

    /* 4. Insert into hash table */
    StringHashEntry* new_entry = (StringHashEntry*)leptris_pool_alloc(pool, sizeof(StringHashEntry));
    if (!new_entry) {
        /* Hash table insert failed, but we have the string - return it anyway */
        return new_string;
    }

    /* CRITICAL FIX: Copy key_data into pool instead of storing pointer to XML buffer
     * This prevents memory corruption when the XML buffer is freed/reused
     * Trade-off: More pool memory usage for safety and correctness */
    new_entry->key_data = (char*)leptris_pool_alloc(pool, sv->length + 1);
    if (new_entry->key_data) {
        memcpy(new_entry->key_data, sv->data, sv->length);
        new_entry->key_data[sv->length] = '\0';
    }
    new_entry->key_length = sv->length;
    new_entry->cached_string = new_string;
    new_entry->next = table->buckets[bucket_index];  /* Prepend to chain */
    table->buckets[bucket_index] = new_entry;
    table->entry_count++;

    /* TODO 36: grow if load factor exceeds 75%. */
    hash_table_maybe_grow(table, pool);

    return new_string;
}

/**
 * Destroy hash table
 *
 * Note: This doesn't free individual entries or strings - they're all pool-allocated
 * and will be freed when the pool itself is destroyed. This function exists only
 * for API completeness and potential future statistics gathering.
 */
void leptris_hash_table_destroy(StringHashTable* table) {
    /* Nothing to do - table, buckets, entries, and strings are all pool-allocated
     * They will be freed when leptris_pool_destroy() frees all pages */
    (void)table;  /* Suppress unused parameter warning */
}

/* ============================================================================
 * Generic Hash Table Functions (for attribute lookup)
 * ============================================================================ */

/**
 * Generic hash function for C strings
 */
static uint32_t hash_cstring(const char* str, size_t len) {
    if (!str) return 0;

    uint32_t hash = 2166136261u;  /* FNV offset basis */
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619u;  /* FNV prime */
    }
    return hash;
}

/**
 * Get value from hash table by string key
 *
 * @param table Hash table to search
 * @param key   C string key
 * @param len   Length of key
 * @return Stored value (void*), or NULL if not found
 */
void* leptris_hash_table_get(StringHashTable* table, const char* key, size_t len) {
    if (!table || !key || len == 0) return NULL;

    uint32_t hash = hash_cstring(key, len);
    size_t bucket_index = hash % table->bucket_count;

    StringHashEntry* entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->key_length == len) {
            if (memcmp(entry->key_data, key, len) == 0) {
                /* Cast cached_string to void* for generic storage */
                return (void*)entry->cached_string;
            }
        }
        entry = entry->next;
    }

    return NULL;
}

/* Maybe grow the hash table when load factor exceeds 75%.
 * Old bucket array stays pool-allocated (reclaimed at pool destroy);
 * we just point at a fresh, larger array and rehash.  See TODO 36. */
static int hash_table_maybe_grow(StringHashTable* table, LeptrisMemoryPool* pool) {
    if (!table || !pool) return 0;
    /* 75% load factor — standard for chained hash tables. */
    if (table->entry_count < (table->bucket_count * 3) / 4) {
        return 0;
    }
    /* Don't grow past 1<<24 buckets (16M entries) — sanity cap. */
    if (table->bucket_count >= (1u << 24)) return 0;

    size_t new_count = table->bucket_count * 2;
    StringHashEntry** new_buckets =
        (StringHashEntry**)leptris_pool_calloc(pool, sizeof(StringHashEntry*) * new_count);
    if (!new_buckets) return -1;

    /* Rehash: walk each existing chain, redistribute into new buckets. */
    for (size_t i = 0; i < table->bucket_count; i++) {
        StringHashEntry* e = table->buckets[i];
        while (e) {
            StringHashEntry* next = e->next;
            size_t b = hash_cstring(e->key_data, e->key_length) % new_count;
            e->next = new_buckets[b];
            new_buckets[b] = e;
            e = next;
        }
    }

    /* Old bucket array is pool-owned; pool will reclaim it on destroy. */
    table->buckets = new_buckets;
    table->bucket_count = new_count;
    return 0;
}

/**
 * Set value in hash table by string key
 *
 * @param table Hash table
 * @param key   C string key (will be copied using pool)
 * @param len   Length of key
 * @param value Value to store (void*)
 * @param pool  Memory pool for allocating entries
 * @return 1 on success, 0 on failure
 */
int leptris_hash_table_set(StringHashTable* table, const char* key, size_t len, void* value, LeptrisMemoryPool* pool) {
    if (!table || !key || len == 0) return 0;

    uint32_t hash = hash_cstring(key, len);
    size_t bucket_index = hash % table->bucket_count;

    /* Check if key already exists */
    StringHashEntry* entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->key_length == len && memcmp(entry->key_data, key, len) == 0) {
            /* Update existing entry */
            entry->cached_string = (char*)value;  /* Cast void* to char* for storage */
            return 1;
        }
        entry = entry->next;
    }

    /* Create new entry using pool allocation (FAST!) */
    StringHashEntry* new_entry;
    char* key_copy;

    if (pool) {
        /* Use pool allocation - O(1) bump-pointer allocation */
        new_entry = (StringHashEntry*)leptris_pool_alloc(pool, sizeof(StringHashEntry));
        if (!new_entry) return 0;

        key_copy = (char*)leptris_pool_alloc(pool, len + 1);
        if (!key_copy) return 0;
    } else {
        /* Fallback to custom allocator if no pool (shouldn't happen in practice) */
        new_entry = (StringHashEntry*)leptris_alloc_hook(sizeof(StringHashEntry));
        if (!new_entry) return 0;

        key_copy = (char*)leptris_alloc_hook(len + 1);
        if (!key_copy) {
            leptris_free_hook(new_entry);
            return 0;
        }
    }

    memcpy(key_copy, key, len);
    key_copy[len] = '\0';

    new_entry->key_data = key_copy;
    new_entry->key_length = len;
    new_entry->cached_string = (char*)value;  /* Cast void* to char* */
    new_entry->next = table->buckets[bucket_index];  /* Prepend to chain */
    table->buckets[bucket_index] = new_entry;
    table->entry_count++;

    /* TODO 36: grow if load factor exceeds 75%.  Amortized O(1). */
    hash_table_maybe_grow(table, pool);

    return 1;
}

/**
 * Remove entry from hash table by string key
 *
 * NOTE: For pool-allocated entries, this only removes from chain.
 * Memory will be freed when pool is destroyed.
 *
 * @param table Hash table
 * @param key   C string key
 * @param len   Length of key
 * @return 1 if found and removed, 0 if not found
 */
int leptris_hash_table_remove(StringHashTable* table, const char* key, size_t len) {
    if (!table || !key || len == 0) return 0;

    uint32_t hash = hash_cstring(key, len);
    size_t bucket_index = hash % table->bucket_count;

    StringHashEntry** entry_ptr = &table->buckets[bucket_index];
    while (*entry_ptr) {
        StringHashEntry* entry = *entry_ptr;
        if (entry->key_length == len && memcmp(entry->key_data, key, len) == 0) {
            /* Remove from chain */
            *entry_ptr = entry->next;

            /* Note: Don't free entry or key if pool-allocated
             * Pool destruction will handle it. We don't track whether
             * it was pool-allocated or malloc'd, so we just never free. */

            table->entry_count--;
            return 1;
        }
        entry_ptr = &entry->next;
    }

    return 0;
}

/* ============================================================================
 * Iterator
 * ============================================================================ */

void leptris_hash_table_for_each(StringHashTable* table,
                                LeptrisHashTableIterator iter,
                                void* user_data) {
    if (!table || !iter) return;
    for (size_t i = 0; i < table->bucket_count; i++) {
        StringHashEntry* entry = table->buckets[i];
        while (entry) {
            StringHashEntry* next = entry->next;
            if (!iter(entry->key_data, entry->key_length,
                     entry->cached_string, user_data)) {
                return;
            }
            entry = next;
        }
    }
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

size_t leptris_pool_total_size(LeptrisMemoryPool* pool) {
    if (!pool) return 0;

    /* Arena mode: capacity is the single allocation. */
    if (pool->arena) return pool->arena->size;

    size_t total = 0;

    /* Pages: header struct + data capacity, summed.  The header uses
     * `char data[1]` as a flexible-array sentinel, so we subtract 1 to
     * avoid double-counting that byte with page_size.  See TODO 10. */
    for (MemoryPage* page = pool->first_page; page; page = page->next) {
        total += sizeof(MemoryPage) - 1 + page->page_size;
    }

    /* Oversized allocations tracked on the side list. */
    for (LeptrisBigAlloc* big = pool->first_big_alloc; big; big = big->next) {
        total += big->size;
    }

    return total;
}

size_t leptris_pool_used_size(LeptrisMemoryPool* pool) {
    /* Distinct from total_size: how many bytes are actually in use right
     * now (busy_size + oversized bytes).  Useful for waste reporting. */
    if (!pool) return 0;

    /* Arena mode: the bump pointer IS the used count. */
    if (pool->arena) return pool->arena->used;

    size_t used = 0;
    for (MemoryPage* page = pool->first_page; page; page = page->next) {
        used += page->busy_size;
    }
    for (LeptrisBigAlloc* big = pool->first_big_alloc; big; big = big->next) {
        used += big->size;
    }
    return used;
}

size_t leptris_pool_page_count(LeptrisMemoryPool* pool) {
    if (!pool) return 0;
    /* Arena mode: one contiguous block — report 1 so callers that use
     * page_count>0 as "has backing storage" stay truthful. */
    if (pool->arena) return 1;
    return pool->page_count;
}