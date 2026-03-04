/* lib/src/dom/element.c - Element node implementation (POINTER-BASED)
 * Copyright (c) 2024, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE:
 * Direct pointers for tree navigation - no offset calculations.
 * O(1) access for all navigation operations.
 * Target: 1.0-1.2x faster than pugixml.
 *
 * Uses ptr_element (72 bytes) with direct pointers.
 * No StringView, no offset calculation - just direct pointer access.
 */

#include "element.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "doctype.h"
#include "node.h"
#include "ptr_element.h"
#include "ptr_accessor.h"
#include "../common/entities.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* ============================================================================
 * Element Creation
 * ============================================================================ */

/* Create element with StringView - converts to null-terminated string */
TaurusElement taurus_element_create_with_view(
    TaurusStringView name_view,
    TaurusMemoryPool* pool
) {
    if (taurus_sv_is_empty(&name_view) || !pool) return NULL;

    /* Allocate element from pool */
    struct ptr_element* elem = (struct ptr_element*)taurus_pool_alloc(
        pool, sizeof(struct ptr_element));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(struct ptr_element));

    /* Initialize node type - directly in struct, no base */
    elem->type = TAURUS_NODE_TYPE_ELEMENT;
    elem->frozen_version = 0;  /* frozen=0, version=0 */

    /* Convert StringView to null-terminated string */
    elem->name = taurus_sv_to_cstr_pooled(&name_view, pool);
    if (!elem->name) {
        /* Pool allocation doesn't require individual free */
        return NULL;
    }

    /* Initialize tree pointers to NULL */
    elem->parent = NULL;
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->next_sibling = NULL;
    elem->prev_sibling = NULL;
    elem->first_attr = NULL;
    elem->document = NULL;

    /* Initialize counts */
    elem->child_count = 0;
    elem->attr_count = 0;
    elem->reserved = 0;

    return elem;
}

/* Create element with in-place string (zero-copy) */
TaurusElement taurus_element_create_pooled_inplace(char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Allocate element from pool */
    struct ptr_element* elem = (struct ptr_element*)taurus_pool_alloc(
        pool, sizeof(struct ptr_element));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(struct ptr_element));

    /* Initialize node type */
    elem->type = TAURUS_NODE_TYPE_ELEMENT;
    elem->frozen_version = 0;

    /* Use the in-place string directly - zero copy! */
    elem->name = name;

    return elem;
}

/* Create element using memory pool (fast O(1) allocation) */
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Allocate element from pool */
    struct ptr_element* elem = (struct ptr_element*)taurus_pool_alloc(
        pool, sizeof(struct ptr_element));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(struct ptr_element));

    /* Initialize node type */
    elem->type = TAURUS_NODE_TYPE_ELEMENT;
    elem->frozen_version = 0;

    /* Copy string to pool */
    size_t name_len = strlen(name);
    char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
    if (!name_copy) {
        return NULL;
    }
    memcpy(name_copy, name, name_len + 1);
    elem->name = name_copy;

    return elem;
}

/* Create element with bulk allocation (optimized) */
TaurusElement taurus_element_create_fast(
    const char* name,
    size_t name_len,
    TaurusMemoryPool* pool
) {
    if (!name || name_len == 0 || !pool) return NULL;

    /* Allocate element from pool */
    struct ptr_element* elem = (struct ptr_element*)taurus_pool_alloc(
        pool, sizeof(struct ptr_element));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(struct ptr_element));

    /* Initialize node type */
    elem->type = TAURUS_NODE_TYPE_ELEMENT;
    elem->frozen_version = 0;

    /* Copy string to pool */
    char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
    if (!name_copy) {
        return NULL;
    }
    memcpy(name_copy, name, name_len);
    name_copy[name_len] = '\0';
    elem->name = name_copy;

    return elem;
}

/* Free element - pool allocated, so this is a no-op */
void taurus_element_free(TaurusElement elem) {
    /* Pool-allocated elements don't need individual free */
    /* This function exists for API compatibility only */
    (void)elem;
}

/* ============================================================================
 * Tree Navigation Setters (Direct Pointers)
 * ============================================================================ */

/* Set parent pointer */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent) {
    if (!elem) return;
    elem->parent = parent;
}

void taurus_element_set_first_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->first_child = child;
}

void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling) {
    if (!elem) return;
    elem->next_sibling = sibling;
}

/* ============================================================================
 * Attribute Access Functions
 * ============================================================================ */

/* Get first attribute from element */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem) {
    if (!elem) return NULL;
    /* ptr_element uses first_attr, need to adapt */
    return (struct taurus_attribute*)elem->first_attr;
}

/* Set first attribute in element */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr) {
    if (!elem) return;
    elem->first_attr = (struct ptr_attribute*)attr;
}

/* Get attribute count */
uint8_t taurus_element_attribute_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->attr_count;
}

/* Get attribute by index */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Walk linked list */
    struct ptr_attribute* attr = elem->first_attr;
    for (uint8_t i = 0; i < index && attr; i++) {
        attr = attr->next_attr;
    }

    return (struct taurus_attribute*)attr;
}

/* Get attribute by name - linear search */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    if (elem->attr_count == 0) return NULL;

    size_t name_len = strlen(name);

    /* Walk linked list */
    struct ptr_attribute* attr = elem->first_attr;
    while (attr) {
        if (attr->name && strcmp(attr->name, name) == 0) {
            return (struct taurus_attribute*)attr;
        }
        /* Also check StringView data if available */
        if (!attr->name && attr->name_view_data &&
            attr->name_view_length == name_len &&
            memcmp(attr->name_view_data, name, name_len) == 0) {
            return (struct taurus_attribute*)attr;
        }
        attr = attr->next_attr;
    }

    return NULL;
}

/* Get attribute by name (StringView version) */
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name) {
    if (!elem || taurus_sv_is_empty(&name)) return NULL;
    if (elem->attr_count == 0) return NULL;

    /* Walk linked list */
    struct ptr_attribute* attr = elem->first_attr;
    while (attr) {
        /* Check null-terminated name */
        if (attr->name) {
            size_t attr_len = strlen(attr->name);
            if (attr_len == name.length &&
                memcmp(attr->name, name.data, name.length) == 0) {
                return (struct taurus_attribute*)attr;
            }
        }
        /* Check StringView data */
        if (attr->name_view_data &&
            attr->name_view_length == name.length &&
            memcmp(attr->name_view_data, name.data, name.length) == 0) {
            return (struct taurus_attribute*)attr;
        }
        attr = attr->next_attr;
    }

    return NULL;
}

/* Add attribute to element */
int taurus_element_add_attribute(TaurusElement elem,
                                TaurusStringView name_view,
                                TaurusStringView value_view,
                                TaurusMemoryPool* pool) {
    if (!elem || taurus_sv_is_empty(&name_view) || !pool) return -1;

    struct ptr_attribute* attr = (struct ptr_attribute*)taurus_pool_alloc(
        pool, sizeof(struct ptr_attribute));
    if (!attr) return -1;

    /* Initialize attribute - store StringView data for zero-copy */
    attr->name_view_data = name_view.data;
    attr->name_view_length = name_view.length;
    attr->value_view_data = value_view.data;
    attr->value_view_length = value_view.length;

    /* Convert to null-terminated strings */
    attr->name = taurus_sv_to_cstr_pooled(&name_view, pool);

    /* Check if value contains entities */
    if (memchr(value_view.data, '&', value_view.length) != NULL) {
        char* decoded = taurus_decode_entities_view(&value_view, pool);
        if (decoded) {
            attr->value = decoded;
        } else {
            attr->value = taurus_sv_to_cstr_pooled(&value_view, pool);
        }
    } else {
        attr->value = taurus_sv_to_cstr_pooled(&value_view, pool);
    }

    attr->next_attr = NULL;

    /* Add to linked list */
    if (!elem->first_attr) {
        elem->first_attr = attr;
    } else {
        /* Find last attribute */
        struct ptr_attribute* last = elem->first_attr;
        while (last->next_attr) {
            last = last->next_attr;
        }
        last->next_attr = attr;
    }

    elem->attr_count++;

    return 0;
}

/* ============================================================================
 * Name and Namespace Access
 * ============================================================================ */

/* Get element name - returns local name only (pugixml compatibility) */
const char* taurus_element_get_name(TaurusElement elem) {
    if (!elem) return NULL;

    /* Type check via TaurusNode-compatible header */
    if (elem->type != TAURUS_NODE_TYPE_ELEMENT) {
        return NULL;
    }

    /* Return local name only (strip prefix if present) */
    if (elem->name) {
        const char* colon = strchr(elem->name, ':');
        if (colon) {
            return colon + 1;  /* Return part after colon */
        }
    }
    return elem->name;
}

/* Set prefix using StringView - not stored separately in ptr_element */
void taurus_element_set_prefix_view(TaurusElement elem, TaurusStringView prefix_view) {
    /* ptr_element doesn't store prefix separately - this is a no-op */
    /* Prefix is embedded in the name as "prefix:localname" */
    (void)elem;
    (void)prefix_view;
}

/* Set namespace URI using StringView - not stored in ptr_element */
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view) {
    /* ptr_element doesn't store namespace separately */
    (void)elem;
    (void)uri_view;
}

/* Get element prefix - extract from name */
const char* taurus_element_get_prefix(TaurusElement elem) {
    if (!elem || !elem->name) return NULL;

    const char* colon = strchr(elem->name, ':');
    if (!colon) return NULL;

    size_t prefix_len = colon - elem->name;
    if (prefix_len == 0) return NULL;

    /* Allocate prefix string from pool or heap */
    char* prefix_buf = NULL;
    if (elem->document && elem->document->pool) {
        prefix_buf = (char*)taurus_pool_alloc(elem->document->pool, prefix_len + 1);
    } else {
        prefix_buf = (char*)taurus_malloc(prefix_len + 1);
    }
    if (prefix_buf) {
        memcpy(prefix_buf, elem->name, prefix_len);
        prefix_buf[prefix_len] = '\0';
    }
    return prefix_buf;
}

/* Get element namespace URI - search xmlns declarations */
const char* taurus_element_get_namespace_uri(TaurusElement elem) {
    if (!elem) return NULL;

    const char* prefix = taurus_element_get_prefix(elem);
    if (!prefix) return NULL;

    /* Search for xmlns:prefix declaration */
    TaurusElement current = elem;
    while (current) {
        struct ptr_attribute* attr = current->first_attr;
        while (attr) {
            if (attr->name && strncmp(attr->name, "xmlns:", 6) == 0) {
                size_t attr_prefix_len = strlen(attr->name) - 6;
                size_t prefix_len = strlen(prefix);
                if (attr_prefix_len == prefix_len &&
                    memcmp(attr->name + 6, prefix, prefix_len) == 0) {
                    return attr->value;
                }
            }
            attr = attr->next_attr;
        }
        current = current->parent;
    }

    return NULL;
}

/* Legacy functions for C string input */
void taurus_element_set_prefix(TaurusElement elem, const char* prefix) {
    /* ptr_element doesn't store prefix separately */
    (void)elem;
    (void)prefix;
}

void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri) {
    /* ptr_element doesn't store namespace separately */
    (void)elem;
    (void)uri;
}

/* ============================================================================
 * Children manipulation
 * ============================================================================ */
void taurus_element_append_child_internal(TaurusElement elem, TaurusNode* child) {
    if (!elem || !child) return;

    if (child->type < TAURUS_NODE_TYPE_ELEMENT ||
        child->type > TAURUS_NODE_TYPE_DOCTYPE) {
        return;
    }

    if (child->type != TAURUS_NODE_TYPE_ELEMENT &&
        child->type != TAURUS_NODE_TYPE_TEXT &&
        child->type != TAURUS_NODE_TYPE_CDATA &&
        child->type != TAURUS_NODE_TYPE_COMMENT &&
        child->type != TAURUS_NODE_TYPE_PI) {
        return;
    }

    /* For element children, set up parent relationship */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        struct ptr_element* child_elem = (struct ptr_element*)child;
        child_elem->parent = elem;
        child_elem->document = elem->document;
    }

    /* Set sibling pointers */
    child->prev_sibling = (TaurusNode*)elem->last_child;
    child->next_sibling = NULL;

    if (elem->last_child) {
        /* Update last child's next_sibling */
        struct ptr_element* last = elem->last_child;
        last->next_sibling = (struct ptr_element*)child;
    } else {
        elem->first_child = (struct ptr_element*)child;
    }
    elem->last_child = (struct ptr_element*)child;

    elem->child_count++;
}

void taurus_element_prepend_child_internal(TaurusElement elem, TaurusNode* child) {
    if (!elem || !child) return;

    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        struct ptr_element* child_elem = (struct ptr_element*)child;
        child_elem->parent = elem;
        child_elem->document = elem->document;
    }

    child->prev_sibling = NULL;
    child->next_sibling = (TaurusNode*)elem->first_child;

    if (elem->first_child) {
        elem->first_child->prev_sibling = (struct ptr_element*)child;
    } else {
        elem->last_child = (struct ptr_element*)child;
    }
    elem->first_child = (struct ptr_element*)child;
    elem->child_count++;
}

/* ============================================================================
 * Child Access Functions
 * ============================================================================ */

/* Note: taurus_element_child_count is defined in element_modify.c to avoid duplicate symbols */

/* ============================================================================
 * Namespace Functions (Legacy)
 * ============================================================================ */

void taurus_element_add_namespace_deprecated(TaurusElement elem, const char* prefix, const char* uri) {
    /* Namespaces stored as xmlns:prefix attributes */
    if (!elem || !prefix || !uri) return;

    TaurusMemoryPool* pool = NULL;
    if (elem->document) {
        pool = elem->document->pool;
    }
    if (!pool) return;

    /* Create xmlns:prefix attribute name */
    size_t prefix_len = strlen(prefix);
    char* attr_name = (char*)taurus_pool_alloc(pool, prefix_len + 7);  /* "xmlns:" + prefix + NUL */
    if (!attr_name) return;

    memcpy(attr_name, "xmlns:", 6);
    memcpy(attr_name + 6, prefix, prefix_len + 1);

    /* Copy URI */
    size_t uri_len = strlen(uri);
    char* uri_copy = (char*)taurus_pool_alloc(pool, uri_len + 1);
    if (!uri_copy) return;
    memcpy(uri_copy, uri, uri_len + 1);

    taurus_element_add_attribute_pooled_inplace(elem, attr_name, uri_copy, pool);
}

void taurus_element_add_namespace_inplace(TaurusElement elem, char* prefix, char* uri, TaurusMemoryPool* pool) {
    if (!elem || !prefix || !uri || !pool) return;

    /* Create xmlns:prefix attribute name */
    size_t prefix_len = strlen(prefix);
    char* attr_name = (char*)taurus_pool_alloc(pool, prefix_len + 7);
    if (!attr_name) return;

    memcpy(attr_name, "xmlns:", 6);
    memcpy(attr_name + 6, prefix, prefix_len + 1);

    taurus_element_add_attribute_pooled_inplace(elem, attr_name, uri, pool);
}

/* taurus_namespace is defined in taurus_internal.h */

struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix, const char* uri, TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    struct taurus_namespace* ns = (struct taurus_namespace*)taurus_pool_alloc(
        pool, sizeof(struct taurus_namespace));
    if (!ns) return NULL;

    if (prefix) {
        size_t prefix_len = strlen(prefix);
        ns->prefix = (char*)taurus_pool_alloc(pool, prefix_len + 1);
        if (ns->prefix) {
            memcpy(ns->prefix, prefix, prefix_len + 1);
        }
    } else {
        ns->prefix = NULL;
    }

    if (uri) {
        size_t uri_len = strlen(uri);
        ns->uri = (char*)taurus_pool_alloc(pool, uri_len + 1);
        if (ns->uri) {
            memcpy(ns->uri, uri, uri_len + 1);
        }
    } else {
        ns->uri = NULL;
    }

    ns->next = NULL;
    return ns;
}

struct taurus_namespace* taurus_namespace_new_with_views(TaurusStringView* prefix_view, TaurusStringView* uri_view, TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    struct taurus_namespace* ns = (struct taurus_namespace*)taurus_pool_alloc(
        pool, sizeof(struct taurus_namespace));
    if (!ns) return NULL;

    if (prefix_view && !taurus_sv_is_empty(prefix_view)) {
        ns->prefix = taurus_sv_to_cstr_pooled(prefix_view, pool);
    } else {
        ns->prefix = NULL;
    }

    if (uri_view && !taurus_sv_is_empty(uri_view)) {
        ns->uri = taurus_sv_to_cstr_pooled(uri_view, pool);
    } else {
        ns->uri = NULL;
    }

    ns->next = NULL;
    return ns;
}

const char* taurus_element_lookup_namespace(TaurusElement elem, const char* prefix) {
    if (!elem) return NULL;

    /* Search for xmlns:prefix declaration */
    TaurusElement current = elem;
    while (current) {
        struct ptr_attribute* attr = current->first_attr;
        while (attr) {
            if (attr->name) {
                if (prefix && strlen(prefix) > 0) {
                    /* Look for xmlns:prefix */
                    size_t prefix_len = strlen(prefix);
                    if (strncmp(attr->name, "xmlns:", 6) == 0 &&
                        strlen(attr->name) == 6 + prefix_len &&
                        memcmp(attr->name + 6, prefix, prefix_len) == 0) {
                        return attr->value;
                    }
                } else {
                    /* Look for default namespace xmlns */
                    if (strcmp(attr->name, "xmlns") == 0) {
                        return attr->value;
                    }
                }
            }
            attr = attr->next_attr;
        }
        current = current->parent;
    }

    return NULL;
}

/* ============================================================================
 * Text Content
 * ============================================================================ */

/* Helper: Count text length recursively */
static size_t count_text_length_recursive(struct ptr_element* elem) {
    if (!elem) return 0;

    size_t len = 0;
    struct ptr_element* child = elem->first_child;
    while (child) {
        if (child->type == PTR_NODE_TYPE_TEXT || child->type == PTR_NODE_TYPE_CDATA) {
            struct ptr_text* text = (struct ptr_text*)child;
            if (text->text) {
                len += strlen(text->text);
            }
        } else if (child->type == PTR_NODE_TYPE_ELEMENT) {
            len += count_text_length_recursive(child);
        }
        child = child->next_sibling;
    }
    return len;
}

/* Helper: Copy text content recursively */
static char* copy_text_recursive(struct ptr_element* elem, char* p) {
    if (!elem) return p;

    struct ptr_element* child = elem->first_child;
    while (child) {
        if (child->type == PTR_NODE_TYPE_TEXT || child->type == PTR_NODE_TYPE_CDATA) {
            struct ptr_text* text = (struct ptr_text*)child;
            if (text->text) {
                size_t len = strlen(text->text);
                memcpy(p, text->text, len);
                p += len;
            }
        } else if (child->type == PTR_NODE_TYPE_ELEMENT) {
            p = copy_text_recursive(child, p);
        }
        child = child->next_sibling;
    }
    return p;
}

char* taurus_element_get_text_content(TaurusElement elem) {
    if (!elem) return NULL;

    /* Count total text length recursively */
    size_t total_len = count_text_length_recursive(elem);

    if (total_len == 0) return taurus_strdup("");

    /* Allocate result */
    char* result = (char*)taurus_malloc(total_len + 1);
    if (!result) return NULL;

    /* Concatenate text recursively */
    char* p = copy_text_recursive(elem, result);
    *p = '\0';

    return result;
}

/* ============================================================================
 * Document Tree Operations
 * ============================================================================ */

void taurus_element_set_document_tree(TaurusElement elem, struct taurus_document* doc) {
    if (!elem) return;

    elem->document = doc;

    /* Set document for all children */
    struct ptr_element* child = elem->first_child;
    while (child) {
        taurus_element_set_document_tree(child, doc);
        child = child->next_sibling;
    }
}

/* ============================================================================
 * Subtree Analysis
 * ============================================================================ */

void taurus_element_count_subtree(TaurusElement elem, TaurusSubtreeStats* stats) {
    if (!elem || !stats) return;

    /* Count this element */
    stats->element_count++;

    /* Count attributes */
    stats->attribute_count += elem->attr_count;

    /* Recurse into children */
    struct ptr_element* child = elem->first_child;
    while (child) {
        struct ptr_node* node = (struct ptr_node*)child;
        switch (node->type) {
            case PTR_NODE_TYPE_ELEMENT:
                taurus_element_count_subtree(child, stats);
                break;
            case PTR_NODE_TYPE_TEXT:
                stats->text_count++;
                break;
            case PTR_NODE_TYPE_COMMENT:
                stats->comment_count++;
                break;
            case PTR_NODE_TYPE_CDATA:
                stats->cdata_count++;
                break;
            case PTR_NODE_TYPE_PI:
                stats->pi_count++;
                break;
            default:
                break;
        }
        child = child->next_sibling;
    }
}
