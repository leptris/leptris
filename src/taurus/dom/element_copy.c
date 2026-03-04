/* element_copy.c - DOM Copy Operations
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for copying DOM elements and subtrees.
 * POINTER-BASED ARCHITECTURE: Uses direct pointers for tree navigation.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
#include "element.h"
#include "ptr_element.h"
#include "ptr_accessor.h"
#include "node.h"
#include "text.h"
#include "cdata.h"
#include <string.h>

/* Forward declaration from element_modify.c */
TaurusStatus taurus_element_remove_all_children(TaurusElement elem);

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Internal: Validate and prepare for element copy
 */
static int prepare_element_copy(
    TaurusElement parent,
    TaurusElement source,
    const char** name_out
) {
    if (!parent || !source || !name_out) return 0;

    volatile struct taurus_document* parent_doc = parent->document;
    if (!parent_doc || !parent_doc->pool) return 0;

    if (!source->document) return 0;

    *name_out = source->name;
    if (!*name_out) return 0;

    return 1;
}

/**
 * Internal: Copy attributes from source to destination element
 */
static void copy_element_attributes(TaurusElement dst, TaurusElement src, int is_cross_doc) {
    struct ptr_attribute* attr = src->first_attr;
    while (attr) {
        const char* name = attr->name;
        const char* value = attr->value;

        if (!name) {
            attr = attr->next_attr;
            continue;
        }

        if (dst->document && dst->document->pool) {
            TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
            TaurusStringView value_view = value ? taurus_sv_from_cstr((char*)value) : taurus_sv_empty();

            if (is_cross_doc) {
                /* Copy strings for cross-document */
                char* name_copy = taurus_sv_to_cstr_pooled(&name_view, dst->document->pool);
                char* value_copy = value ? taurus_sv_to_cstr_pooled(&value_view, dst->document->pool) : NULL;
                if (name_copy) {
                    name_view = taurus_sv_from_cstr(name_copy);
                }
                if (value && value_copy) {
                    value_view = taurus_sv_from_cstr(value_copy);
                }
            }

            taurus_element_add_attribute(dst, name_view, value_view, dst->document->pool);
        } else {
            taurus_element_set_attribute(dst, name, value);
        }

        attr = attr->next_attr;
    }
}

/**
 * Internal: Copy child nodes from source to destination element
 */
static void copy_element_children(TaurusElement dst, TaurusElement src) {
    struct ptr_element* child = src->first_child;
    while (child) {
        /* Check node type via first field */
        uint32_t node_type = child->type;

        if (node_type == TAURUS_NODE_TYPE_ELEMENT) {
            taurus_element_append_copy(dst, child);
        } else if (node_type == TAURUS_NODE_TYPE_TEXT) {
            struct ptr_text* text = (struct ptr_text*)child;
            if (text->text) {
                TaurusTextNode* text_copy = taurus_text_create(text->text);
                if (text_copy) {
                    taurus_element_append_child_internal(dst, (TaurusNode*)text_copy);
                }
            }
        } else if (node_type == TAURUS_NODE_TYPE_CDATA) {
            struct ptr_text* cdata = (struct ptr_text*)child;
            if (cdata->text) {
                TaurusCDATANode* cdata_copy = taurus_cdata_create(cdata->text);
                if (cdata_copy) {
                    taurus_element_append_child_internal(dst, (TaurusNode*)cdata_copy);
                }
            }
        }

        child = child->next_sibling;
    }
}

/**
 * Internal: Complete element copy after preparation
 */
static TaurusElement complete_element_copy(
    TaurusElement parent,
    TaurusElement source,
    const char* name
) {
    TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    /* Fast path for simple elements */
    if (!source->first_attr && !source->first_child) {
        if (taurus_element_append_child(parent, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    /* Copy attributes and children */
    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_append_child(parent, copy) != TAURUS_OK) return NULL;
    return copy;
}

/* ============================================================================
 * Bulk Allocation Helpers
 * ============================================================================ */

static TaurusElement* taurus_element_create_copy_map(TaurusMemoryPool* pool, size_t count) {
    if (!pool || count == 0) return NULL;
    return (TaurusElement*)taurus_pool_alloc(pool, sizeof(TaurusElement) * count);
}

static TaurusElement taurus_element_copy_subtree_bulk_internal(
    TaurusElement parent_copy,
    TaurusElement source,
    TaurusElement* copy_map,
    size_t* copy_index,
    TaurusMemoryPool* pool
) {
    if (!source || !copy_map || !copy_index) return NULL;

    size_t this_index = *copy_index;
    (*copy_index)++;

    TaurusElement copy = copy_map[this_index];
    if (!copy) return NULL;

    /* Initialize copy using ptr_element */
    memset(copy, 0, sizeof(struct ptr_element));
    copy->type = TAURUS_NODE_TYPE_ELEMENT;
    copy->name = source->name;  /* Will be duplicated if needed */
    copy->parent = parent_copy;
    copy->attr_count = source->attr_count;
    copy->document = parent_copy ? parent_copy->document : NULL;

    /* Copy attributes */
    struct ptr_attribute* src_attr = source->first_attr;
    struct ptr_attribute* prev_dst_attr = NULL;

    while (src_attr) {
        struct ptr_attribute* dst_attr = (struct ptr_attribute*)taurus_pool_alloc(
            pool, sizeof(struct ptr_attribute));
        if (!dst_attr) break;

        memset(dst_attr, 0, sizeof(struct ptr_attribute));
        dst_attr->name = src_attr->name ? taurus_pool_strdup(pool, src_attr->name) : NULL;
        dst_attr->value = src_attr->value ? taurus_pool_strdup(pool, src_attr->value) : NULL;
        dst_attr->name_view_data = dst_attr->name;
        dst_attr->name_view_length = dst_attr->name ? strlen(dst_attr->name) : 0;
        dst_attr->value_view_data = dst_attr->value;
        dst_attr->value_view_length = dst_attr->value ? strlen(dst_attr->value) : 0;

        if (!prev_dst_attr) {
            copy->first_attr = dst_attr;
        } else {
            prev_dst_attr->next_attr = dst_attr;
        }
        prev_dst_attr = dst_attr;
        src_attr = src_attr->next_attr;
    }

    /* Copy children */
    struct ptr_element* first_child = NULL;
    struct ptr_element* last_child = NULL;
    struct ptr_element* child = source->first_child;

    while (child) {
        uint32_t node_type = child->type;

        if (node_type == TAURUS_NODE_TYPE_ELEMENT) {
            TaurusElement child_copy = taurus_element_copy_subtree_bulk_internal(
                copy, child, copy_map, copy_index, pool
            );
            if (child_copy) {
                if (!first_child) first_child = child_copy;
                if (last_child) last_child->next_sibling = child_copy;
                child_copy->prev_sibling = last_child;
                last_child = child_copy;
            }
        } else if (node_type == TAURUS_NODE_TYPE_TEXT) {
            struct ptr_text* text = (struct ptr_text*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->text);
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }

        child = child->next_sibling;
    }

    copy->first_child = first_child;
    copy->last_child = last_child;
    copy->child_count = source->child_count;

    return copy;
}

/* ============================================================================
 * Public API Implementation - Copy Operations
 * ============================================================================ */

TaurusElement taurus_element_append_copy(TaurusElement parent, TaurusElement source) {
    const char* name;
    if (prepare_element_copy(parent, source, &name) == 0) return NULL;
    return complete_element_copy(parent, source, name);
}

TaurusElement taurus_element_prepend_copy(TaurusElement parent, TaurusElement source) {
    const char* name;
    if (prepare_element_copy(parent, source, &name) == 0) return NULL;

    TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!source->first_attr && !source->first_child) {
        if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) return NULL;
    return copy;
}

TaurusElement taurus_element_insert_copy_after(TaurusElement sibling, TaurusElement source) {
    TaurusElement parent = sibling->parent;
    if (!parent) return NULL;

    const char* name;
    if (prepare_element_copy(parent, source, &name) == 0) return NULL;

    TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!source->first_attr && !source->first_child) {
        if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) return NULL;
    return copy;
}

TaurusElement taurus_element_insert_copy_before(TaurusElement sibling, TaurusElement source) {
    TaurusElement parent = sibling->parent;
    if (!parent) return NULL;

    const char* name;
    if (prepare_element_copy(parent, source, &name) == 0) return NULL;

    TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!source->first_attr && !source->first_child) {
        if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) return NULL;
    return copy;
}

TaurusStatus taurus_element_remove_children(TaurusElement elem) {
    return taurus_element_remove_all_children(elem);
}

TaurusElement taurus_element_append_copy_bulk(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;
    if (!parent->document || !parent->document->pool) {
        return taurus_element_append_copy(parent, source);
    }

    TaurusMemoryPool* pool = parent->document->pool;
    TaurusSubtreeStats stats;
    taurus_element_count_subtree(source, &stats);

    size_t total_elements = stats.element_count;
    if (total_elements == 0) return NULL;

    TaurusElement* copy_map = taurus_element_create_copy_map(pool, total_elements);
    if (!copy_map) return taurus_element_append_copy(parent, source);

    /* Pre-allocate all elements */
    for (size_t i = 0; i < total_elements; i++) {
        copy_map[i] = (TaurusElement)taurus_pool_alloc(pool, sizeof(struct ptr_element));
        if (!copy_map[i]) {
            return taurus_element_append_copy(parent, source);
        }
    }

    size_t copy_index = 0;
    TaurusElement copy = taurus_element_copy_subtree_bulk_internal(
        NULL, source, copy_map, &copy_index, pool
    );

    if (!copy) return NULL;

    copy->document = parent->document;

    if (taurus_element_append_child(parent, copy) != TAURUS_OK) return NULL;
    return copy;
}
