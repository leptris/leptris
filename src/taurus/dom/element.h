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

/* Element node - compact architecture with O(1) child access
 *
 * Uses inline child array for O(1) access by index (beats pugixml!).
 * This is the key optimization for achieving 1.2x+ performance vs pugixml.
 *
 * Size: ~128 bytes (vs 192 bytes in legacy design = 1.5x reduction!)
 *
 * Key features:
 * - Inline children[] array: O(1) child access by index
 * - Regular pointers for parent and sibling navigation
 * - StringView storage for zero-copy parsing
 * - Falls back to linked list for >4 children
 */
struct taurus_element {
    /* Compact base node (4 bytes) - only type, no redundant pointers */
    TaurusNode base;                   /* MUST be first - allows casting */

    /* Compact header (2 bytes) */
    TaurusCompactHeader header;        /* Page offset and flags */

    /* StringView storage (48 bytes) - zero-copy into XML buffer */
    TaurusStringView name_view;       /* Element name */
    TaurusStringView prefix_view;     /* Namespace prefix (zero-copy) */
    TaurusStringView namespace_uri_view; /* Namespace URI (zero-copy) */

    /* Cached NULL-terminated strings (24 bytes) - lazy conversion */
    char* name;                      /* NULL until first access */
    char* prefix;                    /* NULL until first access */
    char* namespace_uri;             /* NULL until first access */

    /* Tree pointers - linked list for all children */
    struct taurus_element* parent;     /* 8 bytes - parent is always an element */
    struct taurus_node* first_child;   /* 8 bytes - first child (any node type) */
    struct taurus_node* last_child;    /* 8 bytes - last child (any node type) */
    struct taurus_node* next_sibling;  /* 8 bytes - next sibling (any node type) */

    /* INLINE CHILD ARRAY (32 bytes) - O(1) child access by index!
     * Stores element children only for O(1) access by index.
     * Falls back to linked list traversal via first_child for non-element children. */
    struct taurus_node* children[4];    /* 32 bytes - inline array for O(1) access */

    /* Attributes */
    struct taurus_attribute* first_attribute; /* 8 bytes */
    uint8_t attr_count;                /* Number of attributes */

    /* Children */
    uint16_t child_count;               /* Total element children count */
    struct taurus_namespace* namespaces; /* Linked list of namespace declarations */
    struct taurus_document* document;  /* NULL if not attached to document */

};

/* Public API type - opaque pointer typedef (matches taurus.h public API) */
typedef struct taurus_element* TaurusElement;

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

/* Create element using memory pool (fast O(1) allocation) */
TaurusElement taurus_element_create_pooled(const char* name, TaurusMemoryPool* pool);

/* Create element with bulk allocation (optimized) */
TaurusElement taurus_element_create_fast(
    const char* name,
    size_t name_len,
    TaurusMemoryPool* pool
);

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

/* ============================================================================
 * Tree Navigation Functions (optimized with inline array)
 * ============================================================================ */

/* Get first child element - uses inline array first, then falls back */
static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* Fast path: check inline array first */
    if (elem->child_count > 0) {
        TaurusNode* node = elem->children[0];
        if (node && node->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)node;
        }
    }

    /* Fallback: use linked list */
    TaurusNode* node = elem->first_child;
    while (node) {
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)node;
        }
        node = taurus_node_get_next_sibling(node);
    }
    return NULL;
}

static inline TaurusElement taurus_element_get_last_child(TaurusElement elem) {
    if (!elem || elem->child_count == 0) return NULL;

    /* For ≤4 children, use inline array */
    if (elem->child_count <= 4) {
        TaurusNode* node = elem->children[elem->child_count - 1];
        if (node && node->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)node;
        }
    }

    /* For >4 children or non-element in array, walk linked list */
    TaurusNode* node = elem->first_child;
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
    if (!elem || !elem->parent) return NULL;

    /* O(1) via parent's inline array for first 4 children */
    TaurusElement parent = elem->parent;
    for (uint16_t i = 0; i < parent->child_count && i < 4; i++) {
        if ((TaurusElement)parent->children[i] == elem) {
            /* Found in inline array */
            if (i + 1 < parent->child_count && i + 1 < 4) {
                /* Next sibling also in array */
                TaurusNode* next = parent->children[i + 1];
                if (next && next->type == TAURUS_NODE_TYPE_ELEMENT) {
                    return (TaurusElement)next;
                }
            }
            /* Fall through to linked list */
            break;
        }
    }

    /* Fallback: Use linked list next_sibling pointer */
    TaurusNode* next = elem->next_sibling;
    while (next) {
        if (next->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)next;
        }
        next = taurus_node_get_next_sibling(next);
    }
    return NULL;
}

/* Get child by index - O(1) via inline array! */
static inline TaurusElement taurus_element_get_child(TaurusElement elem, uint16_t index) {
    if (!elem || index >= elem->child_count) return NULL;
    return (TaurusElement)elem->children[index];
}

/* ============================================================================
 * Tree Navigation Functions (O(1) via inline array!)
 * ============================================================================ */

/* Get first child element (O(1) via inline array) */
static inline TaurusElement taurus_element_get_first_child(TaurusElement elem);

/* Get last child element (O(1) via inline array) */
static inline TaurusElement taurus_element_get_last_child(TaurusElement elem);

/* Get next sibling element (O(1) via parent's inline array) */
static inline TaurusElement taurus_element_get_next_sibling(TaurusElement elem);

/* Get child by index (O(1) via inline array) */
static inline TaurusElement taurus_element_get_child(TaurusElement elem, uint16_t index);

/* Set functions */
void taurus_element_set_parent(TaurusElement elem, TaurusElement parent);
void taurus_element_set_first_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_last_child(TaurusElement elem, TaurusElement child);
void taurus_element_set_next_sibling(TaurusElement elem, TaurusElement sibling);

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

/* Set namespace URI using StringView (zero-copy!) */
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

/* Get element namespace URI as StringView (NO conversion, O(1) access) */
static inline TaurusStringView taurus_element_namespace_view(TaurusElement elem) {
    return elem ? elem->namespace_uri_view : taurus_sv_empty();
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

/* OPTIMIZATION (Phase B): Create namespace with StringViews - ZERO COPY!
 * Stores StringViews directly instead of copying strings.
 * This eliminates the triple allocation (malloc -> pool_strdup -> free).
 *
 * The StringViews should point into the XML buffer which has the same lifetime
 * as the document. Lazy conversion to C strings happens on first access if needed.
 */
struct taurus_namespace* taurus_namespace_new_with_views(
    TaurusStringView* prefix_view,
    TaurusStringView* uri_view,
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
