/* lib/src/dom/compact.h - Compressed Pointer Architecture
 * Copyright (c) 2024, Ribose Inc.
 *
 * Based on pugixml's compact mode design.
 * Enables 6x smaller element structures (32 bytes vs 192 bytes).
 *
 * Key innovations:
 * - 1-byte pointers instead of 8-byte pointers (for local references)
 * - 2-byte pointers for parent (which may be far)
 * - Inline string storage (1-3 bytes vs 8 bytes)
 * - Hash table fallback for large offsets
 *
 * This is the key to beating pugixml's performance!
 */

#ifndef TAURUS_DOM_COMPACT_H
#define TAURUS_DOM_COMPACT_H

#include <stddef.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/* Alignment for compact pointers (4 bytes, not 8!) */
#define TAURUS_COMPACT_ALIGNMENT 4
#define TAURUS_COMPACT_ALIGNMENT_LOG2 2

/* Special compact pointer values */
#define TAURUS_COMPACT_PTR_NULL       0    /* NULL pointer */

/* ============================================================================
 * Core Compact Types
 * ============================================================================ */

/**
 * Compact header (2 bytes)
 *
 * Stores page offset and node type flags.
 * All structures in compact mode must start with this header.
 */
typedef struct taurus_compact_header {
    uint8_t page_offset;   /* Offset from page_base in 4-byte units */
    uint8_t flags;         /* Node type flags (TAURUS_NODE_ELEMENT, etc.) */
} TaurusCompactHeader;

/* ============================================================================
 * Overflow Hash Table
 * ============================================================================ */

/**
 * Overflow hash table entry
 *
 * Stores pointer values that don't fit in compact representation.
 */
typedef struct taurus_compact_overflow_entry {
    const void* key;              /* Pointer to compact field */
    void* value;                  /* Actual pointer value */
    struct taurus_document* doc;   /* Document that owns this entry */
    struct taurus_compact_overflow_entry* next;
} TaurusCompactOverflowEntry;

/**
 * Overflow hash table
 *
 * Maps compact field pointers to actual pointer values.
 */
typedef struct taurus_compact_overflow_table {
    TaurusCompactOverflowEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
} TaurusCompactOverflowTable;

/* ============================================================================
 * Encoding/Decoding Functions
 * ============================================================================ */

/* TODO 121: int32 compact pointer encode/decode for tree edges.
 *
 * Used by element.h, text.h, comment.h, cdata.h, pi.h for parent/
 * first/last/next-sibling offsets.  On macOS, ASLR can place pool-
 * resident nodes > 2GB apart, overflowing int32_t.  The encode side
 * detects overflow and registers the mapping in the global overflow
 * table keyed on `field_addr` (the address of the int32_t field
 * itself), returning INT32_MIN as a sentinel.  Decode recognises the
 * sentinel and consults the table.
 *
 * The helpers are exposed because the call sites are inlined in
 * headers; they can't reach file-static helpers in compact.c. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int32_t taurus_compact_int32_encode(void* base, void* target,
                                     const int32_t* field_addr);
void*   taurus_compact_int32_decode(void* base, int32_t off,
                                     const int32_t* field_addr);
#ifdef __cplusplus
}
#endif

/* TODO 178: 1-byte and 2-byte compact pointer encoders.
 *
 * Same overflow-table mechanism as int32, but tighter encoding for
 * nearby-pointer cases common in tree edges (text/comment/cdata/pi
 * sibling chains, attribute lists). The byte offset between base and
 * target is scaled by `1 << align_log2` before storage, so the
 * representable range with N-byte signed storage becomes:
 *
 *   1-byte, align_log2=3: ±127 * 8  = ±1016 bytes (covers small pages)
 *   2-byte, align_log2=3: ±32767 * 8 = ±262 KB   (covers any doc)
 *
 * NULL is encoded as 0 (the field's natural zero-init state, so
 * calloc'd memory is correctly NULL). INT8_MIN / INT16_MIN are
 * reserved as overflow sentinels: when encode returns one, the
 * (field_addr -> target) mapping has been registered in the global
 * overflow table; decode recognises the sentinel and looks it up.
 *
 * The base pointer is the address of the struct containing the field
 * (e.g., for `text_node->next_sibling_cp`, base is `text_node`).
 * field_addr is `&text_node->next_sibling_cp` — used as the hash key
 * so distinct fields on the same struct don't collide. */
#define TAURUS_COMPACT_PTR8_OVERFLOW  INT8_MIN
#define TAURUS_COMPACT_PTR16_OVERFLOW INT16_MIN

#ifdef __cplusplus
extern "C" {
#endif
int8_t  taurus_compact_ptr8_encode(const void* base, const void* target,
                                    int align_log2,
                                    const int8_t* field_addr);
void*   taurus_compact_ptr8_decode(const void* base, int8_t off,
                                    int align_log2,
                                    const int8_t* field_addr);

int16_t taurus_compact_ptr16_encode(const void* base, const void* target,
                                     int align_log2,
                                     const int16_t* field_addr);
void*   taurus_compact_ptr16_decode(const void* base, int16_t off,
                                     int align_log2,
                                     const int16_t* field_addr);
#ifdef __cplusplus
}
#endif

/* ============================================================================
 * Overflow Hash Table Functions
 * ============================================================================ */

/**
 * Create overflow hash table
 *
 * @param bucket_count Number of buckets (must be power of 2)
 * @return New table, or NULL on failure
 */
TaurusCompactOverflowTable* taurus_compact_overflow_table_create(size_t bucket_count);

/**
 * Destroy overflow hash table
 *
 * @param table Table to destroy
 */
void taurus_compact_overflow_table_destroy(TaurusCompactOverflowTable* table);

/**
 * Store pointer in overflow table
 *
 * @param table  Overflow table
 * @param key    Pointer to compact field
 * @param value  Actual pointer value
 * @param doc    Document that owns this entry (required)
 * @return 0 on success, -1 on failure
 */
int taurus_compact_overflow_set(TaurusCompactOverflowTable* table,
                                const void* key,
                                void* value,
                                struct taurus_document* doc);

/**
 * Cleanup all overflow entries for a specific document
 *
 * This removes all overflow table entries that were created for this document,
 * preventing stale pointers when the document's memory is freed.
 *
 * @param doc  Document whose overflow entries should be removed
 */
void taurus_compact_cleanup_document(struct taurus_document* doc);

/**
 * Load pointer from overflow table
 *
 * @param table  Overflow table
 * @param key    Pointer to compact field
 * @return Stored pointer value, or NULL if not found
 */
void* taurus_compact_overflow_get(TaurusCompactOverflowTable* table,
                                  const void* key);

/**
 * Cleanup thread-local overflow table
 *
 * This function destroys the global thread-local overflow table.
 */
void taurus_compact_cleanup(void);

/**
 * Cleanup all overflow entries for a specific document
 *
 * This removes all overflow table entries that were created for this document,
 * preventing stale pointers when the document's memory is freed.
 * Must be called BEFORE the document's memory pool is destroyed.
 *
 * @param doc  Document whose overflow entries should be removed
 */
void taurus_compact_cleanup_document(struct taurus_document* doc);

/**
 * Increment overflow table reference count
 *
 * Call this when creating a new document that might use compact pointers.
 * The overflow table will be destroyed when the reference count reaches 0.
 */
void taurus_compact_table_addref(void);

/**
 * Decrement overflow table reference count
 *
 * Call this when freeing a document. If the reference count reaches 0,
 * the overflow table will be destroyed.
 */
void taurus_compact_table_release(void);

/**
 * Set current document for overflow tracking during encoding
 *
 * @param doc  Document to set as current (NULL to clear)
 */
void taurus_compact_set_current_document(struct taurus_document* doc);

/* ============================================================================
 * Compact Header Functions
 * ============================================================================ */

/**
 * Get memory page from compact header
 *
 * @param header    Compact header
 * @param page_base Base pointer for page offset calculation
 * @return          Pointer to memory page
 */
void* taurus_compact_header_get_page(const TaurusCompactHeader* header,
                                     void* page_base);

/**
 * Initialize compact header
 *
 * @param header    Compact header to initialize
 * @param page_base Base pointer for offset calculation
 * @param flags     Node type flags
 */
void taurus_compact_header_init(TaurusCompactHeader* header,
                                void* page_base,
                                uint8_t flags);

#endif /* TAURUS_DOM_COMPACT_H */
