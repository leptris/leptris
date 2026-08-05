/* lib/src/dom/comment.c - Comment node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "comment.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create comment node.  Single pool-routed entry point: struct + content
 * allocated contiguously (TODO 18 consolidated the dual _create/_fast paths). */
TaurusCommentNode* taurus_comment_create(const char* content,
                                          size_t content_len,
                                          TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    size_t total_size = sizeof(TaurusCommentNode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusCommentNode* node = (TaurusCommentNode*)memory;
    char* content_storage = memory + sizeof(TaurusCommentNode);

    node->base.type = TAURUS_NODE_TYPE_COMMENT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling_off = 0;

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