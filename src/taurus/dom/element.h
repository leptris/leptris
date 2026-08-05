/* lib/src/dom/element.h - Element node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Element nodes contain:
 * - Name and namespace information
 * - Attributes
 * - Child nodes (elements, text, comments, CDATA, PIs)
 *
 * COMPACT ARCHITECTURE:
 * Uses compressed pointer encoding for minimal memory footprint.
 * - ~96 bytes per element (vs 192 bytes in legacy design = 2x reduction!)
 * - 1-2 byte compressed pointers instead of 8-byte pointers
 * - Better cache locality and faster tree traversal
 */

#ifndef TAURUS_DOM_ELEMENT_H
#define TAURUS_DOM_ELEMENT_H

/* These typedefs match the public taurus.h / taurus/types.h.  Guarded
 * so internal headers can be included in any order without triggering
 * C99's typedef-redefinition warning.  See TODO 12. */
#ifndef TAURUS_INTERNAL_TYPES_DEFINED
#define TAURUS_INTERNAL_TYPES_DEFINED
typedef struct taurus_node*            TaurusNodeRef;
typedef struct taurus_document*        TaurusDocument;
typedef struct taurus_element*         TaurusElement;
typedef struct taurus_attribute*       TaurusAttribute;
typedef const char*                    TaurusNamespace;
typedef struct taurus_xpath_result*    TaurusXPathResult;
#endif

#include "node.h"
#include "../common/string_view.h"
#include "compact.h"  /* Compressed pointer types */

/* Attribute structure - inline storage for minimal memory footprint
 *
 * Instead of storing pointers to attribute structures, we store attributes
 * as a linked list with inline StringView storage. This eliminates pointer
 * indirection and improves cache locality.
 *
 * Size: ~32 bytes vs 64 bytes for legacy attribute design
 */
struct taurus_attribute {
    /* StringView storage (zero-copy, points into XML buffer) */
    TaurusStringView name_view;
    TaurusStringView value_view;
    TaurusStringView prefix_view;
    TaurusStringView namespace_uri_view;

    /* Cached NULL-terminated (lazy conversion from pool) */
    char* name;                  /* NULL until first access */
    char* value;                 /* NULL until first access */
    char* prefix;                /* NULL until first access */
    char* namespace_uri;         /* NULL until first access */

    /* Next attribute in linked list */
    struct taurus_attribute* next;

    /* Performance: Pre-computed entity flag (set during parsing) */
    unsigned char has_entities;  /* 1 if value_view contains '&', 0 otherwise */
};

/* Element node - compact architecture
 *
 * Uses compressed pointers and inline strings for minimal memory footprint.
 * This enables better cache locality and faster tree traversal.
 *
 * Size: ~96 bytes (vs 192 bytes in legacy design = 2x reduction!)
 *
 * Key features:
 * - 1-byte compressed pointers for child/sibling/attribute (±504 bytes)
 * - 2-byte compressed pointer for parent (±262KB)
 * - StringView storage for zero-copy parsing
 * - uint8_t counts instead of size_t (1 byte vs 8 bytes)
 * - 4-byte base node (vs 32 bytes in legacy) - no redundant pointers
 * - Falls back to hash table for large offsets
 */
struct taurus_element {
    /* Compact base node (4 bytes) - only type, no redundant pointers */
    TaurusNode base;                   /* MUST be first - allows casting */

    /* Compact header (2 bytes) */
    TaurusCompactHeader header;        /* Page offset and flags */

    /* StringView storage (32 bytes) - zero-copy into XML buffer.
     * namespace_uri_view REMOVED (TODO 90) — the field was always
     * set from the input buffer and lazily converted to namespace_uri
     * char*.  Removing it makes the eager conversion the only path.
     * Saves 16 bytes per element. */
    TaurusStringView name_view;       /* Element name */
    TaurusStringView prefix_view;     /* Namespace prefix (zero-copy) */

    /* Cached NULL-terminated strings (24 bytes) - lazy conversion */
    char* name;                      /* NULL until first access */
    char* prefix;                    /* NULL until first access */
    char* namespace_uri;             /* NULL until first access */

    /* Tree pointers (32 bytes) - Regular pointers for performance!
     * Uses TaurusNode* for type-safe mixed content (elements, text, CDATA, etc.)
     * COMPACT vs REGULAR POINTERS TRADE-OFF:
     * - Compact: 3 bytes, but requires page_base calculation (SLOW!)
     * - Regular: 32 bytes (4×8 bytes), but direct pointer access (FAST!)
     *
     * Hybrid approach: Use regular pointers for hot navigation paths
     * to achieve 1.2x performance target. Still 2x better than legacy 192 bytes.
     *
     * NOTE: Using TaurusNode* instead of TaurusElement for type safety:
     * - Allows mixed content (elements + text nodes) without type confusion
     * - Consistent with text/cdata/comment/PI node structures
     * - Traversal uses taurus_node_get_next_sibling() helper */
    struct taurus_element* parent;     /* 8 bytes - parent is always an element */
    struct taurus_node* first_child;   /* 8 bytes - can be any node type */
    struct taurus_node* last_child;    /* 8 bytes - can be any node type */
    struct taurus_node* next_sibling;  /* 8 bytes - can be any node type */

    /* Attributes (9 bytes) - Use regular pointers for correctness and robustness
     * Compact pointers for attributes require careful page_base management which
     * causes fragility during parsing. Regular pointers are more reliable. */
    struct taurus_attribute* first_attribute; /* 8 bytes - regular pointer */
    struct taurus_attribute* last_attribute;  /* 8 bytes - tail pointer for O(1) appends (TODO 106) */
    uint8_t attr_count;                /* Number of attributes */

    /* Children (3 bytes) */
    uint16_t child_count;             /* Number of elements (max 65535) */
    struct taurus_namespace* namespaces; /* Linked list of namespace declarations */
    struct taurus_document* document;  /* NULL if not attached to document */

    /* O(1) indexed child access cache (TODO 103 Phase 4).
     *
     * NULL until the first call to taurus_element_child(elem, j). Built
     * lazily from the first_child linked list, then invalidated (set to
     * NULL) by any structural mutation (append/prepend/insert/remove
     * child).  Pool-allocated; lives for the document's lifetime.
     *
     * Threading: safe for the common parse-then-read pattern (single
     * threaded first access, then concurrent reads of the immutable
     * array).  Concurrent first-access from multiple threads would
     * race; document as a known limitation. */
    struct taurus_element** children_array;
};

/* Compile-time element-size tracker (TODO 90).
 *
 * Current layout uses regular 8-byte pointers + StringView (16 bytes each).
 * pugixml compact node: 12 bytes. Our compact-mode target: ~23 bytes.
 * This assert catches accidental growth.  When the compact-pointer
 * migration lands, the struct shrinks and this upper bound drops. */
#ifndef __cplusplus
_Static_assert(sizeof(struct taurus_element) <= 200,
    "taurus_element grew beyond 200 bytes — check for accidental field additions");
#else
static_assert(sizeof(struct taurus_element) <= 200,
    "taurus_element grew beyond 200 bytes");
#endif

/* TaurusElement typedef comes from the public include/taurus/types.h
 * (re-exported via taurus.h).  No local redefinition — see TODO 12. */

/* ============================================================================
 * Element Creation
 * ============================================================================ */

/* Create element with StringView (true zero-copy!) */
TaurusElement taurus_element_create_with_view(
    TaurusStringView name_view,
    TaurusMemoryPool* pool
);

/* Create element with in-place string (zero-copy) */
TaurusElement taurus_element_create_pooled_inplace(char* name, TaurusMemoryPool* pool);

/* Create element using memory pool (O(1) bump allocation).
 * Single pool-routed entry point — TODO 26 removed the _fast wrapper. */
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool);

void taurus_element_free(TaurusElement elem);

/* ============================================================================
 * Hot Accessor Functions (static inline for performance)
 * ============================================================================ */

/* These are static inline to eliminate function call overhead.
 * The compiler can inline these across translation units for maximum performance.
 * All are simple field accesses with NULL checks - O(1) operations. */

static inline TaurusElement taurus_element_get_parent(TaurusElement elem) {
    return elem ? elem->parent : NULL;
}

static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* Fast path: if first child is an element, return immediately
     * This covers the common case where elements are directly nested */
    TaurusNode* node = (TaurusNode*)elem->first_child;
    if (node && node->type == TAURUS_NODE_TYPE_ELEMENT) {
        return (TaurusElement)node;
    }

    /* Slow path: skip non-element nodes (text, comments, etc.) */
    while (node) {
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)node;
        }
        node = taurus_node_get_next_sibling(node);
    }
    return NULL;
}

static inline TaurusElement taurus_element_get_last_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* last_child might point to a non-element node (text, comment, etc.)
     * We need to scan from first_child to find the last element child */
    TaurusNode* node = (TaurusNode*)elem->first_child;
    TaurusElement last_elem = NULL;

    while (node) {
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            last_elem = (TaurusElement)node;
        }
        node = taurus_node_get_next_sibling(node);
    }
    return last_elem;
}

static inline TaurusElement taurus_element_get_next_sibling(TaurusElement elem) {
    if (!elem) return NULL;
    /* Get next sibling and skip non-element nodes
     * This implements the XPath semantics where sibling axis only returns elements */
    TaurusNode* next = (TaurusNode*)elem->next_sibling;

    /* Fast path: if next sibling is an element, return immediately
     * This covers the common case where elements are directly nested */
    if (next && next->type == TAURUS_NODE_TYPE_ELEMENT) {
        return (TaurusElement)next;
    }

    /* Slow path: skip non-element nodes (text, comments, etc.) */
    while (next && next->type != TAURUS_NODE_TYPE_ELEMENT) {
        next = taurus_node_get_next_sibling(next);
    }

    return (TaurusElement)next;
}

/* ============================================================================
 * Compressed Pointer Access Functions (deprecated/removed - use inline above)
 * ============================================================================ */
TaurusElement taurus_element_get_parent(TaurusElement elem);
TaurusElement taurus_element_get_first_child(TaurusElement elem);
TaurusElement taurus_element_get_last_child(TaurusElement elem);
TaurusElement taurus_element_get_next_sibling(TaurusElement elem);

/* Set encoded pointers in element */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent);
void taurus_element_set_first_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_last_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling);

/* Cache invalidation: call after any structural mutation (child add/
 * remove/insert) to mark the indexed-access cache as stale.  Next
 * taurus_element_child(elem, j) call rebuilds it lazily.  Encapsulates
 * the cache so mutation sites don't poke at children_array directly
 * — and so compact-storage migration (TODO 90) can change the cache
 * representation without touching every call site. */
static inline void taurus_element_invalidate_child_cache(TaurusElement elem) {
    if (elem) elem->children_array = NULL;
}

/* ============================================================================
 * Attribute Access Functions
 * ============================================================================ */

/* Get first attribute from element */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem);

/* Set first attribute in element */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr);

/* Get attribute count */
uint8_t taurus_element_attribute_count(TaurusElement elem);

/* Get attribute by index */
struct taurus_attribute* taurus_element_get_attribute_by_index(TaurusElement elem, uint8_t index);

/* Get attribute by name */
struct taurus_attribute* taurus_element_get_attribute_by_name(TaurusElement elem, const char* name);

/* Get attribute by name (StringView version - internal, faster) */
struct taurus_attribute* taurus_element_get_attribute_by_name_view(TaurusElement elem, TaurusStringView name);

/* Add attribute to element */
int taurus_element_add_attribute(TaurusElement elem,
                                TaurusStringView name_view,
                                TaurusStringView value_view,
                                TaurusMemoryPool* pool);

/* ============================================================================
 * Name and Namespace Access
 * ============================================================================ */

/* Get element name (lazy conversion from StringView to C string) */
const char* taurus_element_get_name(TaurusElement elem);

/* Set prefix using StringView (zero-copy!) */
void taurus_element_set_prefix_view(TaurusElement elem, TaurusStringView prefix_view);

/* Set namespace URI from StringView (eager pool-strdup — TODO 90).
 * The namespace_uri_view field was removed from the struct; this
 * setter does the conversion eagerly so the cached char* is always
 * available. */
void taurus_element_set_namespace_uri_view(TaurusElement elem, TaurusStringView uri_view);

/* Get element prefix */
const char* taurus_element_get_prefix(TaurusElement elem);

/* Get element namespace URI */
const char* taurus_element_get_namespace_uri(TaurusElement elem);

/* Legacy functions for C string input */
void taurus_element_set_prefix(TaurusElement elem, const char* prefix);
void taurus_element_set_namespace_uri(TaurusElement elem, const char* uri);

/* ============================================================================
 * Internal StringView Accessors (for performance-critical internal code)
 * ============================================================================ */

/* Get element name as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_element_name_view(TaurusElement elem) {
    return elem ? elem->name_view : taurus_sv_empty();
}

/* Get element prefix as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_element_prefix_view(TaurusElement elem) {
    return elem ? elem->prefix_view : taurus_sv_empty();
}

/* Get element namespace URI as StringView (derived from cached char*).
 * TODO 90: namespace_uri_view removed from struct; this accessor
 * reconstructs the view on demand. Safe because namespace_uri is
 * always NUL-terminated and pool-owned. */
static inline TaurusStringView taurus_element_namespace_view(TaurusElement elem) {
    return elem && elem->namespace_uri
        ? taurus_sv_from_ptr(elem->namespace_uri, strlen(elem->namespace_uri))
        : taurus_sv_empty();
}

/* Get attribute name as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_attribute_name_view(const struct taurus_attribute* attr) {
    return attr ? attr->name_view : taurus_sv_empty();
}

/* Get attribute value as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_attribute_value_view(const struct taurus_attribute* attr) {
    return attr ? attr->value_view : taurus_sv_empty();
}

/* Fast name comparison helpers (for hot paths like traversal) */
static inline int taurus_element_name_equals(TaurusElement elem, TaurusStringView name) {
    return taurus_sv_equals(&elem->name_view, &name);
}

static inline int taurus_element_name_equals_lit(TaurusElement elem, const char* lit) {
    if (!elem || !lit) return 0;
    size_t lit_len = strlen(lit);
    return elem->name_view.length == lit_len &&
           memcmp(elem->name_view.data, lit, lit_len) == 0;
}

/* ============================================================================
 * Child Access Functions
 * ============================================================================ */

/* Get child count */
size_t taurus_element_child_count(TaurusElement elem);

/* Get child by index */
TaurusElement taurus_element_get_child(TaurusElement elem, uint16_t index);

/* ============================================================================
 * Legacy Public API Functions (compatibility wrappers)
 * ============================================================================ */

/* Add attribute (legacy C string API) */
void taurus_element_add_attribute_legacy(TaurusElement elem,
                                   const char* name,
                                   const char* value);

/* Add attribute using memory pool (fast) */
void taurus_element_add_attribute_pooled(TaurusElement elem,
                                   const char* name,
                                   const char* value,
                                   TaurusMemoryPool* pool);

/* Add attribute with in-place strings (zero-copy) */
void taurus_element_add_attribute_pooled_inplace(TaurusElement elem,
                                                   char* name,
                                                   char* value,
                                                   TaurusMemoryPool* pool);

/* Get attribute value by name (legacy API) */
const char* taurus_element_get_attribute_legacy(TaurusElement elem, const char* name);

/* NOTE: Use taurus_namespace_new() + taurus_element_add_namespace() from taurus_memory.h
 * for proper namespace management. This function is deprecated. */
void taurus_element_add_namespace_deprecated(TaurusElement elem,
                                             const char* prefix,
                                             const char* uri);

/* Add namespace with in-place strings (zero-copy) */
void taurus_element_add_namespace_inplace(TaurusElement elem,
                                           char* prefix,
                                           char* uri,
                                           TaurusMemoryPool* pool);

/* Create namespace structure using pool allocation (recommended for parsing)
 * Namespace and strings are automatically freed when pool is destroyed.
 * This supports multiple documents being parsed simultaneously. */
struct taurus_namespace* taurus_namespace_new_pooled(
    const char* prefix,
    const char* uri,
    TaurusMemoryPool* pool);

/* Lookup namespace URI by prefix */
const char* taurus_element_lookup_namespace(TaurusElement elem,
                                             const char* prefix);

/* Children manipulation */
void taurus_element_append_child_internal(TaurusElement elem, TaurusNode* child);
void taurus_element_prepend_child_internal(TaurusElement elem, TaurusNode* child);

/* Bulk allocation for subtree copy (10-15% faster for large subtrees) */
TaurusElement taurus_element_append_copy_bulk(TaurusElement parent, TaurusElement source);

/* Text content extraction (concatenates all text nodes) */
char* taurus_element_get_text_content(TaurusElement elem);

/* Document tree operations */
void taurus_element_set_document_tree(TaurusElement elem, struct taurus_document* doc);

/* ============================================================================
 * Subtree Analysis (for bulk allocation planning)
 * ============================================================================ */

/* Structure to hold subtree statistics for bulk allocation */
typedef struct taurus_subtree_stats {
    uint32_t element_count;    /* Total elements in subtree */
    uint32_t attribute_count;  /* Total attributes in subtree */
    uint32_t text_count;       /* Total text nodes in subtree */
    uint32_t comment_count;    /* Total comment nodes in subtree */
    uint32_t cdata_count;     /* Total CDATA sections in subtree */
    uint32_t pi_count;         /* Total PIs in subtree */
} TaurusSubtreeStats;

/* Count all nodes in subtree (for bulk allocation planning)
 * Recursively traverses element tree and counts all nodes by type.
 * Used to pre-calculate allocation size for bulk operations.
 *
 * @param elem Root element to count
 * @param stats Output structure to fill with counts
 */
void taurus_element_count_subtree(TaurusElement elem, TaurusSubtreeStats* stats);

/* ============================================================================
 * Casting Helpers
 * ============================================================================ */

#define TAURUS_NODE_AS_ELEMENT(node) \
    (TAURUS_NODE_IS_ELEMENT(node) ? (TaurusElement)(node) : NULL)

#define TAURUS_ELEMENT_AS_NODE(elem) \
    ((TaurusNode*)(elem))

#endif /* TAURUS_DOM_ELEMENT_H */
