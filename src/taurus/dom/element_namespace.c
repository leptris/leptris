/* element_namespace.c - Element namespace manipulation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides namespace manipulation for elements:
 * - Adding namespace declarations
 * - Namespace URI lookup by prefix
 */

#include "element.h"
#include "node.h"
#include "../common/string_view.h"
#include "../taurus_memory.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Namespace Manipulation
 * ============================================================================ */

/* Add namespace with in-place strings (zero-copy) */
void taurus_element_add_namespace_inplace(TaurusElement elem,
                                           char* prefix,
                                           char* uri,
                                           TaurusMemoryPool* pool) {
    (void)pool;  /* Unused in compact mode */
    if (!elem || !uri) return;

    /* Store namespace in element's prefix/namespace_uri fields for now */
    /* TODO: Implement proper namespace list */

    /* CRITICAL: Do NOT clear prefix_view when setting namespace prefix */
    if (prefix) {
        elem->prefix = prefix;
    }
    elem->namespace_uri = uri;
    /* Clear StringView for namespace_uri (but NOT prefix_view!) */
    elem->namespace_uri_view = taurus_sv_empty();
}

/* ============================================================================
 * Namespace Lookup
 * ============================================================================ */

/* Lookup namespace URI by prefix */
const char* taurus_element_lookup_namespace(TaurusElement elem,
                                             const char* prefix) {
    if (!elem) return NULL;

    /* Search namespaces linked list on current element
     * OPTIMIZATION (Phase B): Check both C strings and StringViews
     * After Phase B optimization, prefix/uri may be NULL while prefix_view/uri_view have data */
    struct taurus_namespace* ns = elem->namespaces;
    while (ns) {
        /* Get prefix - check C string first, then StringView */
        const char* ns_prefix = ns->prefix;
        char* ns_prefix_alloc = NULL;
        if (!ns_prefix && !taurus_sv_is_empty(&ns->prefix_view)) {
            ns_prefix_alloc = taurus_sv_to_cstr(&ns->prefix_view);
            ns_prefix = ns_prefix_alloc;
        }

        int prefix_matches = 0;
        if ((prefix == NULL && ns_prefix == NULL) ||
            (prefix && ns_prefix && strcmp(prefix, ns_prefix) == 0)) {
            prefix_matches = 1;
        }

        if (prefix_matches) {
            /* Get URI - check C string first, then StringView */
            const char* uri = ns->uri;
            char* uri_alloc = NULL;
            if (!uri && !taurus_sv_is_empty(&ns->uri_view)) {
                uri_alloc = taurus_sv_to_cstr(&ns->uri_view);
                uri = uri_alloc;
            }

            /* If we found a URI, we need to return it (caller's responsibility to free if needed) */
            if (uri) {
                /* Cache it in the namespace struct for future lookups */
                if (!ns->uri && uri_alloc) {
                    ns->uri = uri_alloc;  /* Take ownership */
                    uri_alloc = NULL;  /* Don't free below */
                }
                if (ns_prefix_alloc) free(ns_prefix_alloc);
                return uri;
            }
        }

        if (ns_prefix_alloc) free(ns_prefix_alloc);
        ns = ns->next;
    }

    /* Search parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (parent) {
        return taurus_element_lookup_namespace(parent, prefix);
    }

    return NULL;
}
