/* lib/src/dom/node.h - Base node type for DOM tree
 * Copyright (c) 2024, Ribose Inc.
 *
 * All DOM nodes inherit from TaurusNode base structure.
 * MECE Principle: Every XML construct maps to exactly one node type.
 *
 * POINTER-BASED ARCHITECTURE:
 * Uses direct pointers for tree navigation.
 * - 20-byte base node with direct sibling pointers
 * - O(1) navigation without offset calculations
 */

#ifndef TAURUS_DOM_NODE_H
#define TAURUS_DOM_NODE_H

#include "../taurus_internal.h"

/* Node type enumeration
 * IMPORTANT: These values MUST match:
 *   - TaurusNodeType in taurus_internal.h
 *   - PTR_NODE_TYPE_* macros in ptr_element.h
 */
typedef enum {
    TAURUS_NODE_TYPE_ELEMENT = 0,   /* TAURUS_NODE_ELEMENT / PTR_NODE_TYPE_ELEMENT */
    TAURUS_NODE_TYPE_ATTRIBUTE = 1, /* TAURUS_NODE_ATTRIBUTE */
    TAURUS_NODE_TYPE_TEXT = 2,      /* TAURUS_NODE_TEXT / PTR_NODE_TYPE_TEXT */
    TAURUS_NODE_TYPE_COMMENT = 3,   /* TAURUS_NODE_COMMENT / PTR_NODE_TYPE_COMMENT */
    TAURUS_NODE_TYPE_CDATA = 7,     /* Unique value for CDATA (different from COMMENT) */
    TAURUS_NODE_TYPE_PI = 4,        /* TAURUS_NODE_PI / PTR_NODE_TYPE_PI */
    TAURUS_NODE_TYPE_DOCTYPE = 5,   /* TAURUS_NODE_DOCTYPE / PTR_NODE_TYPE_DOCTYPE */
    TAURUS_NODE_TYPE_NAMESPACE = 6  /* TAURUS_NODE_NAMESPACE */
} TaurusNodeTypeEnum;

/* Base node - all nodes start with this structure
 * CRITICAL: All node types MUST have this as their first member
 * This allows safe casting between node types
 *
 * Size: 20 bytes (pointer-based)
 */
typedef struct taurus_node {
    TaurusNodeTypeEnum type;           /* Node type discriminator (4 bytes) */
    unsigned int frozen : 1;           /* COW: 0 = mutable, 1 = frozen */
    unsigned int version : 31;         /* COW: Node version tracking */
    struct taurus_node* next_sibling;  /* Next sibling (8 bytes) */
    struct taurus_node* prev_sibling;  /* Previous sibling (8 bytes) */
} TaurusNode;

/* Node creation and destruction */
TaurusNode* taurus_node_create(TaurusNodeTypeEnum type, size_t size);

/* Create node using memory pool (fast O(1) allocation) */
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type, size_t size, TaurusMemoryPool* pool);

void taurus_node_free(TaurusNode* node);

/* Tree manipulation - maintain parent/sibling links */
void taurus_node_append_child(TaurusNode* parent, TaurusNode* child);
void taurus_node_prepend_child(TaurusNode* parent, TaurusNode* child);
void taurus_node_insert_before(TaurusNode* sibling, TaurusNode* new_node);
void taurus_node_insert_after(TaurusNode* sibling, TaurusNode* new_node);
void taurus_node_remove(TaurusNode* node);

/* Tree navigation helpers - Internal functions (use _internal suffix)
 * These work with TaurusNode* (pointers to struct) internally
 * Public API wrappers in taurus.c work with TaurusNode (opaque typedef) */
TaurusNode* taurus_node_first_child_internal(TaurusNode* node);
TaurusNode* taurus_node_last_child_internal(TaurusNode* node);
size_t taurus_node_child_count_internal(TaurusNode* node);

/* COW: Freezing/thawing functions */
/* Mark node and all its descendants as frozen (immutable) */
void taurus_node_freeze(TaurusNode* node);

/* Check if node is frozen (returns 1 if frozen, 0 if mutable) */
int taurus_node_is_frozen(TaurusNode* node);

/* Thaw node (prepare for modification) - returns mutable copy or NULL on failure
 * NOTE: For Phase 2.1, this just returns the node if not frozen
 * Full COW implementation will be in Phase 2.4 */
TaurusNode* taurus_node_thaw(TaurusNode* node);

/* Freeze entire document tree (starting from root element) */
void taurus_document_freeze_tree(struct taurus_document* doc);

/* COW 2.2: Version tracking functions */
/* Get node version (returns version number) */
unsigned int taurus_node_get_version(TaurusNode* node);

/* Increment node version (called on modifications) */
void taurus_node_increment_version(TaurusNode* node);

/* Type checking macros */
#define TAURUS_NODE_IS_ELEMENT(node)   ((node) && (node)->type == TAURUS_NODE_TYPE_ELEMENT)
#define TAURUS_NODE_IS_TEXT(node)      ((node) && (node)->type == TAURUS_NODE_TYPE_TEXT)
#define TAURUS_NODE_IS_COMMENT(node)   ((node) && (node)->type == TAURUS_NODE_TYPE_COMMENT)
#define TAURUS_NODE_IS_CDATA(node)     ((node) && (node)->type == TAURUS_NODE_TYPE_CDATA)
#define TAURUS_NODE_IS_PI(node)        ((node) && (node)->type == TAURUS_NODE_TYPE_PI)
#define TAURUS_NODE_IS_DOCTYPE(node)   ((node) && (node)->type == TAURUS_NODE_TYPE_DOCTYPE)
#define TAURUS_NODE_IS_ATTRIBUTE(node) ((node) && (node)->type == TAURUS_NODE_TYPE_ATTRIBUTE)

/* Generic next_sibling accessor - handles all node types correctly
 * Each node type has next_sibling at a different offset, so we need
 * to dispatch based on node type. */
TaurusNode* taurus_node_get_next_sibling(TaurusNode* node);

/* Generic next_sibling setter - handles all node types correctly
 * Sets the next_sibling pointer for any node type. */
void taurus_node_set_next_sibling(TaurusNode* node, TaurusNode* sibling);

#endif /* TAURUS_DOM_NODE_H */
