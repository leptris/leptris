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

/* Attribute hash table entry for O(1) lookup
 * Used when element has >4 attributes to avoid O(n) linked list traversal.
 */
struct taurus_attr_hash_entry {
    struct taurus_attribute* attr;
    struct taurus_attr_hash_entry* next;  /* Collision chain */
};

/* Element node - compact architecture with O(1) child and attribute access
 *
 * Uses inline arrays for O(1) access by index (beats pugixml!).
 * This is the key optimization for achieving 1.2x+ performance vs pugixml.
 *
 * Size: ~168 bytes (with inline attribute optimization)
 *
 * Key features:
 * - Inline children[] array: O(1) child access by index
 * - Inline attributes[] array: O(1) attribute access for first 4 attrs
 * - Hash table for >4 attributes: O(1) lookup even with many attributes
 * - Regular pointers for parent and sibling navigation
 * - StringView storage for zero-copy parsing
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

    /* INLINE ATTRIBUTE ARRAY (32 bytes) - O(1) attribute access for first 4!
     * For elements with <=4 attributes, direct array access is O(1).
     * For >4 attributes, a hash table is created for O(1) lookup. */
    struct taurus_attribute* attributes_inline[4];  /* 32 bytes - inline array */

    /* Hash table for O(1) attribute lookup when >4 attributes
     * Allocated from pool when attr_count > 4.
     * Uses FNV-1a hash for attribute name hashing. */
    struct taurus_attr_hash_entry** attr_hash;  /* 8 bytes - NULL if <=4 attrs */
    uint8_t attr_hash_size;           /* Hash table size (power of 2, 0 if no hash) */

    /* Attribute linked list (for iteration and as fallback) */
    struct taurus_attribute* first_attribute; /* 8 bytes - head of linked list */
    struct taurus_attribute* last_attribute;  /* 8 bytes - tail for O(1) append */
    uint8_t attr_count;                /* Number of attributes */

    /* Children */
    uint16_t child_count;               /* Total element children count */
    struct taurus_namespace* namespaces; /* Linked list of namespace declarations */
    struct taurus_document* document;  /* NULL if not attached to document */

    /* Compact mode support (4 bytes)
     * When document->is_compact is true, this stores the offset to the
     * corresponding compact_element within the document's compact block.
     * When 0 and in compact mode, this element IS the root element. */
    uint32_t compact_offset;           /* Offset to compact element, 0 if root or not compact */

    /* ============================================================================
     * COMPACT-ONLY MIGRATION: Offset fields (Phase 2)
     * ============================================================================
     * These offset fields parallel the pointer fields above. During migration,
     * accessor functions will use offsets when document->is_compact is true.
     * Once migration is complete, pointer fields will be removed.
     *
     * Target: 48-byte element with these offsets + document pointer
     * Future: 28-byte element (remove document pointer, use external lookup)
     */

    /* Tree navigation offsets (16 bytes) - parallel to pointer fields */
    uint32_t parent_offset;            /* Offset to parent element, 0 if root */
    uint32_t first_child_offset;       /* Offset to first child node, 0 if none */
    uint32_t last_child_offset;        /* Offset to last child node, 0 if none */
    uint32_t next_sibling_offset;      /* Offset to next sibling node, 0 if none */

    /* String table offsets (12 bytes) - for compact string storage */
    uint32_t name_offset;              /* Offset to element name in string table */
    uint32_t namespace_uri_offset;     /* Offset to namespace URI, 0 if none */
    uint32_t prefix_offset;            /* Offset to namespace prefix, 0 if none */

    /* Attribute offset (4 bytes) */
    uint32_t first_attr_offset;        /* Offset to first attribute, 0 if none */

};

/* Public API type - opaque pointer typedef (matches taurus.h public API) */
typedef struct taurus_element* TaurusElement;

/* ============================================================================
 * Compact Mode Helper Macros (Phase 2 Migration)
 * ============================================================================
 * These macros support the transition from pointer-based to offset-based
 * element navigation. When document->is_compact is true, offsets are used.
 */

/* Forward declaration - document structure is in taurus_internal.h */
struct taurus_document;

/* Check if element's document is in compact mode */
#define TAURUS_ELEM_IS_COMPACT(elem) \
    ((elem) && (elem)->document && (elem)->document->is_compact)

/* Get compact base pointer from element's document */
#define TAURUS_ELEM_COMPACT_BASE(elem) \
    ((elem)->document ? (elem)->document->compact_base : NULL)

/* Resolve offset to pointer (returns NULL if offset is 0) */
#define TAURUS_OFFSET_TO_PTR(base, offset) \
    ((offset) ? (void*)((char*)(base) + (offset)) : NULL)

/* Resolve node offset to pointer (for tree navigation) */
#define TAURUS_RESOLVE_NODE_OFFSET(elem, offset) \
    (TAURUS_ELEM_IS_COMPACT(elem) ? \
        (TaurusNode*)TAURUS_OFFSET_TO_PTR(TAURUS_ELEM_COMPACT_BASE(elem), (offset)) : \
        NULL)

/* Resolve element offset to pointer */
#define TAURUS_RESOLVE_ELEM_OFFSET(elem, offset) \
    (TAURUS_ELEM_IS_COMPACT(elem) ? \
        (TaurusElement)TAURUS_OFFSET_TO_PTR(TAURUS_ELEM_COMPACT_BASE(elem), (offset)) : \
        NULL)

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
    if (!elem) return NULL;

    /* COMPACT MODE: Use offset-based navigation */
    if (TAURUS_ELEM_IS_COMPACT(elem)) {
        return TAURUS_RESOLVE_ELEM_OFFSET(elem, elem->parent_offset);
    }

    /* LEGACY MODE: Use pointer-based navigation */
    return elem->parent;
}

/* ============================================================================
 * Tree Navigation Functions (optimized with inline array)
 * ============================================================================ */

/* Get first child element - uses inline array first, then falls back */
static inline TaurusElement taurus_element_get_first_child(TaurusElement elem) {
    if (!elem) return NULL;

    /* COMPACT MODE: Use offset-based navigation */
    if (TAURUS_ELEM_IS_COMPACT(elem)) {
        TaurusNode* node = TAURUS_RESOLVE_NODE_OFFSET(elem, elem->first_child_offset);
        while (node) {
            if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
                return (TaurusElement)node;
            }
            /* In compact mode, use offset for next sibling */
            /* For now, fall back to pointer-based for non-element siblings */
            node = taurus_node_get_next_sibling(node);
        }
        return NULL;
    }

    /* LEGACY MODE: Fast path - check inline array first */
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

    /* COMPACT MODE: Use offset-based navigation */
    if (TAURUS_ELEM_IS_COMPACT(elem)) {
        TaurusNode* node = TAURUS_RESOLVE_NODE_OFFSET(elem, elem->first_child_offset);
        TaurusElement last_elem = NULL;
        while (node) {
            if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
                last_elem = (TaurusElement)node;
            }
            node = taurus_node_get_next_sibling(node);
        }
        return last_elem;
    }

    /* LEGACY MODE: For ≤4 children, use inline array */
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
    if (!elem) return NULL;

    /* COMPACT MODE: Use offset-based navigation */
    if (TAURUS_ELEM_IS_COMPACT(elem)) {
        TaurusNode* next = TAURUS_RESOLVE_NODE_OFFSET(elem, elem->next_sibling_offset);
        while (next) {
            if (next->type == TAURUS_NODE_TYPE_ELEMENT) {
                return (TaurusElement)next;
            }
            /* For compact mode, use accessor for non-element siblings */
            next = taurus_node_get_next_sibling(next);
        }
        return NULL;
    }

    /* LEGACY MODE: Fast path - use linked list next_sibling pointer directly */
    TaurusNode* next = elem->next_sibling;

    /* OPTIMIZED: Inline the most common cases to avoid function call overhead
     * Most non-element siblings are text nodes (whitespace), so handle them inline.
     * This avoids calling taurus_node_get_next_sibling() in the common case. */
    while (next) {
        if (next->type == TAURUS_NODE_TYPE_ELEMENT) {
            /* PREFETCH OPTIMIZATION: Prefetch the next sibling when returning
             * This warms the cache for the subsequent iteration, improving
             * wide iteration performance (0.66x -> 0.90x+ target).
             * locality=0 (read), temporal=3 (keep in all cache levels) */
            TaurusNode* next_next = ((TaurusElement)next)->next_sibling;
            if (next_next) {
                __builtin_prefetch(next_next, 0, 3);
            }
            return (TaurusElement)next;
        }

        /* Inline next_sibling access for common node types
         * Text, CDATA, Comment all have next_sibling at offset +12 from base
         * This matches the layout in node.c and avoids the switch overhead */
        if (next->type == TAURUS_NODE_TYPE_TEXT) {
            /* Text node layout: base + content + next_sibling */
            typedef struct { TaurusNode base; char* content; void* next_sibling; } text_layout;
            next = (TaurusNode*)((text_layout*)next)->next_sibling;
        } else if (next->type == TAURUS_NODE_TYPE_CDATA ||
                   next->type == TAURUS_NODE_TYPE_COMMENT) {
            /* CDATA and Comment have same layout as Text */
            typedef struct { TaurusNode base; char* content; void* next_sibling; } text_layout;
            next = (TaurusNode*)((text_layout*)next)->next_sibling;
        } else if (next->type == TAURUS_NODE_TYPE_PI) {
            /* PI has different layout: base + target + data + next_sibling */
            typedef struct { TaurusNode base; char* target; char* data; void* next_sibling; } pi_layout;
            next = (TaurusNode*)((pi_layout*)next)->next_sibling;
        } else {
            /* Fallback for DOCTYPE and other rare types */
            next = taurus_node_get_next_sibling(next);
        }
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
