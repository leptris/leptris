/* lib/src/dom/node.c - Base node implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * POINTER-BASED ARCHITECTURE:
 * Direct pointers for tree navigation.
 * O(1) access without offset calculations.
 */

#include "node.h"
#include "element.h"
#include "text.h"
#include "cdata.h"
#include "comment.h"
#include "pi.h"
#include "../../include/taurus.h"  /* For taurus_document_root() */
#include "../taurus_internal.h"
#include <stdlib.h>
#include <string.h>

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
    node->version = 0;       /* COW: Initial version */
    node->next_sibling = NULL;
    node->prev_sibling = NULL;

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
    node->version = 0;       /* COW: Initial version */
    node->next_sibling = NULL;
    node->prev_sibling = NULL;

    return node;
}

/* Free a node (does not free children - caller's responsibility) */
void taurus_node_free(TaurusNode* node) {
    if (!node) return;
    free(node);
}

/* Append child to parent's children list */
void taurus_node_append_child(TaurusNode* parent, TaurusNode* child) {
    if (!parent || !child) return;
    if (parent->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type */
    TaurusElement parent_elem = (TaurusElement)parent;

    /* Set parent relationship using direct pointer */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;
        child_elem->parent = parent_elem;
    }

    /* Set sibling pointers */
    if (parent_elem->last_child) {
        parent_elem->last_child->next_sibling = child;
        child->prev_sibling = parent_elem->last_child;
    } else {
        parent_elem->first_child = child;
    }
    parent_elem->last_child = child;
    parent_elem->child_count++;
}

/* Prepend child to parent's children list */
void taurus_node_prepend_child(TaurusNode* parent, TaurusNode* child) {
    if (!parent || !child) return;
    if (parent->type != TAURUS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type */
    TaurusElement parent_elem = (TaurusElement)parent;

    /* Set parent relationship using direct pointer */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement child_elem = (TaurusElement)child;
        child_elem->parent = parent_elem;
    }

    /* Set sibling pointers */
    if (parent_elem->first_child) {
        child->next_sibling = parent_elem->first_child;
        parent_elem->first_child->prev_sibling = child;
    } else {
        parent_elem->last_child = child;
    }
    parent_elem->first_child = child;
    parent_elem->child_count++;
}

/* Insert new node before sibling */
void taurus_node_insert_before(TaurusNode* sibling, TaurusNode* new_node) {
    if (!sibling || !new_node) return;

    /* Get parent */
    TaurusElement parent = NULL;
    if (sibling->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent = ((TaurusElement)sibling)->parent;
    }
    if (!parent) return;

    /* Set parent relationship */
    if (new_node->type == TAURUS_NODE_TYPE_ELEMENT) {
        ((TaurusElement)new_node)->parent = parent;
    }

    /* Insert before sibling using direct pointers */
    new_node->next_sibling = sibling;
    new_node->prev_sibling = sibling->prev_sibling;

    if (sibling->prev_sibling) {
        sibling->prev_sibling->next_sibling = new_node;
    } else {
        parent->first_child = new_node;
    }
    sibling->prev_sibling = new_node;
    parent->child_count++;
}

/* Insert new node after sibling */
void taurus_node_insert_after(TaurusNode* sibling, TaurusNode* new_node) {
    if (!sibling || !new_node) return;

    /* Get parent */
    TaurusElement parent = NULL;
    if (sibling->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent = ((TaurusElement)sibling)->parent;
    }
    if (!parent) return;

    /* Set parent relationship */
    if (new_node->type == TAURUS_NODE_TYPE_ELEMENT) {
        ((TaurusElement)new_node)->parent = parent;
    }

    /* Insert after sibling using direct pointers */
    new_node->prev_sibling = sibling;
    new_node->next_sibling = sibling->next_sibling;

    if (sibling->next_sibling) {
        sibling->next_sibling->prev_sibling = new_node;
    } else {
        parent->last_child = new_node;
    }
    sibling->next_sibling = new_node;
    parent->child_count++;
}

/* Remove node from tree */
void taurus_node_remove(TaurusNode* node) {
    if (!node) return;

    /* Get parent */
    TaurusElement parent = NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent = ((TaurusElement)node)->parent;
    }
    if (!parent) return;

    /* Update sibling pointers */
    if (node->prev_sibling) {
        node->prev_sibling->next_sibling = node->next_sibling;
    } else {
        parent->first_child = node->next_sibling;
    }

    if (node->next_sibling) {
        node->next_sibling->prev_sibling = node->prev_sibling;
    } else {
        parent->last_child = node->prev_sibling;
    }

    /* Clear node's links */
    node->prev_sibling = NULL;
    node->next_sibling = NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        ((TaurusElement)node)->parent = NULL;
    }
    parent->child_count--;
}

/* Get first child of node (any type: element, text, comment, CDATA, etc.) */
TaurusNode* taurus_node_first_child_internal(TaurusNode* node) {
    if (!node) return NULL;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;

    TaurusElement elem = (TaurusElement)node;
    return elem->first_child;
}

/* Get last child of node (any type) */
TaurusNode* taurus_node_last_child_internal(TaurusNode* node) {
    if (!node) return NULL;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;

    TaurusElement elem = (TaurusElement)node;
    return elem->last_child;
}

/* Get child count */
size_t taurus_node_child_count_internal(TaurusNode* node) {
    if (!node) return 0;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) return 0;

    TaurusElement elem = (TaurusElement)node;
    return (size_t)elem->child_count;
}

/* COW: Freeze node and all descendants */
void taurus_node_freeze(TaurusNode* node) {
    if (!node) return;

    node->frozen = 1;

    /* Recursively freeze children if element */
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement elem = (TaurusElement)node;
        TaurusNode* child = elem->first_child;
        while (child) {
            taurus_node_freeze(child);
            child = child->next_sibling;
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
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);
    if (root) {
        taurus_node_freeze((TaurusNode*)root);
    }
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

/* Generic next_sibling accessor - uses direct pointer from base node */
TaurusNode* taurus_node_get_next_sibling(TaurusNode* node) {
    if (!node) return NULL;
    return node->next_sibling;
}

/* Set next_sibling pointer for any node type */
void taurus_node_set_next_sibling(TaurusNode* node, TaurusNode* sibling) {
    if (!node) return;
    node->next_sibling = sibling;
}
