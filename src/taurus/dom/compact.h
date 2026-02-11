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
#define TAURUS_COMPACT_PTR_OVERFLOW8  255  /* Use hash table (8-bit) */
#define TAURUS_COMPACT_PTR_OVERFLOW16 65535 /* Use hash table (16-bit) */

/* Offset ranges for compact pointers (in 4-byte units) */
#define TAURUS_COMPACT_PTR8_MIN  (-126)  /* -504 bytes */
#define TAURUS_COMPACT_PTR8_MAX  (+127)  /* +508 bytes */
#define TAURUS_COMPACT_PTR16_MIN (-65533) /* -262KB */
#define TAURUS_COMPACT_PTR16_MAX (+65534) /* +262KB */

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

/**
 * Compact pointer (1 byte)
 *
 * Stores offset to target in 4-byte units.
 * Can address ±504 bytes from base.
 *
 * Usage: For child/sibling/attribute pointers (typically close)
 */
typedef struct taurus_compact_ptr8 {
    uint8_t offset;        /* Signed offset in 4-byte units [0-254] */
} TaurusCompactPtr8;

/**
 * Extended compact pointer (2 bytes)
 *
 * Stores offset to target in 4-byte units.
 * Can address ±262KB from base.
 *
 * Usage: For parent pointer (may be far away)
 */
typedef struct taurus_compact_ptr16 {
    uint16_t offset;       /* Signed offset in 4-byte units [0-65534] */
} TaurusCompactPtr16;

/**
 * Compact string (1-3 bytes)
 *
 * Stores offset to string data in 4-byte units.
 * Uses two-part encoding: base (high bits) + data (low 7 bits).
 * Can address up to 8MB of strings inline.
 *
 * Usage: For element/attribute names and values
 */
typedef struct taurus_compact_string {
    uint16_t base;         /* High bits of offset */
    uint8_t data;          /* Low 7 bits of offset */
} TaurusCompactString;

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

/**
 * Encode a full pointer to 8-bit compact representation
 *
 * @param compact   Compact pointer to store result
 * @param target    Target pointer to encode
 * @param base      Base pointer for offset calculation
 *
 * If target fits in ±504 bytes, stores offset in compact.
 * Otherwise, stores in overflow hash table.
 */
void taurus_compact_ptr8_encode(TaurusCompactPtr8* compact,
                                void* target,
                                void* base);

/**
 * Decode 8-bit compact pointer to full pointer (FAST PATH - inline version)
 *
 * This is an optimized version that handles the common case (no overflow)
 * without branching to overflow table logic. Use this in hot paths.
 *
 * @param compact   Compact pointer to decode
 * @param base      Base pointer for offset calculation
 * @return          Decoded pointer (or NULL if compact is NULL)
 */
static inline void* taurus_compact_ptr8_decode_fast(const TaurusCompactPtr8* compact,
                                                      void* base) {
    uint8_t offset = compact->offset;
    /* NULL is encoded as 0, overflow as 255 */
    if (offset == 0 || offset == 255) return NULL;
    /* Remove bias from [1, 254] to [-126, +127] */
    int8_t offset_bias = (int8_t)offset - 127;
    /* Round down base to alignment boundary */
    uintptr_t base_aligned = (uintptr_t)base & ~(uintptr_t)(TAURUS_COMPACT_ALIGNMENT - 1);
    /* Calculate final pointer (multiply by 4 via left shift) */
    return (char*)base_aligned + ((uintptr_t)offset_bias << 2);
}

/**
 * Decode 8-bit compact pointer to full pointer
 *
 * @param compact   Compact pointer to decode
 * @param base      Base pointer for offset calculation
 * @return          Decoded pointer (or NULL if compact is NULL)
 */
void* taurus_compact_ptr8_decode(const TaurusCompactPtr8* compact,
                                  void* base);

/**
 * Encode a full pointer to 16-bit compact representation
 *
 * @param compact   Compact pointer to store result
 * @param target    Target pointer to encode
 * @param base      Base pointer for offset calculation
 *
 * If target fits in ±262KB, stores offset in compact.
 * Otherwise, stores in overflow hash table.
 */
void taurus_compact_ptr16_encode(TaurusCompactPtr16* compact,
                                 void* target,
                                 void* base);

/**
 * Decode 16-bit compact pointer to full pointer
 *
 * @param compact   Compact pointer to decode
 * @param base      Base pointer for offset calculation
 * @return          Decoded pointer (or NULL if compact is NULL)
 */
void* taurus_compact_ptr16_decode(const TaurusCompactPtr16* compact,
                                   void* base);

/**
 * Encode a string pointer to compact representation
 *
 * @param compact   Compact string to store result
 * @param str       String pointer to encode
 * @param base      Base pointer for offset calculation (string_base)
 *
 * If string fits in 8MB offset, stores offset in compact.
 * Otherwise, stores in overflow hash table.
 */
void taurus_compact_string_encode(TaurusCompactString* compact,
                                  const char* str,
                                  const char* base);

/**
 * Decode compact string to full pointer
 *
 * @param compact   Compact string to decode
 * @param base      Base pointer for offset calculation (string_base)
 * @return          Decoded string pointer (or NULL if compact is NULL)
 */
const char* taurus_compact_string_decode(const TaurusCompactString* compact,
                                         const char* base);

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
