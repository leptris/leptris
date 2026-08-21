/* lib/src/dom/text.h - Text node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Text nodes contain character data.
 * CRITICAL: Content is NEVER trimmed - preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_TEXT_H
#define TAURUS_DOM_TEXT_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Text node - inherits from TaurusNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16,
 * scaled by 8). Covers ±256 KB — never overflows for realistic docs.
 *
 * TODO 115 Phase A: content_len is the byte length of `content`,
 * excluding the NUL terminator.
 *
 * TODO 115 Phase B: a "borrowed" text node stores a pointer into a
 * caller-owned buffer (typically the parser's writable input buffer)
 * instead of pool-resident storage. Its `content` is NOT NUL-terminated
 * — `content_len` is the only authoritative size. The `pool` field
 * holds the pool to use for lazy materialization when a consumer asks
 * for a NUL-terminated view via taurus_text_get_content.
 *
 * Issue #168: parent_off mirrors next_sibling_off so the parent of a
 * text node can be queried in O(1). */
typedef struct taurus_text_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Text content - NEVER trim! */
    size_t content_len;               /* Byte length of content (excl. NUL) */
    TaurusMemoryPool* pool;           /* Pool for lazy materialization (NULL if content is NUL-term'd) */
    int borrowed;                     /* 1 = content is borrowed (non-NUL-term'd) */
    /* (#450) int32 sibling edge — was cp16 (±256 KB). Text nodes
     * link to ELEMENT siblings across the parse-time element↔text
     * block gap, which scales with document size and regularly
     * exceeds cp16's range on large documents; the raw store
     * truncated and walks decoded into stale arena memory. int32
     * absorbs the struct's existing padding: sizeof is unchanged.
     * 0 = NULL. */
    int32_t next_sibling_off;
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
} TaurusTextNode;

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
 * Ownership: pool-allocated; taurus_document_free releases via pool. */
TaurusTextNode* taurus_text_create(const char* content,
                                    size_t content_len,
                                    TaurusMemoryPool* pool);

/* Create a borrowed text node (TODO 115 Phase B).
 *
 * Stores `content` as a non-owning pointer into the caller's buffer.
 * Content is NOT NUL-terminated; `content_len` is authoritative.
 *
 * The pool is stored on the node so that taurus_text_get_content can
 * lazily allocate a NUL-terminated copy on demand. Callers must ensure
 * `content` outlives the document (e.g. point into `doc->xml_buffer`).
 *
 * Use when the source bytes already live in a long-lived buffer and
 * the per-node pool allocation + memcpy that taurus_text_create would
 * do is worth avoiding on the parse hot path. */
TaurusTextNode* taurus_text_create_borrowed(const char* content,
                                             size_t content_len,
                                             TaurusMemoryPool* pool);

void taurus_text_free(TaurusTextNode* text);

/* Content access */
const char* taurus_text_get_content(TaurusTextNode* text);
void taurus_text_set_content(TaurusTextNode* text, const char* content);

/* Casting helpers */
#define TAURUS_NODE_AS_TEXT(node) \
    (TAURUS_NODE_IS_TEXT(node) ? (TaurusTextNode*)(node) : NULL)

#define TAURUS_TEXT_AS_NODE(text) \
    ((TaurusNode*)(text))

/* Compact next_sibling accessors. (#450) Stored as an unscaled
 * int32 byte offset (0 = NULL) — same encoding as element sibling
 * edges. Was cp16 (±256 KB): text nodes link to element siblings
 * across the parse-time block gap, which exceeds cp16 on large
 * documents; the raw store truncated. */
static inline TaurusNode* taurus_textnode_next_sibling(const TaurusTextNode* t) {
    return (t)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)t, t->next_sibling_off, &t->next_sibling_off)
        : NULL;
}

static inline void taurus_textnode_set_next_sibling(TaurusTextNode* t, TaurusNode* sibling) {
    if (!t) return;
    t->next_sibling_off = taurus_compact_int32_encode(t, sibling, &t->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline TaurusElement taurus_textnode_parent(const TaurusTextNode* t) {
    return (t)
        ? (TaurusElement)taurus_compact_int32_decode((void*)t, t->parent_off, &t->parent_off)
        : NULL;
}

static inline void taurus_textnode_set_parent(TaurusTextNode* t, TaurusElement parent) {
    if (!t) return;
    t->parent_off = taurus_compact_int32_encode(t, parent, &t->parent_off);
}

#endif /* TAURUS_DOM_TEXT_H */