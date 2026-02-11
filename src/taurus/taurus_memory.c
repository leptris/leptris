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

    attr->name = taurus_strdup(name);
    if (!attr->name) {
        free(attr);
        return NULL;
    }

    attr->prefix = NULL;
    attr->namespace_uri = NULL;
    attr->value = value ? taurus_strdup(value) : NULL;

    if (value && !attr->value) {
        free(attr->name);
        free(attr);
        return NULL;
    }

    return attr;
}

void taurus_attribute_free(struct taurus_attribute* attr) {
    if (!attr) return;

    if (attr->name) free(attr->name);
    if (attr->prefix) free(attr->prefix);
    if (attr->namespace_uri) free(attr->namespace_uri);
    if (attr->value) free(attr->value);

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
    ns = elem->namespaces;
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

/* Add namespace to element's namespace linked list */
int taurus_element_add_namespace(struct taurus_element* elem, struct taurus_namespace* ns) {
    if (!elem || !ns) return -1;

    /* Add to front of linked list */
    ns->next = elem->namespaces;
    elem->namespaces = ns;

    return 0;
}
