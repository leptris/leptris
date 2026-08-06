/* lib/src/dom/comment.h - Comment node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comment nodes contain <!-- comment --> content.
 * Content is preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_COMMENT_H
#define TAURUS_DOM_COMMENT_H

#include "node.h"
#include "compact.h"  /* int32 compact-pointer helpers (TODO 121) */

/* Comment node - inherits from TaurusNode.
 * Phase 2c of TODO 90: next_sibling is a 4-byte offset (0=NULL). */
typedef struct taurus_comment_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Comment content */
    int32_t next_sibling_off;         /* Byte offset to next sibling (0=NULL) */
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

/* Compact next_sibling accessors (TODO 90 Phase 2c). */
static inline TaurusNode* taurus_comment_next_sibling(const TaurusCommentNode* c) {
    return (c)
        ? (TaurusNode*)taurus_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void taurus_comment_set_next_sibling(TaurusCommentNode* c, TaurusNode* sibling) {
    if (!c) return;
    c->next_sibling_off = taurus_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

#endif /* TAURUS_DOM_COMMENT_H */