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

/* Text node - inherits from LeptrisNode. 48 bytes (#1285 slice 4,
 * was 64 — the pool/borrowed fields are gone because content is
 * ALWAYS NUL-terminated: every producer terminates in place, so
 * leptris_text_get_content is a plain field read).
 *
 * #1285 slice 4 invariants:
 * - `content` is NUL-terminated at content[content_len] in ALL
 *   cases — pooled copies (leptris_text_create) and in-place runs
 *   into the doc-owned input buffer alike (every parse lane
 *   terminates the run at carve time).
 * - content_len is uint32: a single text run is capped at 4 GB
 *   (dp_text_create rejects beyond that); parse buffers that size
 *   are outside every supported target.
 *
 * Issue #168: parent_off mirrors next_sibling_off so the parent of a
 * text node can be queried in O(1). */
typedef struct leptris_text_node {
    LeptrisNode base;                   /* MUST be first */
    char* content;                    /* Text content - NEVER trim! NUL-terminated at [content_len] */
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
void leptris_text_set_content(LeptrisTextNode* text, const char* content);

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

#endif /* LEPTRIS_DOM_TEXT_H */