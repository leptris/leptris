/* element_dispatch.h - Compact-Only Accessor Layer
 * Copyright (c) 2024, Ribose Inc.
 *
 * COMPACT-ONLY ARCHITECTURE:
 * This provides accessor functions that work with compact element handles.
 * All element access goes through these dispatch functions.
 */

#ifndef TAURUS_ELEMENT_DISPATCH_H
#define TAURUS_ELEMENT_DISPATCH_H

#include "element_handle.h"
#include "compact_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct taurus_attribute;
struct taurus_namespace;

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
 */

/**
 * Get compact element pointer from handle
 */
static inline struct compact_element* taurus_dispatch_get_compact_elem(
    const TaurusElement* handle
) {
    if (!handle || !handle->doc) return NULL;
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
 */
static inline void* taurus_dispatch_get_compact_base(struct taurus_document* doc) {
    if (!doc) return NULL;
    extern void* taurus_document_get_compact_base(struct taurus_document* doc);
    return taurus_document_get_compact_base(doc);
}

/**
 * Calculate offset from compact base
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
 * Dispatch Functions (Compact-Only)
 * ============================================================================
 */

/**
 * Get element name
 */
static inline const char* taurus_dispatch_get_name(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return "";
    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    return elem ? compact_element_get_name(elem, handle->doc) : "";
}

/**
 * Get parent element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_parent(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    if (!elem) return taurus_element_handle_null();

    struct compact_element* parent = compact_element_get_parent(elem, handle->doc);
    if (!parent) return taurus_element_handle_null();

    uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, parent);
    return taurus_element_handle_from_compact(offset, handle->doc);
}

/**
 * Get first child element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_first_child(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    if (!elem) return taurus_element_handle_null();

    struct compact_element* child = compact_element_get_first_child(elem, handle->doc);
    if (!child) return taurus_element_handle_null();

    uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, child);
    return taurus_element_handle_from_compact(offset, handle->doc);
}

/**
 * Get next sibling element - returns a new handle
 */
static inline TaurusElement taurus_dispatch_get_next_sibling(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return taurus_element_handle_null();

    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    if (!elem) return taurus_element_handle_null();

    struct compact_element* sibling = compact_element_get_next_sibling(elem, handle->doc);
    if (!sibling) return taurus_element_handle_null();

    uint32_t offset = taurus_dispatch_ptr_to_offset(handle->doc, sibling);
    return taurus_element_handle_from_compact(offset, handle->doc);
}

/**
 * Get first attribute
 */
static inline struct taurus_attribute* taurus_dispatch_get_first_attribute(
    const TaurusElement* handle
) {
    if (taurus_element_is_null(handle)) return NULL;

    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    if (!elem) return NULL;

    return (struct taurus_attribute*)compact_element_get_first_attribute(elem, handle->doc);
}

/**
 * Get element text content
 */
static inline const char* taurus_dispatch_get_text(const TaurusElement* handle) {
    if (taurus_element_is_null(handle)) return "";

    struct compact_element* elem = taurus_dispatch_get_compact_elem(handle);
    return elem ? compact_element_get_text(elem, handle->doc) : "";
}

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_ELEMENT_DISPATCH_H */
