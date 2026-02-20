/* element_dispatch.h - Accessor Dispatch Layer for Union Element Handle
 * Copyright (c) 2024, Ribose Inc.
 *
 * This provides accessor macros and inline functions that dispatch
 * to the correct implementation based on handle type (compact vs legacy).
 *
 * This is the core of the union handle abstraction - all element access
 * goes through these dispatch functions.
 */

#ifndef TAURUS_ELEMENT_DISPATCH_H
#define TAURUS_ELEMENT_DISPATCH_H

#include "element_handle.h"
#include "compact_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for legacy accessors */
struct taurus_element;
struct taurus_attribute;
struct taurus_namespace;
struct TaurusStringView;

/* Legacy accessor declarations (implemented in element.c) */
const char* legacy_element_get_name(struct taurus_element* elem);
struct taurus_element* legacy_element_get_parent(struct taurus_element* elem);
struct taurus_element* legacy_element_get_first_child(struct taurus_element* elem);
struct taurus_element* legacy_element_get_next_sibling(struct taurus_element* elem);
struct taurus_attribute* legacy_element_get_first_attribute(struct taurus_element* elem);
const char* legacy_element_get_text(struct taurus_element* elem);

/* Compact accessor declarations (implemented in compact_accessor.c) */
const char* compact_element_get_name(struct compact_element* elem, struct taurus_document* doc);
struct compact_element* compact_element_get_parent(struct compact_element* elem, struct taurus_document* doc);
struct compact_element* compact_element_get_first_child(struct compact_element* elem, struct taurus_document* doc);
struct compact_element* compact_element_get_next_sibling(struct compact_element* elem, struct taurus_document* doc);
struct compact_attribute* compact_element_get_first_attribute(struct compact_element* elem, struct taurus_document* doc);
const char* compact_element_get_text(struct compact_element* elem, struct taurus_document* doc);

/* ============================================================================
 * Compact Element Resolution
 * ============================================================================
 *
 * Convert a compact offset to a pointer using document's compact_base.
 */

/**
 * Get compact element pointer from handle
 * Returns NULL if not in compact mode or offset is 0
 */
static inline struct compact_element* taurus_dispatch_get_compact_elem(
    const TaurusElement* handle
) {
    if (!handle || !handle->doc) return NULL;
    if (!taurus_element_is_compact(handle)) return NULL;
    if (handle->u.compact.offset == 0) return NULL;

    /* Get document's compact base and add offset */
    struct taurus_document* doc = handle->doc;
    extern void* taurus_document_get_compact_base(struct taurus_document* doc);
    void* base = taurus_document_get_compact_base(doc);
    if (!base) return NULL;

    return (struct compact_element*)((char*)base + handle->u.compact.offset);
}

/**
 * Get compact base pointer from document
 * This is a helper for the dispatch layer
 */
static inline void* taurus_dispatch_get_compact_base(struct taurus_document* doc) {
    if (!doc) return NULL;
    /* Access the compact_base field added to document structure */
    extern void* taurus_document_get_compact_base(struct taurus_document* doc);
    return taurus_document_get_compact_base(doc);
}

/**
 * Calculate offset from compact base
 * Used to create compact handles from compact element pointers
 */
static inline uint32_t taurus_dispatch_ptr_to_offset(
    struct taurus_document* doc,
    void* ptr
) {
    if (!doc || !ptr) return 0;
    void* base = taurus_dispatch_get_compact_base(doc);
    if (!base) return 0;
    return (uint32_t)((char*)ptr - (char*)base);
}

/* ============================================================================
 * Dispatch Macros
 * ============================================================================
 *
 * These macros dispatch to the correct implementation based on handle type.
 * They are the primary way to access element data through the union handle.
 */

/**
 * Get element name - dispatches to correct implementation
 */
static inline const char* taurus_dispatch_get_name(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return "";

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        return elem ? compact_element_get_name(elem, handle->doc) : "";
    } else {
        return legacy_element_get_name(handle->u.legacy);
    }
}

/**
 * Get parent element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_parent(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        if (!elem) return taurus_element_handle_null();

        struct compact_element* parent = compact_element_get_parent(elem, handle->doc);
        if (!parent) return taurus_element_handle_null();

        /* Calculate offset from compact base */
        uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, parent);
        return taurus_element_handle_from_compact(offset, handle->doc);
    } else {
        struct taurus_element* parent = legacy_element_get_parent(handle->u.legacy);
        return taurus_element_handle_from_legacy(parent, handle->doc);
    }
}

/**
 * Get first child element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_first_child(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        if (!elem) return taurus_element_handle_null();

        struct compact_element* child = compact_element_get_first_child(elem, handle->doc);
        if (!child) return taurus_element_handle_null();

        uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, child);
        return taurus_element_handle_from_compact(offset, handle->doc);
    } else {
        struct taurus_element* child = legacy_element_get_first_child(handle->u.legacy);
        return taurus_element_handle_from_legacy(child, handle->doc);
    }
}

/**
 * Get next sibling element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_next_sibling(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        if (!elem) return taurus_element_handle_null();

        struct compact_element* sibling = compact_element_get_next_sibling(elem, handle->doc);
        if (!sibling) return taurus_element_handle_null();

        uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, sibling);
        return taurus_element_handle_from_compact(offset, handle->doc);
    } else {
        struct taurus_element* sibling = legacy_element_get_next_sibling(handle->u.legacy);
        return taurus_element_handle_from_legacy(sibling, handle->doc);
    }
}

/**
 * Get first attribute
 */
static inline struct taurus_attribute* taurus_dispatch_get_first_attribute(
    const TaurusElement* handle
) {
    if (taurus_element_is_null(handle)) return NULL;

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        if (!elem) return NULL;

        /* Compact attributes need special handling - return as legacy-compatible struct */
        return (struct taurus_attribute*)compact_element_get_first_attribute(elem, handle->doc);
    } else {
        return legacy_element_get_first_attribute(handle->u.legacy);
    }
}

/**
 * Get element text content
 */
static inline const char* taurus_dispatch_get_text(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return "";

    if (taurus_element_is_compact(handle)) {
        struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
        return elem ? compact_element_get_text(elem, handle->doc) : "";
    } else {
        return legacy_element_get_text(handle->u.legacy);
    }
}

/* ============================================================================
 * Lazy Conversion
 * ============================================================================
 *
 * Convert compact element to legacy on first modification.
 * This is called by modification functions (set_attribute, append_child, etc.)
 */

/**
 * Ensure element is in legacy mode for modification
 * If element is compact, converts it to legacy mode
 * Returns legacy pointer, or NULL on error
 */
struct taurus_element* taurus_element_ensure_legacy(TaurusElement* handle);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ELEMENT_DISPATCH_H */
