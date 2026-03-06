/* lib/src/dom/element.h - Element node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE:
 * Uses ptr_element directly - no wrapper structures.
 * Target: 1.0-1.2x faster than pugixml in all operations.
 *
 * Key features:
 * - TaurusElement is ptr_element* - direct access, no conversion
 * - Direct pointers for O(1) tree navigation
 * - Null-terminated strings (like pugixml)
 * - Pool allocation for O(1) creation
 */

#ifndef TAURUS_DOM_ELEMENT_H
#define TAURUS_DOM_ELEMENT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ptr_element.h"
#include "ptr_accessor.h"
#include "node.h"
#include "../common/string_view.h"
#include "../memory/pool.h"

/* Forward declarations */
struct taurus_document;
struct taurus_namespace;

/* ============================================================================
 * Attribute Structure (~32 bytes)
 * ============================================================================ */

struct taurus_attribute {
    /* StringView storage (zero-copy, points into XML buffer) */
    TaurusStringView name_view;
    TaurusStringView value_view;
    TaurusStringView prefix_view;
    TaurusStringView namespace_uri_view;

    /* Cached NULL-terminated strings (lazy conversion) */
    char* name;
    char* value;
    char* prefix;
    char* namespace_uri;

    /* Next attribute in linked list */
    struct taurus_attribute* next;

    /* Pre-computed entity flag */
    unsigned char has_entities;
};

/* ============================================================================
 * Attribute Hash Entry (for O(1) lookup when modifying)
 * ============================================================================ */

struct taurus_attr_hash_entry {
    struct taurus_attribute* attr;
    struct taurus_attr_hash_entry* next;
};

/* ============================================================================
 * Element Creation
 * ============================================================================ */

TaurusElement taurus_element_create_with_view(TaurusStringView name_view, TaurusMemoryPool* pool);
TaurusElement taurus_element_create_pooled_inplace(char* name, TaurusMemoryPool* pool);
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool);
TaurusElement taurus_element_create_fast(const char* name, size_t name_len, TaurusMemoryPool* pool);
void taurus_element_free(TaurusElement elem);

/* ============================================================================
 * Hot Accessor Functions (static inline for performance)
 * NOW USING ptr_element DIRECTLY - NO CONVERSION OVERHEAD!
 * ============================================================================ */

/* Get parent element - O(1) direct pointer */
static inline TaurusElement taurus_element_get_parent(TaurusElement elem) {
    return elem ? elem->parent : NULL;
}

/* Get first child element - skips non-element nodes */
static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    return ptr_element_get_first_child(elem);
}

/* Get next sibling element - skips non-element nodes */
static inline TaurusElement taurus_element_get_next_sibling(TaurusElement elem) {
    return ptr_element_get_next_sibling(elem);
}

/* Get child count - O(n) walk */
size_t taurus_element_child_count(TaurusElement elem);

/* ============================================================================
 * StringView Accessors (for compatibility)
 * NOTE: ptr_element uses C strings, so we create temporary StringViews
 * ============================================================================ */

static inline TaurusStringView taurus_element_name_view(TaurusElement elem) {
    TaurusStringView sv = taurus_sv_empty();
    if (elem && elem->name) {
        sv.data = elem->name;
        sv.length = strlen(elem->name);
    }
    return sv;
}

static inline TaurusStringView taurus_element_prefix_view(TaurusElement elem) {
    /* ptr_element doesn't store prefix separately - return empty */
    (void)elem;
    return taurus_sv_empty();
}

static inline TaurusStringView taurus_element_namespace_view(TaurusElement elem) {
    /* ptr_element doesn't store namespace separately - return empty */
    (void)elem;
    return taurus_sv_empty();
}

static inline TaurusStringView taurus_attribute_name_view(const struct taurus_attribute* attr) {
    TaurusStringView sv = taurus_sv_empty();
    if (attr && attr->name) {
        sv.data = attr->name;
        sv.length = strlen(attr->name);
    }
    return sv;
}

static inline TaurusStringView taurus_attribute_value_view(const struct taurus_attribute* attr) {
    TaurusStringView sv = taurus_sv_empty();
    if (attr && attr->value) {
        sv.data = attr->value;
        sv.length = strlen(attr->value);
    }
    return sv;
}

/* Fast name comparison */
static inline int taurus_element_name_equals(TaurusElement elem, TaurusStringView name) {
    if (!elem || !elem->name) return 0;
    return strlen(elem->name) == name.length &&
           memcmp(elem->name, name.data, name.length) == 0;
}

static inline int taurus_element_name_equals_lit(TaurusElement elem, const char* lit) {
    if (!elem || !elem->name || !lit) return 0;
    return strcmp(elem->name, lit) == 0;
}

/* ============================================================================
 * Tree Navigation Setters
 * ============================================================================ */

void taurus_element_set_parent(TaurusElement elem, TaurusElement parent);
void taurus_element_set_first_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling);

/* ============================================================================
 * Attribute Access Functions
 * ============================================================================ */

struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem);
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr);
uint8_t taurus_element_attribute_count(TaurusElement elem);
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index);
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name);
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name);

int taurus_element_add_attribute(TaurusElement elem,
                                TaurusStringView name_view,
                                TaurusStringView value_view,
                                TaurusMemoryPool* pool);

/* ============================================================================
 * Name and Namespace Access
 * ============================================================================ */

const char* taurus_element_get_name(TaurusElement elem);
void taurus_element_set_prefix_view(TaurusElement elem, TaurusStringView prefix_view);
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view);
const char* taurus_element_get_prefix(TaurusElement elem);
const char* taurus_element_get_namespace_uri(TaurusElement elem);
void taurus_element_set_prefix(TaurusElement elem, const char* prefix);
void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri);

/* ============================================================================
 * Legacy Public API Functions
 *
 * DEPRECATED: These functions are kept for backward compatibility only.
 * Use the standard functions (taurus_element_set_attribute, etc.) instead.
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
#define TAURUS_DEPRECATED __attribute__((deprecated))
#elif defined(_MSC_VER)
#define TAURUS_DEPRECATED __declspec(deprecated)
#else
#define TAURUS_DEPRECATED
#endif

TAURUS_DEPRECATED void taurus_element_add_attribute_legacy(TaurusElement elem, const char* name, const char* value);
TAURUS_DEPRECATED void taurus_element_add_attribute_pooled(TaurusElement elem, const char* name, const char* value, TaurusMemoryPool* pool);
TAURUS_DEPRECATED void taurus_element_add_attribute_pooled_inplace(TaurusElement elem, char* name, char* value, TaurusMemoryPool* pool);
TAURUS_DEPRECATED const char* taurus_element_get_attribute_legacy(TaurusElement elem, const char* name);

TAURUS_DEPRECATED void taurus_element_add_namespace_deprecated(TaurusElement elem, const char* prefix, const char* uri);
TAURUS_DEPRECATED void taurus_element_add_namespace_inplace(TaurusElement elem, char* prefix, char* uri, TaurusMemoryPool* pool);

struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix, const char* uri, TaurusMemoryPool* pool);
struct taurus_namespace* taurus_namespace_new_with_views(TaurusStringView* prefix_view, TaurusStringView* uri_view, TaurusMemoryPool* pool);

const char* taurus_element_lookup_namespace(TaurusElement elem, const char* prefix);

/* Children manipulation */
void taurus_element_append_child_internal(TaurusElement elem, TaurusNode* child);
void taurus_element_prepend_child_internal(TaurusElement elem, TaurusNode* child);

/* Bulk allocation for subtree copy */
TaurusElement taurus_element_append_copy_bulk(TaurusElement parent, TaurusElement source);

/* Text content extraction */
char* taurus_element_get_text_content(TaurusElement elem);

/* Document tree operations */
void taurus_element_set_document_tree(TaurusElement elem, struct taurus_document* doc);

/* ============================================================================
 * Subtree Analysis
 * ============================================================================ */

typedef struct taurus_subtree_stats {
    uint32_t element_count;
    uint32_t attribute_count;
    uint32_t text_count;
    uint32_t comment_count;
    uint32_t cdata_count;
    uint32_t pi_count;
} TaurusSubtreeStats;

void taurus_element_count_subtree(TaurusElement elem, TaurusSubtreeStats* stats);

/* ============================================================================
 * Casting Helpers
 * ============================================================================ */

#define TAURUS_NODE_AS_ELEMENT(node) \
    (TAURUS_NODE_IS_ELEMENT(node) ? (TaurusElement)(node) : NULL)

#define TAURUS_ELEMENT_AS_NODE(elem) \
    ((TaurusNode*)(elem))

#endif /* TAURUS_DOM_ELEMENT_H */
