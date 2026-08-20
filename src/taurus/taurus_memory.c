/* libtaurus - Memory management implementation
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 */

#include "taurus_memory.h"
#include "dom/element.h"
#include "memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Attribute Management (moved from old location for compatibility)
 * ============================================================================ */

struct taurus_attribute* taurus_attribute_new(const char* name, const char* value) {
    if (!name) return NULL;

    struct taurus_attribute* attr = TAURUS_ALLOC(struct taurus_attribute);
    if (!attr) return NULL;
    /* Zero the whole struct: next_cp/has_entities/name_hash have no
     * owners on this heap-allocated lifecycle. */
    memset(attr, 0, sizeof(*attr));

    /* Single representation (TODO 184 round 4): owned NUL-terminated
     * copies stored AS views. taurus_attribute_free releases them. */
    char* name_copy = taurus_strdup(name);
    if (!name_copy) {
        free(attr);
        return NULL;
    }
    attr->name_view = taurus_sv_from_cstr(name_copy);

    if (value) {
        char* value_copy = taurus_strdup(value);
        if (!value_copy) {
            free(name_copy);
            free(attr);
            return NULL;
        }
        attr->value_view = taurus_sv_from_cstr(value_copy);
    }

    return attr;
}

void taurus_attribute_free(struct taurus_attribute* attr) {
    if (!attr) return;

    /* The views hold owned heap copies on this lifecycle — cast away
     * const for free (the data IS mutable storage; the view type is
     * const only for read-side safety). */
    free((void*)(uintptr_t)attr->name_view.data);
    struct taurus_attr_ns_cache* nsc = attr_get_ns_cache(attr);
    if (nsc) {
        if (nsc->prefix) free(nsc->prefix);
        if (nsc->namespace_uri) free(nsc->namespace_uri);
        free(nsc);
    }
    free((void*)(uintptr_t)attr->value_view.data);

    free(attr);
}

/* ============================================================================
 * Namespace Management
 * ============================================================================ */

struct taurus_namespace* taurus_namespace_new(const char* prefix, const char* uri) {
    if (!uri) return NULL;

    struct taurus_namespace* ns = TAURUS_ALLOC(struct taurus_namespace);
    if (!ns) return NULL;

    ns->prefix = prefix ? taurus_strdup(prefix) : NULL;
    ns->uri = taurus_strdup(uri);
    ns->next = NULL;

    if (!ns->uri || (prefix && !ns->prefix)) {
        if (ns->prefix) free(ns->prefix);
        if (ns->uri) free(ns->uri);
        free(ns);
        return NULL;
    }

    return ns;
}

struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix,
                                                      const char* uri,
                                                      TaurusMemoryPool* pool) {
    if (!uri || !pool) return NULL;

    struct taurus_namespace* ns = (struct taurus_namespace*)taurus_pool_alloc(pool, sizeof(struct taurus_namespace));
    if (!ns) return NULL;

    ns->prefix = prefix ? taurus_pool_strdup(pool, prefix) : NULL;
    ns->uri = taurus_pool_strdup(pool, uri);
    ns->next = NULL;

    /* If allocation failed, return NULL - pool will be cleaned up on document free */
    if (!ns->uri) {
        return NULL;
    }

    return ns;
}

void taurus_namespace_free_single(struct taurus_namespace* ns) {
    if (!ns) return;

    if (ns->prefix) free(ns->prefix);
    if (ns->uri) free(ns->uri);
    free(ns);
}

void taurus_namespace_free_chain(struct taurus_namespace* ns) {
    struct taurus_namespace* next;

    while (ns) {
        next = ns->next;
        taurus_namespace_free_single(ns);
        ns = next;
    }
}

struct taurus_namespace* taurus_namespace_find(struct taurus_element* elem, const char* prefix) {
    struct taurus_namespace* ns;

    if (!elem) return NULL;

    /* Check current element */
    ns = taurus_elem_namespaces(elem);
    while (ns) {
        if ((prefix == NULL && ns->prefix == NULL) ||
            (prefix && ns->prefix && strcmp(prefix, ns->prefix) == 0)) {
            return ns;
        }
        ns = ns->next;
    }

    /* Search parent */
    struct taurus_element* parent = taurus_element_get_parent(elem);
    if (parent) {
        return taurus_namespace_find(parent, prefix);
    }

    return NULL;
}

/* Add namespace to element's namespace linked list.
 *
 * Issue #171: append in source order so taurus_element_namespace_decl_*
 * returns declarations in the order they appear in the document.
 * Previously this prepended, giving consumers a reversed view. */
int taurus_element_add_namespace(struct taurus_element* elem, struct taurus_namespace* ns) {
    if (!elem || !ns) return -1;

    ns->next = NULL;
    /* ns_cache is required to hold the declarations head. Allocate
     * on demand from the document's pool. TODO 155 Phase B. */
    struct taurus_memory_pool* pool = taurus_element_get_pool(elem);
    struct taurus_namespace** head_ptr = taurus_elem_namespaces_ptr(elem, pool);
    if (!head_ptr) return -1;
    if (!*head_ptr) {
        *head_ptr = ns;
        return 0;
    }
    struct taurus_namespace* tail = *head_ptr;
    while (tail->next) tail = tail->next;
    tail->next = ns;
    return 0;
}
