/* lib/src/dom/comment.h - Comment node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comment nodes contain <!-- comment --> content.
 * Content is preserved exactly as parsed.
 */

#ifndef LEPTRIS_DOM_COMMENT_H
#define LEPTRIS_DOM_COMMENT_H

#include "node.h"
#include "compact.h"  /* compact-pointer helpers (TODO 121, TODO 178) */

/* Comment node - inherits from LeptrisNode.
 * TODO 179 Phase B: next_sibling is a 2-byte compact pointer (cp16).
 * Issue #168: parent_off mirrors next_sibling_off so the parent of a
 * non-element node can be queried in O(1). */
typedef struct leptris_comment_node {
    LeptrisNode base;                   /* MUST be first */
    char* content;                    /* Comment content */
    int32_t next_sibling_off;         /* (#450) unscaled int32 sibling edge — cp16's
                                     ±256 KB range cannot hold cross-block sibling
                                     links on large documents. 0 = NULL. */
    int32_t parent_off;               /* Byte offset to parent element (0=NULL) */
} LeptrisCommentNode;

/* Comment node creation.  Pool-allocated with contiguous content
 * storage (TODO 18 + TODO 26: single entry point, no _fast variant). */
LeptrisCommentNode* leptris_comment_create(const char* content,
                                          size_t content_len,
                                          struct leptris_memory_pool* pool);
void leptris_comment_free(LeptrisCommentNode* comment);

/* Content access */
const char* leptris_comment_get_content(LeptrisCommentNode* comment);

/* Casting helpers */
#define LEPTRIS_NODE_AS_COMMENT(node) \
    (LEPTRIS_NODE_IS_COMMENT(node) ? (LeptrisCommentNode*)(node) : NULL)

#define LEPTRIS_COMMENT_AS_NODE(comment) \
    ((LeptrisNode*)(comment))

/* Compact next_sibling accessors (TODO 179 Phase B — cp16). */
static inline LeptrisNode* leptris_comment_next_sibling(const LeptrisCommentNode* c) {
    return (c)
        ? (LeptrisNode*)leptris_compact_int32_decode((void*)c, c->next_sibling_off, &c->next_sibling_off)
        : NULL;
}

static inline void leptris_comment_set_next_sibling(LeptrisCommentNode* c, LeptrisNode* sibling) {
    if (!c) return;
    c->next_sibling_off = leptris_compact_int32_encode(c, sibling, &c->next_sibling_off);
}

/* Compact parent accessors (issue #168). */
static inline LeptrisElement leptris_comment_parent(const LeptrisCommentNode* c) {
    return (c)
        ? (LeptrisElement)leptris_compact_int32_decode((void*)c, c->parent_off, &c->parent_off)
        : NULL;
}

static inline void leptris_comment_set_parent(LeptrisCommentNode* c, LeptrisElement parent) {
    if (!c) return;
    c->parent_off = leptris_compact_int32_encode(c, parent, &c->parent_off);
}

#endif /* LEPTRIS_DOM_COMMENT_H */