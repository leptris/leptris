/* libtaurus - Memory management implementation
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * POINTER-BASED ARCHITECTURE:
 * Uses ptr_element directly for namespace operations.
 * Namespaces are stored as xmlns:prefix attributes.
 */

#include "taurus_memory.h"
#include "dom/element.h"
#include "dom/ptr_element.h"
#include "dom/ptr_accessor.h"
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

    /* Initialize all fields */
    ns->prefix_view = taurus_sv_empty();
    ns->uri_view = taurus_sv_empty();
    ns->prefix = prefix ? taurus_pool_strdup(pool, prefix) : NULL;
    ns->uri = taurus_pool_strdup(pool, uri);
    ns->next = NULL;

    /* If allocation failed, return NULL - pool will be cleaned up on document free */
    if (!ns->uri) {
        return NULL;
    }

    return ns;
}

/* OPTIMIZATION (Phase B): Create namespace with StringViews - ZERO COPY!
 * This is the preferred constructor during parsing as it eliminates all string
 * allocations. The StringViews point directly into the XML buffer.
 */
struct taurus_namespace* taurus_namespace_new_with_views(
    TaurusStringView* prefix_view,
    TaurusStringView* uri_view,
    TaurusMemoryPool* pool
) {
    if (!uri_view || taurus_sv_is_empty(uri_view) || !pool) return NULL;

    struct taurus_namespace* ns = (struct taurus_namespace*)taurus_pool_alloc(pool, sizeof(struct taurus_namespace));
    if (!ns) return NULL;

    /* Store StringViews directly - NO STRING COPIES! */
    ns->prefix_view = prefix_view ? *prefix_view : taurus_sv_empty();
    ns->uri_view = *uri_view;

    /* Initialize cached strings to NULL - lazy conversion on first access */
    ns->prefix = NULL;
    ns->uri = NULL;
    ns->next = NULL;

    /* Check for in-place null termination optimization */
    if (taurus_sv_is_null_terminated(&ns->uri_view)) {
        ns->uri = (char*)ns->uri_view.data;  /* Safe to use directly */
    }
    if (!taurus_sv_is_empty(&ns->prefix_view) && taurus_sv_is_null_terminated(&ns->prefix_view)) {
        ns->prefix = (char*)ns->prefix_view.data;  /* Safe to use directly */
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

/* Find namespace on element
 *
 * In ptr_element architecture, namespaces are stored as xmlns:prefix attributes.
 * This function searches for the namespace in the attribute list.
 */
struct taurus_namespace* taurus_namespace_find(TaurusElement elem, const char* prefix) {
    if (!elem) return NULL;

    /* In ptr_element architecture, namespaces are stored as attributes.
     * Use taurus_element_lookup_namespace which searches xmlns:prefix attributes.
     * This returns a static string, so we create a taurus_namespace structure
     * to maintain API compatibility.
     *
     * NOTE: The returned namespace structure is allocated and should be freed
     * by the caller. For better performance, use taurus_element_lookup_namespace
     * directly.
     */
    const char* uri = taurus_element_lookup_namespace(elem, prefix);
    if (!uri) return NULL;

    /* Create a temporary namespace structure for API compatibility */
    /* Note: Caller should free this */
    return taurus_namespace_new(prefix, uri);
}

/* Add namespace to element
 *
 * In ptr_element architecture, namespaces are stored as xmlns:prefix attributes.
 * This function adds the namespace as an attribute.
 */
int taurus_element_add_namespace(TaurusElement elem, struct taurus_namespace* ns) {
    if (!elem || !ns) return -1;

    /* Get the document pool */
    struct taurus_document* doc = elem->document;
    if (!doc || !doc->pool) return -1;

    /* Add as xmlns:prefix attribute */
    const char* prefix = ns->prefix;
    const char* uri = ns->uri;

    /* Use StringViews if C strings not available */
    if (!uri && !taurus_sv_is_empty(&ns->uri_view)) {
        uri = taurus_sv_to_cstr_pooled(&ns->uri_view, doc->pool);
    }
    if (!prefix && !taurus_sv_is_empty(&ns->prefix_view)) {
        prefix = taurus_sv_to_cstr_pooled(&ns->prefix_view, doc->pool);
    }

    /* Create attribute name */
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    char* attr_name;

    if (prefix && prefix_len > 0) {
        attr_name = (char*)taurus_pool_alloc(doc->pool, prefix_len + 7);
        if (!attr_name) return -1;
        memcpy(attr_name, "xmlns:", 6);
        memcpy(attr_name + 6, prefix, prefix_len + 1);
    } else {
        attr_name = (char*)taurus_pool_alloc(doc->pool, 6);
        if (!attr_name) return -1;
        memcpy(attr_name, "xmlns", 6);
    }

    /* Add attribute */
    TaurusStringView name_view = taurus_sv_from_cstr(attr_name);
    TaurusStringView uri_view = uri ? taurus_sv_from_cstr((char*)uri) : taurus_sv_empty();

    return taurus_element_add_attribute(elem, name_view, uri_view, doc->pool);
}
