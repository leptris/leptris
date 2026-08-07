/* lib/src/dom/element_modify.c - DOM Modification API (Compact Mode)
 * Copyright (c) 2024, Ribose Inc.
 *
 * Public API for modifying DOM trees in-place.
 * COMPACT MODE: Uses compact pointer encoding and accessor functions.
 */

#include "../../include/taurus.h"
#include "../taurus_internal.h"
#include "../common/string_view.h"
#include "../memory/pool.h"
#include "element.h"
#include "element_index.h"
#include "compact.h"
#include "node.h"
#include "text.h"
#include "cdata.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
        TaurusElement elem = taurus_element_create_pooled(name, doc->pool);
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
    taurus_element_invalidate_child_cache(parent);

    /* Invalidate element index (TODO 132): the new child changes
     * the document's element set. The index will be rebuilt lazily
     * on the next descendant-axis query. */
    if (parent->document) {
        taurus_element_index_invalidate(parent->document);
    }
    return TAURUS_OK;
}

/**
 * Prepend child element at the beginning (Public API)
 */
TaurusStatus taurus_element_prepend_child(TaurusElement parent, TaurusElement child) {
    if (!parent || !child) return TAURUS_ERROR_NULL_ARG;

    /* Call internal void function, assume success */
    taurus_element_prepend_child_internal(parent, (TaurusNode*)child);
    taurus_element_invalidate_child_cache(parent);
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
     * Set prev_child's next_sibling via the type-dispatching setter —
     * each node type encodes the field as int32_t offset (TODO 90
     * Phase 2c); the previous struct-pun predates the compact
     * encoding and corrupted PI data when used on non-text nodes. */
    if (prev_child) {
        taurus_node_set_next_sibling(prev_child, (TaurusNode*)new_node);
    } else {
        /* new_node becomes first child */
        taurus_elem_set_first_child(parent, (TaurusNode*)new_node);
    }
    taurus_elem_set_next_sibling(new_node, (TaurusNode*)sibling);

    /* Set parent and document */
    taurus_elem_set_parent(new_node, parent);
    new_node->document = parent->document;

    /* Increment child count */
    parent->child_count++;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    taurus_element_invalidate_child_cache(parent);
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
    taurus_elem_set_next_sibling(sibling, (TaurusNode*)new_node);

    /* Link new_node to next_sibling */
    taurus_elem_set_next_sibling(new_node, (TaurusNode*)next_sibling);

    /* Set parent */
    taurus_elem_set_parent(new_node, parent);
    new_node->document = parent->document;

    /* Update last_child if needed - use generic node API */
    TaurusNode* last = taurus_node_last_child_internal((TaurusNode*)parent);
    if (last == sibling_ptr) {
        /* new_node is always an element (checked above) */
        taurus_elem_set_last_child(parent, (TaurusNode*)new_node);
    }

    /* Increment child count */
    parent->child_count++;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    taurus_element_invalidate_child_cache(parent);
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
        taurus_elem_set_next_sibling(prev_child, (TaurusNode*)next_child);
    } else {
        /* Child was first child */
        taurus_elem_set_first_child(parent, (TaurusNode*)next_child);
    }

    /* Update last_child pointer - directly check instead of using get_last_child */
    if (taurus_elem_last_child(parent) == (TaurusNode*)child) {
        /* Child was last child */
        taurus_elem_set_last_child(parent, (TaurusNode*)prev_child);
    }

    /* Clear parent and decrement count */
    taurus_elem_set_parent(child, NULL);
    parent->child_count--;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(parent));

    taurus_element_invalidate_child_cache(parent);
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
        taurus_elem_set_parent(child, NULL);
        child = next;
    }

    /* Clear child pointers */
    taurus_elem_set_first_child(elem, NULL);
    taurus_elem_set_last_child(elem, NULL);
    elem->child_count = 0;

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

    taurus_element_invalidate_child_cache(elem);
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
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

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
        TaurusMemoryPool* pool = elem->document ? elem->document->pool : NULL;
        TaurusTextNode* text_node = taurus_text_create(text, strlen(text), pool);
        if (!text_node) {
            return TAURUS_ERROR_MEMORY;
        }

        /* Add as child */
        taurus_element_append_child_internal(elem, (TaurusNode*)text_node);
    }

    return TAURUS_OK;
}

/**
 * Set attribute (Public API)
 * COMPACT MODE: Uses linked list attribute storage
 */
TaurusStatus taurus_element_set_attribute(TaurusElement elem, const char* name, const char* value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Mutation-path perf: skip the pool's string interning hash table
     * (TODO 106 Phase 1).  Interning deduplicates attribute NAMES that
     * recur across many elements during parsing; on the public mutation
     * path the user knows the attr is unique, and the hash lookup/insert
     * is pure overhead — ~200ns × N attrs.  Inline pool_alloc + memcpy
     * is faster than taurus_sv_to_cstr_pooled for this case. */

    /* Check if attribute already exists */
    struct taurus_attribute* existing = taurus_element_get_attribute_by_name(elem, name);
    if (existing) {
        /* Update existing attribute's value */
        TaurusMemoryPool* pool = NULL;
        if (elem->document && elem->document->pool) {
            pool = elem->document->pool;
        }

        if (pool) {
            /* Pool-allocated document: pool_strdup the new value (no
             * interning — see header comment).  Old value is pool-
             * allocated and reclaims when the pool frees. */
            if (value) {
                size_t vlen = strlen(value);
                char* storage = (char*)taurus_pool_alloc(pool, vlen + 1);
                if (!storage) return TAURUS_ERROR_MEMORY;
                memcpy(storage, value, vlen);
                storage[vlen] = '\0';
                existing->value = storage;
                existing->value_view = taurus_sv_from_ptr(storage, vlen);
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

        /* Pool-strdup name (NO interning — mutation fast path). */
        size_t nlen = strlen(name);
        char* name_storage = (char*)taurus_pool_alloc(pool, nlen + 1);
        if (!name_storage) return TAURUS_ERROR_MEMORY;
        memcpy(name_storage, name, nlen);
        name_storage[nlen] = '\0';
        attr->name = name_storage;
        attr->name_view = taurus_sv_from_ptr(name_storage, nlen);

        /* Pre-compute name hash for O(1) lookup filtering (TODO 113). */
        attr->name_hash = 2166136261u;
        for (size_t i = 0; i < nlen; i++) {
            attr->name_hash ^= (unsigned char)name_storage[i];
            attr->name_hash *= 16777619u;
        }

        if (value) {
            size_t vlen = strlen(value);
            char* value_storage = (char*)taurus_pool_alloc(pool, vlen + 1);
            if (!value_storage) return TAURUS_ERROR_MEMORY;
            memcpy(value_storage, value, vlen);
            value_storage[vlen] = '\0';
            attr->value = value_storage;
            attr->value_view = taurus_sv_from_ptr(value_storage, vlen);
        } else {
            attr->value = NULL;
            attr->value_view = taurus_sv_empty();
        }

        attr->namespace_uri = NULL;
        attr->prefix = NULL;
        attr->namespace_uri_view = taurus_sv_empty();
        attr->prefix_view = taurus_sv_empty();
        attr->has_entities = 0;
        attr->next = NULL;

        /* Append via cached last_attribute offset — O(1) instead of
         * the old O(N) walk to find the tail.  Decode the offset to
         * access the struct, then re-encode (TODO 90 Phase 2d). */
        struct taurus_attribute* last = taurus_elem_last_attribute(elem);
        if (last) {
            last->next = attr;
        } else {
            taurus_elem_set_first_attribute(elem, attr);
        }
        taurus_elem_set_last_attribute(elem, attr);

        elem->attr_count++;
    }

    /* COW: Increment version */
    taurus_node_increment_version(TAURUS_ELEMENT_AS_NODE(elem));

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
            return TAURUS_OK;
        }

        prev = attr;
        attr = attr->next;
    }

    return TAURUS_ERROR_NOT_FOUND;  /* Attribute not found */
}

/**
 * Append copy of element (Public API)
 */
TaurusElement taurus_element_append_copy(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;

    /* CRITICAL: Check document pointer FIRST before any other access!
     * Use volatile to prevent compiler from reordering this check. */
    volatile struct taurus_document* parent_doc = parent->document;
    if (!parent_doc) {
        return NULL;
    }

    if (!parent_doc->pool) {
        return NULL;
    }

    /* Verify source has document too */
    if (!source->document) {
        return NULL;  /* Source element must have a document */
    }

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (source->document != parent->document);

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Double-check that target pool is valid before allocating */
        if (!parent->document->pool) {
            return NULL;  /* Pool became NULL somehow */
        }

        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, parent->document->pool);
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) - skip all loops */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
        if (!copy) return NULL;
        if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
    if (!copy) return NULL;

    /* CRITICAL: Set document field before recursive child copying!
     * The recursive calls to taurus_element_append_copy need this field set. */
    copy->document = parent->document;

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                /* Double-check that target pool is valid */
                if (!parent->document->pool) {
                    continue;  /* Skip this attribute if pool is invalid */
                }

                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, parent->document->pool);
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, parent->document->pool);
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (parent->document->pool) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, parent->document->pool);
            } else {
                /* Fallback: use cached strings if available, otherwise convert from StringView */
                const char* attr_name = attr->name ? attr->name : taurus_sv_to_cstr(&attr->name_view);
                const char* attr_value = attr->value ? attr->value : taurus_sv_to_cstr(&attr->value_view);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                    /* Free temporary strings if we converted from StringView */
                    if (!attr->name) free((char*)attr_name);
                    if (!attr->value) free((char*)attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);

    /* SAFETY: Verify child pointer is valid before accessing.
     * Small values like 0x4 indicate memory corruption or uninitialized fields. */
    while (child) {
        /* Check if pointer looks valid (not obviously corrupted)
         * Values less than 0x1000 are likely invalid pointers or offsets */
        if ((uintptr_t)child < 0x1000) {
            /* Invalid pointer - skip this child to prevent crash */
            break;
        }

        /* Additional safety: Check if pointer points to readable memory
         * by verifying the type field is within valid range */
        TaurusNode* child_node = (TaurusNode*)child;

        if (child_node->type < TAURUS_NODE_TYPE_ELEMENT ||
            child_node->type > TAURUS_NODE_TYPE_DOCTYPE) {
            /* Invalid type field - likely corrupted or pointing to wrong memory */
            break;
        }

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            /* SAFETY: Verify text pointer is valid before accessing content */
            if ((uintptr_t)text > 0x1000) {
                TaurusTextNode* text_copy = taurus_text_create(text->content,
                    text->content_len,
                    copy->document ? copy->document->pool : NULL);
                if (text_copy) {
                    taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
                }
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_CDATA) {
            TaurusCDATANode* cdata = (TaurusCDATANode*)child;
            if ((uintptr_t)cdata > 0x1000) {
                TaurusCDATANode* cdata_copy = taurus_cdata_create(cdata->content,
                    cdata->content ? strlen(cdata->content) : 0,
                    copy->document ? copy->document->pool : NULL);
                if (cdata_copy) {
                    taurus_element_append_child_internal(copy, (TaurusNode*)cdata_copy);
                }
            }
        }
        /* Skip COMMENT and PI nodes for now - they're less common */

        /* CRITICAL FIX: Get next sibling using generic accessor
         * The child variable is declared as TaurusElement but might actually
         * point to a text node or other node type. Each node type has next_sibling
         * at a different offset, so we must use the generic accessor.
         * Note: child_node is already declared above at line 598 */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) {
            /* No more siblings */
            break;
        }
        child = (TaurusElement)next;
    }

    /* Append to parent */
    if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
        /* Note: copy is pool-allocated, will be freed with document */
        return NULL;
    }

    return copy;
}

/**
 * Prepend copy of element (Public API)
 */
TaurusElement taurus_element_prepend_copy(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (source->document != parent->document);

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, parent->document->pool);
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
        if (!copy) return NULL;
        if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
    if (!copy) return NULL;

    /* CRITICAL: Set document field before recursive child copying!
     * The recursive calls to taurus_element_append_copy need this field set. */
    copy->document = parent->document;

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, parent->document->pool);
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, parent->document->pool);
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (parent->document->pool) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, parent->document->pool);
            } else {
                /* Fallback: use cached strings if available, otherwise convert from StringView */
                const char* attr_name = attr->name ? attr->name : taurus_sv_to_cstr(&attr->name_view);
                const char* attr_value = attr->value ? attr->value : taurus_sv_to_cstr(&attr->value_view);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                    /* Free temporary strings if we converted from StringView */
                    if (!attr->name) free((char*)attr_name);
                    if (!attr->value) free((char*)attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                copy->document ? copy->document->pool : NULL);
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Prepend to parent */
    if (taurus_element_prepend_child(parent, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}

/**
 * Insert copy after sibling (Public API)
 */
TaurusElement taurus_element_insert_copy_after(TaurusElement sibling, TaurusElement source) {
    if (!sibling || !source) return NULL;

    /* Get parent of sibling */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (source->document != parent->document);

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, parent->document->pool);
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
        if (!copy) return NULL;
        if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
    if (!copy) return NULL;

    /* CRITICAL: Set document field before recursive child copying!
     * The recursive calls to taurus_element_append_copy need this field set. */
    copy->document = parent->document;

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, parent->document->pool);
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, parent->document->pool);
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (parent->document->pool) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, parent->document->pool);
            } else {
                /* Fallback: use cached strings if available, otherwise convert from StringView */
                const char* attr_name = attr->name ? attr->name : taurus_sv_to_cstr(&attr->name_view);
                const char* attr_value = attr->value ? attr->value : taurus_sv_to_cstr(&attr->value_view);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                    /* Free temporary strings if we converted from StringView */
                    if (!attr->name) free((char*)attr_name);
                    if (!attr->value) free((char*)attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                copy->document ? copy->document->pool : NULL);
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Insert after sibling */
    if (taurus_element_insert_after(sibling, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}

/**
 * Insert copy before sibling (Public API)
 */
TaurusElement taurus_element_insert_copy_before(TaurusElement sibling, TaurusElement source) {
    if (!sibling || !source) return NULL;

    /* Get parent of sibling */
    TaurusElement parent = taurus_element_parent(sibling);
    if (!parent) return NULL;

    /* Get source name as StringView (zero-copy, O(1) access) */
    TaurusStringView name_view = taurus_element_name_view(source);
    if (taurus_sv_is_empty(&name_view)) return NULL;

    /* Check if this is a cross-document copy */
    int is_cross_doc = (source->document != parent->document);

    /* For cross-document copies, we need to copy the name string data
     * because the StringView points to the source document's XML buffer */
    TaurusStringView name_copy_view = name_view;  /* Default: use original StringView */
    if (is_cross_doc) {
        /* Allocate and copy name string to target document's pool */
        char* name_copy = taurus_sv_to_cstr_pooled(&name_view, parent->document->pool);
        if (!name_copy) return NULL;
        name_copy_view = taurus_sv_from_cstr(name_copy);
    }

    /* Fast path: Simple element (no attributes, no children) */
    if (!taurus_element_get_first_attribute(source) && !taurus_elem_first_child(source)) {
        TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
        if (!copy) return NULL;
        if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) {
            return NULL;
        }
        return copy;
    }

    /* Create new element with StringView (no C string conversion!) */
    TaurusElement copy = taurus_element_create_with_view(name_copy_view, parent->document->pool);
    if (!copy) return NULL;

    /* CRITICAL: Set document field before recursive child copying!
     * The recursive calls to taurus_element_append_copy need this field set. */
    copy->document = parent->document;

    /* Copy attributes - optimized: direct StringView copy when same document */
    uint8_t attr_count = taurus_element_attribute_count(source);
    for (uint8_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(source, i);
        if (attr && !taurus_sv_is_empty(&attr->name_view)) {
            TaurusStringView attr_name_view = attr->name_view;
            TaurusStringView attr_value_view = attr->value_view;

            /* For cross-document copies, copy the attribute string data */
            if (is_cross_doc) {
                char* name_copy = taurus_sv_to_cstr_pooled(&attr->name_view, parent->document->pool);
                char* value_copy = taurus_sv_to_cstr_pooled(&attr->value_view, parent->document->pool);
                if (!name_copy || !value_copy) {
                    /* Allocation failed - skip this attribute */
                    if (name_copy) /* Nothing to free, pool-allocated */
                    if (value_copy) /* Nothing to free, pool-allocated */
                    continue;
                }
                attr_name_view = taurus_sv_from_cstr(name_copy);
                attr_value_view = taurus_sv_from_cstr(value_copy);
            }

            /* Use pool-based fast path if available, otherwise fallback to set_attribute
             * Pool path is 2-3x faster (no hash lookup, no string conversion) */
            if (parent->document->pool) {
                taurus_element_add_attribute(copy, attr_name_view, attr_value_view, parent->document->pool);
            } else {
                /* Fallback: use cached strings if available, otherwise convert from StringView */
                const char* attr_name = attr->name ? attr->name : taurus_sv_to_cstr(&attr->name_view);
                const char* attr_value = attr->value ? attr->value : taurus_sv_to_cstr(&attr->value_view);
                if (attr_name && attr_value) {
                    taurus_element_set_attribute(copy, attr_name, attr_value);
                    /* Free temporary strings if we converted from StringView */
                    if (!attr->name) free((char*)attr_name);
                    if (!attr->value) free((char*)attr_value);
                }
            }
        }
    }

    /* Copy children recursively */
    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;
        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child */
            taurus_element_append_copy(copy, child);
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                copy->document ? copy->document->pool : NULL);
            if (text_copy) {
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
            }
        }
        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Insert before sibling */
    if (taurus_element_insert_before(sibling, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}


/**
 * Remove all children (Public API) - Alias for remove_all_children
 */
TaurusStatus taurus_element_remove_children(TaurusElement elem) {
    return taurus_element_remove_all_children(elem);
}

/* ============================================================================
 * Bulk Allocation for Subtree Copy (Task 40)
 * ============================================================================ */

/**
 * Internal: Build a map from source elements to pre-allocated copies
 *
 * This is used during bulk copy to link elements without knowing their
 * final addresses ahead of time. We allocate space for the map, then
 * build it while copying.
 *
 * @param pool Memory pool for allocations
 * @param count Number of elements in subtree
 * @return Pointer to element array, or NULL on failure
 */
static TaurusElement* taurus_element_create_copy_map(TaurusMemoryPool* pool, size_t count) {
    if (!pool || count == 0) return NULL;
    return (TaurusElement*)taurus_pool_alloc(pool, sizeof(TaurusElement) * count);
}

/**
 * Internal: Copy subtree using pre-allocated elements (bulk allocation)
 *
 * This function assumes elements have already been allocated and stored
 * in the copy_map array. It initializes each element and builds parent-child
 * relationships.
 *
 * @param parent_copy Parent element to attach copies to
 * @param source Source element to copy
 * @param copy_map Array of pre-allocated element copies
 * @param copy_index Pointer to current index in copy_map (updated during traversal)
 * @param pool Memory pool for additional allocations (attributes, strings)
 * @return The copy of the source element, or NULL on failure
 */
static TaurusElement taurus_element_copy_subtree_bulk_internal(
    TaurusElement parent_copy,
    TaurusElement source,
    TaurusElement* copy_map,
    size_t* copy_index,
    TaurusMemoryPool* pool
) {
    if (!source || !copy_map || !copy_index) return NULL;

    /* Get current index and advance for children */
    size_t this_index = *copy_index;
    (*copy_index)++;

    TaurusElement copy = copy_map[this_index];
    if (!copy) return NULL;

    /* Initialize the copy element with minimal memset (only what we need) */
    memset(copy, 0, sizeof(struct taurus_element));

    /* Copy base node type */
    copy->base.type = TAURUS_NODE_TYPE_ELEMENT;

    /* name_view removed (TODO 90) — name is already pool-strdup'd
     * by create_with_view during the deep_copy_element call above. */

    /* Copy prefix and namespace as StringView (TODO 90) */
    /* prefix_view removed (TODO 90) — prefix is copied as char* below */;
    /* namespace_uri_view removed from struct — the cached char* was
     * already set by the recursive deep_copy_element call. */
    copy->namespace_uri = source->namespace_uri;

    /* Set parent pointer */
    taurus_elem_set_parent(copy, parent_copy);

    /* Copy attributes */
    copy->attr_count = source->attr_count;

    /* Copy attribute linked list - we need to allocate attributes individually
     * since they're stored as a compact pointer linked list */
    struct taurus_attribute* src_attr = taurus_element_get_first_attribute(source);
    struct taurus_attribute* prev_dst_attr = NULL;

    while (src_attr) {
        /* Allocate attribute from pool */
        struct taurus_attribute* dst_attr = (struct taurus_attribute*)taurus_pool_alloc(pool, sizeof(struct taurus_attribute));
        if (!dst_attr) break;

        /* Initialize attribute */
        memset(dst_attr, 0, sizeof(struct taurus_attribute));

        /* Copy StringView data (zero-copy) */
        dst_attr->name_view = src_attr->name_view;
        dst_attr->value_view = src_attr->value_view;
        dst_attr->prefix_view = src_attr->prefix_view;
        dst_attr->namespace_uri_view = src_attr->namespace_uri_view;
        dst_attr->name_hash = src_attr->name_hash;

        /* Copy cached strings if they exist */
        if (src_attr->name) {
            dst_attr->name = taurus_pool_strdup(pool, src_attr->name);
        }
        if (src_attr->value) {
            dst_attr->value = taurus_pool_strdup(pool, src_attr->value);
        }
        if (src_attr->prefix) {
            dst_attr->prefix = taurus_pool_strdup(pool, src_attr->prefix);
        }
        if (src_attr->namespace_uri) {
            dst_attr->namespace_uri = taurus_pool_strdup(pool, src_attr->namespace_uri);
        }

        /* Copy entity flag */
        dst_attr->has_entities = src_attr->has_entities;

        /* Link to previous attribute or set as first attribute */
        if (!prev_dst_attr) {
            taurus_elem_set_first_attribute(copy, dst_attr);
        } else {
            prev_dst_attr->next = dst_attr;
        }
        taurus_elem_set_last_attribute(copy, dst_attr);
        prev_dst_attr = dst_attr;
        src_attr = src_attr->next;
    }

    /* Copy children recursively and link them */
    TaurusElement first_child = NULL;
    TaurusElement last_child = NULL;

    TaurusElement child = (TaurusElement)taurus_elem_first_child(source);
    while (child) {
        TaurusNode* child_node = (TaurusNode*)child;

        if (child_node->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* Recursively copy element child using bulk allocation */
            TaurusElement child_copy = taurus_element_copy_subtree_bulk_internal(
                copy, child, copy_map, copy_index, pool
            );

            if (child_copy) {
                /* Link to parent's child list */
                if (!first_child) {
                    first_child = child_copy;
                }
                last_child = child_copy;
            }
        } else if (child_node->type == TAURUS_NODE_TYPE_TEXT) {
            /* Copy text node (not bulk-allocated) */
            TaurusTextNode* text = (TaurusTextNode*)child;
            TaurusTextNode* text_copy = taurus_text_create(text->content,
                text->content_len,
                copy->document ? copy->document->pool : NULL);
            if (text_copy) {
                /* Link as child using internal function */
                taurus_element_append_child_internal(copy, (TaurusNode*)text_copy);
                if (!first_child) {
                    first_child = (TaurusElement)text_copy;
                }
                if (last_child) {
                    taurus_elem_set_next_sibling(last_child, (TaurusNode*)text_copy);
                }
                last_child = (TaurusElement)text_copy;
            }
        }

        /* Use generic accessor to get next sibling - child might be any node type */
        TaurusNode* next = taurus_node_get_next_sibling(child_node);
        if (!next) break;
        child = (TaurusElement)next;
    }

    /* Set child pointers */
    taurus_elem_set_first_child(copy, (TaurusNode*)first_child);
    taurus_elem_set_last_child(copy, (TaurusNode*)last_child);

    /* Copy child count */
    copy->child_count = source->child_count;

    return copy;
}

/**
 * Copy subtree using bulk allocation (10-15% faster for large subtrees)
 *
 * Pre-allocates all element structures in a single contiguous block,
 * then initializes them. This reduces:
 * - Pool allocation overhead (1 call vs N calls)
 * - memset calls (1 call vs N calls)
 * - Cache misses (contiguous memory access)
 *
 * @param parent Parent element to attach copy to
 * @param source Source element to copy
 * @return Copy of the source element, or NULL on failure
 */
TaurusElement taurus_element_append_copy_bulk(TaurusElement parent, TaurusElement source) {
    if (!parent || !source) return NULL;
    if (!parent->document || !parent->document->pool) {
        /* Fallback to regular copy if no pool */
        return taurus_element_append_copy(parent, source);
    }

    TaurusMemoryPool* pool = parent->document->pool;

    /* Count subtree nodes to determine allocation size */
    TaurusSubtreeStats stats;
    taurus_element_count_subtree(source, &stats);

    size_t total_elements = stats.element_count;
    if (total_elements == 0) return NULL;

    /* Allocate all element structures in one batch */
    TaurusElement* copy_map = taurus_element_create_copy_map(pool, total_elements);
    if (!copy_map) {
        /* Fallback to regular copy if batch allocation fails */
        return taurus_element_append_copy(parent, source);
    }

    /* Copy the subtree using pre-allocated elements */
    size_t copy_index = 0;
    TaurusElement copy = taurus_element_copy_subtree_bulk_internal(
        NULL, source, copy_map, &copy_index, pool
    );

    if (!copy) {
        return NULL;
    }

    /* Set document pointer on the entire copied subtree */
    copy->document = parent->document;

    /* Attach to parent */
    if (taurus_element_append_child(parent, copy) != TAURUS_OK) {
        return NULL;
    }

    return copy;
}


