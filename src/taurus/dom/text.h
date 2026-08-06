/* lib/src/dom/text.h - Text node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Text nodes contain character data.
 * CRITICAL: Content is NEVER trimmed - preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_TEXT_H
#define TAURUS_DOM_TEXT_H

#include "node.h"

/* Text node - inherits from TaurusNode.
 * Phase 2c of TODO 90: next_sibling is a 4-byte offset to the next
 * sibling (byte distance from this node's address). 0 means NULL.
 *
 * TODO 115 Phase A: content_len is the byte length of `content`,
 * excluding the NUL terminator.
 *
 * TODO 115 Phase B: a "borrowed" text node stores a pointer into a
 * caller-owned buffer (typically the parser's writable input buffer)
 * instead of pool-resident storage. Its `content` is NOT NUL-terminated
 * — `content_len` is the only authoritative size. The `pool` field
 * holds the pool to use for lazy materialization when a consumer asks
 * for a NUL-terminated view via taurus_text_get_content. */
typedef struct taurus_text_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Text content - NEVER trim! */
    size_t content_len;               /* Byte length of content (excl. NUL) */
    TaurusMemoryPool* pool;           /* Pool for lazy materialization (NULL if content is NUL-term'd) */
    int borrowed;                     /* 1 = content is borrowed (non-NUL-term'd) */
    int32_t next_sibling_off;         /* Byte offset to next sibling (0=NULL) */
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

/* Compact next_sibling accessors (TODO 90 Phase 2c).
 * next_sibling is stored as a 4-byte byte-offset relative to this
 * node's own address; 0 encodes NULL. Pool-allocated text nodes and
 * their siblings live within +/-2GB of each other on any realistic
 * document. */
static inline TaurusNode* taurus_textnode_next_sibling(const TaurusTextNode* t) {
    return (t && t->next_sibling_off != 0)
        ? (TaurusNode*)((const char*)t + t->next_sibling_off)
        : NULL;
}

static inline void taurus_textnode_set_next_sibling(TaurusTextNode* t, TaurusNode* sibling) {
    if (!t) return;
    if (!sibling) { t->next_sibling_off = 0; return; }
    ptrdiff_t d = (const char*)sibling - (const char*)t;
    if (d < INT32_MIN || d > INT32_MAX) { t->next_sibling_off = 0; return; }
    t->next_sibling_off = (int32_t)d;
}

#endif /* TAURUS_DOM_TEXT_H */