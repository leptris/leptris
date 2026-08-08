/* lib/src/dom/node.h - Base node type for DOM tree
 * Copyright (c) 2024, Ribose Inc.
 *
 * All DOM nodes inherit from TaurusNode base structure.
 * MECE Principle: Every XML construct maps to exactly one node type.
 *
 * COMPACT ARCHITECTURE:
 * Uses compressed pointer encoding for minimal memory footprint.
 * - 4-byte base node (vs 32 bytes in legacy design)
 * - Parent/sibling pointers stored in compressed form in node types
 * - No reference counting (simplifies ownership model)
 */

#ifndef TAURUS_DOM_NODE_H
#define TAURUS_DOM_NODE_H

#include "../taurus_internal.h"

/* Node type enumeration - extended from taurus_internal.h */
typedef enum {
    TAURUS_NODE_TYPE_ELEMENT = 0,
    TAURUS_NODE_TYPE_TEXT = 1,
    TAURUS_NODE_TYPE_COMMENT = 2,
    TAURUS_NODE_TYPE_CDATA = 3,
    TAURUS_NODE_TYPE_PI = 4,           /* Processing Instruction */
    TAURUS_NODE_TYPE_DOCTYPE = 5,
    TAURUS_NODE_TYPE_ATTRIBUTE = 6     /* For XPath attribute nodes */
} TaurusNodeTypeEnum;

/* ============================================================================
 * Node vtable (TODO 23/29, phases 2-3) — see full design below.
 * ============================================================================ */
struct SerializeBuffer;  /* forward */

/* Defined after TaurusNode below. */
typedef struct taurus_node_vtable TaurusNodeVTable;

/* Base node - all nodes start with this structure
 * CRITICAL: All node types MUST have this as their first member
 * This allows safe casting between node types
 *
 * Size: 8 bytes (vs 32 bytes in legacy design)
 * - No parent/sibling pointers (stored in compressed form in node types)
 * - No reference counting (simpler ownership model)
 * - uint32_t line: source line number for error reporting and source
 *   maps (issue #223). 0 = unknown (e.g. programmatically-created
 *   nodes or parsers that don't track line). */
typedef struct taurus_node {
    TaurusNodeTypeEnum type;           /* Node type discriminator (4 bytes) */
    unsigned int frozen : 1;           /* COW: 0 = mutable, 1 = frozen (immutable) */
    unsigned int version : 31;         /* COW 2.2: Node version for tracking modifications */
    uint32_t line;                     /* Source line (1-based, 0 = unknown). Issue #223 */
    /* NOTE: Parent/sibling pointers stored in compressed form in specific node types */
} TaurusNode;

/* ============================================================================
 * Node vtable — full design
 *
 * Each node type registers a vtable holding per-type operations:
 * serialize, type_name, type_enum.  Dispatch becomes data, not code —
 * new node types register through a new vtable entry, no switch to edit.
 *
 * Lookup strategy: a global array indexed by TaurusNodeTypeEnum, so
 * dispatch is `g_node_vtables[node->type].serialize(node, buf)` —
 * no per-node vtable pointer (preserves the compact 4-byte TaurusNode
 * layout that the compact-pointer system relies on).
 * ============================================================================ */
struct taurus_node_vtable {
    /* Serialize this node to buf.  Required for every concrete type. */
    void (*serialize)(TaurusNode* self, struct SerializeBuffer* buf);

    /* Diagnostic type name (e.g., "text", "element").  Used in error
     * messages; never NULL for a registered vtable. */
    const char* type_name;

    /* The TaurusNodeTypeEnum value — lets generic code recover the
     * type without a separate field on the node. */
    TaurusNodeTypeEnum type_enum;
};

/* Global vtable registry — indexed by TaurusNodeTypeEnum.  Populated
 * by node_vtable.c at link time; accessed via taurus_node_vtable_for.
 * See TODO 29. */
const TaurusNodeVTable* taurus_node_vtable_for(TaurusNodeTypeEnum type);

/* Number of entries in the registry (one per TaurusNodeTypeEnum value). */
#define TAURUS_NODE_TYPE_COUNT 7

/* Node creation.
 *
 * Ownership invariant (TODO 05/17): every node is pool-allocated.
 * There is no non-pool creation path.  Freeing is pool-scoped — call
 * taurus_document_free, which destroys the pool.  taurus_node_free
 * is forbidden (would double-free against the pool). */
TaurusNode* taurus_node_create_pooled(TaurusNodeTypeEnum type,
                                       size_t size,
                                       TaurusMemoryPool* pool);

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

/* Freeze is a permanent marker — see taurus.h for the public contract.
 * The internal `taurus_node_thaw` was a TODO stub that just cleared the
 * frozen bit in place, which is unsafe under any COW contract; removed
 * in TODO 88.  Mutations on a frozen doc are currently NOT rejected —
 * the flag is advisory only.  If true read-only enforcement is needed,
 * add explicit `is_frozen` checks at each mutation entry point. */
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
