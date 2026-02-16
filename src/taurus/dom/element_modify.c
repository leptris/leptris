/* lib/src/dom/element_modify.c - DOM Modification API (Compact Mode)
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for modifying DOM trees in-place.
 * COMPACT MODE: Uses compact pointer encoding and accessor functions.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../common/entities.h"
#include "../memory/pool.h"
#include "element.h"
#include "compact.h"
#include "node.h"
#include "text.h"
#include "cdata.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===========================================================================
 * Internal Helper: Rebuild Children Array
 *
 * The inline children[4] array is used for O(1) child access by index.
 * This function rebuilds the array from the linked list after modifications.
 * =========================================================================== */

/* Helper macro to emit observer events with minimal overhead when no observers */
#define EMIT_EVENT(elem, type, parent, sibling, name, old_val, new_val) \
    do { \
        if ((elem) && (elem)->document && \
            taurus_document_has_observers((elem)->document)) { \
            taurus_emit_event((elem)->document, type, (elem), parent, sibling, \
                              name, old_val, new_val); \
        } \
    } while (0)

static void rebuild_children_array(TaurusElement parent) {
    if (!parent) return;

    /* Clear the array first */
    memset(parent->children, 0, sizeof(parent->children));

    /* Walk the linked list and populate the array with element children */
    TaurusNode* child = parent->first_child;
    uint16_t array_index = 0;

    while (child && array_index < 4) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            parent->children[array_index++] = child;
        }
        child = taurus_node_get_next_sibling(child);
    }
}

/* ===========================================================================
 * Public API Implementation - DOM Modification (Compact Mode)
 * =========================================================================== */

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
 * COMPACT MODE: Simplified implementation - only handles element insertion
 */
TaurusStatus taurus_element_insert_before(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    /* Get parent using accessor function */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    /* For element children, we need to handle the linked list properly */
    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Remove new node from old parent if attached */
    if (taurus_element_parent(new_node)) {
        /* For now, return error if new_node already has a parent */
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* CRITICAL FIX: Use generic node API to find previous sibling
     * The element-only API skips text nodes, which causes text to be lost
     * when inserting elements. We must use taurus_node_first_child_internal and
     * taurus_node_get_next_sibling to preserve all node types. */
    TaurusNode* prev_child = NULL;
    TaurusNode* current = taurus_node_first_child_internal((TaurusNode*)parent);
    while (current && current != sibling_ptr) {
        prev_child = current;
        current = taurus_node_get_next_sibling(current);
    }

    if (!current) {
        /* Sibling not found */
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Insert new_node before sibling
     * We need to set next_sibling on prev_child to point to new_node
     * Since prev_child can be any node type (text, comment, etc.), we need
     * to handle the different structure layouts. */
    if (prev_child) {
        switch (prev_child->type) {
            case TAURUS_NODE_TYPE_TEXT:
            case TAURUS_NODE_TYPE_COMMENT:
            case TAURUS_NODE_TYPE_CDATA: {
                /* These node types have: base(4) + content(8) + next_sibling(8)
                 * next_sibling is at offset 12 */
                struct {
                    TaurusNode base;
                    char* content;
                    void* next_sibling;
                } *text_node = (void*)prev_child;
                text_node->next_sibling = new_node;
                break;
            }
            case TAURUS_NODE_TYPE_ELEMENT: {
                /* Element has next_sibling as a struct member */
                ((TaurusElement)prev_child)->next_sibling = new_node;
                break;
            }
            default:
                /* PI, DOCTYPE, etc. - not expected in this context */
                return TAURUS_ERROR_INVALID_ARG;
        }
    } else {
        /* new_node becomes first child */
        parent->first_child = new_node;
    }
    new_node->next_sibling = sibling;

    /* Set parent and document */
    new_node->parent = parent;
    new_node->document = parent->document;

    /* Increment child count */
    parent->child_count++;

    /* Rebuild children array for O(1) access */
    rebuild_children_array(parent);

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    /* Emit observer event */
    EMIT_EVENT(new_node, TAURUS_EVENT_ELEMENT_ADDED, parent, sibling, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Insert new node after a sibling (Public API)
 * COMPACT MODE: Properly handles mixed content
 */
TaurusStatus taurus_element_insert_after(TaurusElement sibling, TaurusElement new_node) {
    if (!sibling || !new_node) return TAURUS_ERROR_NULL_ARG;

    TaurusNode* new_node_ptr = (TaurusNode*)new_node;
    TaurusNode* sibling_ptr = (TaurusNode*)sibling;

    /* Get parent using accessor function */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return TAURUS_ERROR_INVALID_ARG;

    if (new_node_ptr->type != TAURUS_NODE_TYPE_ELEMENT) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Check if new_node already has a parent */
    if (taurus_element_parent(new_node)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* CRITICAL FIX: Use generic node API to get next sibling
     * The element-only API skips text nodes, which breaks insertion order.
     * Use taurus_node_get_next_sibling to get the true next sibling. */
    TaurusNode* next_sibling = taurus_node_get_next_sibling(sibling_ptr);

    /* Link sibling to new_node */
    sibling->next_sibling = new_node;

    /* Link new_node to next_sibling */
    new_node->next_sibling = next_sibling;

    /* Set parent */
    new_node->parent = parent;
    new_node->document = parent->document;

    /* Update last_child if needed - use generic node API */
    TaurusNode* last = taurus_node_last_child_internal((TaurusNode*)parent);
    if (last == sibling_ptr) {
        /* new_node is always an element (checked above) */
        parent->last_child = new_node;
    }

    /* Increment child count */
    parent->child_count++;

    /* Rebuild children array for O(1) access */
    rebuild_children_array(parent);

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    /* Emit observer event */
    EMIT_EVENT(new_node, TAURUS_EVENT_ELEMENT_ADDED, parent, sibling, NULL, NULL, NULL);

    return TAURUS_OK;
}

/**
 * Remove child element (Public API)
 * COMPACT MODE: Simplified implementation
 */
TaurusStatus taurus_element_remove_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Verify child is actually a child of parent */
    TaurusElement found = NULL;
    TaurusElement current = taurus_element_get_first_child(parent);
    while (current) {
        if (current == child) {
            found = current;
            break;
        }
        current = taurus_element_get_next_sibling(current);
    }

    if (!found) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    /* Find the node before child in the list */
    TaurusElement prev_child = NULL;
    current = taurus_element_get_first_child(parent);
    while (current && current != child) {
        prev_child = current;
        current = taurus_element_get_next_sibling(current);
    }

    TaurusElement next_child = taurus_element_get_next_sibling(child);

    /* Unlink child from the list */
    if (prev_child) {
        prev_child->next_sibling = next_child;
    } else {
        /* Child was first child */
        parent->first_child = next_child;
    }

    /* Update last_child pointer - directly check instead of using get_last_child */
    if ((TaurusNode*)parent->last_child == child) {
        /* Child was last child */
        parent->last_child = prev_child;
    }

    /* Clear parent and decrement count */
    child->parent = NULL;
    parent->child_count--;

    /* Rebuild children array for O(1) access */
    rebuild_children_array(parent);

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

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
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusElement next = taurus_element_get_next_sibling(child);
        child->parent = NULL;
        child = next;
    }

    /* Clear child pointers */
    elem->first_child = NULL;
    elem->last_child = NULL;
    elem->child_count = 0;

    /* Clear children array */
    memset(elem->children, 0, sizeof(elem->children));

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

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
    elem->name_view = taurus_sv_from_cstr(name);

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_NAME_CHANGED, NULL, NULL, NULL, NULL, name);

    return TAURUS_OK;
}

/**
 * Set element text content (Public API)
 * COMPACT MODE: Removes all existing children and adds a single text node
 */
TaurusStatus taurus_element_set_text(TaurusElement elem, const char* text) {
    if (!elem) return TAURUS_ERROR_NULL_ARG;

    /* Remove all existing children */
    taurus_element_remove_all_children(elem);

    if (text) {
        /* Create new text node (even if empty to match pugixml behavior) */
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
 * COMPACT MODE: Uses linked list attribute storage
 */
TaurusStatus taurus_element_set_attribute(TaurusElement elem, const char* name, const char* value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Check if attribute already exists */
    struct taurus_attribute* existing = taurus_element_get_attribute_by_name(elem, name);
    if (existing) {
        /* Update existing attribute's value */
        /* Get the memory pool from the document */
        TaurusMemoryPool* pool = NULL;
        if (elem->document && elem->document->pool) {
            pool = elem->document->pool;
        }

        if (pool) {
            /* Pool-allocated document: Use pool allocation for new value
             * CRITICAL: Don't free existing->value - it was pool-allocated and
             * individual pool allocations cannot be freed. The old value will be
             * reclaimed when the entire document/pool is freed. */
            if (value) {
                TaurusStringView value_view = taurus_sv_from_cstr(value);
                existing->value = taurus_sv_to_cstr_pooled(&value_view, pool);
                existing->value_view = taurus_sv_from_cstr(existing->value);
            } else {
                existing->value = NULL;
                existing->value_view = taurus_sv_empty();
            }
        } else {
            /* No pool available: Use malloc/free (fallback for edge cases) */
            if (existing->value) {
                TAURUS_FREE(existing->value);
            }
            existing->value = value ? taurus_strdup(value) : NULL;
            existing->value_view = value ? taurus_sv_from_cstr(value) : taurus_sv_empty();
        }
    } else {
        /* Get the memory pool from the document */
        TaurusMemoryPool* pool = NULL;
        if (elem->document && elem->document->pool) {
            pool = elem->document->pool;
        } else {
            /* CRITICAL: No pool available - cannot allocate attributes without memory pool */
            return TAURUS_ERROR_MEMORY;
        }

        /* Allocate attribute from pool (not malloc!) - CRITICAL for correct pointer handling */
        struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
            pool, sizeof(struct taurus_attribute));
        if (!attr) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Initialize attribute */
        /* CRITICAL: Pool-allocate strings to avoid pointing to stack memory!
         * If we use taurus_strdup(), the StringView still points to the input
         * parameter (stack memory), which gets corrupted when reused. */
        TaurusStringView name_view = taurus_sv_from_cstr(name);
        attr->name = taurus_sv_to_cstr_pooled(&name_view, pool);

        if (value) {
            TaurusStringView value_view = taurus_sv_from_cstr(value);
            attr->value = taurus_sv_to_cstr_pooled(&value_view, pool);
        } else {
            attr->value = NULL;
        }

        attr->namespace_uri = NULL;
        attr->prefix = NULL;
        /* CRITICAL: StringView must point to the allocated strings, not the input parameters!
         * Otherwise StringView points to stack memory which gets corrupted. */
        attr->name_view = taurus_sv_from_cstr(attr->name);
        attr->value_view = value ? taurus_sv_from_cstr(attr->value) : taurus_sv_empty();
        attr->namespace_uri_view = taurus_sv_empty();
        attr->prefix_view = taurus_sv_empty();
        attr->has_entities = 0;
        attr->next = NULL;

        /* Add to linked list */
        struct taurus_attribute* first_attr = taurus_element_get_first_attribute(elem);
        if (first_attr) {
            /* Find end of list */
            struct taurus_attribute* last = first_attr;
            while (last->next) {
                last = last->next;
            }
            last->next = attr;
        } else {
            /* First attribute - direct pointer assignment (no encoding!) */
            elem->first_attribute = attr;
        }

        elem->attr_count++;
    }

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    /* Emit observer event */
    EMIT_EVENT(elem, TAURUS_EVENT_ATTRIBUTE_SET, NULL, NULL, name, NULL, value);

    return TAURUS_OK;
}

/**
 * Remove attribute (Public API)
 */
TaurusStatus taurus_element_remove_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    struct taurus_attribute* attr = taurus_element_get_first_attribute(elem);
    struct taurus_attribute* prev = NULL;

    while (attr) {
        const char* attr_name = attr->name;
        if (!attr_name && !taurus_sv_is_empty(&attr->name_view)) {
            attr_name = taurus_sv_to_cstr(&attr->name_view);
        }

        int match = 0;
        if (attr_name) {
            match = (strcmp(attr_name, name) == 0);
        }
        /* Free temporary string if we converted from StringView */
        if (attr_name && attr_name != attr->name) {
            char* temp = (char*)attr_name;
            TAURUS_FREE(temp);
        }

        if (match) {
            /* Store attribute name for event before removal */
            const char* removed_name = attr->name;

            /* Found - remove from linked list */
            if (prev) {
                prev->next = attr->next;
            } else {
                /* Was first attribute - update first_attribute pointer */
                if (attr->next) {
                    taurus_element_set_first_attribute(elem, attr->next);
                } else {
                    /* No more attributes */
                    taurus_element_set_first_attribute(elem, NULL);
                }
            }

            /* CRITICAL: Don't free pool-allocated attributes!
             * Attributes are allocated from the memory pool and will be freed
             * when the pool is destroyed. If we free() them here, we get undefined behavior.
             * Just decrement the count and let the pool handle cleanup. */

            /* CRITICAL: Don't free attribute strings!
             * Attributes created by parser or programmatically use pool allocation.
             * Pool-allocated strings cannot be individually freed - they will be
             * reclaimed when the entire document/pool is freed.
             * Trying to free() them here causes undefined behavior (Abort trap). */

            elem->attr_count--;
            taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

            /* Emit observer event */
            EMIT_EVENT(elem, TAURUS_EVENT_ATTRIBUTE_REMOVED, NULL, NULL, removed_name, NULL, NULL);

            return TAURUS_OK;
        }

        prev = attr;
        attr = attr->next;
    }

    return TAURUS_ERROR_NOT_FOUND;  /* Attribute not found */
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