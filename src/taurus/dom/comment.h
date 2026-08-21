/* lib/src/dom/comment.h - Comment node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comment nodes contain <!-- comment --> content.
 * Content is preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_COMMENT_H
#define TAURUS_DOM_COMMENT_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Comment node - inherits from TaurusNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off so the parent of a
 * non-element node can be queried in O(1). */
typedef struct taurus_comment_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Comment content */
    int32_t next_sibling_off;         /* (#450) unscaled int32 sibling edge — cp16's
                                     ±256 KB range cannot hold cross-block sibling
                                     links on large documents. 0 = NULL. */
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
} TaurusCommentNode;

/* Comment node creation.  Pool-allocated with contiguous content
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
TaurusCommentNode* taurus_comment_create(const char* content,
                                          size_t content_len,
                                          struct taurus_memory_pool* pool);
void taurus_comment_free(TaurusCommentNode* comment);

/* Content access */
const char* taurus_comment_get_content(TaurusCommentNode* comment);

/* Casting helpers */
#define TAURUS_NODE_AS_COMMENT(node) \
    (TAURUS_NODE_IS_COMMENT(node) ? (TaurusCommentNode*)(node) : NULL)

#define TAURUS_COMMENT_AS_NODE(comment) \
    ((TaurusNode*)(comment))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline TaurusNode* taurus_comment_next_sibling(const TaurusCommentNode* c) {
    return (c)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void taurus_comment_set_next_sibling(TaurusCommentNode* c, TaurusNode* sibling) {
    if (!c) return;
    c->next_sibling_off = taurus_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline TaurusElement taurus_comment_parent(const TaurusCommentNode* c) {
    return (c)
        ? (TaurusElement)taurus_compact_int32_decode((void*)c, c->parent_off, &c->parent_off)
        : NULL;
}

static inline void taurus_comment_set_parent(TaurusCommentNode* c, TaurusElement parent) {
    if (!c) return;
    c->parent_off = taurus_compact_int32_encode(c, parent, &c->parent_off);
}

#endif /* TAURUS_DOM_COMMENT_H */