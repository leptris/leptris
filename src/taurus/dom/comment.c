/* lib/src/dom/comment.c - Comment node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "comment.h"
#include <stdlib.h>
#include <string.h>

/* Create comment node */
TaurusCommentNode* taurus_comment_create(const char* content) {
    TaurusCommentNode* comment = (TaurusCommentNode*)taurus_node_create(
        TAURUS_NODE_TYPE_COMMENT,
        sizeof(TaurusCommentNode)
    );

    if (!comment) return NULL;

    comment->content = content ? taurus_strdup(content) : NULL;
    comment->next_sibling = NULL;  /* Initialize sibling pointer */

    return comment;
}

/**
 * Create comment node with bulk allocation (OPTIMIZED)
 *
 * Allocates node structure and content string in single pool allocation
 * for better performance and cache locality.
 *
 * @param content      Comment content
 * @param content_len  Length of content (avoid strlen call)
 * @param pool         Memory pool for allocation
 * @return New comment node, or NULL on failure
 */
TaurusCommentNode* taurus_comment_create_fast(
    const char* content,
    size_t content_len,
    TaurusMemoryPool* pool
) {
    if (!content || !pool) return NULL;

    /* Single allocation: node + content */
    size_t total_size = sizeof(TaurusCommentNode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    /* Comment structure at start of memory */
    TaurusCommentNode* node = (TaurusCommentNode*)memory;

    /* Content string immediately after structure */
    char* content_storage = memory + sizeof(TaurusCommentNode);
    memcpy(content_storage, content, content_len);
    content_storage[content_len] = '\0';

    /* Initialize base node - note: parent/sibling pointers removed in compact architecture */
    node->base.type = TAURUS_NODE_TYPE_COMMENT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling = NULL;  /* Initialize sibling pointer */

    /* Set content pointer to adjacent storage */
    node->content = content_storage;

    return node;
}

/* Free comment node */
void taurus_comment_free(TaurusCommentNode* comment) {
    if (!comment) return;

    /* Only free content if it's separately allocated (not embedded via _create_fast)
     * Embedded content is part of the same allocation as the node structure */
    if (comment->content) {
        /* Check if content is embedded (pointing right after the node structure)
         * If content points to memory immediately after the node, it's embedded */
        char* expected_embedded = (char*)comment + sizeof(TaurusCommentNode);
        if (comment->content != expected_embedded) {
            free(comment->content);
        }
        /* If embedded, it will be freed along with the node structure */
    }
    free(comment);
}

/* Get comment content */
const char* taurus_comment_get_content(TaurusCommentNode* comment) {
    return comment ? comment->content : NULL;
}