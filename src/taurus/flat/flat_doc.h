/* flat/flat_doc.h — Flat document buffer (TODO 139 Phase A).
 *
 * A FlatDoc is a parallel parse representation that exists alongside
 * the compact-pointer TaurusDocument tree. It exists for one reason:
 * the parse hot path. The compact-pointer tree costs ~1.5 us per
 * element to BUILD (pool alloc + memset + compact pointer encode),
 * while pugixml's flat buffer costs ~0.1 us per element. That 15x
 * gap is the entire reason taurus is 15x slower than pugixml on
 * parse but faster on every other axis.
 *
 * The flat representation is a contiguous array of 28-byte FlatNode
 * records plus a contiguous array of 16-byte FlatAttr records. Both
 * arrays reference the input XML buffer by raw byte offset (zero
 * copy). Tree edges are array indices (uint32_t), not pointers.
 *
 * Lifecycle:
 *
 *   XML input
 *       |
 *   flat_parser  ---->  FlatDoc  (Phase B)
 *                           |
 *   (lazy, on first access) |
 *                           v
 *                      flat_promote  ---->  TaurusDocument  (Phase C/D)
 *
 * A FlatDoc owns its node/attr arrays but borrows the XML buffer.
 * The XML buffer is either:
 *   - borrowed from the caller (in-place parse path, zero copy), or
 *   - duplicated into FlatDoc::xml_buffer_owned (caller freed theirs).
 *
 * Memory footprint vs the compact tree (50 elem, 100 attr doc):
 *
 *   Representation      Per-node      Total
 *   Current compact     96 B + 88 B   13.6 KB
 *   Flat (this file)    28 B + 16 B   3.0 KB
 *   pugixml             32 B + 24 B   4.0 KB
 *
 * The flat representation is 4.5x smaller than the current compact
 * tree and smaller than pugixml — we don't store parent pointers in
 * the flat node, parent is implicit in the nesting.
 *
 * This is an INTERNAL header — not part of the public API. It lives
 * under src/taurus/flat/ and is only included by the flat parser,
 * the promote pass, and the lazy-promote shim in taurus_parse_string.
 */
#ifndef TAURUS_FLAT_FLAT_DOC_H
#define TAURUS_FLAT_FLAT_DOC_H

#include "../taurus_internal.h"

/* Node type codes. These are deliberately distinct from the public
 * TaurusNodeType enum so that FlatDoc can grow new node kinds
 * (e.g. a synthetic ROOT sentinel) without polluting the public
 * enum. The promote pass translates flat types to TaurusNodeType.
 *
 * Keep the values small — they pack into 16 bits with the name_len
 * field, so values 0..255 are safe. */
typedef enum {
    FLAT_NODE_ELEMENT = 1,
    FLAT_NODE_TEXT    = 2,
    FLAT_NODE_COMMENT = 3,
    FLAT_NODE_CDATA   = 4,
    FLAT_NODE_PI      = 5
} FlatNodeType;

/* Sentinel index meaning "no such edge". Any edge field equal to
 * FLAT_INDEX_NULL is absent. UINT32_MAX is the natural sentinel
 * because array indexing will fault loudly if we ever deref it. */
#define FLAT_INDEX_NULL ((uint32_t)0xFFFFFFFFu)

/* FlatNode — one parse node, 28 bytes when naturally packed.
 *
 * Layout is chosen so that the hot parse path (append-node + set
 * edges) writes contiguous fields:
 *
 *   bytes 0-3:   type + name_len (one 32-bit store)
 *   bytes 4-7:   name_offset
 *   bytes 8-11:  parent
 *   bytes 12-15: first_child
 *   bytes 16-19: next_sibling
 *   bytes 20-23: text_offset / text_len (for TEXT/CDATA/COMMENT/PI)
 *                OR attr_start (for ELEMENT)
 *   bytes 24-27: text_len / attr_count (overloaded, see below)
 *
 * The byte-20..27 fields are overloaded by node type so the struct
 * stays at 28 bytes for all node kinds:
 *
 *   ELEMENT:  attr_start (uint32) + attr_count (uint16) + depth (uint16)
 *   TEXT/CDATA/COMMENT: text_offset (uint32) + text_len (uint32)
 *   PI:       target_offset (uint32) + data_offset (uint32)
 *            (target_len comes from the data side via convention;
 *             PI is rare, so we accept the small wart.)
 *
 * For non-element nodes, first_child is always FLAT_INDEX_NULL.
 */
typedef struct flat_node {
    uint16_t type;              /* FlatNodeType */
    uint16_t name_len;          /* Length of name (0 for non-element) */
    uint32_t name_offset;       /* Byte offset into xml_buffer */

    uint32_t parent;            /* Index of parent in nodes[] (or FLAT_INDEX_NULL for root) */
    uint32_t first_child;       /* Index of first child (or FLAT_INDEX_NULL) */
    uint32_t next_sibling;      /* Index of next sibling (or FLAT_INDEX_NULL) */

    /* Element-only fields. For non-element nodes, attr_start holds
     * text_offset and attr_count + depth together hold text_len. */
    uint32_t attr_start;        /* Index into attrs[] (or FLAT_INDEX_NULL) */
    uint16_t attr_count;        /* Number of attributes on this element */
    uint16_t depth;             /* Nesting depth (0 = root element) */

    /* Source line (1-based, 0 = unknown). Issue #223: tracked by
     * flat_parser and copied to TaurusNode.base.line by flat_promote
     * so taurus_node_line returns the right value on the entity/DTD
     * fallback path. */
    uint32_t line;
} FlatNode;

/* Static size guarantee — the promote pass and the parser both rely
 * on the exact 32-byte layout for memcpy-friendly traversal. */
/* cppcheck-suppress unusedFunction */
#ifdef __cplusplus
static_assert(sizeof(FlatNode) == 32, "FlatNode must be 32 bytes");
#else
_Static_assert(sizeof(FlatNode) == 32, "FlatNode must be 32 bytes");
#endif

/* FlatAttr — one attribute, 12 bytes.
 *
 * Attributes for an element are stored contiguously in attrs[],
 * starting at the owning element's attr_start and spanning
 * attr_count entries. The parser appends attrs in source order;
 * the promote pass preserves that order in the compact tree.
 *
 * The natural layout is 12 bytes (no padding). Earlier drafts
 * targeted 16 bytes for spare flag bits; we kept the smaller
 * layout because attribute density directly affects cache
 * locality in the promote pass, and we have no concrete need
 * for per-attr flags today.
 */
typedef struct flat_attr {
    uint32_t name_offset;       /* Byte offset into xml_buffer */
    uint32_t value_offset;      /* Byte offset into xml_buffer */
    uint16_t name_len;
    uint16_t value_len;
} FlatAttr;

#ifdef __cplusplus
static_assert(sizeof(FlatAttr) == 12, "FlatAttr must be 12 bytes");
#else
_Static_assert(sizeof(FlatAttr) == 12, "FlatAttr must be 12 bytes");
#endif

/* FlatDoc — the flat document. Owns node/attr arrays. Borrows or
 * owns the XML buffer (see xml_buffer_owned). */
typedef struct flat_doc {
    FlatNode* nodes;            /* Contiguous array, count entries */
    size_t    node_count;
    size_t    node_capacity;

    FlatAttr* attrs;            /* Contiguous array, count entries */
    size_t    attr_count;
    size_t    attr_capacity;

    /* XML buffer reference. Either:
     *   xml_buffer != NULL && xml_buffer_owned == NULL:
     *       borrowed from caller, caller owns the lifetime.
     *   xml_buffer == NULL && xml_buffer_owned != NULL:
     *       we duplicated it; we free on flat_doc_free.
     * Exactly one of the two is non-NULL after a successful parse. */
    const char* xml_buffer;         /* Borrowed reference (do not free) */
    char*       xml_buffer_owned;   /* Owned copy (free on destroy) */
    size_t      xml_len;

    /* XML declaration. Offsets point into xml_buffer/xml_buffer_owned.
     * NULL/0 if absent. */
    uint32_t version_offset;
    uint16_t version_len;
    uint32_t encoding_offset;
    uint16_t encoding_len;
    int      standalone;        /* -1=not set, 0=no, 1=yes */

    /* Index of the document element (root element). FLAT_INDEX_NULL
     * if the document is empty. Set by the parser when it sees the
     * first top-level element. */
    uint32_t root_index;
} FlatDoc;

/* ============================================================================
 * Allocator API
 *
 * All functions are thread-safe in the sense that they take no locks
 * but also don't touch global state. A FlatDoc is owned by exactly
 * one thread.
 * ============================================================================ */

/* Create a FlatDoc with capacity pre-sized for the given XML length.
 *
 * Heuristic: ~1 node per 100 bytes of XML, ~2 attrs per node. This
 * avoids the realloc growth churn during parse. The arrays grow
 * geometrically if the heuristic underestimates.
 *
 * The xml_buffer is borrowed (not copied). Callers that need the
 * FlatDoc to outlive the input buffer must call flat_doc_dup_xml
 * after creation.
 *
 * Returns NULL on allocation failure. */
FlatDoc* flat_doc_new(const char* xml_buffer, size_t xml_len);

/* Free a FlatDoc and its arrays. Safe to call on NULL. */
void flat_doc_free(FlatDoc* doc);

/* Append a node and return its index. Fields other than type /
 * name_offset / name_len are initialized to "no edge" sentinels
 * (parent = first_child = next_sibling = FLAT_INDEX_NULL,
 * attr_start = FLAT_INDEX_NULL, attr_count = 0, depth = 0).
 *
 * Returns FLAT_INDEX_NULL on allocation failure. */
uint32_t flat_doc_append_node(FlatDoc* doc, FlatNodeType type,
                              uint32_t name_offset, uint16_t name_len);

/* Append an attribute and return its index. Attributes must be
 * appended in the order they appear on each element; the owning
 * element records the (attr_start, attr_count) range after all
 * its attributes have been appended.
 *
 * Returns FLAT_INDEX_NULL on allocation failure. */
uint32_t flat_doc_append_attr(FlatDoc* doc,
                              uint32_t name_offset, uint16_t name_len,
                              uint32_t value_offset, uint16_t value_len);

/* Take ownership of a private copy of the XML buffer. Call this if
 * the FlatDoc needs to outlive the caller's input buffer.
 *
 * No-op if already owned. Returns 0 on success, -1 on alloc failure
 * (in which case the FlatDoc is unchanged and still borrows). */
int flat_doc_dup_xml(FlatDoc* doc);

/* Grow-only accessors — used by the parser to set edges after a node
 * is appended. These are intentionally inlined: the parser calls
 * them in tight loops during build. */
static inline void flat_node_set_parent(FlatNode* n, uint32_t idx) {
    n->parent = idx;
}
static inline void flat_node_set_first_child(FlatNode* n, uint32_t idx) {
    n->first_child = idx;
}
static inline void flat_node_set_next_sibling(FlatNode* n, uint32_t idx) {
    n->next_sibling = idx;
}
static inline void flat_node_set_attrs(FlatNode* n, uint32_t start, uint16_t count) {
    n->attr_start = start;
    n->attr_count = count;
}
static inline void flat_node_set_depth(FlatNode* n, uint16_t depth) {
    n->depth = depth;
}

/* Text-node payload accessors. The byte-20..27 fields are overloaded
 * by node type; these helpers hide the overload so callers don't
 * have to remember the bit layout. */
static inline void flat_node_set_text(FlatNode* n,
                                       uint32_t text_offset, uint32_t text_len) {
    n->attr_start = text_offset;
    n->attr_count = (uint16_t)(text_len & 0xFFFFu);
    n->depth      = (uint16_t)(text_len >> 16);
}
static inline uint32_t flat_node_text_offset(const FlatNode* n) {
    return n->attr_start;
}
static inline uint32_t flat_node_text_len(const FlatNode* n) {
    return ((uint32_t)n->depth << 16) | (uint32_t)n->attr_count;
}

/* PI payload: target_offset lives in name_offset/name_len (PI has no
 * "name" per XML — we treat the target as the name). data lives in
 * the text slot. */
static inline void flat_node_set_pi_data(FlatNode* n, uint32_t data_offset, uint32_t data_len) {
    flat_node_set_text(n, data_offset, data_len);
}
static inline uint32_t flat_node_pi_data_offset(const FlatNode* n) {
    return flat_node_text_offset(n);
}
static inline uint32_t flat_node_pi_data_len(const FlatNode* n) {
    return flat_node_text_len(n);
}

#endif /* TAURUS_FLAT_FLAT_DOC_H */
