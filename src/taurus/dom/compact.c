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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* ============================================================================
 * Hash Function for Overflow Table
 * ============================================================================ */

static inline size_t hash_pointer(const void* key, size_t bucket_count) {
    uintptr_t k = (uintptr_t)key;
    size_t hash = 2166136261u;

    hash ^= (k >> 0) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 8) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 16) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 24) & 0xFF;
    hash *= 16777619;

#if UINTPTR_MAX >= 0xFFFFFFFFFFFFFFFF
    hash ^= (k >> 32) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 40) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 48) & 0xFF;
    hash *= 16777619;
    hash ^= (k >> 56) & 0xFF;
    hash *= 16777619;
#endif

    return hash & (bucket_count - 1);
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

    return table;
}

void taurus_compact_overflow_table_destroy(TaurusCompactOverflowTable* table) {
    if (!table) return;

    for (size_t i = 0; i < table->bucket_count; i++) {
        TaurusCompactOverflowEntry* entry = table->buckets[i];
        while (entry) {
            TaurusCompactOverflowEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }

    free(table->buckets);
    free(table);
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

    entry = (TaurusCompactOverflowEntry*)malloc(sizeof(TaurusCompactOverflowEntry));
    if (!entry) return -1;

    entry->key = key;
    entry->value = value;
    entry->doc = doc;
    entry->next = table->buckets[index];
    table->buckets[index] = entry;
    table->entry_count++;

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

static __thread TaurusCompactOverflowTable* g_overflow_table = NULL;
static __thread size_t g_overflow_table_refcount = 0;

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
                *entry_ptr = entry->next;
                free(entry);
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

static __thread struct taurus_document* g_current_document = NULL;

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
