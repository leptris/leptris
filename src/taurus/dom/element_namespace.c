/* element_namespace.c - Element namespace manipulation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides namespace manipulation for elements:
 * - Adding namespace declarations
 * - Namespace URI lookup by prefix
 *
 * POINTER-BASED ARCHITECTURE:
 * Namespaces are stored as xmlns:prefix attributes on elements.
 */

#include "element.h"
#include "ptr_element.h"
#include "ptr_accessor.h"
#include "node.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
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
    if (!elem || !uri || !pool) return;

    /* Create xmlns:prefix attribute name */
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    char* attr_name;

    if (prefix && prefix_len > 0) {
        /* Create "xmlns:prefix" */
        attr_name = (char*)taurus_pool_alloc(pool, prefix_len + 7);  /* "xmlns:" + prefix + NUL */
        if (!attr_name) return;
        memcpy(attr_name, "xmlns:", 6);
        memcpy(attr_name + 6, prefix, prefix_len + 1);
    } else {
        /* Default namespace - just "xmlns" */
        attr_name = (char*)taurus_pool_alloc(pool, 6);
        if (!attr_name) return;
        memcpy(attr_name, "xmlns", 6);
    }

    /* Add attribute using the pooled inplace function */
    taurus_element_add_attribute_pooled_inplace(elem, attr_name, uri, pool);
}

/* ============================================================================
 * Namespace Lookup
 * ============================================================================ */

/* Lookup namespace URI by prefix
 *
 * In ptr_element architecture, namespaces are stored as xmlns:prefix attributes.
 * We search the current element and then parent elements.
 */
const char* taurus_element_lookup_namespace(TaurusElement elem,
                                             const char* prefix) {
    if (!elem) return NULL;

    /* Search xmlns:prefix attributes on current element */
    struct ptr_attribute* attr = elem->first_attr;
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

    /* Search parent */
    TaurusElement parent = elem->parent;
    if (parent) {
        return taurus_element_lookup_namespace(parent, prefix);
    }

    return NULL;
}
