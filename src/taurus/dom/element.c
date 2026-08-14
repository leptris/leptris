/* lib/src/dom/element.c - Element node implementation (COMPACT ONLY)
 * Copyright (c) 2024, Ribose Inc.
 *
 * HYBRID ARCHITECTURE:
 * Uses regular pointers for hot navigation paths (parent, first_child, next_sibling)
 * to achieve 1.2x performance target vs pugixml.
 * - ~117 bytes per element (vs 192 bytes in legacy design = 1.6x reduction!)
 * - Direct pointer access (no page_base calculation)
 * - Better cache locality than pure compact design
 * - Compact pointers only for cold paths (attributes)
 *
 * This is the ONLY element implementation - no backwards compatibility.
 */

#include "element.h"
#include "compact.h"
#include "root_doc_map.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "../../include/taurus.h"  /* for TAURUS_API on public exports */
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

/* From taurus_memory.c — declared here to avoid pulling in the
 * full taurus_memory.h, which conflicts with pi.h's taurus_pi_free. */
int taurus_element_add_namespace(struct taurus_element* elem, struct taurus_namespace* ns);
struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix, const char* uri, TaurusMemoryPool* pool);

/* ============================================================================
 * Element Creation
 * ============================================================================ */

/* Create element with StringView (true zero-copy!) */
TaurusElement taurus_element_create_with_view(
    TaurusStringView name_view,
    TaurusMemoryPool* pool
) {
    if (taurus_sv_is_empty(&name_view) || !pool) return NULL;

    /* Allocate + zero element from pool. memset handles ALL field
     * initialization to 0/NULL. Only type and name need non-zero
     * values. This eliminates ~20 redundant stores that the old
     * per-field init did after memset. */
    TaurusElement elem = (TaurusElement)taurus_pool_alloc(pool, sizeof(struct taurus_element));
    if (!elem) return NULL;

    memset(elem, 0, sizeof(struct taurus_element));
    elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
    elem->name = taurus_sv_to_cstr_pooled(&name_view, pool);
    elem->name_hash = taurus_name_hash_compute(elem->name);

    return elem;
}

/* Create element with in-place string (zero-copy) */
TaurusElement taurus_element_create_pooled_inplace(char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Create StringView from in-place string */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    return taurus_element_create_with_view(name_view, pool);
}

/* Create an element skeleton with no name — for the deferred-NUL
 * zero-copy parser path (TODO 113 Phase 5). The parser fills in
 * elem->name/prefix after consuming the opening tag's terminator,
 * at which point writing a NUL at name_view.data[name_view.length]
 * is safe (the parser has moved past that byte).
 *
 * Caller MUST finalize via the parser before any code reads name. */
TaurusElement taurus_element_create_zero_copy(TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    TaurusElement elem = (TaurusElement)taurus_pool_alloc(
        pool, sizeof(struct taurus_element));
    if (!elem) return NULL;

    memset(elem, 0, sizeof(struct taurus_element));
    elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
    return elem;
}

/* Create element using memory pool.
 *
 * The name is COPIED into the pool — callers can free or reuse their
 * buffer immediately.  This is the safe contract for the public
 * taurus_element_create() and for cross-document copies; the parser
 * uses taurus_element_create_with_view directly for true zero-copy. */
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Pool-owned copy; lives for the document's lifetime. */
    char* name_copy = taurus_pool_strdup(pool, name);
    if (!name_copy) return NULL;

    /* Hand ownership to create_with_view via the inplace path so the
     * name_view points into pool-owned storage, not the caller's buffer. */
    return taurus_element_create_pooled_inplace(name_copy, pool);
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

/* Legacy public setter entrypoints — delegate to the compact-offset
 * setters in element.h (TODO 90 Phase 2b). Kept as out-of-line
 * symbols so the public API doesn't depend on the inline bodies. */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent) {
    taurus_elem_set_parent(elem, parent);
}

void taurus_element_set_first_child(TaurusElement elem, TaurusElement child) {
    taurus_elem_set_first_child(elem, (TaurusNode*)child);
}

void taurus_element_set_last_child(TaurusElement elem, TaurusElement child) {
    taurus_elem_set_last_child(elem, (TaurusNode*)child);
}

void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling) {
    taurus_elem_set_next_sibling(elem, (TaurusNode*)sibling);
}

/* ============================================================================
 * Attribute Access Functions (still uses compact pointers - cold path)
 * ============================================================================ */

/* Get first attribute — decodes the int32_t offset (TODO 90 Phase 2d). */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem) {
    return taurus_elem_first_attribute(elem);
}

/* Set first attribute — encodes the int32_t offset (TODO 90 Phase 2d). */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr) {
    taurus_elem_set_first_attribute(elem, attr);
}

/* Get attribute count — returns size_t for public API (TODO 138).
 * Declared TAURUS_API so the symbol is exported from the shared library
 * for FFI bindings. */
TAURUS_API size_t taurus_element_attribute_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->attr_count;
}

/* Get attribute by index */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Walk the linked list to find the attribute at index */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    for (uint8_t i = 0; i < index && attr; i++) {
        /* Validate attr pointer before accessing next */
        if ((uintptr_t)attr < 0x1000) return NULL;  /* Invalid pointer */
        attr = taurus_attr_next(attr);
    }

    /* Final validation before returning */
    if ((uintptr_t)attr < 0x1000) return NULL;

    return attr;
}

/* Get attribute by name */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Hash-filtered attribute lookup (TODO 113 Phase 4).
     * Compute the FNV-1a hash of the search name once, then compare
     * 4-byte hashes in the loop before touching string data. This
     * turns O(N × strlen) into O(N × uint32) for the non-matching
     * case. */
    size_t name_len = strlen(name);
    uint32_t name_hash = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        name_hash ^= (unsigned char)name[i];
        name_hash *= 16777619u;
    }

    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        /* Hash pre-filter: reject most non-matching attrs in one
         * integer comparison. Lazy compute on first read (TODO 172).
         * Only when hash AND length match do we do the full memcmp. */
        if (attr_name_hash(attr) == name_hash &&
            attr->name_view.length == name_len) {
            if (attr->name && memcmp(attr->name, name, name_len) == 0) {
                return attr;
            }
            if (!taurus_sv_is_empty(&attr->name_view) &&
                memcmp(attr->name_view.data, name, name_len) == 0) {
                return attr;
            }
        }
        attr = taurus_attr_next(attr);
    }

    return NULL;
}

/* Get attribute by name (StringView version - internal, faster)
 * This is 2-3x faster than the C string version because:
 * 1. No strlen() call needed
 * 2. Uses length-based comparison first (O(1) mismatch check)
 * 3. No C string conversion needed
 */
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name) {
    if (!elem || taurus_sv_is_empty(&name)) return NULL;

    /* Walk the attribute linked list */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        /* Direct StringView comparison (O(1) length check + memcmp) */
        if (taurus_sv_equals(&attr->name_view, &name)) {
            return attr;
        }
        attr = taurus_attr_next(attr);
    }

    return NULL;
}

/* Add attribute to element */
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

    /* Pre-compute FNV-1a hash of attribute name for O(1) lookup
     * filtering (TODO 113 Phase 4). */
    attr->name_hash = 2166136261u;
    for (size_t i = 0; i < name_view.length; i++) {
        attr->name_hash ^= (unsigned char)name_view.data[i];
        attr->name_hash *= 16777619u;
    }

    /* CRITICAL FIX: Initialize namespace/prefix fields to prevent stale data.
     * TODO 173: these now live in the side-cache struct (attr->ns_cache).
     * NULL ns_cache = no prefix / namespace_uri = empty/NULL. */
    attr->ns_cache = NULL;

    /* EAGER STRING CONVERSION: Convert attribute name and value to NULL-terminated C-strings.
     *
     * PERFORMANCE (TODO 22): attribute NAMES are interned via the pool's
     * hash table (they recur across elements, so dedup pays off).
     * attribute VALUES bypass interning — values are almost always
     * unique per element, so the hash-table lookup/insert cost is
     * wasted.  Direct pool allocation instead.
     *
     * CRITICAL: Decode XML entities in attribute values BEFORE converting to C string.
     * Entities like &lt; &gt; &amp; must be decoded to < > & during parsing. */
    attr->name = taurus_sv_to_cstr_pooled(&name_view, pool);

    /* Allocate value storage directly from the pool — no interning. */
    char* value_storage = (char*)taurus_pool_alloc(pool, value_view.length + 1);
    if (value_storage) {
        memcpy(value_storage, value_view.data, value_view.length);
        value_storage[value_view.length] = '\0';

        /* Check if value contains entities and decode them */
        if (memchr(value_storage, '&', value_view.length) != NULL) {
            /* Value contains entities - decode them first.
             * taurus_decode_entities_view allocates from the pool, so
             * the value_storage we just allocated becomes garbage
             * (pool will reclaim it eventually).  This is a minor
             * waste; entity-bearing attribute values are rare. */
            TaurusStringView decoded_sv = { value_storage, value_view.length };
            char* decoded = taurus_decode_entities_view(&decoded_sv, pool);
            if (decoded) {
                attr->value = decoded;
                attr->has_entities = 0;
            } else {
                attr->value = value_storage;
                attr->has_entities = 1;
            }
        } else {
            attr->value = value_storage;
            attr->has_entities = 0;
        }
    } else {
        attr->value = NULL;
        attr->has_entities = 0;
    }

    taurus_attr_set_next(attr, NULL);

    /* Append via cached last_attribute pointer (TODO 106).
     * Maintains the same invariant as taurus_element_set_attribute.
     * Decode the int32_t offset to access the struct, then update via
     * the encoder (TODO 90 Phase 2d). */
    struct taurus_attribute* last = taurus_elem_last_attribute(elem);
    if (last) {
        taurus_attr_set_next(last, attr);
    } else {
        taurus_elem_set_first_attribute(elem, attr);
    }
    taurus_elem_set_last_attribute(elem, attr);

    /* Increment attribute count */
    elem->attr_count++;

    return 0;
}

/* Add attribute in deferred-NUL mode (TODO 113 Phase 5).
 *
 * Mirrors taurus_element_add_attribute but leaves attr->name and
 * attr->value NULL when no entity decoding is needed. The parser
 * fills them in by NUL-terminating in the writable XML buffer after
 * the opening tag is consumed — saving two pool allocations per
 * attribute on the common (no-entity) path. Entity-bearing values
 * are still pool-allocated eagerly (rare path, can't be zero-copied
 * because the decoded bytes don't exist in the source buffer). */
int taurus_element_add_attribute_zero_copy(TaurusElement elem,
                                           TaurusStringView name_view,
                                           TaurusStringView value_view,
                                           TaurusMemoryPool* pool) {
    if (!elem || taurus_sv_is_empty(&name_view) || !pool) return -1;

    struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attribute));
    if (!attr) return -1;

    attr->name_view = name_view;
    attr->value_view = value_view;
    attr->name = NULL;
    attr->value = NULL;

    attr->name_hash = 2166136261u;
    for (size_t i = 0; i < name_view.length; i++) {
        attr->name_hash ^= (unsigned char)name_view.data[i];
        attr->name_hash *= 16777619u;
    }

    attr->ns_cache = NULL;  /* TODO 173: side cache allocated on demand. */

    if (memchr(value_view.data, '&', value_view.length) != NULL) {
        char* value_storage = (char*)taurus_pool_alloc(pool, value_view.length + 1);
        if (value_storage) {
            memcpy(value_storage, value_view.data, value_view.length);
            value_storage[value_view.length] = '\0';
            TaurusStringView decoded_sv = { value_storage, value_view.length };
            char* decoded = taurus_decode_entities_view(&decoded_sv, pool);
            if (decoded) {
                attr->value = decoded;
                attr->has_entities = 0;
            } else {
                attr->value = value_storage;
                attr->has_entities = 1;
            }
        } else {
            attr->has_entities = 0;
        }
    } else {
        attr->has_entities = 0;
    }

    taurus_attr_set_next(attr, NULL);

    struct taurus_attribute* last = taurus_elem_last_attribute(elem);
    if (last) {
        taurus_attr_set_next(last, attr);
    } else {
        taurus_elem_set_first_attribute(elem, attr);
    }
    taurus_elem_set_last_attribute(elem, attr);

    elem->attr_count++;
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

    /* name_view removed (TODO 90) — name is set eagerly by create_with_view. */
    return elem->name;
}

/* Set prefix using StringView (zero-copy!) */

/* Set namespace URI from StringView (eager pool-strdup — no lazy staging).
 * TODO 90: namespace_uri_view removed from struct; this setter is now
 * the only path. It does the conversion eagerly so subsequent
 * taurus_element_get_namespace_uri() just returns the cached char*. */
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view) {
    if (!elem) return;
    if (taurus_sv_is_empty(&uri_view)) {
        if (elem->ns_cache) elem->ns_cache->namespace_uri = NULL;
        return;
    }
    TaurusMemoryPool* pool = taurus_element_get_pool(elem);
    if (pool) {
        char* storage = (char*)taurus_pool_alloc(pool, uri_view.length + 1);
        if (!storage) return;
        memcpy(storage, uri_view.data, uri_view.length);
        storage[uri_view.length] = '\0';
        taurus_elem_set_ns_uri(elem, storage, pool);
    } else {
        taurus_elem_set_ns_uri(elem, (char*)uri_view.data, NULL);
    }
}

/* Get element prefix */
const char* taurus_element_get_prefix(TaurusElement elem) {
    return taurus_elem_prefix(elem);
}

/* Get element namespace URI */
const char* taurus_element_get_namespace_uri(TaurusElement elem) {
    if (!elem) return NULL;

    char* cached = taurus_elem_ns_uri(elem);
    if (cached) return cached;

    /* LAZY NAMESPACE RESOLUTION: resolve via ancestors. */
    const char* prefix = taurus_elem_prefix(elem);
    const char* uri = taurus_element_lookup_namespace(elem, prefix);
    if (uri) {
        TaurusMemoryPool* pool = taurus_element_get_pool(elem);
        if (pool) {
            size_t len = strlen(uri);
            char* copy = (char*)taurus_pool_alloc(pool, len + 1);
            if (copy) {
                memcpy(copy, uri, len);
                copy[len] = '\0';
                taurus_elem_set_ns_uri(elem, copy, pool);
            }
        } else {
            taurus_elem_set_ns_uri(elem, taurus_strdup(uri), NULL);
        }
    }
    return taurus_elem_ns_uri(elem);
}


/* Legacy functions for C string input */
void taurus_element_set_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return;
    char* old = taurus_elem_prefix(elem);
    if (old) free(old);
    char* copy = prefix ? taurus_strdup(prefix) : NULL;
    TaurusMemoryPool* pool = taurus_element_get_pool(elem);
    taurus_elem_set_prefix(elem, copy, pool);
}

void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri) {
    if (!elem) return;
    char* old = taurus_elem_ns_uri(elem);
    if (old) free(old);
    char* copy = uri ? taurus_strdup(uri) : NULL;
    TaurusMemoryPool* pool = taurus_element_get_pool(elem);
    taurus_elem_set_ns_uri(elem, copy, pool);
}

/* ============================================================================
 * Child Access Functions
 * ============================================================================ */

/* Get child count */
size_t taurus_element_child_count(TaurusElement elem) {
    if (!elem) return 0;
    return elem->child_count;
}

/* Get child by index */
TaurusElement taurus_element_get_child(TaurusElement elem, uint16_t index) {
    if (!elem || index >= elem->child_count) return NULL;

    /* Walk the child linked list */
    TaurusElement child = taurus_element_get_first_child(elem);
    for (uint8_t i = 0; i < index && child; i++) {
        child = taurus_element_get_next_sibling(child);
    }

    return child;
}

/* ============================================================================
 * Legacy Public API Functions (compatibility wrappers)
 * ============================================================================ */

/* Add attribute (legacy C string API) */
void taurus_element_add_attribute_legacy(
    TaurusElement elem,
    const char* name,
    const char* value
) {
    if (!elem || !name) return;

    /* Create StringViews from C strings */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    TaurusStringView value_view = taurus_sv_from_cstr(value ? value : "");

    /* For now, we need a pool. Use element's document pool if available */
    TaurusMemoryPool* pool = taurus_element_get_pool(elem);
    if (!pool) {
        /* No pool available - can't add attribute in compact mode */
        return;
    }

    taurus_element_add_attribute(elem, name_view, value_view, pool);
}

/* Add attribute using memory pool (fast) */
void taurus_element_add_attribute_pooled(
    TaurusElement elem,
    const char* name,
    const char* value,
    TaurusMemoryPool* pool
) {
    if (!elem || !name || !pool) return;

    /* Create StringViews from C strings */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    TaurusStringView value_view = taurus_sv_from_cstr(value ? value : "");

    taurus_element_add_attribute(elem, name_view, value_view, pool);
}

/* Add attribute with in-place strings (zero-copy) */
void taurus_element_add_attribute_pooled_inplace(
    TaurusElement elem,
    char* name,
    char* value,
    TaurusMemoryPool* pool
) {
    if (!elem || !name || !pool) return;

    /* Create StringViews from in-place strings */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    TaurusStringView value_view = taurus_sv_from_cstr(value ? value : "");

    taurus_element_add_attribute(elem, name_view, value_view, pool);
}

/* Get attribute value by name (legacy API) */
const char* taurus_element_get_attribute_legacy(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;

    /* Lazy convert value_view to C string */
    if (!attr->value && !taurus_sv_is_empty(&attr->value_view)) {
        if (taurus_element_get_document(elem) && taurus_element_get_pool(elem)) {
            if (attr->has_entities) {
                attr->value = taurus_decode_entities_view(&attr->value_view, taurus_element_get_pool(elem));
            }
            if (!attr->value) {
                attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, taurus_element_get_pool(elem));
            }
        } else {
            attr->value = taurus_sv_to_cstr(&attr->value_view);
        }
    }

    return attr->value;
}

/* Add namespace with in-place strings (zero-copy).
 *
 * Stores prefix/uri in the element's own namespace-declaration fields
 * AND adds a taurus_namespace entry to elem->namespaces so that
 * taurus_element_lookup_namespace() can find it. The strings are
 * NOT copied; caller must ensure they outlive the document (typically
 * pool-allocated). */
void taurus_element_add_namespace_inplace(TaurusElement elem,
                                           char* prefix,
                                           char* uri,
                                           TaurusMemoryPool* pool) {
    if (!elem || !uri) return;

    if (prefix) {
        taurus_elem_set_prefix(elem, prefix, pool);
    }
    taurus_elem_set_ns_uri(elem, uri, pool);

    /* Also register the namespace declaration on elem->namespaces so
     * descendant lookups via taurus_element_lookup_namespace find it.
     * If pool allocation fails, the in-place fields are still set; the
     * namespace just won't be discoverable via prefix lookup. */
    if (pool) {
        struct taurus_namespace* ns = taurus_namespace_new_pooled(prefix, uri, pool);
        if (ns) {
            taurus_element_add_namespace(elem, ns);
        }
    }
}

/* Lookup namespace URI by prefix */
const char* taurus_element_lookup_namespace(TaurusElement elem,
                                             const char* prefix) {
    if (!elem) return NULL;

    /* Search namespaces linked list on current element */
    struct taurus_namespace* ns = taurus_elem_namespaces(elem);
    while (ns) {
        if ((prefix == NULL && ns->prefix == NULL) ||
            (prefix && ns->prefix && strcmp(prefix, ns->prefix) == 0)) {
            return ns->uri;
        }
        ns = ns->next;
    }

    /* Search parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (parent) {
        return taurus_element_lookup_namespace(parent, prefix);
    }

    return NULL;
}

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

    /* Issue #217: if child is already attached to a parent, unlink
     * it first — even if the parent is the SAME element (re-ordering
     * within the same parent). Without this, the child appears twice
     * in the chain and child_count is inflated. */
    TaurusElement old_parent = taurus_node_parent(child);
    if (old_parent) {
        taurus_node_unlink(child);
    }

    /* For element children, set up linked list structure */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;

        /* Set parent relationship */
        taurus_element_set_parent(child_elem, elem);

        /* TODO 155 Phase A: document field removed; non-root walks to root. */
        /* Append to end of children list.
         * last_child may point to any node type (text/comment/etc.) so
         * we set its next_sibling via the type-dispatching setter. */
        TaurusNode* last_node = taurus_elem_last_child(elem);
        if (last_node) {
            taurus_node_set_next_sibling(last_node, (TaurusNode*)child_elem);

            /* Set last_child to the new child */
            taurus_elem_set_last_child(elem, (TaurusNode*)child_elem);
        } else {
            /* No children yet - set first and last child */
            taurus_elem_set_first_child(elem, (TaurusNode*)child_elem);
            taurus_elem_set_last_child(elem, (TaurusNode*)child_elem);
        }

        /* Increment child count */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), append to linked list */
        TaurusNode* last = taurus_elem_last_child(elem);
        if (last) {
            taurus_node_set_next_sibling(last, (TaurusNode*)child);
            taurus_elem_set_last_child(elem, (TaurusNode*)child);
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            taurus_elem_set_first_child(elem, (TaurusNode*)child);
            taurus_elem_set_last_child(elem, (TaurusNode*)child);
        }

        /* Issue #168: set parent_off on the non-element child so
         * taurus_node_parent can resolve in O(1). */
        switch (child->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)child, elem);
                break;
            default:
                break;
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

    /* Issue #217: if child is already attached to a parent, unlink
     * it first — even if the parent is the SAME element (re-ordering
     * within the same parent). Without this, the child appears twice
     * in the chain and child_count is inflated. */
    TaurusElement old_parent = taurus_node_parent(child);
    if (old_parent) {
        taurus_node_unlink(child);
    }

    /* For element children, set up linked list structure */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;

        /* Set parent relationship */
        taurus_element_set_parent(child_elem, elem);

        /* TODO 155 Phase A: document field removed; non-root walks to root. */
        /* Insert at beginning of children list.
         * first_child may point to a non-element node; the new element's
         * next_sibling is set via the compact-offset setter. */
        TaurusNode* first_node = taurus_elem_first_child(elem);
        if (first_node) {
            taurus_elem_set_next_sibling(child_elem, first_node);
            taurus_elem_set_first_child(elem, (TaurusNode*)child_elem);
        } else {
            /* No children yet - set first and last child */
            taurus_elem_set_first_child(elem, (TaurusNode*)child_elem);
            taurus_elem_set_last_child(elem, (TaurusNode*)child_elem);
        }

        /* Increment child count */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), insert at beginning */
        TaurusNode* first = taurus_elem_first_child(elem);
        if (first) {
            /* Set the new child's next_sibling via the type-dispatching setter */
            taurus_node_set_next_sibling(child, first);
            taurus_elem_set_first_child(elem, (TaurusNode*)child);
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            taurus_elem_set_first_child(elem, (TaurusNode*)child);
            taurus_elem_set_last_child(elem, (TaurusNode*)child);
        }

        /* Issue #168: set parent_off on the non-element child. */
        switch (child->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)child, elem);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)child, elem);
                break;
            default:
                break;
        }

        /* Don't increment child_count for non-element children */
    }

    /* COW: Increment version on modification */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));
}

/* Helper function to calculate text length recursively */
static size_t calculate_text_length_recursive(TaurusNode* node) {
    if (!node) return 0;

    size_t len = 0;
    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            /* Route through taurus_text_get_content so entity-
             * containing borrowed text (the fast-parse path) is
             * expanded before measuring. After the call the node
             * is materialized, so the copy pass can use
             * text->content_len directly. */
            const char* content = taurus_text_get_content(text);
            if (content) {
                len += strlen(content);
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if (cdata->content) {
                len += strlen(cdata->content);
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            len += calculate_text_length_recursive(child);
        }

        child = taurus_node_get_next_sibling(child);
    }

    return len;
}

/* Helper function to copy text content recursively */
static void copy_text_content_recursive(TaurusNode* node, char* result, size_t* offset) {
    if (!node || !result || !offset) return;

    TaurusNode* child = taurus_node_first_child_internal(node);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            const char* content = taurus_text_get_content(text);
            if (content) {
                size_t clen = strlen(content);
                memcpy(result + *offset, content, clen);
                *offset += clen;
            }
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if (cdata->content) {
                size_t len = strlen(cdata->content);
                memcpy(result + *offset, cdata->content, len);
                *offset += len;
            }
        } else if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively include text from child elements */
            copy_text_content_recursive(child, result, offset);
        }

        child = taurus_node_get_next_sibling(child);
    }
}

/* Text content extraction (concatenates ALL text nodes recursively) */
char* taurus_element_get_text_content(TaurusElement elem) {
    if (!elem) return NULL;

    /* First pass: calculate total length needed recursively */
    size_t total_len = calculate_text_length_recursive((TaurusNode*)elem);

    if (total_len == 0) {
        return taurus_strdup("");
    }

    /* Allocate buffer for concatenated text */
    char* result = (char*)taurus_malloc(total_len + 1);
    if (!result) return NULL;

    /* Second pass: copy text content recursively */
    size_t offset = 0;
    copy_text_content_recursive((TaurusNode*)elem, result, &offset);

    result[offset] = '\0';
    return result;
}

/* Document tree operations */
void taurus_element_set_document_tree(TaurusElement elem, struct taurus_document* doc) {
    /* TODO 155 Phase A: the document field is removed. We only need
     * to register the ROOT in the thread-local root→doc map; non-root
     * elements walk parent_off to find the root and look up there. */
    if (!elem) return;
    taurus_root_doc_register(elem, doc);
}

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
        struct taurus_attribute* next = taurus_attr_next(attr);
        /* Don't free attr or any of its strings - all pool-allocated!
         * They will be reclaimed when the document/pool is freed. */
        attr = next;
    }

    /* Clear the attribute-list head/tail offsets and count */
    taurus_elem_set_first_attribute(elem, NULL);
    taurus_elem_set_last_attribute(elem, NULL);
    elem->attr_count = 0;

    return 0; /* TAURUS_OK */
}

/* ============================================================================
 * Subtree Analysis (for bulk allocation planning)
 * ============================================================================ */

/**
 * Count all nodes in subtree (for bulk allocation planning)
 *
 * Recursively traverses element tree and counts all nodes by type.
 * Used to pre-calculate allocation size for bulk operations.
 *
 * @param elem Root element to count
 * @param stats Output structure to fill with counts
 */
void taurus_element_count_subtree(TaurusElement elem, TaurusSubtreeStats* stats) {
    if (!elem || !stats) return;

    /* Initialize counts to zero */
    memset(stats, 0, sizeof(TaurusSubtreeStats));

    /* Count this element */
    stats->element_count = 1;

    /* Count attributes on this element */
    stats->attribute_count = elem->attr_count;

    /* Recursively traverse all children */
    TaurusNode* child = taurus_elem_first_child(elem);
    while (child) {
        switch (child->type) {
            case TAURUS_NODE_TYPE_ELEMENT:
                stats->element_count++;
                taurus_element_count_subtree((TaurusElement)child, stats);
                break;

            case TAURUS_NODE_TYPE_TEXT:
                stats->text_count++;
                break;

            case TAURUS_NODE_TYPE_COMMENT:
                stats->comment_count++;
                break;

            case TAURUS_NODE_TYPE_CDATA:
                stats->cdata_count++;
                break;

            case TAURUS_NODE_TYPE_PI:
                stats->pi_count++;
                break;

            default:
                /* Unknown node type, skip */
                break;
        }

        child = taurus_node_get_next_sibling(child);
    }
}
