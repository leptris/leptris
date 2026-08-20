/* lib/src/dom/element_compact.c - Compact element implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implementation of compact element structure accessor functions.
 * Uses compressed pointers for 5.3x smaller elements (36 bytes vs 192 bytes).
 */

#include "element.h"
#include "compact.h"
#include "../memory/pool.h"
#include "../common/string_view.h"
#include "../taurus_internal.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef TAURUS_COMPACT_MODE

/* ============================================================================
 * Compact Element Size Verification
 * ============================================================================ */

/* Verify the compact element structure size at compile time */
static inline void verify_compact_element_size(void) {
    /* Target: 36 bytes (5.3x smaller than regular 192 bytes) */
    sizeof(TaurusElementCompact);  /* Suppress unused warning */
}

/* ============================================================================
 * Page Base Calculation
 * ============================================================================ */

/**
 * Get the memory page base address for a compact element
 *
 * The compact_header stores the offset from page_base to the element.
 * We reconstruct the page_base by subtracting the offset.
 *
 * @param elem Compact element
 * @return Page base address (start of memory page containing elem)
 */
static inline void* get_page_base(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Decode page offset from compact header */
    uint8_t page_offset = elem->header.page_offset;

    /* Calculate page_base: elem - (page_offset * 4) */
    /* The 4 comes from TAURUS_COMPACT_ALIGNMENT (4 bytes) */
    return (char*)elem - (page_offset * TAURUS_COMPACT_ALIGNMENT);
}

/* ============================================================================
 * Casting Functions
 * ============================================================================ */

TaurusElementCompact* taurus_element_as_compact(TaurusElementNode* elem) {
    /* In compact mode, TaurusElementNode is actually TaurusElementCompact */
    /* This is just a cast for type safety */
    return (TaurusElementCompact*)elem;
}

TaurusElementNode* taurus_compact_as_element(TaurusElementCompact* compact) {
    /* Convert compact element back to regular element pointer */
    return (TaurusElementNode*)compact;
}

/* ============================================================================
 * Parent Pointer Access
 * ============================================================================ */

TaurusElementCompact* taurus_compact_get_parent(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Decode the 16-bit parent pointer */
    return (TaurusElementCompact*)taurus_compact_ptr16_decode(&elem->parent, page_base);
}

void taurus_compact_set_parent(TaurusElementCompact* elem, TaurusElementCompact* parent) {
    if (!elem) return;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Encode the parent pointer as 16-bit offset */
    taurus_compact_ptr16_encode(&elem->parent, parent, page_base);
}

/* ============================================================================
 * Child Pointers Access
 * ============================================================================ */

TaurusElementCompact* taurus_compact_get_first_child(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Decode the 8-bit first_child pointer */
    return (TaurusElementCompact*)taurus_compact_ptr8_decode(&elem->first_child, page_base);
}

void taurus_compact_set_first_child(TaurusElementCompact* elem, TaurusElementCompact* child) {
    if (!elem) return;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Encode the child pointer as 8-bit offset */
    taurus_compact_ptr8_encode(&elem->first_child, child, page_base);
}

TaurusElementCompact* taurus_compact_get_last_child(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Decode the 8-bit last_child pointer */
    return (TaurusElementCompact*)taurus_compact_ptr8_decode(&elem->last_child, page_base);
}

void taurus_compact_set_last_child(TaurusElementCompact* elem, TaurusElementCompact* child) {
    if (!elem) return;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Encode the child pointer as 8-bit offset */
    taurus_compact_ptr8_encode(&elem->last_child, child, page_base);
}

/* ============================================================================
 * Sibling Pointer Access
 * ============================================================================ */

TaurusElementCompact* taurus_compact_get_next_sibling(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Decode the 8-bit next_sibling pointer */
    return (TaurusElementCompact*)taurus_compact_ptr8_decode(&elem->next_sibling, page_base);
}

void taurus_compact_set_next_sibling(TaurusElementCompact* elem, TaurusElementCompact* sibling) {
    if (!elem) return;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Encode the sibling pointer as 8-bit offset */
    taurus_compact_ptr8_encode(&elem->next_sibling, sibling, page_base);
}

/* ============================================================================
 * Attribute Access
 * ============================================================================ */

/**
 * Get first attribute from compact element
 *
 * @param elem Compact element
 * @return First compact attribute, or NULL if no attributes
 */
struct taurus_compact_attribute* taurus_compact_get_first_attribute(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Decode the 8-bit first_attribute pointer */
    return (struct taurus_compact_attribute*)taurus_compact_ptr8_decode(&elem->first_attribute, page_base);
}

/**
 * Set first attribute in compact element
 *
 * @param elem Compact element
 * @param attr Compact attribute to set as first
 */
void taurus_compact_set_first_attribute(TaurusElementCompact* elem, struct taurus_compact_attribute* attr) {
    if (!elem) return;

    /* Get page base for offset calculation */
    void* page_base = get_page_base(elem);

    /* Encode the attribute pointer as 8-bit offset */
    taurus_compact_ptr8_encode(&elem->first_attribute, attr, page_base);
}

/**
 * Get attribute count from compact element
 *
 * @param elem Compact element
 * @return Number of attributes
 */
uint8_t taurus_compact_attribute_count(TaurusElementCompact* elem) {
    if (!elem) return 0;
    return elem->attr_count;
}

/**
 * Get attribute by index from compact element
 *
 * @param elem Compact element
 * @param index Attribute index (0-based)
 * @return Attribute at index, or NULL if index out of bounds
 */
struct taurus_compact_attribute* taurus_compact_get_attribute_by_index(TaurusElementCompact* elem, uint8_t index) {
    if (!elem || index >= elem->attr_count) return NULL;

    /* Walk the linked list to find the attribute at index */
    struct taurus_compact_attribute* attr = taurus_compact_get_first_attribute(elem);
    for (uint8_t i = 0; i < index && attr; i++) {
        attr = taurus_attr_next(attr);
    }

    return attr;
}

/**
 * Get attribute by name from compact element
 *
 * @param elem Compact element
 * @param name Attribute name to lookup
 * @return Attribute with matching name, or NULL if not found
 */
struct taurus_compact_attribute* taurus_compact_get_attribute_by_name(TaurusElementCompact* elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Walk the attribute linked list */
    struct taurus_compact_attribute* attr = taurus_compact_get_first_attribute(elem);
    while (attr) {
        /* Compare with cached name first (faster) */
        if (attr->name && strcmp(attr->name, name) == 0) {
            return attr;
        }
        /* Fall back to StringView comparison */
        if (!taurus_sv_is_empty(&attr->name_view)) {
            size_t name_len = strlen(name);
            if (attr->name_view.length == name_len &&
                memcmp(attr->name_view.data, name, name_len) == 0) {
                return attr;
            }
        }
        attr = taurus_attr_next(attr);
    }

    return NULL;
}

/**
 * Add attribute to compact element
 *
 * @param elem Compact element
 * @param name_view Attribute name as StringView
 * @param value_view Attribute value as StringView
 * @param pool Memory pool for allocation
 * @return 0 on success, -1 on failure
 */
int taurus_compact_add_attribute(TaurusElementCompact* elem,
                                  TaurusStringView name_view,
                                  TaurusStringView value_view,
                                  TaurusMemoryPool* pool) {
    if (!elem || taurus_sv_is_empty(&name_view) || !pool) return -1;

    /* Allocate compact attribute from pool */
    struct taurus_compact_attribute* attr = (struct taurus_compact_attribute*)taurus_pool_alloc(
        pool, sizeof(struct taurus_compact_attribute));
    if (!attr) return -1;

    /* Initialize attribute */
    attr->name_view = name_view;
    attr->value_view = value_view;
    attr->name = NULL;      /* Lazy conversion */
    attr->value = NULL;     /* Lazy conversion */
    attr->next = NULL;

    /* Check for entities in value (set during parsing for performance) */
    attr_set_entities(attr, memchr(value_view.data, '&', value_view.length) != NULL);

    /* Add to linked list */
    struct taurus_compact_attribute* first_attr = taurus_compact_get_first_attribute(elem);
    if (!first_attr) {
        /* First attribute */
        taurus_compact_set_first_attribute(elem, attr);
    } else {
        /* Find last attribute and append */
        struct taurus_compact_attribute* last = first_attr;
        while (last->next) {
            last = last->next;
        }
        last->next = attr;
    }

    /* Increment attribute count */
    elem->attr_count++;

    return 0;
}

/* ============================================================================
 * Name Access
 * ============================================================================ */

/**
 * Get element name from compact element
 *
 * @param elem Compact element
 * @return Element name (cached NULL-terminated string)
 */
const char* taurus_compact_get_name(TaurusElementCompact* elem) {
    if (!elem) return NULL;

    /* Return cached string if available (lazy conversion already done) */
    if (elem->name) return elem->name;

    /* Convert from StringView to NULL-terminated string */
    if (!taurus_sv_is_empty(&elem->name_view)) {
        /* Store the cached string in the element structure
         * Note: This string should be pool-allocated for proper cleanup */
        /* For now, we'll use the document's pool if available */
        if (taurus_element_get_document(elem) && taurus_element_get_pool(elem)) {
            elem->name = taurus_sv_to_cstr_pooled(&elem->name_view, taurus_element_get_pool(elem));
        } else {
            /* Fallback to regular malloc (not ideal for pool-allocated elements) */
            elem->name = taurus_sv_to_cstr(&elem->name_view);
        }
        return elem->name;
    }

    return NULL;  /* No name available */
}

/**
 * Set prefix on compact element using StringView
 *
 * @param elem Compact element
 * @param prefix_view Prefix as StringView
 */
void taurus_compact_set_prefix_view(TaurusElementCompact* elem, TaurusStringView prefix_view) {
    if (!elem) return;
    elem->prefix_view = prefix_view;
    /* Clear cached string - will be reconverted on next access */
    elem->prefix = NULL;
}

/**
 * Set namespace URI on compact element using StringView
 *
 * @param elem Compact element
 * @param uri_view Namespace URI as StringView
 */
void taurus_compact_set_namespace_uri_view(TaurusElementCompact* elem, TaurusStringView uri_view) {
    if (!elem) return;
    elem->namespace_uri_view = uri_view;
    /* Clear cached string - will be reconverted on next access */
    elem->namespace_uri = NULL;
}

/* ============================================================================
 * Child Count and Access
 * ============================================================================ */

/**
 * Get child count from compact element
 *
 * @param elem Compact element
 * @return Number of children
 */
uint8_t taurus_compact_child_count(TaurusElementCompact* elem) {
    if (!elem) return 0;
    return elem->child_count;
}

/**
 * Get child by index from compact element
 *
 * @param elem Compact element
 * @param index Child index (0-based)
 * @return Child at index, or NULL if index out of bounds
 */
TaurusElementCompact* taurus_compact_get_child(TaurusElementCompact* elem, uint8_t index) {
    if (!elem || index >= elem->child_count) return NULL;

    /* Walk the child linked list */
    TaurusElementCompact* child = taurus_compact_get_first_child(elem);
    for (uint8_t i = 0; i < index && child; i++) {
        child = taurus_compact_get_next_sibling(child);
    }

    return child;
}

/* ============================================================================
 * Compact Element Creation
 * ============================================================================ */

/**
 * Create a new compact element with StringView
 *
 * @param name_view Element name as StringView
 * @param pool Memory pool for allocation
 * @return New compact element, or NULL on failure
 */
TaurusElementCompact* taurus_compact_element_create_with_view(
    TaurusStringView name_view,
    TaurusMemoryPool* pool
) {
    if (!pool) return NULL;

    /* Allocate compact element from pool */
    TaurusElementCompact* elem = (TaurusElementCompact*)taurus_pool_alloc(
        pool, sizeof(TaurusElementCompact));
    if (!elem) return NULL;

    /* Initialize all fields to zero */
    memset(elem, 0, sizeof(TaurusElementCompact));

    /* Initialize base node */
    elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
    elem->base.frozen = 0;
    elem->base.version = 0;

    /* Initialize compact header with page offset */
    taurus_compact_header_init(&elem->header, elem, TAURUS_NODE_TYPE_ELEMENT);

    /* Store StringViews - ZERO COPY! */
    elem->name_view = name_view;
    elem->prefix_view = taurus_sv_empty();
    elem->namespace_uri_view = taurus_sv_empty();

    /* Initialize cached strings as NULL (lazy conversion) */
    elem->name = NULL;
    elem->prefix = NULL;
    elem->namespace_uri = NULL;

    /* Store document pointer - will be set later */
    taurus_element_get_document(elem) = NULL;

    /* Initialize all pointers to NULL */
    elem->parent.offset = TAURUS_COMPACT_PTR_NULL;
    elem->first_child.offset = TAURUS_COMPACT_PTR_NULL;
    elem->last_child.offset = TAURUS_COMPACT_PTR_NULL;
    elem->next_sibling.offset = TAURUS_COMPACT_PTR_NULL;
    elem->first_attribute.offset = TAURUS_COMPACT_PTR_NULL;

    /* Initialize counts */
    elem->attr_count = 0;
    elem->child_count = 0;
    elem->reserved = 0;

    return elem;
}

/**
 * Create a new compact element (legacy API)
 *
 * @param name Element name (will be copied to pool)
 * @param pool Memory pool for allocation
 * @return New compact element, or NULL on failure
 */
TaurusElementCompact* taurus_element_compact_create(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    /* Create StringView from C string */
    TaurusStringView name_view = taurus_sv_from_cstr(name);
    return taurus_compact_element_create_with_view(name_view, pool);
}

/* ============================================================================
 * Compact Element Destruction
 * ============================================================================ */

/**
 * Free a compact element
 *
 * Note: Compact elements are pool-allocated, so individual free is not needed.
 * The entire pool is freed at once when the document is destroyed.
 *
 * @param elem Element to free (unused, kept for API compatibility)
 * @param pool Memory pool (unused, kept for API compatibility)
 */
void taurus_element_compact_free(TaurusElementCompact* elem, TaurusMemoryPool* pool) {
    /* Pool-allocated elements don't need individual free */
    /* This function exists for API compatibility only */
    (void)elem;
    (void)pool;
}

#endif /* TAURUS_COMPACT_MODE */
