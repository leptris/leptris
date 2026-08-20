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

/* Forward decl: TaurusMemoryPool is defined in memory/pool.h. */
struct taurus_memory_pool;

/* Attribute structure - inline storage for minimal memory footprint
 *
 * Instead of storing pointers to attribute structures, we store attributes
 * as a linked list with inline StringView storage. This eliminates pointer
 * indirection and improves cache locality.
 *
 * Size: 72 bytes (was 112 before TODO 173).
 *
 * TODO 173: prefix / namespace_uri (views + pointers) moved into a side
 * cache struct (taurus_attr_ns_cache) allocated only when actually needed.
 * The common case (no namespace activity) has ns_cache == NULL — saves
 * 48 bytes per attribute. For 100,000 attrs that's 4.8 MB less memory
 * pressure and better cache locality. */
struct taurus_attr_ns_cache {
    TaurusStringView prefix_view;
    TaurusStringView namespace_uri_view;
    char* prefix;                /* NULL until first access */
    char* namespace_uri;         /* NULL until first access */
};

struct taurus_attribute {
    /* SINGLE string representation (TODO 184 round 4): the views.
     * - Parse path: zero-copy into the document buffer (NUL-
     *   terminated in place by the parser; document-lifetime).
     * - Mutation API / entity expansion: view.data is an OWNED
     *   pool/heap copy, NUL-terminated, length set. Owned copies
     *   REPLACE the view because callers' strings may be temporary
     *   and the raw entity text is never needed post-expansion.
     * 40 bytes: 16 + 16 + 8-byte packed tail (round 19). */

    TaurusStringView name_view;
    TaurusStringView value_view;

    /* Side cache for namespace activity. 0 when the attr has no
     * prefix and no namespace_uri (the common case). Points via
     * self-relative offset at a pool-allocated struct; use
     * attr_get_ns_cache()/attr_set_ns_cache(). TODO 173.
     * Round 19: was a raw pointer — now an int32 offset so the
     * struct fits 40 bytes (compact.c overflow-table fallback
     * covers >2GB spans). */
    int32_t ns_cache_off;

    /* Next attribute in linked list. TODO 183 Phase 5 (TODO 181
     * Phase D): cp16 compact pointer — the attr `next` edge only
     * connects attrs of the SAME element, which are allocated as
     * adjacent slots in the parser's attr_block (contiguous by
     * construction since TODO 183 Phase 3; distance ≤ K × sizeof,
     * ~3 KB at K=100 — always within cp16's ±256 KB). Mutation-
     * created attrs (different region) go through the encoder's
     * overflow-table path. 0 = NULL end-of-list. */
    int16_t next_cp;

    /* Bits 0-14: 15-bit FNV-1a of the name, LAZY (0 = not yet
     * computed; the first read via attr_name_hash() computes and
     * caches; a real hash is never stored as 0 — the compute maps
     * 0 to 1). Bit 15: has_entities (1 if value_view contains '&',
     * 0 otherwise; set during parsing, cleared after eager
     * expansion). The parse path skips the hash entirely (saves
     * ~5ns per attr on attr-heavy inputs); query paths pay it on
     * first access, then cached. The hash is a pre-filter only —
     * every hash hit is confirmed by memcmp — so 15 bits suffice.
     * TODO 172.
     *
     * Layout note: ns_cache_off + name_hash + next_cp pack into one
     * 8-byte tail — sizeof is 40 (was 48, 64 with separate char*
     * fields, 72 with a raw next pointer). */
    uint16_t name_hash;

    /* MEASURED LAYOUT DECISION (TODO 186, revises TODO 184 round 4;
     * round 19 revises again): sizeof was 48 through v0.26.x. 40 B
     * (ns_cache as int32 offset + 15-bit hash + entity flag in the
     * tail) reaches pugixml attr density (1.6/line vs 1.33). The
     * 56 B point measured dead and the 32 B split-stream upper
     * bound dead — 40 was the last unmeasured point on the axis. */
};

/* Size pin lives with the TAURUS_STATIC_ASSERT macro below (C++-
 * compatible), next to the element size pin. */

/* C-string accessors (TODO 184 rounds 3–4). The views are the
 * single representation; their data is always NUL-terminated
 * (parse path: in the document buffer; mutation/expansion: owned
 * copy). attr_cvalue does NOT expand entities — callers needing
 * expansion use taurus_element_attribute(), which materializes
 * lazily. */
static inline const char* attr_cname(const struct taurus_attribute* a) {
    return a->name_view.data ? a->name_view.data : "";
}

static inline const char* attr_cvalue(const struct taurus_attribute* a) {
    return a->value_view.data ? a->value_view.data : "";
}

/* Attr list-edge accessors (TODO 183 Phase 5). next_cp stores the
 * byte offset to the next attr scaled by 8 (align_log2=3); attrs are
 * 8-byte aligned pool/arena residents. The encoder falls back to the
 * per-field overflow table when the pair spans regions (mutation-
 * created attrs). */
static inline struct taurus_attribute* taurus_attr_next(
    const struct taurus_attribute* a) {
    return (a)
        ? (struct taurus_attribute*)taurus_compact_ptr16_decode(
              (const void*)a, a->next_cp, 3, &a->next_cp)
        : NULL;
}

static inline void taurus_attr_set_next(struct taurus_attribute* a,
                                         struct taurus_attribute* next) {
    if (!a) return;
    a->next_cp = taurus_compact_ptr16_encode(a, next, 3, &a->next_cp);
}

/* 15-bit FNV-1a of a name (round 19). Single source of truth for
 * both the lazy attr accessor and query-side pre-filters — both
 * sides must truncate identically or hash compares silently
 * mismatch. Truncation: keep bits 16-30 of the 32-bit FNV (the
 * low bits sit under the multiply's carry chain; the upper half
 * mixes better). Returns nonzero: 0 maps to 1 so the "uncomputed"
 * sentinel stays unambiguous. */
static inline uint16_t attr_hash15(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    uint16_t h15 = (uint16_t)((h >> 16) & 0x7FFFu);
    return h15 ? h15 : 1;
}

/* Entity flag (round 19): name_hash bit 15. */
static inline int attr_has_entities(const struct taurus_attribute* a) {
    return (a->name_hash & 0x8000u) != 0;
}
static inline void attr_set_entities(struct taurus_attribute* a, int v) {
    if (v) a->name_hash |= 0x8000u;
    else   a->name_hash &= 0x7FFFu;
}

/* Lazy 15-bit hash accessor (TODO 172). Computes and caches on first
 * call, preserving the entity flag in bit 15. Thread-unsafe in the
 * strict sense (racy writes), but the worst case is two threads
 * both writing the same value (idempotent). Documents are
 * single-threaded by contract. */
static inline uint16_t attr_name_hash(struct taurus_attribute* a) {
    if ((a->name_hash & 0x7FFFu) == 0) {
        a->name_hash = (uint16_t)((a->name_hash & 0x8000u) |
            attr_hash15(a->name_view.data, a->name_view.length));
    }
    return (uint16_t)(a->name_hash & 0x7FFFu);
}

/* Attribute namespace-cache accessors (TODO 173). The prefix and
 * namespace_uri (both view and cstr form) live in a side cache struct
 * that's only allocated when one of them is set. The common case (attr
 * without prefix, no namespace_uri) has ns_cache_off == 0.
 *
 * Readers use these helpers — they return NULL / empty when no cache.
 * Round 19: the cache is reached via a self-relative int32 offset
 * (0 = none; overflow-table fallback for >2GB spans). */
static inline struct taurus_attr_ns_cache* attr_get_ns_cache(
    struct taurus_attribute* a) {
    return (struct taurus_attr_ns_cache*)taurus_compact_int32_decode(
        a, a->ns_cache_off, &a->ns_cache_off);
}
static inline void attr_set_ns_cache(struct taurus_attribute* a,
                                     struct taurus_attr_ns_cache* ns) {
    a->ns_cache_off = taurus_compact_int32_encode(a, ns, &a->ns_cache_off);
}
static inline const char* attr_get_prefix(struct taurus_attribute* a) {
    struct taurus_attr_ns_cache* c = attr_get_ns_cache(a);
    return c ? c->prefix : NULL;
}
static inline const char* attr_get_namespace_uri(struct taurus_attribute* a) {
    struct taurus_attr_ns_cache* c = attr_get_ns_cache(a);
    return c ? c->namespace_uri : NULL;
}
static inline TaurusStringView attr_get_prefix_view(struct taurus_attribute* a) {
    struct taurus_attr_ns_cache* c = attr_get_ns_cache(a);
    return c ? c->prefix_view : taurus_sv_empty();
}
static inline TaurusStringView attr_get_namespace_uri_view(struct taurus_attribute* a) {
    struct taurus_attr_ns_cache* c = attr_get_ns_cache(a);
    return c ? c->namespace_uri_view : taurus_sv_empty();
}

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
/* Phase 2e-B + TODO 155 Phase B: ns_cache holds BOTH the element's
 * own prefix/URI AND the linked list of xmlns:* declarations parsed
 * on this element. Merging the parallel `namespaces` head pointer
 * into ns_cache saves 8 bytes per element (88 → 80).
 *
 * Most elements have no namespace activity → ns_cache is NULL,
 * zero overhead. Elements that declare namespaces OR have a prefix
 * pay one 16-byte pool allocation for the cache struct. */
struct taurus_ns_cache {
    char* prefix;                       /* This element's prefix (from `<p:loc>`) */
    char* namespace_uri;                /* Resolved URI for this element's prefix */
    struct taurus_namespace* declarations;  /* xmlns:* declared on this element */
};

struct taurus_element {
    /* Compact base node (12 bytes) - MUST be first for casting. */
    TaurusNode base;

    /* Packed header + counts + name_hash (7 bytes, fills the 8-byte
     * tail of base's alignment slot). The name_hash is a 16-bit FNV-1a
     * of the element's local name, computed at parse time. Used by
     * XPath child-axis lookups and close-tag comparison to reject
     * non-matching names via 2-byte hash compare before strcmp.
     * Phase 2a of TODO 90 + TODO 159 fast-child-lookup. */
    TaurusCompactHeader header;        /* 2 bytes */
    uint16_t name_hash;               /* 2 bytes — FNV-1a of local name */
    uint8_t attr_count;                /* 1 byte */
    /* Local-name byte length, 0xFF = name longer than 254 bytes
     * (callers fall back to strlen). Fills the last padding byte
     * of this packed header — sizeof stays 64. Kills the per-element
     * strlen in the serializer (8% of text-heavy serialize) and the
     * close-tag comparison in the parser. */
    uint8_t name_len;                  /* 1 byte */
    uint16_t child_count;              /* 2 bytes */

    /* Cached NULL-terminated strings (16 bytes).
     * Phase 2e-B: prefix + namespace_uri merged into ns_cache (8 bytes
     * instead of 16). name stays inline — it's accessed on every
     * serialize/XPath hit. */
    char* name;                        /* NULL until first access */
    struct taurus_ns_cache* ns_cache;  /* NULL for non-namespaced elements */

    /* Tree edges (12 bytes; was 16 before TODO 155 Phase C).
     * last_child_off is GONE — append operations walk the child list
     * (O(child_count)) or use the parser-local cache during parse.
     * Saves 4 bytes per element. */
    int32_t parent_off;
    int32_t first_child_off;
    int32_t next_sibling_off;

    /* Attribute list (4 bytes; was 8 before TODO 155 Phase C).
     * last_attribute_off is GONE — same reasoning. Append walks the
     * list. Saves 4 bytes per element. */
    int32_t first_attribute_off;

    /* TODO 155 Phase A: `document` field is GONE — element now fits
     * one 64-byte cache line. Non-root elements reach their document
     * via taurus_element_get_document() in dom/root_doc_map.h. */
};

struct taurus_document* taurus_element_get_document(TaurusElement elem);
TaurusMemoryPool* taurus_element_get_pool(TaurusElement elem);

/* Compute 16-bit FNV-1a hash of an element name string. Used
 * together with elem->name_hash for fast pre-filtering in child-
 * axis lookups. TODO 159: fast child-name matching. */
static inline uint16_t taurus_name_hash_compute(const char* name) {
    uint16_t h = 0x811C;
    for (const char* c = name; *c; c++) {
        h ^= (unsigned char)*c;
        h *= 0x0193;
    }
    return h;
}

/* Fast element-name equality check: compare 2-byte hash first,
 * fall back to strcmp only on hash match. Rejects non-matching
 * names in ~1ns vs ~5ns for strcmp on short names. */
static inline int taurus_elem_name_is(TaurusElement e, const char* name,
                                       uint16_t target_hash) {
    if (!e || !e->name || e->name_hash != target_hash) return 0;
    return strcmp(e->name, name) == 0;
}

/* Inline accessors — use these instead of direct field access. */
static inline char* taurus_elem_prefix(const TaurusElement e) {
    return e && e->ns_cache ? e->ns_cache->prefix : NULL;
}
static inline char* taurus_elem_ns_uri(const TaurusElement e) {
    return e && e->ns_cache ? e->ns_cache->namespace_uri : NULL;
}

/* Allocate ns_cache if needed, then set prefix. Pool required for
 * the one-time allocation. The cache struct is zeroed so all three
 * fields (prefix, namespace_uri, declarations) start as NULL. */
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
        e->ns_cache->declarations = NULL;
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
        e->ns_cache->namespace_uri = NULL;
        e->ns_cache->declarations = NULL;
    }
    e->ns_cache->namespace_uri = uri;
}

/* Get the linked list of xmlns:* declarations on this element.
 * Returns NULL when the element has no namespace declarations
 * (the overwhelmingly common case). TODO 155 Phase B. */
static inline struct taurus_namespace* taurus_elem_namespaces(const TaurusElement e) {
    return e && e->ns_cache ? e->ns_cache->declarations : NULL;
}

/* Ensure ns_cache exists (allocating if needed) and return a
 * writable pointer to the declarations head. Used by mutation
 * paths that append xmlns:* declarations. */
static inline struct taurus_namespace** taurus_elem_namespaces_ptr(
    TaurusElement e, TaurusMemoryPool* pool) {
    if (!e) return NULL;
    if (!e->ns_cache) {
        if (!pool) return NULL;
        e->ns_cache = (struct taurus_ns_cache*)
            taurus_pool_alloc(pool, sizeof(struct taurus_ns_cache));
        if (!e->ns_cache) return NULL;
        e->ns_cache->prefix = NULL;
        e->ns_cache->namespace_uri = NULL;
        e->ns_cache->declarations = NULL;
    }
    return &e->ns_cache->declarations;
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
 *   - 8 bytes binding_wrapper (TaurusNode base, #262 FFI cache)
 *
 * pugixml compact node: 44 bytes. The binding_wrapper field adds
 * ~10% memory but eliminates per-node FFI call overhead for
 * language bindings (7× nodeset XPath speedup at Ruby level). */
#ifndef TAURUS_STATIC_ASSERT
#  ifdef __cplusplus
#    define TAURUS_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#  elif defined(_MSC_VER)
#    define TAURUS_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#  else
#    define TAURUS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#  endif
#endif

TAURUS_STATIC_ASSERT(sizeof(struct taurus_element) == 64,
    "taurus_element must fit one cache line (TODO 155 Phase A)");

TAURUS_STATIC_ASSERT(sizeof(struct taurus_attribute) == 40,
    "round 19 attr layout: 16+16+4+2+2 = 40");

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
    /* TODO 155 Phase C: last_child_off removed. Walk the child list
     * to find the tail. O(child_count); the typical element has few
     * children, and cold paths (mutation, traversal) tolerable.
     * Parse path uses a parser-local cache (DParser.last_child_of_depth)
     * to avoid this walk during the hot parse loop. */
    if (!e) return NULL;
    TaurusNode* c = taurus_elem_first_child(e);
    if (!c) return NULL;
    while (1) {
        TaurusNode* next = taurus_node_get_next_sibling(c);
        if (!next) return c;
        c = next;
    }
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
    /* TODO 155 Phase C: last_child_off removed. This setter is now a
     * no-op retained for ABI compatibility. Callers that need to
     * append a child use taurus_element_append_child_internal, which
     * walks the child list (or uses the parser-local cache). */
    (void)e; (void)child;
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
    /* TODO 155 Phase C: last_attribute_off removed. Walk the attr
     * list to find the tail. O(attr_count) — typically ≤ 10. */
    if (!e) return NULL;
    struct taurus_attribute* a = taurus_elem_first_attribute(e);
    if (!a) return NULL;
    while (taurus_attr_next(a)) a = taurus_attr_next(a);
    return a;
}

static inline void taurus_elem_set_first_attribute(TaurusElement e, struct taurus_attribute* attr) {
    if (!e) return;
    e->first_attribute_off = taurus_compact_int32_encode(e, attr, &e->first_attribute_off);
}

static inline void taurus_elem_set_last_attribute(TaurusElement e, struct taurus_attribute* attr) {
    /* TODO 155 Phase C: last_attribute_off removed. No-op retained
     * for ABI compatibility. Callers should use the append helpers
     * (taurus_elem_last_attribute walks the list to find the tail). */
    (void)e; (void)attr;
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
TAURUS_API size_t taurus_element_attribute_count(TaurusElement elem);

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
TAURUS_API size_t taurus_element_child_count(TaurusElement elem);

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
void taurus_element_append_child_internal_doc(TaurusElement elem, TaurusNode* child,
                                              struct taurus_document* doc);
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
