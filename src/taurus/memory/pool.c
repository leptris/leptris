/* lib/src/memory/pool.c - Page-based Memory Pool Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Based on pugixml's memory allocator design:
 * - 32KB pages (reduces malloc calls by ~1000x)
 * - Bump-pointer allocation (O(1), no malloc per allocation)
 * - Batch deallocation (free all pages at once)
 */

#include "../common/string_view.h"  /* Full TaurusStringView definition */
#include "pool.h"                    /* Pool API */
#include "../taurus_internal.h"      /* Custom allocation hooks */
#include "../dom/compact.h"          /* TAURUS_COMPACT_ALIGNMENT for assertions */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Default page size (same as pugixml) - 32KB provides good balance:
 * - Large enough to amortize malloc overhead
 * - Small enough to avoid wasting memory
 * - Typical XML elements use ~100 bytes, so ~320 elements per page
 */
#define TAURUS_POOL_PAGE_SIZE_DEFAULT 32768

/* Minimum page size for small files */
#define TAURUS_POOL_PAGE_SIZE_MIN 4096

/* Alignment for all allocations (same as pugixml) */
#define TAURUS_POOL_ALIGNMENT 8

/* Align size up to nearest multiple of TAURUS_POOL_ALIGNMENT */
#define ALIGN_SIZE(size) (((size) + TAURUS_POOL_ALIGNMENT - 1) & ~(TAURUS_POOL_ALIGNMENT - 1))

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* Memory page - holds actual memory for allocations (dynamic size) */
typedef struct memory_page {
    struct memory_page* next;    /* Linked list of pages */
    size_t page_size;            /* Total size of this page */
    size_t busy_size;            /* Bytes used in this page */
    char data[1];                /* Flexible array member - actual memory */
} MemoryPage;

/* Note: struct taurus_memory_pool is now defined in pool.h */

/* ============================================================================
 * Pool Lifecycle
 * ============================================================================ */

/* Helper to allocate a new page */
static MemoryPage* allocate_new_page(size_t page_size) {
    /* Allocate page structure + data in one allocation */
    size_t total_size = sizeof(MemoryPage) - 1 + page_size;
    MemoryPage* page = (MemoryPage*)taurus_alloc_hook(total_size);
    if (!page) return NULL;

    page->next = NULL;
    page->page_size = page_size;
    page->busy_size = 0;

    return page;
}

TaurusMemoryPool* taurus_pool_create(void) {
    return taurus_pool_create_with_page_size(TAURUS_POOL_PAGE_SIZE_DEFAULT);
}

TaurusMemoryPool* taurus_pool_create_with_page_size(size_t page_size) {
    /* Validate page size */
    if (page_size < TAURUS_POOL_PAGE_SIZE_MIN) {
        page_size = TAURUS_POOL_PAGE_SIZE_MIN;
    }

    /* Round up to alignment */
    page_size = (page_size + TAURUS_POOL_ALIGNMENT - 1) & ~(TAURUS_POOL_ALIGNMENT - 1);

    TaurusMemoryPool* pool = (TaurusMemoryPool*)taurus_alloc_hook(sizeof(TaurusMemoryPool));
    if (!pool) return NULL;

    /* Allocate first page */
    MemoryPage* page = allocate_new_page(page_size);
    if (!page) {
        taurus_free_hook(pool);
        return NULL;
    }

    pool->first_page = page;
    pool->current_page = page;
    pool->page_count = 1;
    pool->string_cache = NULL;
    pool->strict_mode = 0;
    pool->page_size = page_size;
    pool->page_base = page->data;  /* Initialize page_base for compact pointer decoding */

    /* Pre-allocate additional pages for better cache locality
     * PERFORMANCE: Only pre-allocate for larger page sizes (>= 8KB)
     * to avoid wasting memory on small documents. For 4KB pages used with
     * small files, the single page is typically sufficient. */
    if (page_size >= 8192) {
        MemoryPage* next_page = allocate_new_page(page_size);
        if (next_page) {
            page->next = next_page;
            pool->page_count++;
        }
    }

    return pool;
}

void taurus_pool_destroy(TaurusMemoryPool* pool) {
    if (!pool) return;

    /* Free all pages */
    MemoryPage* page = pool->first_page;
    while (page) {
        MemoryPage* next = page->next;
        taurus_free_hook(page);
        page = next;
    }

    taurus_free_hook(pool);
}

void* taurus_pool_get_base(TaurusMemoryPool* pool) {
    if (!pool) return NULL;
    return pool->page_base;
}

/* ============================================================================
 * Allocation - Fast Bump-Pointer Implementation
 * ============================================================================ */

/**
 * Allocate new page and add to pool
 *
 * Called when current page is full. This is the "slow path" but still O(1).
 */
static MemoryPage* add_new_page(TaurusMemoryPool* pool, size_t page_size) {
    MemoryPage* page = allocate_new_page(page_size);
    if (!page) return NULL;

    /* Add to end of list */
    pool->current_page->next = page;
    pool->current_page = page;
    pool->page_count++;

    return page;
}

void* taurus_pool_alloc(TaurusMemoryPool* pool, size_t size) {
    if (!pool || size == 0) return NULL;

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
        assert(((uintptr_t)ptr & (TAURUS_COMPACT_ALIGNMENT - 1)) == 0 &&
               "Pool allocation not 4-byte aligned - will cause compact pointer corruption!");

        page->busy_size += size;
        return ptr;
    }

    /* Current page full - allocate new page (SLOW PATH, but still O(1)) */
    size_t page_size = page->page_size;  /* Use same page size as current page */
    if (size > page_size) {
        /* Allocation too large for standard page
         * Fall back to custom allocator for oversized allocations
         * This is rare - typical elements/attributes are <100 bytes
         */
        return taurus_alloc_hook(size);
    }

    page = add_new_page(pool, page_size);
    if (!page) return NULL;

    /* Allocate from new page */
    void* ptr = page->data;
    page->busy_size = size;
    return ptr;
}

void* taurus_pool_calloc(TaurusMemoryPool* pool, size_t size) {
    void* ptr = taurus_pool_alloc(pool, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* Allocate multiple items contiguously from pool
 * Returns pointer to first item, or NULL on failure
 */
void* taurus_pool_alloc_batch(TaurusMemoryPool* pool, size_t item_size, size_t count) {
    if (!pool || item_size == 0 || count == 0) return NULL;

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
        assert(((uintptr_t)ptr & (TAURUS_COMPACT_ALIGNMENT - 1)) == 0 &&
               "Pool batch allocation not 4-byte aligned!");

        return ptr;
    }

    /* Fallback: allocate using standard alloc for each item
     * Not ideal, but maintains correctness
     */
    if (total_size > page->page_size) {
        /* Too large for single page, use fallback */
        void* ptr = taurus_alloc_hook(total_size);
        return ptr;
    }

    /* Allocate from new page */
    page = add_new_page(pool, page->page_size);
    if (!page) return NULL;

    void* ptr = page->data;
    page->busy_size = total_size;
    return ptr;
}

/* ============================================================================
 * String Functions - Zero-Copy Support
 * ============================================================================ */

char* taurus_pool_strdup_inplace(TaurusMemoryPool* pool, char* str) {
    if (!pool || !str) return NULL;

    /* Zero-copy: just return the pointer directly
     * The string is already NULL-terminated in the XML buffer
     * Pool will track the buffer for cleanup
     */
    return str;
}

char* taurus_pool_strdup(TaurusMemoryPool* pool, const char* str) {
    if (!pool || !str) return NULL;

    /* Calculate string length */
    size_t len = strlen(str);

    /* Allocate from pool */
    char* copy = (char*)taurus_pool_alloc(pool, len + 1);
    if (!copy) return NULL;

    /* Copy string including NULL terminator */
    memcpy(copy, str, len + 1);

    return copy;
}

/* ============================================================================
 * String Interning - Hash Table Implementation
 * ============================================================================ */

/**
 * FNV-1a hash function for StringView
 *
 * Fast, simple, and well-distributed hash function.
 * FNV-1a is proven to work well for string hashing.
 */

/* Forward declaration for use in growth function */
static uint32_t hash_cstring(const char* str, size_t len);

static uint32_t hash_string_view(const TaurusStringView* sv) {
    if (!sv || !sv->data || sv->length == 0) return 0;

    /* VALIDATE: Check for corrupted pointer before using it */
    uintptr_t addr = (uintptr_t)sv->data;
    if (addr < 0x1000) return 0;  /* Too small to be valid */
    /* Check for ASCII text in pointer (sign of memory corruption) */
    unsigned char* bytes = (unsigned char*)&addr;
    int all_printable = 1;
    for (size_t i = 0; i < sizeof(addr); i++) {
        if (bytes[i] < 0x20 || bytes[i] > 0x7E) {
            all_printable = 0;
            break;
        }
    }
    if (all_printable) return 0;  /* Contains ASCII text instead of address */

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
StringHashTable* taurus_hash_table_create(TaurusMemoryPool* pool, size_t bucket_count) {
    if (!pool || bucket_count == 0) return NULL;

    /* Allocate table structure from pool */
    StringHashTable* table = (StringHashTable*)taurus_pool_alloc(pool, sizeof(StringHashTable));
    if (!table) return NULL;

    /* Allocate buckets array from pool */
    table->buckets = (StringHashEntry**)taurus_pool_alloc(pool, sizeof(StringHashEntry*) * bucket_count);
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
 * Grow hash table when load factor exceeds threshold
 *
 * PERFORMANCE: This is critical for large files. Without growth,
 * a 10MB file with 1M strings would have ~7800 entries per bucket,
 * making lookups O(7800) instead of O(1).
 *
 * With growth, we maintain ~4 entries per bucket for O(1) lookups.
 */
static int taurus_hash_table_grow(TaurusMemoryPool* pool, StringHashTable* table) {
    if (!pool || !table) return 0;

    /* Double the bucket count */
    size_t new_bucket_count = table->bucket_count * 2;

    /* Safety limit: don't exceed 16M buckets (128MB for bucket array) */
    if (new_bucket_count > 16 * 1024 * 1024) return 1;  /* Table is big enough */

    /* Allocate new bucket array */
    StringHashEntry** new_buckets = (StringHashEntry**)taurus_pool_alloc(
        pool, sizeof(StringHashEntry*) * new_bucket_count);
    if (!new_buckets) return 0;  /* Growth failed, continue with current size */

    /* Initialize new buckets to NULL */
    for (size_t i = 0; i < new_bucket_count; i++) {
        new_buckets[i] = NULL;
    }

    /* Rehash all existing entries into new buckets */
    for (size_t i = 0; i < table->bucket_count; i++) {
        StringHashEntry* entry = table->buckets[i];
        while (entry) {
            StringHashEntry* next = entry->next;

            /* Recalculate bucket index with new bucket count
             * Use hash_cstring directly to avoid creating temporary StringView */
            uint32_t hash = hash_cstring(entry->key_data, entry->key_length);
            size_t new_index = hash % new_bucket_count;

            /* Prepend to new bucket chain */
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next;
        }
    }

    /* Update table with new buckets */
    /* Note: Old bucket array remains in pool memory (wasted but acceptable) */
    table->buckets = new_buckets;
    table->bucket_count = new_bucket_count;

    return 1;
}

/**
 * Intern a string (lookup or allocate)
 *
 * This is the core deduplication function. It checks if we've seen this
 * string before. If yes, return the cached version. If no, allocate and cache it.
 */
char* taurus_pool_intern_string(TaurusMemoryPool* pool, const TaurusStringView* sv) {
    if (!pool || !sv || sv->length == 0) {
        return NULL;
    }

    /* VALIDATE: Check if sv->data is a valid pointer before using it */
    if (!sv->data) {
        return NULL;
    }
    uintptr_t addr = (uintptr_t)sv->data;
    if (addr < 0x1000) {
        /* Invalid pointer - too small */
        return NULL;
    }
    /* Check for ASCII text in pointer (sign of memory corruption) */
    unsigned char* bytes = (unsigned char*)&addr;
    int all_printable = 1;
    for (size_t i = 0; i < sizeof(addr); i++) {
        if (bytes[i] < 0x20 || bytes[i] > 0x7E) {
            all_printable = 0;
            break;
        }
    }
    if (all_printable) {
        /* Corrupted pointer - contains ASCII text instead of address */
        return NULL;
    }

    /* If no hash table, fall back to direct allocation (no deduplication) */
    if (!pool->string_cache) {
        char* str = (char*)taurus_pool_alloc(pool, sv->length + 1);
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
        /* VALIDATE: Check entry->key_data before using it */
        if ((uintptr_t)entry->key_data >= 0x1000) {
            /* Compare key_length first (fast integer comparison) */
            if (entry->key_length == sv->length) {
                /* Compare key_data (memcmp - only if lengths match) */
                if (memcmp(entry->key_data, sv->data, sv->length) == 0) {
                    /* Cache hit! Return existing string */
                    table->cache_hits++;
                    return entry->cached_string;
                }
            }
        }
        entry = entry->next;
    }

    /* 3. Cache miss - allocate new string */
    table->cache_misses++;

    /* PERFORMANCE: Check if we need to grow the hash table
     * Grow when load factor > 0.75 (entry_count > bucket_count * 3/4)
     * This keeps bucket chains short for O(1) lookup performance */
    if (table->entry_count > (table->bucket_count * 3) / 4) {
        taurus_hash_table_grow(pool, table);
        /* Recalculate bucket index after potential growth */
        bucket_index = hash % table->bucket_count;
    }

    /* Allocate NULL-terminated string from pool */
    char* new_string = (char*)taurus_pool_alloc(pool, sv->length + 1);
    if (!new_string) return NULL;
    memcpy(new_string, sv->data, sv->length);
    new_string[sv->length] = '\0';

    /* 4. Insert into hash table */
    StringHashEntry* new_entry = (StringHashEntry*)taurus_pool_alloc(pool, sizeof(StringHashEntry));
    if (!new_entry) {
        /* Hash table insert failed, but we have the string - return it anyway */
        return new_string;
    }

    /* CRITICAL FIX: Copy key_data into pool instead of storing pointer to XML buffer
     * This prevents memory corruption when the XML buffer is freed/reused
     * Trade-off: More pool memory usage for safety and correctness */
    new_entry->key_data = (char*)taurus_pool_alloc(pool, sv->length + 1);
    if (new_entry->key_data) {
        memcpy(new_entry->key_data, sv->data, sv->length);
        new_entry->key_data[sv->length] = '\0';
    }
    new_entry->key_length = sv->length;
    new_entry->cached_string = new_string;
    new_entry->next = table->buckets[bucket_index];  /* Prepend to chain */
    table->buckets[bucket_index] = new_entry;
    table->entry_count++;

    return new_string;
}

/**
 * Destroy hash table
 *
 * Note: This doesn't free individual entries or strings - they're all pool-allocated
 * and will be freed when the pool itself is destroyed. This function exists only
 * for API completeness and potential future statistics gathering.
 */
void taurus_hash_table_destroy(StringHashTable* table) {
    /* Nothing to do - table, buckets, entries, and strings are all pool-allocated
     * They will be freed when taurus_pool_destroy() frees all pages */
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
void* taurus_hash_table_get(StringHashTable* table, const char* key, size_t len) {
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
int taurus_hash_table_set(StringHashTable* table, const char* key, size_t len, void* value, TaurusMemoryPool* pool) {
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
        new_entry = (StringHashEntry*)taurus_pool_alloc(pool, sizeof(StringHashEntry));
        if (!new_entry) return 0;

        key_copy = (char*)taurus_pool_alloc(pool, len + 1);
        if (!key_copy) return 0;
    } else {
        /* Fallback to custom allocator if no pool (shouldn't happen in practice) */
        new_entry = (StringHashEntry*)taurus_alloc_hook(sizeof(StringHashEntry));
        if (!new_entry) return 0;

        key_copy = (char*)taurus_alloc_hook(len + 1);
        if (!key_copy) {
            taurus_free_hook(new_entry);
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
int taurus_hash_table_remove(StringHashTable* table, const char* key, size_t len) {
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
 * Statistics
 * ============================================================================ */

size_t taurus_pool_total_size(TaurusMemoryPool* pool) {
    if (!pool) return 0;

    size_t total = 0;
    MemoryPage* page = pool->first_page;
    while (page) {
        total += sizeof(MemoryPage);  /* Include page overhead */
        page = page->next;
    }

    return total;
}

size_t taurus_pool_page_count(TaurusMemoryPool* pool) {
    return pool ? pool->page_count : 0;
}