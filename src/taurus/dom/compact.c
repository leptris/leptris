/* lib/src/dom/compact.c - Compressed Pointer Implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implementation of compressed pointer encoding/decoding.
 * Based on pugixml's compact mode design.
 */

#include "compact.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ============================================================================
 * Hash Function for Overflow Table
 * ============================================================================ */

/* FNV-1a hash for pointer keys */
static inline size_t hash_pointer(const void* key, size_t bucket_count) {
    uintptr_t k = (uintptr_t)key;
    size_t hash = 2166136261u;  /* FNV offset basis */

    hash ^= (k >> 0) & 0xFF;
    hash *= 16777619;  /* FNV prime */
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
    /* Ensure bucket_count is power of 2 */
    if (bucket_count & (bucket_count - 1)) {
        /* Round up to next power of 2 */
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

    /* Check if key already exists */
    TaurusCompactOverflowEntry* entry = table->buckets[index];
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            /* Update doc pointer for per-document cleanup */
            entry->doc = doc;
            return 0;
        }
        entry = entry->next;
    }

    /* Create new entry */
    entry = (TaurusCompactOverflowEntry*)malloc(sizeof(TaurusCompactOverflowEntry));
    if (!entry) return -1;

    entry->key = key;
    entry->value = value;
    entry->doc = doc;  /* Track document ownership for per-document cleanup */
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

/* Thread-local overflow table for compact pointers */
static __thread TaurusCompactOverflowTable* g_overflow_table = NULL;
static __thread size_t g_overflow_table_refcount = 0;

/* Get or create overflow table */
static TaurusCompactOverflowTable* get_overflow_table(void) {
    if (!g_overflow_table) {
        g_overflow_table = taurus_compact_overflow_table_create(256);
        g_overflow_table_refcount = 0;
    }
    return g_overflow_table;
}

/* Cleanup overflow table */
void taurus_compact_cleanup(void) {
    if (g_overflow_table) {
        taurus_compact_overflow_table_destroy(g_overflow_table);
        g_overflow_table = NULL;
    }
    g_overflow_table_refcount = 0;
}

/* Cleanup all overflow entries for a specific document */
void taurus_compact_cleanup_document(struct taurus_document* doc) {
    if (!doc || !g_overflow_table) return;

    /* Iterate through all buckets and remove entries belonging to this document
     * This is smarter than destroying the entire table because it preserves
     * overflow entries for other active documents */
    size_t removed_count = 0;

    for (size_t i = 0; i < g_overflow_table->bucket_count; i++) {
        TaurusCompactOverflowEntry** entry_ptr = &g_overflow_table->buckets[i];
        TaurusCompactOverflowEntry* entry = *entry_ptr;

        while (entry) {
            if (entry->doc == doc) {
                /* Remove this entry from the linked list */
                *entry_ptr = entry->next;
                free(entry);
                entry = *entry_ptr;
                removed_count++;
            } else {
                /* Move to next entry */
                entry_ptr = &entry->next;
                entry = entry->next;
            }
        }
    }

    g_overflow_table->entry_count -= removed_count;

    /* If table is empty, destroy it to save memory */
    if (g_overflow_table->entry_count == 0) {
        taurus_compact_overflow_table_destroy(g_overflow_table);
        g_overflow_table = NULL;
    }
}

/* Thread-local current document for overflow tracking */
static __thread struct taurus_document* g_current_document = NULL;

/* Set current document for overflow tracking (during encoding) */
void taurus_compact_set_current_document(struct taurus_document* doc) {
    g_current_document = doc;
}

/* Get current document for overflow tracking */
static struct taurus_document* get_current_document(void) {
    return g_current_document;
}

/* ============================================================================
 * Compact Pointer 8-Bit Implementation
 * ============================================================================ */

void taurus_compact_ptr8_encode(TaurusCompactPtr8* compact,
                                void* target,
                                void* base) {
    if (!target) {
        compact->offset = TAURUS_COMPACT_PTR_NULL;
        return;
    }

    /* Calculate offset in 4-byte units */

    /* Round down to alignment boundary for base calculation */
    uintptr_t base_aligned = (uintptr_t)base & ~(TAURUS_COMPACT_ALIGNMENT - 1);
    ptrdiff_t diff_aligned = (char*)target - (char*)base_aligned;

    /* Convert to 4-byte units */
    ptrdiff_t offset = diff_aligned / TAURUS_COMPACT_ALIGNMENT;

    /* Check if fits in range [-126, +127] */
    if (offset >= TAURUS_COMPACT_PTR8_MIN && offset <= TAURUS_COMPACT_PTR8_MAX) {
        /* Bias from [-126, +127] to [1, 254] */
        compact->offset = (uint8_t)(offset + 127);
    } else {
        /* Store in overflow table */
        TaurusCompactOverflowTable* table = get_overflow_table();
        /* Get current document for proper per-document cleanup */
        struct taurus_document* current_doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, compact, target, current_doc) == 0) {
            compact->offset = TAURUS_COMPACT_PTR_OVERFLOW8;
        } else {
            /* Fallback: mark as NULL if overflow fails */
            compact->offset = TAURUS_COMPACT_PTR_NULL;
        }
    }
}

void* taurus_compact_ptr8_decode(const TaurusCompactPtr8* compact,
                                  void* base) {
    if (compact->offset == TAURUS_COMPACT_PTR_NULL) {
        return NULL;
    }

    if (compact->offset == TAURUS_COMPACT_PTR_OVERFLOW8) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) {
            return taurus_compact_overflow_get(table, compact);
        }
        return NULL;
    }

    /* Remove bias from [1, 254] to [-126, +127] */
    int8_t offset = (int8_t)compact->offset - 127;

    /* Round down base to alignment boundary */
    uintptr_t base_aligned = (uintptr_t)base & ~(TAURUS_COMPACT_ALIGNMENT - 1);

    return (char*)base_aligned + (offset * TAURUS_COMPACT_ALIGNMENT);
}

/* ============================================================================
 * Compact Pointer 16-Bit Implementation
 * ============================================================================ */

void taurus_compact_ptr16_encode(TaurusCompactPtr16* compact,
                                 void* target,
                                 void* base) {
    if (!target) {
        compact->offset = TAURUS_COMPACT_PTR_NULL;
        return;
    }

    /* Calculate offset in 4-byte units */

    /* Round down to alignment boundary for base calculation */
    uintptr_t base_aligned = (uintptr_t)base & ~(TAURUS_COMPACT_ALIGNMENT - 1);
    ptrdiff_t diff_aligned = (char*)target - (char*)base_aligned;

    /* Convert to 4-byte units */
    ptrdiff_t offset = diff_aligned / TAURUS_COMPACT_ALIGNMENT;

    /* Check if fits in range [-65533, +65534] */
    if (offset >= TAURUS_COMPACT_PTR16_MIN && offset <= TAURUS_COMPACT_PTR16_MAX) {
        /* Bias from [-65533, +65534] to [1, 65534] */
        compact->offset = (uint16_t)(offset + 65533);
    } else {
        /* Store in overflow table */
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* current_doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, compact, target, current_doc) == 0) {
            compact->offset = TAURUS_COMPACT_PTR_OVERFLOW16;
        } else {
            /* Fallback: mark as NULL if overflow fails */
            compact->offset = TAURUS_COMPACT_PTR_NULL;
        }
    }
}

void* taurus_compact_ptr16_decode(const TaurusCompactPtr16* compact,
                                   void* base) {
    if (compact->offset == TAURUS_COMPACT_PTR_NULL) {
        return NULL;
    }

    if (compact->offset == TAURUS_COMPACT_PTR_OVERFLOW16) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) {
            return taurus_compact_overflow_get(table, compact);
        }
        return NULL;
    }

    /* Remove bias from [1, 65534] to [-65533, +65534] */
    int16_t offset = (int16_t)compact->offset - 65533;

    /* Round down base to alignment boundary */
    uintptr_t base_aligned = (uintptr_t)base & ~(TAURUS_COMPACT_ALIGNMENT - 1);

    return (char*)base_aligned + (offset * TAURUS_COMPACT_ALIGNMENT);
}

/* ============================================================================
 * Compact String Implementation
 * ============================================================================ */

void taurus_compact_string_encode(TaurusCompactString* compact,
                                  const char* str,
                                  const char* base) {
    if (!str) {
        compact->base = 0;
        compact->data = 0;
        return;
    }

    /* Calculate offset from base */
    ptrdiff_t offset = str - base;

    /* Check if fits in range (65535 << 7) = ~8MB */
    if (offset >= 0 && offset < (65535LL << 7)) {
        /* Split into high bits (base) and low 7 bits (data) */
        compact->base = (uint16_t)((offset >> 7) + 1);
        compact->data = (uint8_t)((offset & 0x7F) + 1);
    } else {
        /* Store in overflow table */
        TaurusCompactOverflowTable* table = get_overflow_table();
        struct taurus_document* current_doc = get_current_document();
        if (table && taurus_compact_overflow_set(table, compact, (void*)str, current_doc) == 0) {
            compact->data = 255;  /* Overflow marker */
        } else {
            /* Fallback: mark as NULL if overflow fails */
            compact->base = 0;
            compact->data = 0;
        }
    }
}

const char* taurus_compact_string_decode(const TaurusCompactString* compact,
                                         const char* base) {
    if (compact->data == 0) {
        return NULL;
    }

    if (compact->data == 255) {
        TaurusCompactOverflowTable* table = get_overflow_table();
        if (table) {
            return (const char*)taurus_compact_overflow_get(table, compact);
        }
        return NULL;
    }

    /* Reconstruct offset from base (high bits) and data (low 7 bits) */
    ptrdiff_t offset = ((compact->base - 1) << 7) + (compact->data - 1);

    return base + offset;
}

/* ============================================================================
 * Compact Header Implementation
 * ============================================================================ */

void taurus_compact_header_init(TaurusCompactHeader* header,
                                void* page_base,
                                uint8_t flags) {
    /* Calculate offset from page_base to header */
    ptrdiff_t offset = (char*)header - (char*)page_base;

    /* Verify alignment */
    assert(offset % TAURUS_COMPACT_ALIGNMENT == 0);
    assert(offset < (256 * TAURUS_COMPACT_ALIGNMENT));

    /* Store offset in 4-byte units */
    header->page_offset = (uint8_t)(offset >> TAURUS_COMPACT_ALIGNMENT_LOG2);
    header->flags = flags;
}

void* taurus_compact_header_get_page(const TaurusCompactHeader* header,
                                     void* page_base) {
    /* Reconstruct page pointer from offset */
    ptrdiff_t offset = header->page_offset << TAURUS_COMPACT_ALIGNMENT_LOG2;
    return (char*)header - offset;
}
