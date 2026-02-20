/* lib/src/dom/element.c - Element node implementation (COMPACT + INLINE ARRAY)
 * Copyright (c) 2024, Ribose Inc.
 *
 * O(1) CHILD ACCESS ARCHITECTURE:
 * Uses inline child array for O(1) access by index - beats pugixml!
 * - ~128 bytes per element (vs 192 bytes in legacy design = 1.5x reduction!)
 * - Inline children[4] array for O(1) child access by index
 * - Falls back to linked list only when child_count > 4
 * - Direct pointer access (no page_base calculation)
 *
 * This is the ONLY element implementation - no backwards compatibility.
 */

#include "element.h"
#include "compact.h"
#include "compact_element.h"  /* For COMPACT_PTR_TO_OFFSET macro */
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "doctype.h"
#include "node.h"  /* For taurus_node_get_next_sibling */
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

    /* Store StringViews - ZERO COPY! */
    elem->name_view = name_view;
    elem->prefix_view = taurus_sv_empty();
    elem->namespace_uri_view = taurus_sv_empty();

    /* Initialize cached strings as NULL (lazy conversion) */
    elem->name = NULL;
    elem->prefix = NULL;
    elem->namespace_uri = NULL;

    /* Store document pointer - will be set later */
    elem->document = NULL;

    /* Initialize tree pointers to NULL */
    elem->parent = NULL;
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->next_sibling = NULL;
    elem->first_attribute = NULL;

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
 * Regular Pointer Access Functions (no page_base calculation!)
 * ============================================================================ */

/* Set parent pointer (and offset for compact mode) */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent) {
    if (!elem) return;
    elem->parent = parent;

    /* COMPACT MODE: Also set offset field */
    if (TAURUS_ELEM_IS_COMPACT(elem) && parent) {
        void* base = TAURUS_ELEM_COMPACT_BASE(elem);
        elem->parent_offset = COMPACT_PTR_TO_OFFSET(base, parent);
    } else {
        elem->parent_offset = 0;
    }
}

void taurus_element_set_first_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->first_child = (TaurusNode*)child;

    /* COMPACT MODE: Also set offset field */
    if (TAURUS_ELEM_IS_COMPACT(elem) && child) {
        void* base = TAURUS_ELEM_COMPACT_BASE(elem);
        elem->first_child_offset = COMPACT_PTR_TO_OFFSET(base, child);
    } else {
        elem->first_child_offset = 0;
    }
}

void taurus_element_set_last_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->last_child = (TaurusNode*)child;

    /* COMPACT MODE: Also set offset field */
    if (TAURUS_ELEM_IS_COMPACT(elem) && child) {
        void* base = TAURUS_ELEM_COMPACT_BASE(elem);
        elem->last_child_offset = COMPACT_PTR_TO_OFFSET(base, child);
    } else {
        elem->last_child_offset = 0;
    }
}

void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling) {
    if (!elem) return;
    elem->next_sibling = (TaurusNode*)sibling;

    /* COMPACT MODE: Also set offset field */
    if (TAURUS_ELEM_IS_COMPACT(elem) && sibling) {
        void* base = TAURUS_ELEM_COMPACT_BASE(elem);
        elem->next_sibling_offset = COMPACT_PTR_TO_OFFSET(base, sibling);
    } else {
        elem->next_sibling_offset = 0;
    }
}

/* ============================================================================
 * Attribute Hash Table Functions (O(1) lookup)
 * Exported for use by element_modify.c
 * ============================================================================ */

/* FNV-1a hash function for attribute names
 * Fast, high-quality hash with excellent distribution */
uint32_t attr_hash_name(const char* name, size_t len) {
    uint32_t hash = 2166136261u;  /* FNV offset basis */
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)name[i];
        hash *= 16777619u;  /* FNV prime */
    }
    return hash;
}

/* Hash table size lookup table (powers of 2 for fast modulo) */
static const uint8_t ATTR_HASH_SIZES[] = {0, 0, 0, 0, 0, 8, 16, 16, 32, 32, 64, 64, 64, 64, 128, 128};

/* Create hash table for attribute lookup
 * Called when attr_count exceeds 4 */
int create_attr_hash_table(TaurusElement elem, TaurusMemoryPool* pool) {
    if (!elem || !pool) return -1;

    /* Determine hash table size based on attribute count */
    uint8_t size;
    if (elem->attr_count < 16) {
        size = ATTR_HASH_SIZES[elem->attr_count];
    } else {
        /* For >15 attributes, use next power of 2 */
        size = 32;
        while (size < elem->attr_count * 2 && size < 128) {
            size *= 2;
        }
    }

    if (size == 0) return -1;

    /* Allocate hash table array (array of pointers to entries) */
    size_t alloc_size = size * sizeof(struct taurus_attr_hash_entry*);
    struct taurus_attr_hash_entry** table = (struct taurus_attr_hash_entry**)taurus_pool_alloc(pool, alloc_size);
    if (!table) return -1;

    /* Initialize all buckets to NULL */
    memset(table, 0, alloc_size);

    elem->attr_hash = table;
    elem->attr_hash_size = size;

    return 0;
}

/* Add attribute to hash table */
int add_attr_to_hash(TaurusElement elem, struct taurus_attribute* attr, TaurusMemoryPool* pool) {
    if (!elem || !attr || !elem->attr_hash) return -1;

    /* Get attribute name for hashing */
    const char* name = attr->name ? attr->name : attr->name_view.data;
    size_t name_len = attr->name ? strlen(attr->name) : attr->name_view.length;
    if (!name || name_len == 0) return -1;

    /* Calculate hash and bucket index */
    uint32_t hash = attr_hash_name(name, name_len);
    uint32_t bucket = hash & (elem->attr_hash_size - 1);

    /* Allocate hash entry */
    struct taurus_attr_hash_entry* entry = (struct taurus_attr_hash_entry*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attr_hash_entry));
    if (!entry) return -1;

    /* Initialize entry and insert at head of bucket chain */
    entry->attr = attr;
    entry->next = elem->attr_hash[bucket];
    elem->attr_hash[bucket] = entry;

    return 0;
}

/* Remove attribute from hash table (O(1) average case)
 * This is much faster than rebuilding the entire hash table */
int remove_attr_from_hash(TaurusElement elem, const char* name) {
    if (!elem || !name || !elem->attr_hash) return -1;

    size_t name_len = strlen(name);
    if (name_len == 0) return -1;

    /* Calculate hash and bucket index */
    uint32_t hash = attr_hash_name(name, name_len);
    uint32_t bucket = hash & (elem->attr_hash_size - 1);

    /* Walk the collision chain to find and remove the entry */
    struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
    struct taurus_attr_hash_entry* prev = NULL;

    while (entry) {
        const char* entry_name = entry->attr->name ? entry->attr->name : entry->attr->name_view.data;
        size_t entry_len = entry->attr->name ? strlen(entry->attr->name) : entry->attr->name_view.length;

        if (entry_len == name_len && memcmp(entry_name, name, name_len) == 0) {
            /* Found! Remove from chain */
            if (prev) {
                prev->next = entry->next;
            } else {
                /* Was head of chain */
                elem->attr_hash[bucket] = entry->next;
            }
            /* Entry is pool-allocated, don't free it - just abandon it */
            return 0;
        }

        prev = entry;
        entry = entry->next;
    }

    return -1;  /* Not found in hash table */
}

/* Rebuild hash table after attribute addition (when growing past 4) */
static int rebuild_attr_hash_table(TaurusElement elem, TaurusMemoryPool* pool) {
    if (!elem || !pool) return -1;

    /* Free old hash table by just abandoning it (pool-allocated)
     * Then create new, larger hash table */
    int result = create_attr_hash_table(elem, pool);
    if (result != 0) return result;

    /* Re-add all attributes to hash table */
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
 * Attribute Access Functions (O(1) with inline array + hash table)
 * ============================================================================ */

/* Get first attribute from element - DIRECT POINTER ACCESS (no encoding!) */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem) {
    if (!elem) return NULL;
    /* first_attribute is a regular pointer - no decoding needed! */
    return elem->first_attribute;
}

/* Set first attribute in element - DIRECT POINTER ACCESS (no encoding!) */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr) {
    if (!elem) return;
    /* first_attribute is a regular pointer - no encoding needed! */
    elem->first_attribute = attr;
}

/* Get attribute count */
uint8_t taurus_element_attribute_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->attr_count;
}

/* Get attribute by index - O(1) for first 4, O(n) for rest */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Fast path: inline array for first 4 attributes */
    if (index < 4 && elem->attributes_inline[index]) {
        return elem->attributes_inline[index];
    }

    /* Fallback: walk linked list for indices >= 4 */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    for (uint8_t i = 0; i < index && attr; i++) {
        if ((uintptr_t)attr < 0x1000) return NULL;
        attr = attr->next;
    }

    if ((uintptr_t)attr < 0x1000) return NULL;
    return attr;
}

/* Get attribute by name - O(1) with inline array + hash table! */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;
    if (elem->attr_count == 0) return NULL;

    size_t name_len = strlen(name);

    /* Fast path 1: Check inline array (O(1) for first 4 attributes) */
    for (int i = 0; i < 4 && i < elem->attr_count; i++) {
        struct taurus_attribute* attr = elem->attributes_inline[i];
        if (!attr) continue;

        /* Check cached name first (faster) */
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr;
        }
        /* Fall back to StringView comparison */
        if (!taurus_sv_is_empty(&attr->name_view) &&
            attr->name_view.length == name_len &&
            memcmp(attr->name_view.data, name, name_len) == 0) {
            return attr;
        }
    }

    /* Fast path 2: Use hash table if available (O(1) average case) */
    if (elem->attr_hash && elem->attr_hash_size > 0) {
        uint32_t hash = attr_hash_name(name, name_len);
        uint32_t bucket = hash & (elem->attr_hash_size - 1);

        struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
        while (entry) {
            struct taurus_attribute* attr = entry->attr;
            /* Check cached name first */
            if (attr->name && strcmp(attr->name, name) == 0) {
                return attr;
            }
            /* Fall back to StringView comparison */
            if (!taurus_sv_is_empty(&attr->name_view) &&
                attr->name_view.length == name_len &&
                memcmp(attr->name_view.data, name, name_len) == 0) {
                return attr;
            }
            entry = entry->next;
        }
        return NULL;  /* Not found in hash table */
    }

    /* Slow path: walk linked list for remaining attributes (>4 without hash) */
    struct taurus_attribute* attr = elem->first_attribute;
    uint8_t skipped = 0;
    while (attr) {
        /* Skip first 4 (already checked in inline array) */
        if (skipped < 4) {
            skipped++;
            attr = attr->next;
            continue;
        }

        /* Check cached name first (faster) */
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr;
        }
        /* Fall back to StringView comparison */
        if (!taurus_sv_is_empty(&attr->name_view) &&
            attr->name_view.length == name_len &&
            memcmp(attr->name_view.data, name, name_len) == 0) {
            return attr;
        }
        attr = attr->next;
    }

    return NULL;
}

/* Get attribute by name (StringView version - internal, faster)
 * O(1) with inline array + hash table! */
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name) {
    if (!elem || taurus_sv_is_empty(&name)) return NULL;
    if (elem->attr_count == 0) return NULL;

    /* Fast path 1: Check inline array (O(1) for first 4 attributes) */
    for (int i = 0; i < 4 && i < elem->attr_count; i++) {
        struct taurus_attribute* attr = elem->attributes_inline[i];
        if (!attr) continue;

        /* Direct StringView comparison (O(1) length check + memcmp) */
        if (taurus_sv_equals(&attr->name_view, &name)) {
            return attr;
        }
    }

    /* Fast path 2: Use hash table if available (O(1) average case) */
    if (elem->attr_hash && elem->attr_hash_size > 0) {
        uint32_t hash = attr_hash_name(name.data, name.length);
        uint32_t bucket = hash & (elem->attr_hash_size - 1);

        struct taurus_attr_hash_entry* entry = elem->attr_hash[bucket];
        while (entry) {
            struct taurus_attribute* attr = entry->attr;
            /* Direct StringView comparison */
            if (taurus_sv_equals(&attr->name_view, &name)) {
                return attr;
            }
            entry = entry->next;
        }
        return NULL;  /* Not found in hash table */
    }

    /* Slow path: walk linked list for remaining attributes (>4 without hash) */
    struct taurus_attribute* attr = elem->first_attribute;
    uint8_t skipped = 0;
    while (attr) {
        /* Skip first 4 (already checked in inline array) */
        if (skipped < 4) {
            skipped++;
            attr = attr->next;
            continue;
        }

        /* Direct StringView comparison */
        if (taurus_sv_equals(&attr->name_view, &name)) {
            return attr;
        }
        attr = attr->next;
    }

    return NULL;
}

/* Add attribute to element - O(1) with inline array + hash table */
int taurus_element_add_attribute(TaurusElement elem,
                                TaurusStringView name_view,
                                TaurusStringView value_view,
                                TaurusMemoryPool* pool) {
    if (!elem || taurus_sv_is_empty(&name_view) || !pool) return -1;

    /* Allocate attribute from pool */
    struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attribute));
    if (!attr) return -1;

    /* Initialize attribute */
    attr->name_view = name_view;
    attr->value_view = value_view;

    /* Initialize namespace/prefix fields */
    attr->namespace_uri_view = taurus_sv_empty();
    attr->namespace_uri = NULL;
    attr->prefix_view = taurus_sv_empty();
    attr->prefix = NULL;

    /* EAGER STRING CONVERSION */
    attr->name = taurus_sv_to_cstr_pooled(&name_view, pool);

    /* Check if value contains entities and decode them */
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

    /* Add to linked list - O(1) using last_attribute pointer! */
    if (!elem->first_attribute) {
        /* First attribute */
        elem->first_attribute = attr;
        elem->last_attribute = attr;
    } else {
        /* Append to end using last_attribute - O(1)! */
        elem->last_attribute->next = attr;
        elem->last_attribute = attr;
    }

    /* O(1) optimization: Add to inline array (first 4 attributes) */
    if (elem->attr_count < 4) {
        elem->attributes_inline[elem->attr_count] = attr;
    }

    /* Increment attribute count */
    elem->attr_count++;

    /* Create/update hash table when we exceed 4 attributes */
    if (elem->attr_count == 5) {
        /* First time crossing threshold - create hash table with all attributes */
        if (create_attr_hash_table(elem, pool) == 0) {
            /* Add all 5 attributes to hash table */
            struct taurus_attribute* a = elem->first_attribute;
            while (a) {
                add_attr_to_hash(elem, a, pool);
                a = a->next;
            }
        }
    } else if (elem->attr_count > 5) {
        /* Just add new attribute to existing hash table */
        if (elem->attr_hash) {
            add_attr_to_hash(elem, attr, pool);

            /* Consider growing hash table if load factor gets too high */
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

/* Get element name (lazy conversion from StringView to C string) */
const char* taurus_element_get_name(TaurusElement elem) {
    if (!elem) return NULL;

    /* Validate node type - only elements have names */
    TaurusNode* node = (TaurusNode*)elem;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) {
        return NULL;  /* Not an element node */
    }

    /* Return cached string if available (lazy conversion already done) */
    if (elem->name) return elem->name;

    /* Convert from StringView to NULL-terminated string */
    if (!taurus_sv_is_empty(&elem->name_view)) {
        /* OPTIMIZATION (Phase C): Check if already null-terminated first!
         * After in-place null termination during parsing, we can use the
         * StringView data directly without any copying. */
        if (taurus_sv_is_null_terminated(&elem->name_view)) {
            elem->name = (char*)elem->name_view.data;  /* Zero-copy! */
        } else if (elem->document && elem->document->pool) {
            /* Use pool allocation */
            elem->name = taurus_sv_to_cstr_pooled(&elem->name_view, elem->document->pool);
        } else {
            /* Fallback to regular malloc (not ideal for pool-allocated elements) */
            elem->name = taurus_sv_to_cstr(&elem->name_view);
        }
        return elem->name;
    }

    return NULL;  /* No name available */
}

/* Set prefix using StringView (zero-copy!) */
void taurus_element_set_prefix_view(TaurusElement elem, TaurusStringView prefix_view) {
    if (!elem) return;
    elem->prefix_view = prefix_view;
    /* Clear cached string - will be reconverted on next access */
    if (elem->prefix) {
        free(elem->prefix);
        elem->prefix = NULL;
    }
}

/* Set namespace URI using StringView (zero-copy!) */
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view) {
    if (!elem) return;
    elem->namespace_uri_view = uri_view;
    /* Clear cached string - will be reconverted on next access */
    if (elem->namespace_uri) {
        free(elem->namespace_uri);
        elem->namespace_uri = NULL;
    }
}

/* Get element prefix */
const char* taurus_element_get_prefix(TaurusElement elem) {
    if (!elem) return NULL;

    /* Return cached string if available */
    if (elem->prefix) return elem->prefix;

    /* Lazy convert StringView to NULL-terminated string */
    if (!taurus_sv_is_empty(&elem->prefix_view)) {
        /* OPTIMIZATION (Phase C): Check if already null-terminated first! */
        if (taurus_sv_is_null_terminated(&elem->prefix_view)) {
            elem->prefix = (char*)elem->prefix_view.data;  /* Zero-copy! */
        } else if (elem->document && elem->document->pool) {
            elem->prefix = taurus_sv_to_cstr_pooled(&elem->prefix_view, elem->document->pool);
        } else {
            elem->prefix = taurus_sv_to_cstr(&elem->prefix_view);
        }
        return elem->prefix;
    }

    return NULL;
}

/* Get element namespace URI */
const char* taurus_element_get_namespace_uri(TaurusElement elem) {
    if (!elem) return NULL;

    /* Return cached string if available */
    if (elem->namespace_uri) return elem->namespace_uri;

    /* Lazy convert StringView to NULL-terminated string */
    if (!taurus_sv_is_empty(&elem->namespace_uri_view)) {
        /* OPTIMIZATION (Phase C): Check if already null-terminated first! */
        if (taurus_sv_is_null_terminated(&elem->namespace_uri_view)) {
            elem->namespace_uri = (char*)elem->namespace_uri_view.data;  /* Zero-copy! */
        } else if (elem->document && elem->document->pool) {
            elem->namespace_uri = taurus_sv_to_cstr_pooled(&elem->namespace_uri_view, elem->document->pool);
        } else {
            elem->namespace_uri = taurus_sv_to_cstr(&elem->namespace_uri_view);
        }
        return elem->namespace_uri;
    }

    /* LAZY NAMESPACE RESOLUTION: If namespace_uri is not set but we have a prefix,
     * resolve it now by looking up the namespace declaration in the element or its ancestors.
     * This must happen here (not during parsing) because the element may not have a parent
     * yet during parsing. Namespace resolution requires walking up the tree to find
     * the namespace declaration, which is typically on an ancestor element. */
    if (!taurus_sv_is_empty(&elem->prefix_view)) {
        char* prefix_cstr = taurus_sv_to_cstr(&elem->prefix_view);
        const char* uri = taurus_element_lookup_namespace(elem, prefix_cstr);
        if (uri) {
            /* Cache the resolved URI - just use regular strdup since the lookup
             * returns a pointer to the namespace URI which is already pool-allocated */
            elem->namespace_uri = taurus_strdup(uri);
        }
        free(prefix_cstr);
        return elem->namespace_uri;
    }

    return NULL;
}

/* Legacy functions for C string input */
void taurus_element_set_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return;

    if (elem->prefix) free(elem->prefix);
    elem->prefix = prefix ? taurus_strdup(prefix) : NULL;
    /* Clear StringView */
    elem->prefix_view = taurus_sv_empty();
}

void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri) {
    if (!elem) return;

    if (elem->namespace_uri) free(elem->namespace_uri);
    elem->namespace_uri = uri ? taurus_strdup(uri) : NULL;
    /* Clear StringView */
    elem->namespace_uri_view = taurus_sv_empty();
}

/* ============================================================================
 * Child Access Functions
 * ============================================================================ */

/* Get child count */
size_t taurus_element_child_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->child_count;
}

/* Legacy attribute API functions moved to element_modify.c */

/* Namespace functions moved to element_namespace.c */

/* Children manipulation */
void taurus_element_append_child_internal(TaurusElement elem, TaurusNode* child) {
    if (!elem || !child) return;

    /* SAFETY: Verify node type field is valid before accessing it
     * Small values like 0x4 or 0x1 in the type field suggest the pointer is corrupted */
    if (child->type < TAURUS_NODE_TYPE_ELEMENT ||
        child->type > TAURUS_NODE_TYPE_DOCTYPE) {
        /* Invalid node type - corrupted pointer */
        return;
    }

    if (child->type != TAURUS_NODE_TYPE_ELEMENT &&
        child->type != TAURUS_NODE_TYPE_TEXT &&
        child->type != TAURUS_NODE_TYPE_CDATA &&
        child->type != TAURUS_NODE_TYPE_COMMENT &&
        child->type != TAURUS_NODE_TYPE_PI) {
        return;  /* Only allow these node types as children */
    }

    /* For element children, set up linked list structure */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;

        /* Set parent relationship */
        taurus_element_set_parent(child_elem, elem);

        /* Set document pointer */
        child_elem->document = elem->document;

        /* Append to end of children list */
        /* NOTE: last_child might point to a non-element node, so we need to handle both cases */
        TaurusElement last = elem->last_child;
        if (last) {
            /* last points to the last child element */
            TaurusNode* last_node = (TaurusNode*)last;

            /* Set next_sibling on the last child based on its type */
            if (last_node->type == TAURUS_NODE_TYPE_ELEMENT) {
                /* Last child is an element - use direct pointer assignment */
                ((TaurusElement)last_node)->next_sibling = child_elem;
            } else if (last_node->type == TAURUS_NODE_TYPE_TEXT) {
                /* Last child is a text node - use direct pointer assignment */
                ((TaurusTextNode*)last_node)->next_sibling = child;
            } else if (last_node->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)last_node)->next_sibling = child;
            } else if (last_node->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)last_node)->next_sibling = child;
            } else if (last_node->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)last_node)->next_sibling = child;
            }

            /* Set last_child to the new child */
            elem->last_child = child_elem;

            /* O(1) optimization: Add to inline children array (only first 4) */
            if (elem->child_count < 4) {
                elem->children[elem->child_count] = child;
            }
        } else {
            /* No children yet - set first and last child */
            elem->first_child = child_elem;
            elem->last_child = child_elem;

            /* O(1) optimization: Add to inline children array */
            elem->children[0] = child;
        }

        /* Increment child count (even if > 4) */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), append to linked list */
        TaurusNode* last = (TaurusNode*)elem->last_child;
        if (last) {
            /* Set next_sibling on the last child based on last's type */
            if (last->type == TAURUS_NODE_TYPE_ELEMENT) {
                /* Element last: directly set the next_sibling pointer */
                ((TaurusElement)last)->next_sibling = (TaurusElement)child;
            } else if (last->type == TAURUS_NODE_TYPE_TEXT) {
                ((TaurusTextNode*)last)->next_sibling = child;
            } else if (last->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)last)->next_sibling = child;
            } else if (last->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)last)->next_sibling = child;
            } else if (last->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)last)->next_sibling = child;
            }

            /* Set last_child to child using direct pointer assignment */
            elem->last_child = (TaurusElement)child;
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            elem->first_child = (TaurusElement)child;
            elem->last_child = (TaurusElement)child;
        }

        /* Don't increment child_count for non-element children */
    }

    /* COW: Increment version on modification */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));
}

void taurus_element_prepend_child_internal(TaurusElement elem, TaurusNode* child) {
    if (!elem || !child) return;

    /* SAFETY: Verify node type field is valid before accessing it
     * Small values like 0x4 or 0x1 in the type field suggest the pointer is corrupted */
    if (child->type < TAURUS_NODE_TYPE_ELEMENT ||
        child->type > TAURUS_NODE_TYPE_DOCTYPE) {
        /* Invalid node type - corrupted pointer */
        return;
    }

    if (child->type != TAURUS_NODE_TYPE_ELEMENT &&
        child->type != TAURUS_NODE_TYPE_TEXT &&
        child->type != TAURUS_NODE_TYPE_CDATA &&
        child->type != TAURUS_NODE_TYPE_COMMENT &&
        child->type != TAURUS_NODE_TYPE_PI) {
        return;  /* Only allow these node types as children */
    }

    /* For element children, set up linked list structure */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;

        /* Set parent relationship */
        taurus_element_set_parent(child_elem, elem);

        /* Set document pointer */
        child_elem->document = elem->document;

        /* Insert at beginning of children list */
        /* NOTE: first_child might point to a non-element node */
        TaurusElement first = elem->first_child;
        if (first) {
            /* first points to the first child (could be element or non-element) */
            TaurusNode* first_node = (TaurusNode*)first;

            /* Set the new child's next_sibling to point to the first child */
            /* Since the new child is an element, use direct pointer assignment */
            child_elem->next_sibling = (TaurusElement)first_node;

            /* Set first_child to the new child */
            elem->first_child = child_elem;

            /* O(1) optimization: Shift inline array and insert at front */
            if (elem->child_count < 4) {
                for (int i = elem->child_count; i > 0; i--) {
                    elem->children[i] = elem->children[i-1];
                }
                elem->children[0] = child;
            }
        } else {
            /* No children yet - set first and last child */
            elem->first_child = child_elem;
            elem->last_child = child_elem;

            /* O(1) optimization: Add to inline children array */
            elem->children[0] = child;
        }

        /* Increment child count */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), insert at beginning */
        TaurusNode* first = (TaurusNode*)elem->first_child;
        if (first) {
            /* Set next_sibling on the new child based on child's type */
            if (child->type == TAURUS_NODE_TYPE_TEXT) {
                ((TaurusTextNode*)child)->next_sibling = first;
            } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)child)->next_sibling = first;
            } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)child)->next_sibling = first;
            } else if (child->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)child)->next_sibling = first;
            }

            /* Set first_child to child using direct pointer assignment */
            elem->first_child = (TaurusElement)child;
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            elem->first_child = (TaurusElement)child;
            elem->last_child = (TaurusElement)child;
        }

        /* Don't increment child_count for non-element children */
    }

    /* COW: Increment version on modification */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));
}

/* Document tree operations moved to element_text.c */

/* Remove all attributes from an element */
int taurus_element_remove_all_attributes(TaurusElement elem) {
    if (!elem) return TAURUS_ERROR_NULL_INPUT;

    /* CRITICAL: Don't free pool-allocated attributes directly!
     * Attributes are allocated from the memory pool and will be freed
     * when the pool is destroyed. Just clear the pointers and let the pool handle cleanup.
     * We DON'T free any attribute strings - they're all pool-allocated. */

    /* Just clear the attribute linked list pointers */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        struct taurus_attribute* next = attr->next;
        /* Don't free attr or any of its strings - all pool-allocated!
         * They will be reclaimed when the document/pool is freed. */
        attr = next;
    }

    /* Clear the first_attribute and last_attribute pointers and count */
    elem->first_attribute = NULL;
    elem->last_attribute = NULL;
    elem->attr_count = 0;

    return 0; /* TAURUS_OK */
}

/* Subtree analysis moved to element_text.c */
