/* lib/src/dom/text.h - Text node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Text nodes contain character data.
 * CRITICAL: Content is NEVER trimmed - preserved exactly as parsed.
 */

#ifndef LEPTRIS_DOM_TEXT_H
#define LEPTRIS_DOM_TEXT_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Text node - inherits from LeptrisNode. 32 bytes (#1285 slice 4b +
 * #1320, was 40 — the content pointer became an int32 self-relative
 * byte offset and the detached-owner backref joined it; base 12 +
 * five int32s on both LP64 and ILP32).
 *
 * #1285 slice 4 invariants (unchanged):
 * - `content` is NUL-terminated at content[content_len] in ALL
 *   cases — pooled copies (leptris_text_create) and in-place runs
 *   into the doc-owned input buffer alike (every parse lane
 *   terminates the run at carve time).
 * - content_len is uint32: a single text run is capped at 4 GB
 *   (dp_text_create rejects beyond that); parse buffers that size
 *   are outside every supported target.
 *
 * #1285 slice 4b content_off encoding:
 * - 0 = NULL (zero-init pool memory reads as no content).
 * - LEPTRIS_COMPACT_INT32_EMPTY = the static "" (xsl:strip-space
 *   field writes point at a read-only literal far from any node;
 *   the shared sentinel avoids one overflow-table entry per
 *   stripped node).
 * - anything else = byte offset from the node itself; deltas beyond
 *   int32 range spill to the overflow table (compact.h machinery,
 *   same as parent/sibling edges — ASLR can place the pool and the
 *   doc-owned input buffer > 2 GB apart).
 *
 * Issue #168: parent_off mirrors next_sibling_off so the parent of a
 * text node can be queried in O(1).
 *
 * Issue #1320: owner_doc_off gives DETACHED text nodes their owning
 * document — #519 added an owner_doc field to PI/comment/CDATA but
 * skipped text (whose pool backref covered it at the time); #1285
 * slice 4 deleted that backref and detached set_content started
 * returning INVALID_ARG. Stamped by the public creator only —
 * parse-carved nodes are attached by construction. int32, so the
 * LP64 struct keeps its 40 bytes (padding absorbs it). */
typedef struct leptris_text_node {
    LeptrisNode base;                   /* MUST be first */
    int32_t content_off;              /* Self-relative offset to NUL-terminated content (0=NULL, EMPTY=static "") */
    uint32_t content_len;             /* Byte length (excl. NUL); 4 GB cap */
    /* (#450) int32 sibling edge — was cp16 (±256 KB). Text nodes
     * link to ELEMENT siblings across the parse-time element↔text
     * block gap, which scales with document size and regularly
     * exceeds cp16's range on large documents; the raw store
     * truncated and walks decoded into stale arena memory. int32
     * absorbs the struct's existing padding: sizeof is unchanged.
     * 0 = NULL. */
    int32_t next_sibling_off;
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
    int32_t owner_doc_off;            /* Byte offset to owning document (0=unstamped; #1320) */
} LeptrisTextNode;

/* Text node creation.
 *
 * Single pool-routed entry point (TODO 18 consolidated the old
 * _create / _create_fast pair).  The struct and content are allocated
 * contiguously from the pool — one bump, one cache line.
 *
 * `content_len` is required (use strlen() at boundaries that don't
 * have a length-bounded view).  Passing 0 with non-NULL content is
 * treated as an empty string.
 *
 * Ownership: pool-allocated; leptris_document_free releases via pool. */
LeptrisTextNode* leptris_text_create(const char* content,
                                    size_t content_len,
                                    LeptrisMemoryPool* pool);

/* Create a text node borrowing the caller's bytes (parse hot path).
 *
 * The node struct is pool-allocated; `content` is stored as a
 * non-owning pointer into the caller's buffer — no memcpy. CALLER
 * CONTRACT (#1285 slice 4): the run MUST already be NUL-terminated
 * at content[content_len] and must outlive the document (parse
 * lanes terminate the run in the doc-owned input copy before
 * carving the node). */
LeptrisTextNode* leptris_text_create_borrowed(const char* content,
                                             size_t content_len,
                                             LeptrisMemoryPool* pool);

void leptris_text_free(LeptrisTextNode* text);

/* Content access */
const char* leptris_text_get_content(LeptrisTextNode* text);

/* Casting helpers */
#define LEPTRIS_NODE_AS_TEXT(node) \
    (LEPTRIS_NODE_IS_TEXT(node) ? (LeptrisTextNode*)(node) : NULL)

#define LEPTRIS_TEXT_AS_NODE(text) \
    ((LeptrisNode*)(text))

/* Compact next_sibling accessors. (#450) Stored as an unscaled
 * int32 byte offset (0 = NULL) — same encoding as element sibling
 * edges. Was cp16 (±256 KB): text nodes link to element siblings
 * across the parse-time block gap, which exceeds cp16 on large
 * documents; the raw store truncated. */
static inline LeptrisNode* leptris_textnode_next_sibling(const LeptrisTextNode* t) {
    return (t)
        ? (LeptrisNode*)leptris_compact_int32_decode_inline((void*)t, t->next_sibling_off, &t->next_sibling_off)
        : NULL;
}

static inline void leptris_textnode_set_next_sibling(LeptrisTextNode* t, LeptrisNode* sibling) {
    if (!t) return;
    t->next_sibling_off = leptris_compact_int32_encode_inline(t, sibling, &t->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline LeptrisElement leptris_textnode_parent(const LeptrisTextNode* t) {
    return (t)
        ? (LeptrisElement)leptris_compact_int32_decode_inline((void*)t, t->parent_off, &t->parent_off)
        : NULL;
}

static inline void leptris_textnode_set_parent(LeptrisTextNode* t, LeptrisElement parent) {
    if (!t) return;
    t->parent_off = leptris_compact_int32_encode_inline(t, parent, &t->parent_off);
}

/* Compact content accessors (#1285 slice 4b). The content pointer
 * is a self-relative int32 byte offset — same encoding as the edges
 * above. Writers that store an empty string land on the shared
 * EMPTY sentinel; pooled and in-place runs are a small positive
 * offset from the node (the create paths allocate struct + content
 * contiguously); far targets (static literals, cross-block buffer
 * runs) spill to the overflow table. */
static inline const char* leptris_textnode_content(const LeptrisTextNode* t) {
    if (!t || t->content_off == 0) return NULL;
    if (t->content_off == LEPTRIS_COMPACT_INT32_EMPTY) return "";
    return (const char*)leptris_compact_int32_decode_inline(
        (void*)t, t->content_off, &t->content_off);
}

static inline void leptris_textnode_set_content_ptr(LeptrisTextNode* t, const char* p) {
    if (!t) return;
    if (!p) { t->content_off = 0; return; }
    if (*p == '\0') { t->content_off = LEPTRIS_COMPACT_INT32_EMPTY; return; }
    t->content_off = leptris_compact_int32_encode_inline(t, (void*)p, &t->content_off);
}

/* Owning-document access (issue #1320). The stamp is written by
 * leptris_text_node_create with the explicit-doc encoder (an
 * overflow-table entry made outside a parse context must carry the
 * REAL document tag, not the TLS current-doc). Decode is the
 * standard edge decode. */
struct leptris_document;
static inline struct leptris_document* leptris_textnode_owner_doc(const LeptrisTextNode* t) {
    return (t && t->owner_doc_off != 0)
        ? (struct leptris_document*)leptris_compact_int32_decode_inline(
              (void*)t, t->owner_doc_off, &t->owner_doc_off)
        : NULL;
}

#endif /* LEPTRIS_DOM_TEXT_H */