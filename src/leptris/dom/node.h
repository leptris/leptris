/* lib/src/dom/node.h - Base node type for DOM tree
 * Copyright (c) 2024, Ribose Inc.
 *
 * All DOM nodes inherit from LeptrisNode base structure.
 * MECE Principle: Every XML construct maps to exactly one node type.
 *
 * COMPACT ARCHITECTURE:
 * Uses compressed pointer encoding for minimal memory footprint.
 * - 4-byte base node (vs 32 bytes in legacy design)
 * - Parent/sibling pointers stored in compressed form in node types
 * - No reference counting (simplifies ownership model)
 */

#ifndef LEPTRIS_DOM_NODE_H
#define LEPTRIS_DOM_NODE_H

#include "../leptris_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Node type enumeration - extended from leptris_internal.h */
typedef enum {
    LEPTRIS_NODE_TYPE_ELEMENT = 0,
    LEPTRIS_NODE_TYPE_TEXT = 1,
    LEPTRIS_NODE_TYPE_COMMENT = 2,
    LEPTRIS_NODE_TYPE_CDATA = 3,
    LEPTRIS_NODE_TYPE_PI = 4,           /* Processing Instruction */
    LEPTRIS_NODE_TYPE_DOCTYPE = 5,
    LEPTRIS_NODE_TYPE_ATTRIBUTE = 6     /* For XPath attribute nodes */
} LeptrisNodeTypeEnum;

/* ============================================================================
 * Node vtable (TODO 23/29, phases 2-3) — see full design below.
 * ============================================================================ */
struct SerializeBuffer;  /* forward */

/* Defined after LeptrisNode below. */
typedef struct leptris_node_vtable LeptrisNodeVTable;

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
typedef struct leptris_node {
    LeptrisNodeTypeEnum type;           /* Node type discriminator (4 bytes) */
    unsigned int frozen : 1;           /* COW: 0 = mutable, 1 = frozen (immutable) */
    unsigned int version : 31;         /* COW 2.2: Node version for tracking modifications */
    uint32_t line;                     /* Source line (1-based, 0 = unknown). Issue #223 */
    void* binding_wrapper;             /* FFI wrapper cache (#262). NULL when no binding
                                        * is attached. Set by the language binding on
                                        * first node wrap; subsequent traversals find
                                        * the cached wrapper without FFI call overhead. */
    /* NOTE: Parent/sibling pointers stored in compressed form in specific node types */
} LeptrisNode;

/* ============================================================================
 * Node vtable — full design
 *
 * Each node type registers a vtable holding per-type operations:
 * serialize, type_name, type_enum.  Dispatch becomes data, not code —
 * new node types register through a new vtable entry, no switch to edit.
 *
 * Lookup strategy: a global array indexed by LeptrisNodeTypeEnum, so
 * dispatch is `g_node_vtables[node->type].serialize(node, buf)` —
 * no per-node vtable pointer (preserves the compact 4-byte LeptrisNode
 * layout that the compact-pointer system relies on).
 * ============================================================================ */
struct leptris_node_vtable {
    /* Serialize this node to buf.  Required for every concrete type. */
    void (*serialize)(LeptrisNode* self, struct SerializeBuffer* buf);

    /* Diagnostic type name (e.g., "text", "element").  Used in error
     * messages; never NULL for a registered vtable. */
    const char* type_name;

    /* The LeptrisNodeTypeEnum value — lets generic code recover the
     * type without a separate field on the node. */
    LeptrisNodeTypeEnum type_enum;
};

/* Global vtable registry — indexed by LeptrisNodeTypeEnum.  Populated
 * by node_vtable.c at link time; accessed via leptris_node_vtable_for.
 * See TODO 29. */
const LeptrisNodeVTable* leptris_node_vtable_for(LeptrisNodeTypeEnum type);

/* Number of entries in the registry (one per LeptrisNodeTypeEnum value). */
#define LEPTRIS_NODE_TYPE_COUNT 7

/* Node creation.
 *
 * Ownership invariant (TODO 05/17): every node is pool-allocated.
 * There is no non-pool creation path.  Freeing is pool-scoped — call
 * leptris_document_free, which destroys the pool.  leptris_node_free
 * is forbidden (would double-free against the pool). */
LeptrisNode* leptris_node_create_pooled(LeptrisNodeTypeEnum type,
                                       size_t size,
                                       LeptrisMemoryPool* pool);

/* Tree manipulation - maintain parent/sibling links */
void leptris_node_append_child(LeptrisNode* parent, LeptrisNode* child);
void leptris_node_prepend_child(LeptrisNode* parent, LeptrisNode* child);
void leptris_node_insert_before(LeptrisNode* sibling, LeptrisNode* new_node);
void leptris_node_insert_after(LeptrisNode* sibling, LeptrisNode* new_node);
void leptris_node_remove(LeptrisNode* node);

/* Tree navigation helpers - Internal functions (use _internal suffix)
 * These work with LeptrisNode* (pointers to struct) internally
 * Public API wrappers in leptris.c work with LeptrisNode (opaque typedef) */
LeptrisNode* leptris_node_first_child_internal(LeptrisNode* node);
LeptrisNode* leptris_node_last_child_internal(LeptrisNode* node);
size_t leptris_node_child_count_internal(LeptrisNode* node);

/* COW: Freezing/thawing functions */
/* Mark node and all its descendants as frozen (immutable) */
void leptris_node_freeze(LeptrisNode* node);

/* Check if node is frozen (returns 1 if frozen, 0 if mutable) */
int leptris_node_is_frozen(LeptrisNode* node);

/* Freeze is a permanent marker — see leptris.h for the public contract.
 * The internal `leptris_node_thaw` was a TODO stub that just cleared the
 * frozen bit in place, which is unsafe under any COW contract; removed
 * in TODO 88.  Mutations on a frozen doc are currently NOT rejected —
 * the flag is advisory only.  If true read-only enforcement is needed,
 * add explicit `is_frozen` checks at each mutation entry point. */
/* Freeze entire document tree (starting from root element) */
void leptris_document_freeze_tree(struct leptris_document* doc);

/* COW 2.2: Version tracking functions */
/* Get node version (returns version number) */
unsigned int leptris_node_get_version(LeptrisNode* node);

/* Increment node version (called on modifications) */
void leptris_node_increment_version(LeptrisNode* node);

/* Type checking macros */
#define LEPTRIS_NODE_IS_ELEMENT(node)   ((node) && (node)->type == LEPTRIS_NODE_TYPE_ELEMENT)
#define LEPTRIS_NODE_IS_TEXT(node)      ((node) && (node)->type == LEPTRIS_NODE_TYPE_TEXT)
#define LEPTRIS_NODE_IS_COMMENT(node)   ((node) && (node)->type == LEPTRIS_NODE_TYPE_COMMENT)
#define LEPTRIS_NODE_IS_CDATA(node)     ((node) && (node)->type == LEPTRIS_NODE_TYPE_CDATA)
#define LEPTRIS_NODE_IS_PI(node)        ((node) && (node)->type == LEPTRIS_NODE_TYPE_PI)
#define LEPTRIS_NODE_IS_DOCTYPE(node)   ((node) && (node)->type == LEPTRIS_NODE_TYPE_DOCTYPE)
#define LEPTRIS_NODE_IS_ATTRIBUTE(node) ((node) && (node)->type == LEPTRIS_NODE_TYPE_ATTRIBUTE)

/* Generic next_sibling accessor - handles all node types correctly
 * Each node type has next_sibling at a different offset, so we need
 * to dispatch based on node type. */
LeptrisNode* leptris_node_get_next_sibling(LeptrisNode* node);

/* Generic next_sibling setter - handles all node types correctly
 * Sets the next_sibling pointer for any node type. */
void leptris_node_set_next_sibling(LeptrisNode* node, LeptrisNode* sibling);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DOM_NODE_H */
