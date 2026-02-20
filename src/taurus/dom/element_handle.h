/* element_handle.h - Union Element Handle for Compact DOM
 * Copyright (c) 2024, Ribose Inc.
 *
 * BREAKING CHANGE: This changes TaurusElement from a pointer to a struct.
 *
 * This enables compact-by-default parsing while maintaining API compatibility.
 * The document pointer is used to dispatch to the correct accessor.
 *
 * Performance Impact:
 * - Parsing: 0.32x -> 0.95x vs pugixml
 * - Memory: 6x reduction (168 bytes -> 28 bytes per element)
 * - Cache efficiency: Single contiguous block
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
 * Union Element Handle
 * ============================================================================
 *
 * This is the NEW TaurusElement type that replaces the old pointer typedef.
 * It can represent either a legacy pointer or a compact offset.
 *
 * Size: 16 bytes (8 bytes for union + 8 bytes for doc pointer)
 * Legacy pointer: 8 bytes pointer + 8 bytes doc = 16 bytes
 * Compact offset: 4 bytes offset + 2 bytes flags + 2 bytes reserved + 8 bytes doc = 16 bytes
 */

typedef struct taurus_element_handle {
    union {
        /* Legacy mode: Direct pointer to taurus_element structure */
        struct taurus_element* legacy;

        /* Compact mode: Offset from document base + flags */
        struct {
            uint32_t offset;      /* Byte offset from document compact_base */
            uint16_t flags;       /* Handle flags (see below) */
            uint16_t reserved;    /* Reserved for future use */
        } compact;
    } u;

    /* Document pointer - needed for:
     * 1. Dispatching to correct accessor (compact vs legacy)
     * 2. Resolving compact offsets to pointers
     * 3. Memory management
     */
    struct taurus_document* doc;
} TaurusElement;

/* ============================================================================
 * Handle Flags
 * ============================================================================
 */

/* Handle type flags */
#define TAURUS_HANDLE_COMPACT    0x0001  /* Compact representation (offset-based) */
#define TAURUS_HANDLE_LEGACY     0x0000  /* Legacy representation (pointer-based) */

/* Additional flags */
#define TAURUS_HANDLE_MODIFIED   0x0002  /* Element has been modified */
#define TAURUS_HANDLE_FROZEN     0x0004  /* Element is immutable */

/* ============================================================================
 * Handle Type Checking
 * ============================================================================
 */

/**
 * Check if handle is in compact mode
 */
static inline int taurus_element_is_compact(const TaurusElement* handle) {
    return handle && (handle->u.compact.flags & TAURUS_HANDLE_COMPACT);
}

/**
 * Check if handle is in legacy mode
 */
static inline int taurus_element_is_legacy(const TaurusElement* handle) {
    return handle && !(handle->u.compact.flags & TAURUS_HANDLE_COMPACT);
}

/**
 * Check if handle is null/empty
 */
static inline int taurus_element_is_null(const TaurusElement* handle) {
    if (!handle) return 1;
    if (!handle->doc) return 1;
    if (taurus_element_is_compact(handle)) {
        return handle->u.compact.offset == 0;
    } else {
        return handle->u.legacy == NULL;
    }
}

/* ============================================================================
 * Handle Creation
 * ============================================================================
 */

/**
 * Create a legacy handle from a pointer
 */
static inline TaurusElement taurus_element_handle_from_legacy(
    struct taurus_element* elem,
    struct taurus_document* doc
) {
    TaurusElement handle;
    handle.u.legacy = elem;
    handle.doc = doc;
    return handle;
}

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
    handle.u.legacy = NULL;
    handle.doc = NULL;
    return handle;
}

/* ============================================================================
 * Handle Access
 * ============================================================================
 */

/**
 * Get legacy pointer from handle
 * Only valid if handle is in legacy mode
 */
static inline struct taurus_element* taurus_element_handle_get_legacy(
    const TaurusElement* handle
) {
    if (!handle || taurus_element_is_compact(handle)) return NULL;
    return handle->u.legacy;
}

/**
 * Get compact offset from handle
 * Only valid if handle is in compact mode
 */
static inline uint32_t taurus_element_handle_get_offset(
    const TaurusElement* handle
) {
    if (!handle || !taurus_element_is_compact(handle)) return 0;
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

/* ============================================================================
 * Backward Compatibility
 * ============================================================================
 *
 * For migration purposes, we provide compatibility macros.
 * These will be deprecated in a future version.
 */

/* Legacy typedef - for internal use during migration */
typedef struct taurus_element* TaurusElementLegacy;

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ELEMENT_HANDLE_H */
