/* lib/src/dom/node.c - Base node implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * HYBRID ARCHITECTURE:
 * Base node only contains type and metadata (4 bytes total).
 * Parent/sibling pointers stored as regular pointers in specific node types
 * for performance (1.37x vs pugixml target).
 */

#include "node.h"
#include "element.h"
#include "text.h"
#include "cdata.h"
#include "comment.h"
#include "pi.h"
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Create a new node of given type and size
 * The size parameter allows creating larger structures that inherit from TaurusNode */
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type, size_t size) {
    if (size < sizeof(TaurusNode)) {
        size = sizeof(TaurusNode);
    }

    TaurusNode* node = (TaurusNode*)calloc(1, size);
    if (!node) return NULL;

    node->type = type;
    node->frozen = 0;        /* COW: Initially mutable */
    node->version = 0;       /* COW 2.2: Initial version */

    return node;
}

/* Create a new node using memory pool (FAST - no malloc!) */
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type, size_t size, TaurusMemoryPool* pool) {
    if (!pool) {
        /* Fall back to regular allocation if no pool */
        return taurus_node_create(type, size);
    }

    if (size < sizeof(TaurusNode)) {
        size = sizeof(TaurusNode);
    }

    /* Allocate from pool - O(1) bump-pointer allocation */
    TaurusNode* node = (TaurusNode*)taurus_pool_calloc(pool, size);
    if (!node) return NULL;

    node->type = type;
    node->frozen = 0;        /* COW: Initially mutable */
    node->version = 0;       /* COW 2.2: Initial version */

    return node;
}

/* Free a node (does not free children - caller's responsibility) */
void taurus_node_free(TaurusNode* node) {
    if (!node) return;
    free(node);
}

/* Append child to parent's children list
 * NOTE: In compact architecture, this only works for elements
 * Other node types (text, comment, etc.) are handled differently */
void taurus_node_append_child(TaurusNode* parent, TaurusNode* child) {
    if (!parent || !child) return;
    if (parent->type != TAURUS_NODE_TYPE_ELEMENT) return;
    if (child->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type for compact pointer access */
    TaurusElement parent_elem = (TaurusElement)parent;
    TaurusElement child_elem = (TaurusElement)child;

    /* Set parent relationship */
    taurus_element_set_parent(parent_elem, child_elem);

    /* Append to end of children list */
    TaurusElement last = taurus_element_get_last_child(parent_elem);
    if (last) {
        taurus_element_set_next_sibling(last, child_elem);
    } else {
        taurus_element_set_first_child(parent_elem, child_elem);
    }
}

/* Prepend child to parent's children list */
void taurus_node_prepend_child(TaurusNode* parent, TaurusNode* child) {
    if (!parent || !child) return;
    if (parent->type != TAURUS_NODE_TYPE_ELEMENT) return;
    if (child->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type for compact pointer access */
    TaurusElement parent_elem = (TaurusElement)parent;
    TaurusElement child_elem = (TaurusElement)child;

    /* Set parent relationship */
    taurus_element_set_parent(parent_elem, child_elem);

    /* Insert at beginning of children list */
    TaurusElement first = taurus_element_get_first_child(parent_elem);
    if (first) {
        taurus_element_set_next_sibling(child_elem, first);
        taurus_element_set_next_sibling(first, NULL);  /* Will be updated by loop */
        /* Update: Need to set child as first */
        taurus_element_set_first_child(parent_elem, child_elem);
    } else {
        taurus_element_set_first_child(parent_elem, child_elem);
        taurus_element_set_last_child(parent_elem, child_elem);
    }
}

/* Insert new node before sibling */
void taurus_node_insert_before(TaurusNode* sibling, TaurusNode* new_node) {
    if (!sibling || !new_node) return;
    if (sibling->type != TAURUS_NODE_TYPE_ELEMENT) return;
    if (new_node->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Get parent of sibling */
    TaurusElement sibling_elem = (TaurusElement)sibling;
    TaurusElement parent = taurus_element_get_parent(sibling_elem);
    if (!parent) return;

    /* Cast to element type */
    TaurusElement new_elem = (TaurusElement)new_node;

    /* Set parent */
    taurus_element_set_parent(new_elem, parent);

    /* Find previous sibling */
    TaurusElement prev = NULL;
    TaurusElement curr = taurus_element_get_first_child(parent);
    while (curr && curr != sibling_elem) {
        prev = curr;
        curr = taurus_element_get_next_sibling(curr);
    }

    if (prev) {
        taurus_element_set_next_sibling(prev, new_elem);
        taurus_element_set_next_sibling(new_elem, sibling_elem);
        taurus_element_set_next_sibling(sibling_elem, NULL);  /* Updated by loop */
    } else {
        /* Insert at beginning */
        taurus_element_set_first_child(parent, new_elem);
        taurus_element_set_next_sibling(new_elem, sibling_elem);
        taurus_element_set_next_sibling(sibling_elem, NULL);
    }
}

/* Insert new node after sibling */
void taurus_node_insert_after(TaurusNode* sibling, TaurusNode* new_node) {
    if (!sibling || !new_node) return;
    if (sibling->type != TAURUS_NODE_TYPE_ELEMENT) return;
    if (new_node->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Get parent of sibling */
    TaurusElement sibling_elem = (TaurusElement)sibling;
    TaurusElement parent = taurus_element_get_parent(sibling_elem);
    if (!parent) return;

    /* Cast to element type */
    TaurusElement new_elem = (TaurusElement)new_node;

    /* Set parent */
    taurus_element_set_parent(new_elem, parent);

    /* Get next sibling */
    TaurusElement next = taurus_element_get_next_sibling(sibling_elem);

    taurus_element_set_next_sibling(sibling_elem, new_elem);
    taurus_element_set_next_sibling(new_elem, next);

    /* Update last child if needed */
    if (!next) {
        taurus_element_set_last_child(parent, new_elem);
    }
}

/* Remove node from tree */
void taurus_node_remove(TaurusNode* node) {
    if (!node) return;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type */
    TaurusElement elem = (TaurusElement)node;

    /* Get parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return;

    /* Find previous sibling */
    TaurusElement prev = NULL;
    TaurusElement curr = taurus_element_get_first_child(parent);
    while (curr && curr != elem) {
        prev = curr;
        curr = taurus_element_get_next_sibling(curr);
    }

    /* Update pointers */
    if (prev) {
        TaurusElement next = taurus_element_get_next_sibling(elem);
        taurus_element_set_next_sibling(prev, next);
    } else {
        /* Was first child */
        TaurusElement next = taurus_element_get_next_sibling(elem);
        taurus_element_set_first_child(parent, next);
        if (!next) {
            /* Was only child */
            taurus_element_set_last_child(parent, NULL);
        }
    }

    /* Clear parent */
    taurus_element_set_parent(elem, NULL);
    taurus_element_set_next_sibling(elem, NULL);
}

/* Get first child of node (any type: element, text, comment, CDATA, etc.) */
TaurusNode* taurus_node_first_child_internal(TaurusNode* node) {
    if (!node) return NULL;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;

    TaurusElement elem = (TaurusElement)node;
    /* Return first_child directly - it may point to any node type */
    return (TaurusNode*)elem->first_child;
}

/* Get last child of node (any type: element, text, comment, CDATA, etc.) */
TaurusNode* taurus_node_last_child_internal(TaurusNode* node) {
    if (!node) return NULL;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;

    TaurusElement elem = (TaurusElement)node;
    /* Return last_child directly - it may point to any node type */
    return (TaurusNode*)elem->last_child;
}

/* Get child count */
size_t taurus_node_child_count_internal(TaurusNode* node) {
    if (!node) return 0;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return 0;

    TaurusElement elem = (TaurusElement)node;
    return (size_t)taurus_element_child_count(elem);
}

/* COW: Freeze node and all descendants */
void taurus_node_freeze(TaurusNode* node) {
    if (!node) return;

    node->frozen = 1;

    /* Recursively freeze children if element */
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement elem = (TaurusElement)node;
        TaurusElement child = taurus_element_get_first_child(elem);
        while (child) {
            taurus_node_freeze((TaurusNode*)child);
            child = taurus_element_get_next_sibling(child);
        }
    }
}

/* Check if node is frozen */
int taurus_node_is_frozen(TaurusNode* node) {
    return node ? node->frozen : 0;
}

/* Thaw node (prepare for modification) */
TaurusNode* taurus_node_thaw(TaurusNode* node) {
    if (!node) return NULL;

    /* If not frozen, return as-is */
    if (!node->frozen) return node;

    /* TODO: Implement COW deep copy when frozen
     * For now, just unfreeze (not safe for COW) */
    node->frozen = 0;
    return node;
}

/* Freeze entire document tree */
void taurus_document_freeze_tree(struct taurus_document* doc) {
    if (!doc || !doc->root) return;

    TaurusElement root = (TaurusElement)doc->root;
    taurus_node_freeze((TaurusNode*)root);
}

/* Get node version */
unsigned int taurus_node_get_version(TaurusNode* node) {
    return node ? node->version : 0;
}

/* Increment node version */
void taurus_node_increment_version(TaurusNode* node) {
    if (node) {
        node->version++;
    }
}

/* Generic next_sibling accessor - handles all node types correctly
 *
 * CRITICAL: Each node type has next_sibling at a different offset!
 * - Element: offset +32 bytes (after parent, first_child, last_child fields)
 * - Text:    offset +12 bytes (after base + content)
 * - CDATA:   offset +12 bytes (after base + content)
 * - Comment: offset +12 bytes (after base + content)
 * - PI:      offset +16 bytes (after base + target + data)
 *
 * This function dispatches based on node type to access the correct field.
 */
TaurusNode* taurus_node_get_next_sibling(TaurusNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT: {
            /* Return the immediate next sibling, NOT using element accessor
             * which skips non-element nodes. For mixed content traversal,
             * we need to visit all node types. */
            TaurusElement elem = (TaurusElement)node;
            return (TaurusNode*)elem->next_sibling;
        }
        case TAURUS_NODE_TYPE_TEXT: {
            TaurusTextNode* text = (TaurusTextNode*)node;
            return (TaurusNode*)text->next_sibling;
        }
        case TAURUS_NODE_TYPE_CDATA: {
            TaurusCDATANode* cdata = (TaurusCDATANode*)node;
            return (TaurusNode*)cdata->next_sibling;
        }
        case TAURUS_NODE_TYPE_COMMENT: {
            TaurusCommentNode* comment = (TaurusCommentNode*)node;
            return (TaurusNode*)comment->next_sibling;
        }
        case TAURUS_NODE_TYPE_PI: {
            TaurusPINode* pi = (TaurusPINode*)node;
            return (TaurusNode*)pi->next_sibling;
        }
        case TAURUS_NODE_TYPE_DOCTYPE:
        default:
            /* DOCTYPE and other node types don't have siblings in this implementation */
            return NULL;
    }
}

/* Set next_sibling pointer for any node type
 *
 * Generic setter that handles the type-specific offset for next_sibling.
 * Used for mixed content linking (elements, text, CDATA, comments, PIs).
 *
 * @param node The node whose next_sibling to set
 * @param sibling The node to link as next_sibling (can be NULL)
 */
void taurus_node_set_next_sibling(TaurusNode* node, TaurusNode* sibling) {
    if (!node) return;

    switch (node->type) {
        case TAURUS_NODE_TYPE_ELEMENT: {
            /* Use element setter to update both pointer and offset */
            taurus_element_set_next_sibling((TaurusElement)node, (TaurusElement)sibling);
            break;
        }
        case TAURUS_NODE_TYPE_TEXT: {
            TaurusTextNode* text = (TaurusTextNode*)node;
            text->next_sibling = sibling;
            break;
        }
        case TAURUS_NODE_TYPE_CDATA: {
            TaurusCDATANode* cdata = (TaurusCDATANode*)node;
            cdata->next_sibling = sibling;
            break;
        }
        case TAURUS_NODE_TYPE_COMMENT: {
            TaurusCommentNode* comment = (TaurusCommentNode*)node;
            comment->next_sibling = sibling;
            break;
        }
        case TAURUS_NODE_TYPE_PI: {
            TaurusPINode* pi = (TaurusPINode*)node;
            pi->next_sibling = sibling;
            break;
        }
        case TAURUS_NODE_TYPE_DOCTYPE:
        default:
            /* DOCTYPE and other node types don't have siblings */
            break;
    }
}
