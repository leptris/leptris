/* lib/src/dom/comment.h - Comment node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comment nodes contain <!-- comment --> content.
 * Content is preserved exactly as parsed.
 */

#ifndef TAURUS_DOM_COMMENT_H
#define TAURUS_DOM_COMMENT_H

#include "node.h"

/* Comment node - inherits from TaurusNode */
typedef struct taurus_comment_node {
    TaurusNode base;                   /* MUST be first */
    char* content;                    /* Comment content */
    void* next_sibling;               /* Next sibling in linked list (mixed content) */
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

#endif /* TAURUS_DOM_COMMENT_H */