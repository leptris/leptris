/* element_copy.c - DOM Copy Operations
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for copying DOM elements and subtrees.
 * COMPACT MODE: Uses compact pointer encoding and accessor functions.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
#include "element.h"
#include "compact.h"
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
 *
 * Checks that parent and source are valid, returns prepared name view for copy.
 *
 * @param parent Target parent element
 * @param source Source element to copy
 * @param name_view Output: prepared name view for the copy
 * @return 1 on success, 0 on error
 */
static int prepare_element_copy(
    TaurusElement parent,
    TaurusElement source,
    TaurusStringView* name_view
) {
    if (!parent || !source || !name_view) return 0;

    volatile struct taurus_document* parent_doc = parent->document;
    if (!parent_doc || !parent_doc->pool) return 0;

    if (!source->document) return 0;

    *name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(name_view)) return 0;

    /* Check if cross-document copy needed */
    int is_cross_doc = (source->document != parent->document);
    if (is_cross_doc) {
        char* name_copy = taurus_sv_to_cstr_pooled(name_view, parent_doc->pool);
        if (!name_copy) return 0;
        *name_view = taurus_sv_from_cstr(name_copy);
    }

    return 1;
}

/**
 * Internal: Copy attributes from source to destination element
 *
 * @param dst Destination element
 * @param src Source element
 * @param is_cross_doc Whether this is a cross-document copy
 */
static void copy_element_attributes(TaurusElement dst, TaurusElement src, int is_cross_doc) {
    uint8_t attr_count = taurus_element_attribute_count(src);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(src, i);
        if (!attr || taurus_sv_is_empty(&attr->name_view)) continue;

        TaurusStringView name_view = attr->name_view;
        TaurusStringView value_view = attr->value_view;

        if (is_cross_doc) {
            char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, dst->document->pool);
            char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, dst->document->pool);
            if (!name_copy || !value_copy) continue;
            name_view = taurus_sv_from_cstr(name_copy);
            value_view = taurus_sv_from_cstr(value_copy);
        }

        if (dst->document->pool) {
            taurus_element_add_attribute(dst, name_view, value_view, dst->document->pool);
        } else {
            const char* name = attr->name ? attr->name : taurus_sv_to_cstr(&attr->name_view);
            const char* value = attr->value ? attr->value : taurus_sv_to_cstr(&attr->value_view);
            if (name && value) {
                taurus_element_set_attribute(dst, name, value);
                if (!attr->name) free((char*)name);
                if (!attr->value) free((char*)value);
            }
        }
    }
}

/**
 * Internal: Copy child nodes from source to destination element
 *
 * @param dst Destination element
 * @param src Source element
 */
static void copy_element_children(TaurusElement dst, TaurusElement src) {
    TaurusElement child = src->first_child;
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;

        if ((uintptr_t)child < 0x1000) break;
        if (child_node->type < TAURUS_NODE_TYPE_ELEMENT ||
            child_node->type > TAURUS_NODE_TYPE_DOCTYPE) break;

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            taurus_element_append_copy(dst, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            if ((uintptr_t)text > 0x1000) {
                TaurusTextNode* text_copy = taurus_text_create(text->content);
                if (text_copy) {
                    taurus_element_append_child_internal(dst, (TaurusNode*)text_copy);
                }
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if ((uintptr_t)cdata > 0x1000) {
                TaurusCDATANode* cdata_copy = taurus_cdata_create(cdata->content);
                if (cdata_copy) {
                    taurus_element_append_child_internal(dst, (TaurusNode*)cdata_copy);
                }
            }
        }

        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }
}

/**
 * Internal: Complete element copy after preparation
 *
 * @param parent Target parent element
 * @param source Source element
 * @param name_view Prepared name view for the copy
 * @return Copy of the element, or NULL on error
 */
static TaurusElement complete_element_copy(
    TaurusElement parent,
    TaurusElement source,
    TaurusStringView name_view
) {
    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    /* Fast path for simple elements */
    if (!taurus_element_get_first_attribute(source) && !source->first_child) {
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

    memset(copy, 0, sizeof(struct taurus_element));
    copy->base.type = TAURUS_NODE_TYPE_ELEMENT;
    copy->name_view = source->name_view;
    copy->prefix_view = source->prefix_view;
    copy->namespace_uri_view = source->namespace_uri_view;
    copy->parent = parent_copy;
    copy->attr_count = source->attr_count;

    /* Copy attributes */
    struct taurus_attribute* src_attr = taurus_element_get_first_attribute(source);
    struct taurus_attribute* prev_dst_attr = NULL;

    while (src_attr) {
        struct taurus_attribute* dst_attr = (struct taurus_attribute*)taurus_pool_alloc(pool, sizeof(struct taurus_attribute));
        if (!dst_attr) break;

        memset(dst_attr, 0, sizeof(struct taurus_attribute));
        dst_attr->name_view = src_attr->name_view;
        dst_attr->value_view = src_attr->value_view;
        dst_attr->prefix_view = src_attr->prefix_view;
        dst_attr->namespace_uri_view = src_attr->namespace_uri_view;
        dst_attr->has_entities = src_attr->has_entities;

        if (src_attr->name) dst_attr->name = taurus_pool_strdup(pool, src_attr->name);
        if (src_attr->value) dst_attr->value = taurus_pool_strdup(pool, src_attr->value);
        if (src_attr->prefix) dst_attr->prefix = taurus_pool_strdup(pool, src_attr->prefix);
        if (src_attr->namespace_uri) dst_attr->namespace_uri = taurus_pool_strdup(pool, src_attr->namespace_uri);

        if (!prev_dst_attr) {
            void* page_base = (char*)copy - (copy->header.page_offset * TAURUS_COMPACT_ALIGNMENT);
            taurus_compact_ptr8_encode(&copy->first_attribute, dst_attr, page_base);
        } else {
            prev_dst_attr->next = dst_attr;
        }
        prev_dst_attr = dst_attr;
        src_attr = src_attr->next;
    }

    /* Copy children */
    TaurusElement first_child = NULL;
    TaurusElement last_child = NULL;
    TaurusElement child = source->first_child;

    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            TaurusElement child_copy = taurus_element_copy_subtree_bulk_internal(
                copy, child, copy_map, copy_index, pool
            );
            if (child_copy) {
                if (!first_child) first_child = child_copy;
                last_child = child_copy;
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content);
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
                if (!first_child) first_child = (TaurusElement)text_copy;
                if (last_child) last_child->next_sibling = (TaurusElement)text_copy;
                last_child = (TaurusElement)text_copy;
            }
        }

        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
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
    TaurusStringView name_view;
    if (prepare_element_copy(parent, source, &name_view) == 0) return NULL;
    return complete_element_copy(parent, source, name_view);
}

TaurusElement taurus_element_prepend_copy(TaurusElement parent, TaurusElement source) {
    TaurusStringView name_view;
    if (prepare_element_copy(parent, source, &name_view) == 0) return NULL;

    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!taurus_element_get_first_attribute(source) && !source->first_child) {
        if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) return NULL;
    return copy;
}

TaurusElement taurus_element_insert_copy_after(TaurusElement sibling, TaurusElement source) {
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    TaurusStringView name_view;
    if (prepare_element_copy(parent, source, &name_view) == 0) return NULL;

    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!taurus_element_get_first_attribute(source) && !source->first_child) {
        if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) return NULL;
        return copy;
    }

    copy_element_attributes(copy, source, source->document != parent->document);
    copy_element_children(copy, source);

    if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) return NULL;
    return copy;
}

TaurusElement taurus_element_insert_copy_before(TaurusElement sibling, TaurusElement source) {
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    TaurusStringView name_view;
    if (prepare_element_copy(parent, source, &name_view) == 0) return NULL;

    TaurusElement copy = taurus_element_create_with_view(name_view, parent->document->pool);
    if (!copy) return NULL;

    copy->document = parent->document;

    if (!taurus_element_get_first_attribute(source) && !source->first_child) {
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

    size_t copy_index = 0;
    TaurusElement copy = taurus_element_copy_subtree_bulk_internal(
        NULL, source, copy_map, &copy_index, pool
    );

    if (!copy) return NULL;

    copy->document = parent->document;

    if (taurus_element_append_child(parent, copy) != TAURUS_OK) return NULL;
    return copy;
}
