/* element_handle.h - Compact-Only Element Handle
 * Copyright (c) 2024, Ribose Inc.
 *
 * COMPACT-ONLY ARCHITECTURE:
 * This is the TaurusElement type - a compact offset-based handle.
 * No legacy pointer mode - single clean architecture.
 *
 * Size: 16 bytes (4 bytes offset + 2 bytes flags + 2 bytes reserved + 8 bytes doc)
 * Memory: 6x reduction vs legacy (168 bytes -> 28 bytes per element)
 * Cache efficiency: Single contiguous block
 */

#ifndef TAURUS_ELEMENT_HANDLE_H
#define TAURUS_ELEMENT_HANDLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct taurus_element;
struct taurus_document;

/* ============================================================================
 * Compact Element Handle (Compact-Only)
 * ============================================================================
 *
 * This is the TaurusElement type - offset-based for compact representation.
 * Size: 16 bytes total
 */

typedef struct taurus_element_handle {
    union {
        /* Compact mode: Offset from document base + flags */
        struct {
            uint32_t offset;      /* Byte offset from document compact_base */
            uint16_t flags;       /* Handle flags (see below) */
            uint16_t reserved;    /* Reserved for future use */
        } compact;
    } u;

    /* Document pointer - needed for:
     * 1. Resolving compact offsets to pointers
     * 2. Memory management
     */
    struct taurus_document* doc;
} TaurusElement;

/* ============================================================================
 * Handle Flags
 * ============================================================================
 */

#define TAURUS_HANDLE_COMPACT    0x0001  /* Always set for compact */
#define TAURUS_HANDLE_MODIFIED   0x0002  /* Element has been modified */
#define TAURUS_HANDLE_FROZEN     0x0004  /* Element is immutable */

/* ============================================================================
 * Handle Type Checking (Compact-Only)
 * ============================================================================
 */

/**
 * Check if handle is null/empty
 */
static inline int taurus_element_is_null(const TaurusElement* handle) {
    if (!handle) return 1;
    if (!handle->doc) return 1;
    return handle->u.compact.offset == 0;
}

/* ============================================================================
 * Handle Creation
 * ============================================================================
 */

/**
 * Create a compact handle from an offset
 */
static inline TaurusElement taurus_element_handle_from_compact(
    uint32_t offset,
    struct taurus_document* doc
) {
    TaurusElement handle;
    handle.u.compact.offset = offset;
    handle.u.compact.flags = TAURUS_HANDLE_COMPACT;
    handle.u.compact.reserved = 0;
    handle.doc = doc;
    return handle;
}

/**
 * Create a null handle
 */
static inline TaurusElement taurus_element_handle_null(void) {
    TaurusElement handle;
    handle.u.compact.offset = 0;
    handle.u.compact.flags = 0;
    handle.u.compact.reserved = 0;
    handle.doc = NULL;
    return handle;
}

/* ============================================================================
 * Handle Access
 * ============================================================================
 */

/**
 * Get compact offset from handle
 */
static inline uint32_t taurus_element_handle_get_offset(
    const TaurusElement* handle
) {
    if (!handle) return 0;
    return handle->u.compact.offset;
}

/**
 * Get document from handle
 */
static inline struct taurus_document* taurus_element_handle_get_doc(
    const TaurusElement* handle
) {
    return handle ? handle->doc : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ELEMENT_HANDLE_H */
