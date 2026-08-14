/* lib/src/dom/comment.c - Comment node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "comment.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create comment node.  Single pool-routed entry point: struct +
 * content use taurus_pool_alloc_node_with_content so oversized
 * content doesn't drag the struct out of the pool's compact-pointer
 * range (TODO 90 Phase 2b silent-drop fix). */
TaurusCommentNode* taurus_comment_create(const char* content,
                                          size_t content_len,
                                          TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    char* content_storage;
    TaurusCommentNode* node = (TaurusCommentNode*)taurus_pool_alloc_node_with_content(
        pool, sizeof(TaurusCommentNode), content_len, &content_storage);
    if (!node) return NULL;

    node->base.type = TAURUS_NODE_TYPE_COMMENT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->parent_off = 0;
    node->next_sibling_cp = 0;

    if (content && content_len > 0) {
        memcpy(content_storage, content, content_len);
    }
    content_storage[content_len] = '\0';
    node->content = content_storage;

    return node;
}

/* Free comment node — pool-owned (TODO 17); no-op. */
void taurus_comment_free(TaurusCommentNode* comment) {
    (void)comment;
}

/* Get comment content */
const char* taurus_comment_get_content(TaurusCommentNode* comment) {
    return comment ? comment->content : NULL;
}