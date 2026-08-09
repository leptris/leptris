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
typedef struct taurus_doctype*         TaurusDoctype;
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

    /* Performance: FNV-1a hash of the attribute name. Pre-computed at
     * creation so lookup can compare 4-byte hashes before touching
     * the string data. Turns O(N × strlen) into O(N × uint32) for the
     * non-matching case. TODO 113 Phase 4. */
    uint32_t name_hash;
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
/* Phase 2e-B (TODO 150): merged prefix + namespace_uri into a single
 * nullable ns_cache pointer. Most elements have no prefix and no
 * namespace_uri → ns_cache is NULL, zero overhead. Only elements that
 * have a prefix (qualified name like foo:bar) or resolved namespace_uri
 * pay the 16-byte pool allocation for the cache struct.
 *
 * Saves 8 bytes per element (88 → 80). */
struct taurus_ns_cache {
    char* prefix;
    char* namespace_uri;
};

struct taurus_element {
    /* Compact base node (12 bytes) - MUST be first for casting. */
    TaurusNode base;

    /* Packed header + counts (5 bytes, fills the 8-byte tail of base's
     * alignment slot). Phase 2a of TODO 90. */
    TaurusCompactHeader header;        /* 2 bytes */
    uint8_t attr_count;                /* 1 byte */
    uint16_t child_count;              /* 2 bytes */

    /* Cached NULL-terminated strings (16 bytes).
     * Phase 2e-B: prefix + namespace_uri merged into ns_cache (8 bytes
     * instead of 16). name stays inline — it's accessed on every
     * serialize/XPath hit. */
    char* name;                        /* NULL until first access */
    struct taurus_ns_cache* ns_cache;  /* NULL for non-namespaced elements */

    /* Tree edges (16 bytes). */
    int32_t parent_off;
    int32_t first_child_off;
    int32_t last_child_off;
    int32_t next_sibling_off;

    /* Attribute list (8 bytes). */
    int32_t first_attribute_off;
    int32_t last_attribute_off;

    /* Document context (16 bytes). */
    struct taurus_namespace* namespaces;
    struct taurus_document* document;
};

/* Inline accessors — use these instead of direct field access. */
static inline char* taurus_elem_prefix(const TaurusElement e) {
    return e && e->ns_cache ? e->ns_cache->prefix : NULL;
}
static inline char* taurus_elem_ns_uri(const TaurusElement e) {
    return e && e->ns_cache ? e->ns_cache->namespace_uri : NULL;
}

/* Allocate ns_cache if needed, then set prefix. Pool required for
 * the one-time 16-byte alloc. */
static inline void taurus_elem_set_prefix(TaurusElement e, char* prefix,
                                           TaurusMemoryPool* pool) {
    if (!e) return;
    if (!e->ns_cache) {
        if (!pool) return;
        e->ns_cache = (struct taurus_ns_cache*)
            taurus_pool_alloc(pool, sizeof(struct taurus_ns_cache));
        if (!e->ns_cache) return;
        e->ns_cache->prefix = NULL;
        e->ns_cache->namespace_uri = NULL;
    }
    e->ns_cache->prefix = prefix;
}

static inline void taurus_elem_set_ns_uri(TaurusElement e, char* uri,
                                            TaurusMemoryPool* pool) {
    if (!e) return;
    if (!e->ns_cache) {
        if (!pool) return;
        e->ns_cache = (struct taurus_ns_cache*)
            taurus_pool_alloc(pool, sizeof(struct taurus_ns_cache));
        if (!e->ns_cache) return;
        e->ns_cache->prefix = NULL;
    }
    e->ns_cache->namespace_uri = uri;
}


/* Compile-time element-size tracker (TODO 90).
 *
 * Current layout (Phase 1 + 2a + 2b + 2d complete, 88 bytes):
 *   - 12-byte base (TaurusNode + uint32_t line for #223)
 *     + 2-byte header + 1-byte attr_count + 2-byte child_count
 *     packed into the 4-byte tail of base's alignment slot
 *     (5 bytes used, 3 pad)
 *   - 24 bytes of cached name/prefix/namespace_uri char* pointers
 *   - 16 bytes of int32_t tree-edge offsets (parent, first/last/next)
 *   - 8 bytes of int32_t attribute-list offsets (first, last)
 *   - 16 bytes of document context (namespaces, document)
 *
 * pugixml compact node: 12 bytes. Phase 2e of TODO 90 may compress
 * the document-context pointers and string pointers further. */
#ifndef __cplusplus
_Static_assert(sizeof(struct taurus_element) <= 80,
    "taurus_element grew beyond 80 bytes — check for accidental field additions");
#else
static_assert(sizeof(struct taurus_element) <= 80,
    "taurus_element grew beyond 80 bytes");
#endif

/* ============================================================================
 * Compact tree-edge accessors (Phase 2b of TODO 90)
 *
 * Tree edges (parent, first_child, last_child, next_sibling) are stored
 * as int32_t byte-offsets relative to the element's own address, not as
 * raw pointers. This cuts the element struct from 104 → 88 bytes (better
 * cache locality, fewer pool pages on large documents).
 *
 * Offset 0 encodes NULL (no element has zero offset to itself because
 * pool-allocated elements are 8-byte aligned). Read via these inline
 * accessors; never read the `_off` fields directly.
 *
 * Type aliases preserve the original types: parent is always an element;
 * the child/sibling edges may point to any node type (element, text,
 * comment, cdata, pi) so they return TaurusNode*.
 * ============================================================================ */

static inline TaurusElement taurus_elem_parent(const TaurusElement e) {
    return (e)
        ? (TaurusElement)taurus_compact_int32_decode((void*)e, e->parent_off, &e->parent_off)
        : NULL;
}

static inline TaurusNode* taurus_elem_first_child(const TaurusElement e) {
    return (e)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)e, e->first_child_off, &e->first_child_off)
        : NULL;
}

static inline TaurusNode* taurus_elem_last_child(const TaurusElement e) {
    return (e)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)e, e->last_child_off, &e->last_child_off)
        : NULL;
}

static inline TaurusNode* taurus_elem_next_sibling(const TaurusElement e) {
    return (e)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)e, e->next_sibling_off, &e->next_sibling_off)
        : NULL;
}

/* Setters. NULL target resets the offset to 0.  TODO 121: on int32
 * overflow (macOS ASLR can place pool-resident nodes > 2GB apart),
 * fall back to the global overflow table keyed on the field address
 * instead of silently dropping the edge. */
static inline void taurus_elem_set_parent(TaurusElement e, TaurusElement parent) {
    if (!e) return;
    e->parent_off = taurus_compact_int32_encode(e, parent, &e->parent_off);
}

static inline void taurus_elem_set_first_child(TaurusElement e, TaurusNode* child) {
    if (!e) return;
    e->first_child_off = taurus_compact_int32_encode(e, child, &e->first_child_off);
}

static inline void taurus_elem_set_last_child(TaurusElement e, TaurusNode* child) {
    if (!e) return;
    e->last_child_off = taurus_compact_int32_encode(e, child, &e->last_child_off);
}

static inline void taurus_elem_set_next_sibling(TaurusElement e, TaurusNode* sibling) {
    if (!e) return;
    e->next_sibling_off = taurus_compact_int32_encode(e, sibling, &e->next_sibling_off);
}

/* Compact attribute-list accessors (Phase 2d of TODO 90).
 * first_attribute and last_attribute are int32_t byte-offsets to
 * taurus_attribute records in the document's pool; 0 = empty list.
 * Pool-allocated, same safety argument as the tree edges. */
static inline struct taurus_attribute* taurus_elem_first_attribute(const TaurusElement e) {
    return (e)
        ? (struct taurus_attribute*)taurus_compact_int32_decode((void*)e, e->first_attribute_off, &e->first_attribute_off)
        : NULL;
}

static inline struct taurus_attribute* taurus_elem_last_attribute(const TaurusElement e) {
    return (e)
        ? (struct taurus_attribute*)taurus_compact_int32_decode((void*)e, e->last_attribute_off, &e->last_attribute_off)
        : NULL;
}

static inline void taurus_elem_set_first_attribute(TaurusElement e, struct taurus_attribute* attr) {
    if (!e) return;
    e->first_attribute_off = taurus_compact_int32_encode(e, attr, &e->first_attribute_off);
}

static inline void taurus_elem_set_last_attribute(TaurusElement e, struct taurus_attribute* attr) {
    if (!e) return;
    e->last_attribute_off = taurus_compact_int32_encode(e, attr, &e->last_attribute_off);
}

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

/* Create element skeleton for the deferred-NUL zero-copy parser path
 * (TODO 113 Phase 5). Element name is left NULL — parser fills it in
 * after consuming the opening tag. See taurus_element_create_zero_copy. */
TaurusElement taurus_element_create_zero_copy(TaurusMemoryPool* pool);

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
    return taurus_elem_parent(elem);
}

static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* Fast path: if first child is an element, return immediately
     * This covers the common case where elements are directly nested */
    TaurusNode* node = taurus_elem_first_child(elem);
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
    TaurusNode* node = taurus_elem_first_child(elem);
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
    TaurusNode* next = taurus_elem_next_sibling(elem);

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

/* Cache invalidation hook: no-op since children_array was removed in
 * TODO 90 Phase 1. Retained as a single mutation-site chokepoint so a
 * future compact-storage cache (e.g. pugixml-style compact pointer
 * table) can plug in without touching every mutation call site. */
static inline void taurus_element_invalidate_child_cache(TaurusElement elem) {
    (void)elem;
}

/* ============================================================================
 * Attribute Access Functions
 * ============================================================================ */

/* Get first attribute from element */
struct taurus_attribute* taurus_element_get_first_attribute(TaurusElement elem);

/* Set first attribute in element */
void taurus_element_set_first_attribute(TaurusElement elem, struct taurus_attribute* attr);

/* Get attribute count (size_t for public API compatibility — TODO 138) */
size_t taurus_element_attribute_count(TaurusElement elem);

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

/* Add attribute in deferred-NUL mode (TODO 113 Phase 5).
 * Leaves attr->name and attr->value NULL; parser finalizes them
 * after consuming the opening tag. */
int taurus_element_add_attribute_zero_copy(TaurusElement elem,
                                           TaurusStringView name_view,
                                           TaurusStringView value_view,
                                           TaurusMemoryPool* pool);

/* ============================================================================
 * Name and Namespace Access
 * ============================================================================ */

/* Get element name (lazy conversion from StringView to C string) */
const char* taurus_element_get_name(TaurusElement elem);

/* Set prefix using StringView (zero-copy!) */

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

/* Get element name as StringView (derived from cached char* — TODO 90). */
static inline TaurusStringView taurus_element_name_view(TaurusElement elem) {
    return elem && elem->name
        ? taurus_sv_from_ptr(elem->name, strlen(elem->name))
        : taurus_sv_empty();
}

/* Get element prefix as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_element_prefix_view(TaurusElement elem) {
    char* p = taurus_elem_prefix(elem);
    return p ? taurus_sv_from_ptr(p, strlen(p)) : taurus_sv_empty();
}

/* Get element namespace URI as StringView (derived from cached char*). */
static inline TaurusStringView taurus_element_namespace_view(TaurusElement elem) {
    char* u = taurus_elem_ns_uri(elem);
    return u ? taurus_sv_from_ptr(u, strlen(u)) : taurus_sv_empty();
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
    if (!elem || !elem->name) return 0;
    return name.length == strlen(elem->name) &&
           memcmp(elem->name, name.data, name.length) == 0;
}

static inline int taurus_element_name_equals_lit(TaurusElement elem, const char* lit) {
    if (!elem || !lit || !elem->name) return 0;
    return strcmp(elem->name, lit) == 0;
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
