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

    /* Initialize all regular pointers to NULL */
    elem->parent = NULL;
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->next_sibling = NULL;
    elem->first_attribute = NULL;
    elem->last_attribute = NULL;
    elem->children_array = NULL;  /* indexed-access cache, lazy-built (TODO 90: accessor when compact mode lands) */

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

/* Set pointers in element - DIRECT POINTER ACCESS (no encoding!) */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent) {
    if (!elem) return;
    elem->parent = parent;
}

void taurus_element_set_first_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->first_child = (TaurusNode*)child;
}

void taurus_element_set_last_child(TaurusElement elem, TaurusElement child) {
    if (!elem) return;
    elem->last_child = (TaurusNode*)child;
}

void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling) {
    if (!elem) return;
    elem->next_sibling = (TaurusNode*)sibling;
}

/* ============================================================================
 * Attribute Access Functions (still uses compact pointers - cold path)
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

/* Get attribute by index */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Walk the linked list to find the attribute at index */
    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    for (uint8_t i = 0; i < index && attr; i++) {
        /* Validate attr pointer before accessing next */
        if ((uintptr_t)attr < 0x1000) return NULL;  /* Invalid pointer */
        attr = attr->next;
    }

    /* Final validation before returning */
    if ((uintptr_t)attr < 0x1000) return NULL;

    return attr;
}

/* Get attribute by name */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Walk the attribute linked list.  Pre-compute strlen once — the
     * previous version called strlen(name) inside the loop, paying
     * O(N) per attr for an O(N²) total.  See TODO 106. */
    size_t name_len = strlen(name);

    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    while (attr) {
        /* Compare with cached name first (faster) */
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr;
        }
        /* Fall back to StringView comparison */
        if (!taurus_sv_is_empty(&attr->name_view)) {
            if (attr->name_view.length == name_len &&
                memcmp(attr->name_view.data, name, name_len) == 0) {
                return attr;
            }
        }
        attr = attr->next;
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

    /* Allocate attribute from pool */
    struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
        pool, sizeof(struct taurus_attribute));
    if (!attr) return -1;

    /* Initialize attribute */
    attr->name_view = name_view;
    attr->value_view = value_view;

    /* CRITICAL FIX: Initialize namespace/prefix fields to prevent stale data
     * These fields are not set during attribute creation, but they are accessed
     * during finalize_element_strings. Without initialization, they contain
     * garbage data from pool allocation, causing crashes when memory is reused. */
    attr->namespace_uri_view = taurus_sv_empty();
    attr->namespace_uri = NULL;
    attr->prefix_view = taurus_sv_empty();
    attr->prefix = NULL;

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

    attr->next = NULL;

    /* Append via cached last_attribute pointer (TODO 106).
     * Maintains the same invariant as taurus_element_set_attribute. */
    if (elem->last_attribute) {
        elem->last_attribute->next = attr;
    } else {
        taurus_element_set_first_attribute(elem, attr);
    }
    elem->last_attribute = attr;

    /* Increment attribute count */
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

    /* Return cached string if available (lazy conversion already done) */
    if (elem->name) return elem->name;

    /* Convert from StringView to NULL-terminated string */
    if (!taurus_sv_is_empty(&elem->name_view)) {
        /* Store the cached string in the element structure
         * Note: This string should be pool-allocated for proper cleanup */
        /* For now, we'll use the document's pool if available */
        if (elem->document && elem->document->pool) {
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
        if (elem->document && elem->document->pool) {
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
        if (elem->document && elem->document->pool) {
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
    TaurusMemoryPool* pool = elem->document ? elem->document->pool : NULL;
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
        if (elem->document && elem->document->pool) {
            if (attr->has_entities) {
                attr->value = taurus_decode_entities_view(&attr->value_view, elem->document->pool);
            }
            if (!attr->value) {
                attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, elem->document->pool);
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

    /* CRITICAL: Do NOT clear prefix_view when setting namespace prefix */
    if (prefix) {
        elem->prefix = prefix;
    }
    elem->namespace_uri = uri;
    /* Clear StringView for namespace_uri (but NOT prefix_view!) */
    elem->namespace_uri_view = taurus_sv_empty();

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
    struct taurus_namespace* ns = elem->namespaces;
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

    /* For element children, set up linked list structure */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;

        /* Set parent relationship */
        taurus_element_set_parent(child_elem, elem);

        /* Set document pointer */
        child_elem->document = elem->document;

        /* Append to end of children list */
        /* NOTE: last_child might point to a non-element node, so we need to handle both cases */
        TaurusElement last = (TaurusElement)elem->last_child;
        if (last) {
            /* last points to the last child element */
            TaurusNode* last_node = (TaurusNode*)last;

            /* Set next_sibling on the last child based on its type */
            if (last_node->type == TAURUS_NODE_TYPE_ELEMENT) {
                /* Last child is an element - use direct pointer assignment */
                ((TaurusElement)last_node)->next_sibling = (TaurusNode*)child_elem;
            } else if (last_node->type == TAURUS_NODE_TYPE_TEXT) {
                /* Last child is a text node - use direct pointer assignment */
                ((TaurusTextNode*)last_node)->next_sibling = (TaurusNode*)child;
            } else if (last_node->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)last_node)->next_sibling = (TaurusNode*)child;
            } else if (last_node->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)last_node)->next_sibling = (TaurusNode*)child;
            } else if (last_node->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)last_node)->next_sibling = (TaurusNode*)child;
            }

            /* Set last_child to the new child */
            elem->last_child = (TaurusNode*)child_elem;
        } else {
            /* No children yet - set first and last child */
            elem->first_child = (TaurusNode*)child_elem;
            elem->last_child = (TaurusNode*)child_elem;
        }

        /* Increment child count */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), append to linked list */
        TaurusNode* last = (TaurusNode*)elem->last_child;
        if (last) {
            /* Set next_sibling on the last child based on last's type */
            if (last->type == TAURUS_NODE_TYPE_ELEMENT) {
                /* Element last: directly set the next_sibling pointer */
                ((TaurusElement)last)->next_sibling = (TaurusNode*)child;
            } else if (last->type == TAURUS_NODE_TYPE_TEXT) {
                ((TaurusTextNode*)last)->next_sibling = (TaurusNode*)child;
            } else if (last->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)last)->next_sibling = (TaurusNode*)child;
            } else if (last->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)last)->next_sibling = (TaurusNode*)child;
            } else if (last->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)last)->next_sibling = (TaurusNode*)child;
            }

            /* Set last_child to child using direct pointer assignment */
            elem->last_child = (TaurusNode*)child;
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            elem->first_child = (TaurusNode*)child;
            elem->last_child = (TaurusNode*)child;
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
        TaurusElement first = (TaurusElement)elem->first_child;
        if (first) {
            /* first points to the first child (could be element or non-element) */
            TaurusNode* first_node = (TaurusNode*)first;

            /* Set the new child's next_sibling to point to the first child */
            /* Since the new child is an element, use direct pointer assignment */
            child_elem->next_sibling = (TaurusNode*)(TaurusElement)first_node;

            /* Set first_child to the new child */
            elem->first_child = (TaurusNode*)child_elem;
        } else {
            /* No children yet - set first and last child */
            elem->first_child = (TaurusNode*)child_elem;
            elem->last_child = (TaurusNode*)child_elem;
        }

        /* Increment child count */
        elem->child_count++;
    } else {
        /* For non-element children (text, cdata, comment, pi), insert at beginning */
        TaurusNode* first = (TaurusNode*)elem->first_child;
        if (first) {
            /* Set next_sibling on the new child based on child's type */
            if (child->type == TAURUS_NODE_TYPE_TEXT) {
                ((TaurusTextNode*)child)->next_sibling = (TaurusNode*)first;
            } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
                ((TaurusCDATANode*)child)->next_sibling = (TaurusNode*)first;
            } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
                ((TaurusCommentNode*)child)->next_sibling = (TaurusNode*)first;
            } else if (child->type == TAURUS_NODE_TYPE_PI) {
                ((TaurusPINode*)child)->next_sibling = (TaurusNode*)first;
            }

            /* Set first_child to child using direct pointer assignment */
            elem->first_child = (TaurusNode*)child;
        } else {
            /* No children yet - set first and last child using direct pointer assignment */
            elem->first_child = (TaurusNode*)child;
            elem->last_child = (TaurusNode*)child;
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
            if (text->content) {
                len += strlen(text->content);
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
            if (text->content) {
                size_t len = strlen(text->content);
                memcpy(result + *offset, text->content, len);
                *offset += len;
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
    if (!elem) return;

    elem->document = doc;

    /* Recursively set document pointer on all element children
     * CRITICAL: Only recurse on element nodes, not text/comment/CDATA/etc.
     * Use generic node navigation to handle mixed content correctly. */
    TaurusNode* child = taurus_node_first_child_internal((TaurusNode*)elem);
    while (child) {
        /* Check if child is an element node before recursing */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            taurus_element_set_document_tree((TaurusElement)child, doc);
        }
        child = taurus_node_get_next_sibling(child);
    }
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
        struct taurus_attribute* next = attr->next;
        /* Don't free attr or any of its strings - all pool-allocated!
         * They will be reclaimed when the document/pool is freed. */
        attr = next;
    }

    /* Clear the first_attribute pointer and count */
    elem->first_attribute = NULL;
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
    TaurusNode* child = (TaurusNode*)elem->first_child;
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
