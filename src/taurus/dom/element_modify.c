/* lib/src/dom/element_modify.c - DOM Modification API (Pointer-Based)
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for modifying DOM trees in-place.
 * POINTER-BASED ARCHITECTURE: Uses direct pointers for tree navigation.
 * Target: 1.0-1.2x faster than pugixml.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../common/entities.h"
#include "../memory/pool.h"
#include "element.h"
#include "ptr_element.h"
#include "ptr_accessor.h"
#include "node.h"
#include "text.h"
#include "cdata.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Helper macro to emit observer events with minimal overhead when no observers */
#define EMIT_EVENT(elem, type, parent, sibling, name, old_val, new_val) \
    do { \
        if ((elem) && (elem)->document && \
            taurus_document_has_observers((elem)->document)) { \
            taurus_emit_event((elem)->document, type, (struct taurus_element*)(elem), \
                              (struct taurus_element*)(parent), \
                              (struct taurus_element*)(sibling), \
                              name, old_val, new_val); \
        } \
    } while (0)

/* ===========================================================================
 * Public API Implementation - DOM Modification (Pointer-Based)
 * =========================================================================== */

/**
 * Get child count (Public API)
 * NOTE: Only counts ELEMENT children, not text/comment/PI nodes
 */
size_t taurus_element_child_count(TaurusElement elem) {
    if (!elem) return 0;

    /* Count only element children, not text/comment/PI nodes */
    size_t count = 0;
    struct ptr_element* child = elem->first_child;
    while (child) {
        /* Check if this is an element node (type 0) */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            count++;
        }
        child = child->next_sibling;
    }
    return count;
}

/**
 * Create new element in document (Public API)
 */
TaurusElement taurus_element_create(TaurusDocument doc, const char* name) {
    if (!doc || !name) return NULL;

    /* Fast path: use bulk allocation if pool available */
    if (doc->pool) {
        size_t name_len = strlen(name);
        TaurusElement elem = taurus_element_create_fast(name, name_len, doc->pool);
        if (elem) {
            elem->document = doc;  /* Set document pointer */
        }
        return elem;
    }

    /* Fallback: use regular internal creation if no pool */
    TaurusElement elem = taurus_element_create_pooled(name, doc->pool);
    if (elem) {
        elem->document = doc;  /* Set document pointer */
    }
    return elem;
}

/**
 * Append child element (Public API)
 */
TaurusStatus taurus_element_append_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Call internal void function, assume success */
    taurus_element_append_child_internal(parent, (TaurusNode*)child);

    /* Emit observer event */
    EMIT_EVENT(child, TAURUS_EVENT_ELEMENT_ADDED, parent, NULL, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Prepend child element at the beginning (Public API)
 */
TaurusStatus taurus_element_prepend_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Call internal void function, assume success */
    taurus_element_prepend_child_internal(parent, (TaurusNode*)child);

    /* Emit observer event */
    EMIT_EVENT(child, TAURUS_EVENT_ELEMENT_ADDED, parent, NULL, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Insert new node before a sibling (Public API)
 */
TaurusStatus taurus_element_insert_before(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    /* Get parent */
    TaurusElement parent = sibling->parent;
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    /* For element children, we need to handle the linked list properly */
    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Remove new node from old parent if attached */
    if (new_node->parent) {
        /* For now, return error if new_node already has a parent */
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Find previous sibling */
    struct ptr_element* prev_child = NULL;
    struct ptr_element* current = parent->first_child;
    while (current && current != sibling) {
        prev_child = current;
        current = current->next_sibling;
    }

    if (!current) {
        /* Sibling not found */
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Insert new_node before sibling */
    if (prev_child) {
        prev_child->next_sibling = new_node;
    } else {
        /* new_node becomes first child */
        parent->first_child = new_node;
    }
    new_node->next_sibling = sibling;
    new_node->prev_sibling = prev_child;
    sibling->prev_sibling = new_node;

    /* Set parent and document */
    new_node->parent = parent;
    new_node->document = parent->document;

    /* Increment child count */
    parent->child_count++;

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)parent);

    /* Emit observer event */
    EMIT_EVENT(new_node, TAURUS_EVENT_ELEMENT_ADDED, parent, sibling, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Insert new node after a sibling (Public API)
 */
TaurusStatus taurus_element_insert_after(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    /* Get parent */
    TaurusElement parent = sibling->parent;
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Check if new_node already has a parent */
    if (new_node->parent) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Get next sibling */
    struct ptr_element* next_sibling = sibling->next_sibling;

    /* Link sibling to new_node */
    sibling->next_sibling = new_node;
    new_node->prev_sibling = sibling;

    /* Link new_node to next_sibling */
    new_node->next_sibling = next_sibling;
    if (next_sibling) {
        next_sibling->prev_sibling = new_node;
    }

    /* Set parent */
    new_node->parent = parent;
    new_node->document = parent->document;

    /* Update last_child if needed */
    if (parent->last_child == sibling) {
        parent->last_child = new_node;
    }

    /* Increment child count */
    parent->child_count++;

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)parent);

    /* Emit observer event */
    EMIT_EVENT(new_node, TAURUS_EVENT_ELEMENT_ADDED, parent, sibling, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Remove child element (Public API)
 */
TaurusStatus taurus_element_remove_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Verify child is actually a child of parent */
    TaurusElement found = NULL;
    TaurusElement current = parent->first_child;
    while (current) {
        if (current == child) {
            found = current;
            break;
        }
        current = current->next_sibling;
    }

    if (!found) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Find previous and next siblings */
    TaurusElement prev_child = child->prev_sibling;
    TaurusElement next_child = child->next_sibling;

    /* Unlink child from the list */
    if (prev_child) {
        prev_child->next_sibling = next_child;
    } else {
        /* Child was first child */
        parent->first_child = next_child;
    }

    if (next_child) {
        next_child->prev_sibling = prev_child;
    }

    /* Update last_child pointer */
    if (parent->last_child == child) {
        parent->last_child = prev_child;
    }

    /* Clear parent and decrement count */
    child->parent = NULL;
    child->next_sibling = NULL;
    child->prev_sibling = NULL;
    parent->child_count--;

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)parent);

    /* Emit observer event */
    EMIT_EVENT(child, TAURUS_EVENT_ELEMENT_REMOVED, parent, NULL, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Remove all children (Public API)
 */
TaurusStatus taurus_element_remove_all_children(TaurusElement elem) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Walk through all children and clear parent references */
    TaurusElement child = elem->first_child;
    while (child) {
        TaurusElement next = child->next_sibling;
        child->parent = NULL;
        child->prev_sibling = NULL;
        child->next_sibling = NULL;
        child = next;
    }

    /* Clear child pointers */
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->child_count = 0;

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)elem);

    return TAURUS_OK;
}

/**
 * Set element name (Public API)
 */
TaurusStatus taurus_element_set_name(TaurusElement elem, const char* name) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;
    if (!name) return TAURUS_ERROR_INVALID_ARG;

    /* CRITICAL FIX: Don't free old name - it might be pool-allocated!
     * Pool-allocated strings will be freed when the pool is destroyed.
     * If we free() a pool-allocated string, we get undefined behavior. */

    /* Set new name - prefer pool allocation if document is available */
    if (elem->document && elem->document->pool) {
        /* Use pool allocation for consistency with parsing */
        elem->name = taurus_pool_strdup(elem->document->pool, name);
    } else {
        /* Fallback to malloc for standalone elements */
        elem->name = taurus_strdup(name);
    }

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)elem);

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_NAME_CHANGED, NULL, NULL, NULL, NULL, name);

    return TAURUS_OK;
}

/**
 * Set element text content (Public API)
 */
TaurusStatus taurus_element_set_text(TaurusElement elem, const char* text) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Remove all existing children */
    taurus_element_remove_all_children(elem);

    if (text) {
        /* Create new text node */
        TaurusTextNode* text_node = taurus_text_create(text);
        if (!text_node) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Add as child */
        taurus_element_append_child_internal(elem, (TaurusNode*)text_node);
    }

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_TEXT_CHANGED, NULL, NULL, NULL, NULL, text);

    return TAURUS_OK;
}

/**
 * Set attribute (Public API)
 * Uses linked list for attribute storage
 */
TaurusStatus taurus_element_set_attribute(TaurusElement elem, const char* name, const char* value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Get the memory pool from the document */
    TaurusMemoryPool* pool = NULL;
    if (elem->document && elem->document->pool) {
        pool = elem->document->pool;
    }

    /* Check if attribute already exists */
    struct ptr_attribute* existing = ptr_element_find_attr(elem, name);
    if (existing) {
        /* Update existing attribute's value */
        if (pool) {
            if (value) {
                TaurusStringView value_view = taurus_sv_from_cstr((char*)value);
                existing->value = taurus_sv_to_cstr_pooled(&value_view, pool);
                existing->value_view_data = existing->value;
                existing->value_view_length = strlen(existing->value);
            } else {
                existing->value = NULL;
                existing->value_view_data = NULL;
                existing->value_view_length = 0;
            }
        } else {
            /* No pool available - just update the pointer
             * NOTE: We cannot free the old value as it may be pool-allocated.
             * This is a minor memory leak but better than crashing. */
            existing->value = value ? taurus_strdup(value) : NULL;
            existing->value_view_data = existing->value;
            existing->value_view_length = value ? strlen(value) : 0;
        }
    } else {
        /* Create new attribute */
        if (!pool) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Allocate new attribute */
        struct ptr_attribute* attr = (struct ptr_attribute*)taurus_pool_alloc(
            pool, sizeof(struct ptr_attribute));
        if (!attr) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Initialize attribute */
        size_t name_len = strlen(name);
        size_t value_len = value ? strlen(value) : 0;

        char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
        char* value_copy = value ? (char*)taurus_pool_alloc(pool, value_len + 1) : NULL;

        if (!name_copy || (value && !value_copy)) {
            return TAURUS_ERROR_MEMORY;
        }

        memcpy(name_copy, name, name_len + 1);
        if (value) {
            memcpy(value_copy, value, value_len + 1);
        }

        attr->name = name_copy;
        attr->value = value_copy;
        attr->name_view_data = name_copy;
        attr->name_view_length = name_len;
        attr->value_view_data = value_copy;
        attr->value_view_length = value_len;
        attr->next_attr = NULL;

        /* Add to linked list */
        if (!elem->first_attr) {
            elem->first_attr = attr;
        } else {
            /* Find last attribute */
            struct ptr_attribute* last = elem->first_attr;
            while (last->next_attr) {
                last = last->next_attr;
            }
            last->next_attr = attr;
        }

        elem->attr_count++;
    }

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)elem);

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_ATTRIBUTE_SET, NULL, NULL, name, NULL, value);

    return TAURUS_OK;
}

/**
 * Remove attribute (Public API)
 */
TaurusStatus taurus_element_remove_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    struct ptr_attribute* attr = elem->first_attr;
    struct ptr_attribute* prev = NULL;

    while (attr) {
        int match = 0;
        if (attr->name) {
            match = (strcmp(attr->name, name) == 0);
        }

        if (match) {
            /* Found - remove from linked list */
            if (prev) {
                prev->next_attr = attr->next_attr;
            } else {
                elem->first_attr = attr->next_attr;
            }

            /* CRITICAL: Don't free pool-allocated attributes!
             * Just decrement the count and let the pool handle cleanup. */
            elem->attr_count--;

            taurus_node_increment_version((TaurusNode*)elem);

            /* Emit observer event */
            EMIT_EVENT(elem, TAURUS_EVENT_ATTRIBUTE_REMOVED, NULL, NULL, name, NULL, NULL);

            return TAURUS_OK;
        }

        prev = attr;
        attr = attr->next_attr;
    }

    return TAURUS_ERROR_NOT_FOUND;  /* Attribute not found */
}

/**
 * Remove all attributes from element (Public API)
 */
TaurusStatus taurus_element_remove_all_attributes(TaurusElement elem) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Clear attribute pointers */
    elem->first_attr = NULL;
    elem->attr_count = 0;

    /* COW: Increment version */
    taurus_node_increment_version((TaurusNode*)elem);

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_ATTRIBUTE_REMOVED, NULL, NULL, "*all*", NULL, NULL);

    return TAURUS_OK;
}

/* ============================================================================
 * Legacy Attribute API Functions (compatibility wrappers)
 * ============================================================================ */

/* Add attribute (legacy C string API) */
void taurus_element_add_attribute_legacy(
    TaurusElement elem,
    const char* name,
    const char* value
) {
    if (!elem || !name) return;

    /* For now, we need a pool. Use element's document pool if available */
    TaurusMemoryPool* pool = elem->document ? elem->document->pool : NULL;
    if (!pool) {
        return;
    }

    taurus_element_add_attribute_pooled(elem, name, value, pool);
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
    TaurusStringView name_view = taurus_sv_from_cstr((char*)name);
    TaurusStringView value_view = taurus_sv_from_cstr((char*)(value ? value : ""));

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

    struct ptr_attribute* attr = (struct ptr_attribute*)taurus_pool_alloc(
        pool, sizeof(struct ptr_attribute));
    if (!attr) return;

    /* Use strings directly - zero copy */
    attr->name = name;
    attr->value = value;
    attr->name_view_data = name;
    attr->name_view_length = strlen(name);
    attr->value_view_data = value;
    attr->value_view_length = value ? strlen(value) : 0;
    attr->next_attr = NULL;

    /* Add to linked list */
    if (!elem->first_attr) {
        elem->first_attr = attr;
    } else {
        struct ptr_attribute* last = elem->first_attr;
        while (last->next_attr) {
            last = last->next_attr;
        }
        last->next_attr = attr;
    }

    elem->attr_count++;
}

/* Get attribute value by name (legacy API) */
const char* taurus_element_get_attribute_legacy(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    struct ptr_attribute* attr = ptr_element_find_attr(elem, name);
    if (!attr) return NULL;

    return attr->value;
}
