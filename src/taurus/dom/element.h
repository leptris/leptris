/* lib/src/dom/element.h - Element node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE:
 * Uses direct pointers for tree navigation - no offset calculations.
 * Target: 1.0-1.2x faster than pugixml in all operations.
 *
 * Key features:
 * - Direct pointers for O(1) tree navigation
 * - Null-terminated strings (like pugixml)
 * - Pool allocation for O(1) creation
 * - Minimal memory footprint
 */

#ifndef TAURUS_DOM_ELEMENT_H
#define TAURUS_DOM_ELEMENT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
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
 * Attribute Hash Entry (for O(1) lookup)
 * ============================================================================ */

struct taurus_attr_hash_entry {
    struct taurus_attribute* attr;
    struct taurus_attr_hash_entry* next;
};

/* ============================================================================
 * Element Structure - POINTER-ONLY (no offsets)
 * ============================================================================ */

struct taurus_element {
    /* Base node (20 bytes) - MUST be first */
    TaurusNode base;

    /* StringView storage for zero-copy parsing (48 bytes) */
    TaurusStringView name_view;
    TaurusStringView prefix_view;
    TaurusStringView namespace_uri_view;

    /* Cached NULL-terminated strings (24 bytes) */
    char* name;
    char* prefix;
    char* namespace_uri;

    /* Tree navigation - DIRECT POINTERS ONLY */
    struct taurus_element* parent;
    struct taurus_node* first_child;
    struct taurus_node* last_child;
    struct taurus_node* next_sibling;
    struct taurus_node* prev_sibling;    /* For reverse iteration */

    /* Children array for O(1) index access */
    struct taurus_node* children[4];

    /* Attributes */
    struct taurus_attribute* first_attribute;
    struct taurus_attribute* last_attribute;

    /* Attribute hash table for O(1) lookup (when attr_count > 4) */
    struct taurus_attr_hash_entry** attr_hash;
    uint8_t attr_hash_size;

    /* Namespace and document */
    struct taurus_namespace* namespaces;
    struct taurus_document* document;

    /* Counts */
    uint16_t child_count;
    uint8_t attr_count;
};

/* Public API type */
typedef struct taurus_element* TaurusElement;

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
 * ============================================================================ */

/* Get parent element - O(1) direct pointer */
static inline TaurusElement taurus_element_get_parent(TaurusElement elem) {
    if (!elem) return NULL;
    return (TaurusElement)elem->parent;
}

/* Get first child element - O(1) direct pointer, skips non-element nodes */
static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* Walk through children until we find an element */
    struct taurus_node* child = elem->first_child;
    while (child && child->type != TAURUS_NODE_TYPE_ELEMENT) {
        child = child->next_sibling;
    }
    return (TaurusElement)child;
}

/* Get last child element - O(1) direct pointer, skips non-element nodes */
static inline TaurusElement taurus_element_get_last_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* Walk backwards through children until we find an element */
    struct taurus_node* child = elem->last_child;
    while (child && child->type != TAURUS_NODE_TYPE_ELEMENT) {
        child = child->prev_sibling;
    }
    return (TaurusElement)child;
}

/* Get next sibling element - O(1) direct pointer, skips non-element nodes */
static inline TaurusElement taurus_element_get_next_sibling(TaurusElement elem) {
    if (!elem) return NULL;

    struct taurus_node* sibling = (struct taurus_node*)elem;
    sibling = sibling->next_sibling;
    while (sibling && sibling->type != TAURUS_NODE_TYPE_ELEMENT) {
        sibling = sibling->next_sibling;
    }
    return (TaurusElement)sibling;
}

/* Get previous sibling element - O(1) direct pointer, skips non-element nodes */
static inline TaurusElement taurus_element_get_prev_sibling(TaurusElement elem) {
    if (!elem) return NULL;

    struct taurus_node* sibling = (struct taurus_node*)elem;
    sibling = sibling->prev_sibling;
    while (sibling && sibling->type != TAURUS_NODE_TYPE_ELEMENT) {
        sibling = sibling->prev_sibling;
    }
    return (TaurusElement)sibling;
}

/* Get child by index - O(1) via inline array for first 4 children */
static inline TaurusElement taurus_element_get_child(TaurusElement elem, uint16_t index) {
    if (!elem || index >= elem->child_count) return NULL;

    /* Use inline array for first 4 children */
    if (index < 4 && elem->children[index]) {
        return (TaurusElement)elem->children[index];
    }

    /* Fall back to linked list traversal */
    struct taurus_node* child = elem->first_child;
    uint16_t i = 0;
    while (child && i < index) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (i == index) return (TaurusElement)child;
            i++;
        }
        child = child->next_sibling;
    }
    return (TaurusElement)child;
}

/* Get child count - implemented in element_modify.c for API compatibility */
/* Note: Not static inline because there's a public API declaration in taurus.h */
size_t taurus_element_child_count(TaurusElement elem);

/* ============================================================================
 * StringView Accessors (for performance-critical internal code)
 * ============================================================================ */

static inline TaurusStringView taurus_element_name_view(TaurusElement elem) {
    return elem ? elem->name_view : taurus_sv_empty();
}

static inline TaurusStringView taurus_element_prefix_view(TaurusElement elem) {
    return elem ? elem->prefix_view : taurus_sv_empty();
}

static inline TaurusStringView taurus_element_namespace_view(TaurusElement elem) {
    return elem ? elem->namespace_uri_view : taurus_sv_empty();
}

static inline TaurusStringView taurus_attribute_name_view(const struct taurus_attribute* attr) {
    return attr ? attr->name_view : taurus_sv_empty();
}

static inline TaurusStringView taurus_attribute_value_view(const struct taurus_attribute* attr) {
    return attr ? attr->value_view : taurus_sv_empty();
}

/* Fast name comparison */
static inline int taurus_element_name_equals(TaurusElement elem, TaurusStringView name) {
    return elem ? taurus_sv_equals(&elem->name_view, &name) : 0;
}

static inline int taurus_element_name_equals_lit(TaurusElement elem, const char* lit) {
    if (!elem || !lit) return 0;
    size_t lit_len = strlen(lit);
    return elem->name_view.length == lit_len &&
           memcmp(elem->name_view.data, lit, lit_len) == 0;
}

/* ============================================================================
 * Tree Navigation Setters
 * ============================================================================ */

void taurus_element_set_parent(TaurusElement elem, TaurusElement parent);
void taurus_element_set_first_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_last_child(TaurusElement elem, TaurusElement child);
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
 * ============================================================================ */

void taurus_element_add_attribute_legacy(TaurusElement elem, const char* name, const char* value);
void taurus_element_add_attribute_pooled(TaurusElement elem, const char* name, const char* value, TaurusMemoryPool* pool);
void taurus_element_add_attribute_pooled_inplace(TaurusElement elem, char* name, char* value, TaurusMemoryPool* pool);
const char* taurus_element_get_attribute_legacy(TaurusElement elem, const char* name);

void taurus_element_add_namespace_deprecated(TaurusElement elem, const char* prefix, const char* uri);
void taurus_element_add_namespace_inplace(TaurusElement elem, char* prefix, char* uri, TaurusMemoryPool* pool);

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
