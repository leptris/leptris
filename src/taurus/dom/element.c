/* lib/src/dom/element.c - Element node implementation (POINTER-BASED)
 * Copyright (c) 2024, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE:
 * Direct pointers for tree navigation - no offset calculations.
 * O(1) access for all navigation operations.
 * Target: 1.0-1.2x faster than pugixml.
 */

#include "element.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "doctype.h"
#include "node.h"
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

/* Create element with StringView (true zero-copy!) */
TaurusElement taurus_element_create_with_view(
    TaurusStringView name_view,
    TaurusMemoryPool* pool
) {
    if (taurus_sv_is_empty(&name_view) || !pool) return NULL;

    /* Allocate element from pool */
    TaurusElement elem = (TaurusElement)taurus_pool_alloc(pool, sizeof(struct taurus_element));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(struct taurus_element));

    /* Initialize base node */
    elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
    elem->base.frozen = 0;
    elem->base.version = 0;
    elem->base.next_sibling = NULL;
    elem->base.prev_sibling = NULL;

    /* Store StringViews - ZERO COPY! */
    elem->name_view = name_view;
    elem->prefix_view = taurus_sv_empty();
    elem->namespace_uri_view = taurus_sv_empty();

    /* Initialize cached strings as NULL (lazy conversion) */
    elem->name = NULL;
    elem->prefix = NULL;
    elem->namespace_uri = NULL;

    /* Initialize tree pointers to NULL */
    elem->parent = NULL;
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->next_sibling = NULL;
    elem->prev_sibling = NULL;
    elem->first_attribute = NULL;
    elem->last_attribute = NULL;

    /* Initialize namespace and document */
    elem->namespaces = NULL;
    elem->document = NULL;

    /* Initialize counts */
    elem->attr_count = 0;
    elem->child_count = 0;

    return elem;
}

/* Create element with in-place string (zero-copy) */
TaurusElement taurus_element_create_pooled_inplace(char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Create StringView from in-place string */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    return taurus_element_create_with_view(name_view, pool);
}

/* Create element using memory pool (fast O(1) allocation) */
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Create StringView from C string */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    return taurus_element_create_with_view(name_view, pool);
}

/* Create element with bulk allocation (optimized) */
TaurusElement taurus_element_create_fast(
    const char* name,
    size_t name_len,
    TaurusMemoryPool* pool
) {
    if (!name || name_len == 0 || !pool) return NULL;

    /* Create StringView */
    TaurusStringView name_view;
    name_view.data = (char*)name;
    name_view.length = name_len;

    return taurus_element_create_with_view(name_view, pool);
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
    elem->first_child = (struct taurus_node*)child;
}

void taurus_element_set_last_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->last_child = (struct taurus_node*)child;
}

void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling) {
    if (!elem) return;
    elem->next_sibling = (struct taurus_node*)sibling;
    /* Also update base node sibling pointer for generic navigation */
    elem->base.next_sibling = sibling ? (TaurusNode*)sibling : NULL;
}

/* ============================================================================
 * Attribute Hash Table Functions (O(1) lookup)
 * ============================================================================ */

/* FNV-1a hash function for attribute names */
uint32_t attr_hash_name(const char* name, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)name[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Hash table size lookup table (powers of 2 for fast modulo) */
static const uint8_t ATTR_HASH_SIZES[] = {0, 0, 0, 0, 0, 8, 16, 16, 32, 32, 64, 64, 64, 64, 128, 128};

/* Create hash table for attribute lookup */
int create_attr_hash_table(TaurusElement elem, TaurusMemoryPool* pool) {
    if (!elem || !pool) return -1;

    uint8_t size;
    if (elem->attr_count < 16) {
        size = ATTR_HASH_SIZES[elem->attr_count];
    } else {
        size = 32;
        while (size < elem->attr_count * 2 && size < 128) {
            size *= 2;
        }
    }

    if (size == 0) return -1;

    size_t alloc_size = size * sizeof(struct taurus_attr_hash_entry*);
    struct taurus_attr_hash_entry** table = (struct taurus_attr_hash_entry**)taurus_pool_alloc(pool, alloc_size);
    if (!table) return -1;

    memset(table, 0, alloc_size);

    elem->attr_hash = table;
    elem->attr_hash_size = size;

    return 0;
}

/* Add attribute to hash table */
int add_attr_to_hash(TaurusElement elem, struct taurus_attribute* attr, TaurusMemoryPool* pool) {
    if (!elem || !attr || !elem->attr_hash) return -1;

    const char* name = attr->name ? attr->name : attr->name_view.data;
    size_t name_len = attr->name ? strlen(attr->name) : attr->name_view.length;
    if (!name || name_len == 0) return -1;

    uint32_t hash = attr_hash_name(name, name_len);
    uint32_t bucket = hash & (elem->attr_hash_size - 1);

    struct taurus_attr_hash_entry* entry = (struct taurus_attr_hash_entry*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attr_hash_entry));
    if (!entry) return -1;

    entry->attr = attr;
    entry->next = elem->attr_hash[bucket];
    elem->attr_hash[bucket] = entry;

    return 0;
}

/* Remove attribute from hash table */
int remove_attr_from_hash(TaurusElement elem, const char* name) {
    if (!elem || !name || !elem->attr_hash) return -1;

    size_t name_len = strlen(name);
    if (name_len == 0) return -1;

    uint32_t hash = attr_hash_name(name, name_len);
    uint32_t bucket = hash & (elem->attr_hash_size - 1);

    struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
    struct taurus_attr_hash_entry* prev = NULL;

    while (entry) {
        const char* entry_name = entry->attr->name ? entry->attr->name : entry->attr->name_view.data;
        size_t entry_len = entry->attr->name ? strlen(entry->attr->name) : entry->attr->name_view.length;

        if (entry_len == name_len && memcmp(entry_name, name, name_len) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                elem->attr_hash[bucket] = entry->next;
            }
            return 0;
        }

        prev = entry;
        entry = entry->next;
    }

    return -1;
}

/* Rebuild hash table after attribute addition */
static int rebuild_attr_hash_table(TaurusElement elem, TaurusMemoryPool* pool) {
    if (!elem || !pool) return -1;

    int result = create_attr_hash_table(elem, pool);
    if (result != 0) return result;

    struct taurus_attribute* attr = elem->first_attribute;
    while (attr) {
        if (add_attr_to_hash(elem, attr, pool) != 0) {
            return -1;
        }
        attr = attr->next;
    }

    return 0;
}

/* ============================================================================
 * Attribute Access Functions
 * ============================================================================ */

/* Get first attribute from element */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem) {
    if (!elem) return NULL;
    return elem->first_attribute;
}

/* Set first attribute in element */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr) {
    if (!elem) return;
    elem->first_attribute = attr;
}

/* Get attribute count */
uint8_t taurus_element_attribute_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->attr_count;
}

/* Get attribute by index */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Fast path: inline array for first 4 attributes */
    if (index < 4 && elem->children[index]) {
        /* Note: using children array for inline attrs in this simplified version */
    }

    /* Walk linked list */
    struct taurus_attribute* attr = elem->first_attribute;
    for (uint8_t i = 0; i < index && attr; i++) {
        attr = attr->next;
    }

    return attr;
}

/* Get attribute by name - O(1) with hash table */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    if (elem->attr_count == 0) return NULL;

    size_t name_len = strlen(name);

    /* Use hash table if available */
    if (elem->attr_hash && elem->attr_hash_size > 0) {
        uint32_t hash = attr_hash_name(name, name_len);
        uint32_t bucket = hash & (elem->attr_hash_size - 1);

        struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
        while (entry) {
            struct taurus_attribute* attr = entry->attr;
            if (attr->name && strcmp(attr->name, name) == 0) {
                return attr;
            }
            if (!taurus_sv_is_empty(&attr->name_view) &&
                attr->name_view.length == name_len &&
                memcmp(attr->name_view.data, name, name_len) == 0) {
                return attr;
            }
            entry = entry->next;
        }
        return NULL;
    }

    /* Walk linked list */
    struct taurus_attribute* attr = elem->first_attribute;
    while (attr) {
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr;
        }
        if (!taurus_sv_is_empty(&attr->name_view) &&
            attr->name_view.length == name_len &&
            memcmp(attr->name_view.data, name, name_len) == 0) {
            return attr;
        }
        attr = attr->next;
    }

    return NULL;
}

/* Get attribute by name (StringView version) */
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name) {
    if (!elem || taurus_sv_is_empty(&name)) return NULL;
    if (elem->attr_count == 0) return NULL;

    /* Use hash table if available */
    if (elem->attr_hash && elem->attr_hash_size > 0) {
        uint32_t hash = attr_hash_name(name.data, name.length);
        uint32_t bucket = hash & (elem->attr_hash_size - 1);

        struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
        while (entry) {
            struct taurus_attribute* attr = entry->attr;
            if (taurus_sv_equals(&attr->name_view, &name)) {
                return attr;
            }
            entry = entry->next;
        }
        return NULL;
    }

    /* Walk linked list */
    struct taurus_attribute* attr = elem->first_attribute;
    while (attr) {
        if (taurus_sv_equals(&attr->name_view, &name)) {
            return attr;
        }
        attr = attr->next;
    }

    return NULL;
}

/* Add attribute to element */
int taurus_element_add_attribute(TaurusElement elem,
                                TaurusStringView name_view,
                                TaurusStringView value_view,
                                TaurusMemoryPool* pool) {
    if (!elem || taurus_sv_is_empty(&name_view) || !pool) return -1;

    struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attribute));
    if (!attr) return -1;

    /* Initialize attribute */
    attr->name_view = name_view;
    attr->value_view = value_view;

    attr->namespace_uri_view = taurus_sv_empty();
    attr->namespace_uri = NULL;
    attr->prefix_view = taurus_sv_empty();
    attr->prefix = NULL;

    /* Eager string conversion */
    attr->name = taurus_sv_to_cstr_pooled(&name_view, pool);

    /* Check if value contains entities */
    if (memchr(value_view.data, '&', value_view.length) != NULL) {
        char* decoded = taurus_decode_entities_view(&value_view, pool);
        if (decoded) {
            attr->value = decoded;
            attr->has_entities = 0;
        } else {
            attr->value = taurus_sv_to_cstr_pooled(&value_view, pool);
            attr->has_entities = 1;
        }
    } else {
        attr->value = taurus_sv_to_cstr_pooled(&value_view, pool);
        attr->has_entities = 0;
    }

    attr->next = NULL;

    /* Add to linked list */
    if (!elem->first_attribute) {
        elem->first_attribute = attr;
        elem->last_attribute = attr;
    } else {
        elem->last_attribute->next = attr;
        elem->last_attribute = attr;
    }

    elem->attr_count++;

    /* Create/update hash table when we exceed 4 attributes */
    if (elem->attr_count == 5) {
        if (create_attr_hash_table(elem, pool) == 0) {
            struct taurus_attribute* a = elem->first_attribute;
            while (a) {
                add_attr_to_hash(elem, a, pool);
                a = a->next;
            }
        }
    } else if (elem->attr_count > 5) {
        if (elem->attr_hash) {
            add_attr_to_hash(elem, attr, pool);

            if (elem->attr_count > elem->attr_hash_size &&
                elem->attr_hash_size < 64) {
                rebuild_attr_hash_table(elem, pool);
            }
        }
    }

    return 0;
}

/* ============================================================================
 * Name and Namespace Access
 * ============================================================================ */

/* Get element name (lazy conversion from StringView) */
const char* taurus_element_get_name(TaurusElement elem) {
    if (!elem) return NULL;

    TaurusNode* node = (TaurusNode*)elem;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) {
        return NULL;
    }

    /* Return cached string if available */
    if (elem->name) return elem->name;

    /* Convert StringView to C string */
    if (!taurus_sv_is_empty(&elem->name_view)) {
        elem->name = taurus_sv_to_cstr(&elem->name_view);
        return elem->name;
    }

    return NULL;
}

/* Set prefix using StringView */
void taurus_element_set_prefix_view(TaurusElement elem, TaurusStringView prefix_view) {
    if (!elem) return;
    elem->prefix_view = prefix_view;
    if (elem->prefix) {
        free(elem->prefix);
        elem->prefix = NULL;
    }
}

/* Set namespace URI using StringView */
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view) {
    if (!elem) return;
    elem->namespace_uri_view = uri_view;
    if (elem->namespace_uri) {
        free(elem->namespace_uri);
        elem->namespace_uri = NULL;
    }
}

/* Get element prefix */
const char* taurus_element_get_prefix(TaurusElement elem) {
    if (!elem) return NULL;

    if (elem->prefix) return elem->prefix;

    const char* name = taurus_element_get_name(elem);
    if (!name) return NULL;

    const char* colon = strchr(name, ':');
    if (!colon) return NULL;

    size_t prefix_len = colon - name;
    if (prefix_len == 0) return NULL;

    char* prefix_buf = NULL;
    if (elem->document && elem->document->pool) {
        prefix_buf = (char*)taurus_pool_alloc(elem->document->pool, prefix_len + 1);
    } else {
        prefix_buf = (char*)taurus_malloc(prefix_len + 1);
    }
    if (prefix_buf) {
        memcpy(prefix_buf, name, prefix_len);
        prefix_buf[prefix_len] = '\0';
        elem->prefix = prefix_buf;
    }
    return elem->prefix;
}

/* Get element namespace URI */
const char* taurus_element_get_namespace_uri(TaurusElement elem) {
    if (!elem) return NULL;

    if (elem->namespace_uri) return elem->namespace_uri;

    const char* prefix = taurus_element_get_prefix(elem);
    if (prefix) {
        TaurusElement current = elem;
        while (current) {
            struct taurus_attribute* attr = taurus_element_get_first_attribute(current);
            while (attr) {
                if (attr->name_view.length > 6 &&
                    memcmp(attr->name_view.data, "xmlns:", 6) == 0) {
                    size_t attr_prefix_len = attr->name_view.length - 6;
                    size_t prefix_len = strlen(prefix);
                    if (attr_prefix_len == prefix_len &&
                        memcmp(attr->name_view.data + 6, prefix, prefix_len) == 0) {
                        if (elem->document && elem->document->pool) {
                            elem->namespace_uri = taurus_sv_to_cstr_pooled(&attr->value_view, elem->document->pool);
                        } else {
                            elem->namespace_uri = taurus_sv_to_cstr(&attr->value_view);
                        }
                        return elem->namespace_uri;
                    }
                }
                attr = attr->next;
            }
            current = taurus_element_get_parent(current);
        }
    }

    return NULL;
}

/* Legacy functions for C string input */
void taurus_element_set_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return;

    if (elem->prefix) free(elem->prefix);
    elem->prefix = prefix ? taurus_strdup(prefix) : NULL;
    elem->prefix_view = taurus_sv_empty();
}

void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri) {
    if (!elem) return;

    if (elem->namespace_uri) free(elem->namespace_uri);
    elem->namespace_uri = uri ? taurus_strdup(uri) : NULL;
    elem->namespace_uri_view = taurus_sv_empty();
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
        TaurusElement child_elem = (TaurusElement)child;
        child_elem->parent = elem;
        child_elem->document = elem->document;
    }

    /* Set sibling pointers */
    child->prev_sibling = elem->last_child;
    child->next_sibling = NULL;

    if (elem->last_child) {
        elem->last_child->next_sibling = child;
    } else {
        elem->first_child = child;
    }
    elem->last_child = child;

    /* Update inline array for first 4 children */
    if (elem->child_count < 4) {
        elem->children[elem->child_count] = child;
    }
    elem->child_count++;
}

void taurus_element_prepend_child_internal(TaurusElement elem, TaurusNode* child) {
    if (!elem || !child) return;

    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;
        child_elem->parent = elem;
        child_elem->document = elem->document;
    }

    child->prev_sibling = NULL;
    child->next_sibling = elem->first_child;

    if (elem->first_child) {
        elem->first_child->prev_sibling = child;
    } else {
        elem->last_child = child;
    }
    elem->first_child = child;
    elem->child_count++;
}

/* ============================================================================
 * Child Access Functions
 * ============================================================================ */
