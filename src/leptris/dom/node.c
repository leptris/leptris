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
#include "document_node.h"
#include "text.h"
#include "cdata.h"
#include "comment.h"
#include "pi.h"
#include "../leptris_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Create a new node of given type and size, pool-allocated.
 *
 * Ownership invariant (TODO 05/17): every node is pool-allocated.
 * The size parameter allows creating larger structures that inherit
 * from LeptrisNode. */
LeptrisNode* leptris_node_create_pooled(LeptrisNodeTypeEnum type, size_t size, LeptrisMemoryPool* pool) {
    if (!pool) return NULL;

    if (size < sizeof(LeptrisNode)) {
        size = sizeof(LeptrisNode);
    }

    /* Allocate from pool - O(1) bump-pointer allocation */
    LeptrisNode* node = (LeptrisNode*)leptris_pool_calloc(pool, size);
    if (!node) return NULL;

    node->type = type;
    node->frozen = 0;        /* COW: Initially mutable */
    node->version = 0;       /* COW 2.2: Initial version */

    return node;
}

/* Append child to parent's children list
 * NOTE: In compact architecture, this only works for elements
 * Other node types (text, comment, etc.) are handled differently */
void leptris_node_append_child(LeptrisNode* parent, LeptrisNode* child) {
    if (!parent || !child) return;
    if (parent->type != LEPTRIS_NODE_TYPE_ELEMENT) return;
    if (child->type != LEPTRIS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type for compact pointer access */
    LeptrisElement parent_elem = (LeptrisElement)parent;
    LeptrisElement child_elem = (LeptrisElement)child;

    /* Set parent relationship */
    leptris_element_set_parent(parent_elem, child_elem);

    /* Append to end of children list */
    LeptrisElement last = leptris_element_get_last_child(parent_elem);
    if (last) {
        leptris_element_set_next_sibling(last, child_elem);
    } else {
        leptris_element_set_first_child(parent_elem, child_elem);
    }
}

/* Prepend child to parent's children list */
void leptris_node_prepend_child(LeptrisNode* parent, LeptrisNode* child) {
    if (!parent || !child) return;
    if (parent->type != LEPTRIS_NODE_TYPE_ELEMENT) return;
    if (child->type != LEPTRIS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type for compact pointer access */
    LeptrisElement parent_elem = (LeptrisElement)parent;
    LeptrisElement child_elem = (LeptrisElement)child;

    /* Set parent relationship */
    leptris_element_set_parent(parent_elem, child_elem);

    /* Insert at beginning of children list. The old first child's
     * own next-sibling link is untouched — it already points at the
     * rest of the chain; severing it (the old code NULLed it) drops
     * every following sibling from the tree. */
    LeptrisElement first = leptris_element_get_first_child(parent_elem);
    if (first) {
        leptris_element_set_next_sibling(child_elem, first);
        leptris_element_set_first_child(parent_elem, child_elem);
    } else {
        leptris_element_set_first_child(parent_elem, child_elem);
        leptris_element_set_last_child(parent_elem, child_elem);
    }
}

/* Insert new node before sibling */
void leptris_node_insert_before(LeptrisNode* sibling, LeptrisNode* new_node) {
    if (!sibling || !new_node) return;
    if (sibling->type != LEPTRIS_NODE_TYPE_ELEMENT) return;
    if (new_node->type != LEPTRIS_NODE_TYPE_ELEMENT) return;

    /* Get parent of sibling */
    LeptrisElement sibling_elem = (LeptrisElement)sibling;
    LeptrisElement parent = leptris_element_get_parent(sibling_elem);
    if (!parent) return;

    /* Cast to element type */
    LeptrisElement new_elem = (LeptrisElement)new_node;

    /* Set parent */
    leptris_element_set_parent(new_elem, parent);

    /* Find previous sibling */
    LeptrisElement prev = NULL;
    LeptrisElement curr = leptris_element_get_first_child(parent);
    while (curr && curr != sibling_elem) {
        prev = curr;
        curr = leptris_element_get_next_sibling(curr);
    }

    if (prev) {
        leptris_element_set_next_sibling(prev, new_elem);
        leptris_element_set_next_sibling(new_elem, sibling_elem);
        leptris_element_set_next_sibling(sibling_elem, NULL);  /* Updated by loop */
    } else {
        /* Insert at beginning */
        leptris_element_set_first_child(parent, new_elem);
        leptris_element_set_next_sibling(new_elem, sibling_elem);
        leptris_element_set_next_sibling(sibling_elem, NULL);
    }
}

/* Insert new node after sibling */
void leptris_node_insert_after(LeptrisNode* sibling, LeptrisNode* new_node) {
    if (!sibling || !new_node) return;
    if (sibling->type != LEPTRIS_NODE_TYPE_ELEMENT) return;
    if (new_node->type != LEPTRIS_NODE_TYPE_ELEMENT) return;

    /* Get parent of sibling */
    LeptrisElement sibling_elem = (LeptrisElement)sibling;
    LeptrisElement parent = leptris_element_get_parent(sibling_elem);
    if (!parent) return;

    /* Cast to element type */
    LeptrisElement new_elem = (LeptrisElement)new_node;

    /* Set parent */
    leptris_element_set_parent(new_elem, parent);

    /* Get next sibling */
    LeptrisElement next = leptris_element_get_next_sibling(sibling_elem);

    leptris_element_set_next_sibling(sibling_elem, new_elem);
    leptris_element_set_next_sibling(new_elem, next);

    /* Update last child if needed */
    if (!next) {
        leptris_element_set_last_child(parent, new_elem);
    }
}

/* Remove node from tree */
void leptris_node_remove(LeptrisNode* node) {
    if (!node) return;
    if (node->type != LEPTRIS_NODE_TYPE_ELEMENT) return;

    /* Cast to element type */
    LeptrisElement elem = (LeptrisElement)node;

    /* Get parent */
    LeptrisElement parent = leptris_element_get_parent(elem);
    if (!parent) return;

    /* Find previous sibling */
    LeptrisElement prev = NULL;
    LeptrisElement curr = leptris_element_get_first_child(parent);
    while (curr && curr != elem) {
        prev = curr;
        curr = leptris_element_get_next_sibling(curr);
    }

    /* Update pointers */
    if (prev) {
        LeptrisElement next = leptris_element_get_next_sibling(elem);
        leptris_element_set_next_sibling(prev, next);
    } else {
        /* Was first child */
        LeptrisElement next = leptris_element_get_next_sibling(elem);
        leptris_element_set_first_child(parent, next);
        if (!next) {
            /* Was only child */
            leptris_element_set_last_child(parent, NULL);
        }
    }

    /* Clear parent */
    leptris_element_set_parent(elem, NULL);
    leptris_element_set_next_sibling(elem, NULL);
}

/* Get first child of node (any type: element, text, comment, CDATA, etc.) */
LeptrisNode* leptris_node_first_child_internal(LeptrisNode* node) {
    if (!node) return NULL;
    if (node->type == LEPTRIS_NODE_TYPE_DOCUMENT) {
        /* Issue #580: the document node's children are the document
         * child chain — [prolog nodes..., root, epilog nodes...]. */
        return (LeptrisNode*)((LeptrisDocumentNode*)node)->doc
                   ->doc_children_head;
    }
    if (node->type != LEPTRIS_NODE_TYPE_ELEMENT) return NULL;

    LeptrisElement elem = (LeptrisElement)node;
    /* Return first_child directly - it may point to any node type */
    return leptris_elem_first_child(elem);
}

/* Get last child of node (any type: element, text, comment, CDATA, etc.) */
LeptrisNode* leptris_node_last_child_internal(LeptrisNode* node) {
    if (!node) return NULL;
    if (node->type != LEPTRIS_NODE_TYPE_ELEMENT) return NULL;

    LeptrisElement elem = (LeptrisElement)node;
    /* Return last_child directly - it may point to any node type */
    return leptris_elem_last_child(elem);
}

/* Get child count */
size_t leptris_node_child_count_internal(LeptrisNode* node) {
    if (!node) return 0;
    if (node->type != LEPTRIS_NODE_TYPE_ELEMENT) return 0;

    LeptrisElement elem = (LeptrisElement)node;
    return (size_t)leptris_element_child_count(elem);
}

/* COW: Freeze node and all descendants.
 * Iterative implementation (issue #256) — the recursive version
 * could stack-overflow on deeply nested documents under tight parse
 * loops where the thread stack is constrained. Uses an explicit
 * stack to avoid unbounded recursion. */
void leptris_node_freeze(LeptrisNode* node) {
    if (!node) return;

    /* Simple iterative depth-first walk: freeze current, push
     * children, repeat. The sibling walk is handled inline (no
     * stack push needed for siblings). */
    LeptrisElement stack[256];
    int depth = 0;

    node->frozen = 1;
    if (node->type == LEPTRIS_NODE_TYPE_ELEMENT) {
        LeptrisElement child = leptris_element_get_first_child((LeptrisElement)node);
        while (child || depth > 0) {
            if (child) {
                child->base.frozen = 1;
                LeptrisElement next = leptris_element_get_first_child(child);
                if (next) {
                    if (depth < 256) {
                        stack[depth++] = leptris_element_get_next_sibling(child);
                    }
                    child = next;
                } else {
                    child = leptris_element_get_next_sibling(child);
                }
            } else {
                child = stack[--depth];
            }
        }
    }
}

/* Check if node is frozen */
int leptris_node_is_frozen(LeptrisNode* node) {
    return node ? node->frozen : 0;
}

/* Freeze entire document tree */
void leptris_document_freeze_tree(struct leptris_document* doc) {
    if (!doc) return;
    /* TODO 139 Phase D: lazy promote. */
    leptris_document_ensure_promoted(doc);
    /* new_dom_root is the actual root in the compact architecture;
     * doc->root is the legacy field (always NULL in new code). */
    LeptrisElement root = doc->new_dom_root
        ? (LeptrisElement)doc->new_dom_root
        : (LeptrisElement)doc->root;
    if (!root) return;
    leptris_node_freeze((LeptrisNode*)root);
}

/* Get node version */
unsigned int leptris_node_get_version(LeptrisNode* node) {
    return node ? node->version : 0;
}

/* Increment node version */
void leptris_node_increment_version(LeptrisNode* node) {
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
LeptrisNode* leptris_node_get_next_sibling(LeptrisNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case LEPTRIS_NODE_TYPE_ELEMENT:
            /* Element nodes encode sibling as int32_t offset (TODO 90 Phase 2b). */
            return leptris_elem_next_sibling((LeptrisElement)node);
        case LEPTRIS_NODE_TYPE_TEXT:
            return leptris_textnode_next_sibling((LeptrisTextNode*)node);
        case LEPTRIS_NODE_TYPE_CDATA:
            return leptris_cdata_next_sibling((LeptrisCDATANode*)node);
        case LEPTRIS_NODE_TYPE_COMMENT:
            return leptris_comment_next_sibling((LeptrisCommentNode*)node);
        case LEPTRIS_NODE_TYPE_PI:
            return leptris_pi_next_sibling((LeptrisPINode*)node);
        case LEPTRIS_NODE_TYPE_DOCTYPE:
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
void leptris_node_set_next_sibling(LeptrisNode* node, LeptrisNode* sibling) {
    if (!node) return;

    switch (node->type) {
        case LEPTRIS_NODE_TYPE_ELEMENT:
            leptris_elem_set_next_sibling((LeptrisElement)node, sibling);
            break;
        case LEPTRIS_NODE_TYPE_TEXT:
            leptris_textnode_set_next_sibling((LeptrisTextNode*)node, sibling);
            break;
        case LEPTRIS_NODE_TYPE_CDATA:
            leptris_cdata_set_next_sibling((LeptrisCDATANode*)node, sibling);
            break;
        case LEPTRIS_NODE_TYPE_COMMENT:
            leptris_comment_set_next_sibling((LeptrisCommentNode*)node, sibling);
            break;
        case LEPTRIS_NODE_TYPE_PI:
            leptris_pi_set_next_sibling((LeptrisPINode*)node, sibling);
            break;
        case LEPTRIS_NODE_TYPE_DOCTYPE:
        default:
            /* DOCTYPE and other node types don't have siblings */
            break;
    }
}
