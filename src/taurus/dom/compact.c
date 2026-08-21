/* lib/src/dom/compact.c - Compressed Pointer Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implementation of int32 compact pointer encoding/decoding with
 * overflow table fallback. The 8-bit and 16-bit compact pointer
 * types (TaurusCompactPtr8/16) have been removed — they were never
 * used outside this file. Only int32 offsets (used by element.h,
 * text.h, etc. for tree edges) remain.
 */

#include "compact.h"
#include "../common/port.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* ============================================================================
 * Hash Function for Overflow Table
 * ============================================================================ */

static inline size_t hash_pointer(const void* key, size_t bucket_count) {
    /* Multiply-shift pointer hash: 2 ops vs the old byte-wise FNV
     * (8 multiplies). Aligned pointers are well-distributed under
     * Fibonacci hashing; bucket_count is a power of two. */
    uintptr_t k = (uintptr_t)key;
    return (size_t)((k * 0x9E3779B97F4A7C15ULL) >> 32) & (bucket_count - 1);
}

/* ============================================================================
 * Overflow Hash Table Implementation
 * ============================================================================ */

TaurusCompactOverflowTable* taurus_compact_overflow_table_create(size_t bucket_count) {
    if (bucket_count & (bucket_count - 1)) {
        bucket_count--;
        bucket_count |= bucket_count >> 1;
        bucket_count |= bucket_count >> 2;
        bucket_count |= bucket_count >> 4;
        bucket_count |= bucket_count >> 8;
        bucket_count |= bucket_count >> 16;
        bucket_count++;
    }

    TaurusCompactOverflowTable* table = (TaurusCompactOverflowTable*)malloc(sizeof(TaurusCompactOverflowTable));
    if (!table) return NULL;

    table->buckets = (TaurusCompactOverflowEntry**)calloc(bucket_count, sizeof(TaurusCompactOverflowEntry*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    table->bucket_count = bucket_count;
    table->entry_count = 0;
    table->slabs = NULL;
    table->slab_count = 0;
    table->slab_capacity = 0;
    table->slab_next = NULL;
    table->slab_remaining = 0;

    return table;
}

#define OVERFLOW_SLAB_ENTRIES 256

/* Carve an entry from the current slab, starting a new slab when
 * exhausted. ~1 malloc per 256 entries instead of one per entry. */
static TaurusCompactOverflowEntry* overflow_entry_alloc(
    TaurusCompactOverflowTable* table) {
    if (table->slab_remaining == 0) {
        if (table->slab_count == table->slab_capacity) {
            size_t new_cap = table->slab_capacity ? table->slab_capacity * 2 : 4;
            void** grown = (void**)realloc(table->slabs, new_cap * sizeof(void*));
            if (!grown) return NULL;
            table->slabs = grown;
            table->slab_capacity = new_cap;
        }
        char* slab = (char*)malloc(OVERFLOW_SLAB_ENTRIES *
                                   sizeof(TaurusCompactOverflowEntry));
        if (!slab) return NULL;
        table->slabs[table->slab_count++] = slab;
        table->slab_next = (TaurusCompactOverflowEntry*)slab;
        table->slab_remaining = OVERFLOW_SLAB_ENTRIES;
    }
    table->slab_remaining--;
    return table->slab_next++;
}

void taurus_compact_overflow_table_destroy(TaurusCompactOverflowTable* table) {
    if (!table) return;

    /* Entries live in slabs — freeing the slabs frees them all. */
    for (size_t i = 0; i < table->slab_count; i++) {
        free(table->slabs[i]);
    }
    free(table->slabs);

    free(table->buckets);
    free(table);
}

/* Double the bucket array and rehash every entry. Mutation-created
 * trees store a parent-edge entry per node here (their elements sit
 * in different malloc regions than the parse arena, beyond the int32
 * compact field); with the fixed 256-bucket table those chains grew
 * linearly with tree size and every set/get walked them — the
 * measured rising append cost. Load-factor 1.0 growth keeps chains
 * O(1). Rehash iterates the OLD bucket chains (the authoritative
 * structure); slab storage is untouched. */
static int overflow_table_grow(TaurusCompactOverflowTable* table) {
    size_t new_count = table->bucket_count * 2;
    TaurusCompactOverflowEntry** nb = (TaurusCompactOverflowEntry**)
        calloc(new_count, sizeof(TaurusCompactOverflowEntry*));
    if (!nb) return -1;   /* stay at current size: correct, slower */
    for (size_t i = 0; i < table->bucket_count; i++) {
        TaurusCompactOverflowEntry* e = table->buckets[i];
        while (e) {
            TaurusCompactOverflowEntry* next = e->next;
            size_t idx = hash_pointer(e->key, new_count);
            e->next = nb[idx];
            nb[idx] = e;
            e = next;
        }
    }
    free(table->buckets);
    table->buckets = nb;
    table->bucket_count = new_count;
    return 0;
}

int taurus_compact_overflow_set(TaurusCompactOverflowTable* table,
                                const void* key,
                                void* value,
                                struct taurus_document* doc) {
    if (!table || !key) return -1;

    size_t index = hash_pointer(key, table->bucket_count);

    TaurusCompactOverflowEntry* entry = table->buckets[index];
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            entry->doc = doc;
            return 0;
        }
        entry = entry->next;
    }

    entry = overflow_entry_alloc(table);
    if (!entry) return -1;

    entry->key = key;
    entry->value = value;
    entry->doc = doc;
    entry->next = table->buckets[index];
    table->buckets[index] = entry;
    table->entry_count++;

    if (table->entry_count >= table->bucket_count) {
        overflow_table_grow(table);
    }

    return 0;
}

void* taurus_compact_overflow_get(TaurusCompactOverflowTable* table,
                                  const void* key) {
    if (!table || !key) return NULL;

    size_t index = hash_pointer(key, table->bucket_count);
    TaurusCompactOverflowEntry* entry = table->buckets[index];

    while (entry) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

/* ============================================================================
 * Global Overflow Table (Thread-local for thread safety)
 * ============================================================================ */

static TAURUS_THREAD_LOCAL TaurusCompactOverflowTable* g_overflow_table = NULL;
static TAURUS_THREAD_LOCAL size_t g_overflow_table_refcount = 0;

static TaurusCompactOverflowTable* get_overflow_table(void) {
    if (!g_overflow_table) {
        g_overflow_table = taurus_compact_overflow_table_create(256);
        g_overflow_table_refcount = 0;
    }
    return g_overflow_table;
}

void taurus_compact_cleanup(void) {
    if (g_overflow_table) {
        taurus_compact_overflow_table_destroy(g_overflow_table);
        g_overflow_table = NULL;
    }
    g_overflow_table_refcount = 0;
}

void taurus_compact_cleanup_document(struct taurus_document* doc) {
    if (!doc || !g_overflow_table) return;

    size_t removed_count = 0;

    for (size_t i = 0; i < g_overflow_table->bucket_count; i++) {
        TaurusCompactOverflowEntry** entry_ptr = &g_overflow_table->buckets[i];
        TaurusCompactOverflowEntry* entry = *entry_ptr;

        while (entry) {
            if (entry->doc == doc) {
                /* Unlink only — entries live in SLABS owned by the
                 * table; free()ing them corrupted the heap whenever
                 * a document had overflow entries (common since the
                 * #450 sibling-edge work routed far links here). */
                *entry_ptr = entry->next;
                entry = *entry_ptr;
                removed_count++;
            } else {
                entry_ptr = &entry->next;
                entry = entry->next;
            }
        }
    }

    g_overflow_table->entry_count -= removed_count;

    if (g_overflow_table->entry_count == 0) {
        taurus_compact_overflow_table_destroy(g_overflow_table);
        g_overflow_table = NULL;
    }
}

static TAURUS_THREAD_LOCAL struct taurus_document* g_current_document = NULL;

void taurus_compact_set_current_document(struct taurus_document* doc) {
    g_current_document = doc;
}

static struct taurus_document* get_current_document(void) {
    return g_current_document;
}

/* ============================================================================
 * int32 Compact Pointer with Overflow Fallback (TODO 121)
 *
 * Tree edges (parent, first/last/next sibling) stored as int32_t byte
 * offsets relative to the node's own address. On overflow (>2GB), the
 * (field address -> target) mapping is registered in the overflow table
 * and INT32_MIN is stored as a sentinel. NULL stays 0.
 * ============================================================================ */

#define TAURUS_INT32_OVERFLOW_SENTINEL INT32_MIN

int32_t taurus_compact_int32_encode(void* base, void* target,
                                     const int32_t* field_addr) {
    if (!target) return 0;
    ptrdiff_t d = (char*)target - (char*)base;
    if (d < INT32_MIN || d > INT32_MAX) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, field_addr,
                                                  target, doc) == 0) {
            return TAURUS_INT32_OVERFLOW_SENTINEL;
        }
        return 0;
    }
    return (int32_t)d;
}

void* taurus_compact_int32_decode(void* base, int32_t off,
                                   const int32_t* field_addr) {
    if (off == 0) return NULL;
    if (off == TAURUS_INT32_OVERFLOW_SENTINEL) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) return taurus_compact_overflow_get(table, field_addr);
        return NULL;
    }
    return (char*)base + off;
}

/* ============================================================================
 * 1-byte and 2-byte Compact Pointers (TODO 178)
 *
 * Same overflow mechanism as int32, but tighter storage for nearby
 * tree edges (sibling chains, attribute lists). Offsets are scaled by
 * `1 << align_log2` before encoding, expanding the representable
 * range at the cost of an alignment requirement on base and target.
 * ============================================================================ */

int8_t taurus_compact_ptr8_encode(const void* base, const void* target,
                                   int align_log2,
                                   const int8_t* field_addr) {
    if (!target) return 0;
    ptrdiff_t d = (const char*)target - (const char*)base;
    ptrdiff_t align_mask = ((ptrdiff_t)1 << align_log2) - 1;
    if (d & align_mask) {
        /* Misaligned — can't represent, fall through to overflow. */
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, field_addr,
                                                  (void*)target, doc) == 0) {
            return TAURUS_COMPACT_PTR8_OVERFLOW;
        }
        return 0;
    }
    ptrdiff_t scaled = d >> align_log2;
    if (scaled <= INT8_MIN || scaled > INT8_MAX) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, field_addr,
                                                  (void*)target, doc) == 0) {
            return TAURUS_COMPACT_PTR8_OVERFLOW;
        }
        return 0;
    }
    return (int8_t)scaled;
}

void* taurus_compact_ptr8_decode(const void* base, int8_t off,
                                  int align_log2,
                                  const int8_t* field_addr) {
    if (off == 0) return NULL;
    if (off == TAURUS_COMPACT_PTR8_OVERFLOW) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) return taurus_compact_overflow_get(table, field_addr);
        return NULL;
    }
    return (char*)base + ((ptrdiff_t)off << align_log2);
}

int16_t taurus_compact_ptr16_encode(const void* base, const void* target,
                                     int align_log2,
                                     const int16_t* field_addr) {
    if (!target) return 0;
    ptrdiff_t d = (const char*)target - (const char*)base;
    ptrdiff_t align_mask = ((ptrdiff_t)1 << align_log2) - 1;
    if (d & align_mask) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, field_addr,
                                                  (void*)target, doc) == 0) {
            return TAURUS_COMPACT_PTR16_OVERFLOW;
        }
        return 0;
    }
    ptrdiff_t scaled = d >> align_log2;
    if (scaled <= INT16_MIN || scaled > INT16_MAX) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, field_addr,
                                                  (void*)target, doc) == 0) {
            return TAURUS_COMPACT_PTR16_OVERFLOW;
        }
        return 0;
    }
    return (int16_t)scaled;
}

void* taurus_compact_ptr16_decode(const void* base, int16_t off,
                                   int align_log2,
                                   const int16_t* field_addr) {
    if (off == 0) return NULL;
    if (off == TAURUS_COMPACT_PTR16_OVERFLOW) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) return taurus_compact_overflow_get(table, field_addr);
        return NULL;
    }
    return (char*)base + ((ptrdiff_t)off << align_log2);
}

/* ============================================================================
 * Compact Header Implementation
 * ============================================================================ */

void taurus_compact_header_init(TaurusCompactHeader* header,
                                void* page_base,
                                uint8_t flags) {
    ptrdiff_t offset = (char*)header - (char*)page_base;
    assert(offset % TAURUS_COMPACT_ALIGNMENT == 0);
    assert(offset < (256 * TAURUS_COMPACT_ALIGNMENT));
    header->page_offset = (uint8_t)(offset >> TAURUS_COMPACT_ALIGNMENT_LOG2);
    header->flags = flags;
}

void* taurus_compact_header_get_page(const TaurusCompactHeader* header,
                                     void* page_base) {
    ptrdiff_t offset = header->page_offset << TAURUS_COMPACT_ALIGNMENT_LOG2;
    return (char*)header - offset;
}
