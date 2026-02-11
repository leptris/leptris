/* lib/src/dom/text.c - Text node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "text.h"
#include <stdlib.h>
#include <string.h>

/* Create text node with content
 * CRITICAL: Content is NEVER trimmed */
TaurusTextNode* taurus_text_create(const char* content) {
    TaurusTextNode* text = (TaurusTextNode*)taurus_node_create(
        TAURUS_NODE_TYPE_TEXT,
        sizeof(TaurusTextNode)
    );

    if (!text) return NULL;

    text->content = content ? taurus_strdup(content) : NULL;
    text->next_sibling = NULL;  /* Initialize sibling pointer */

    return text;
}

/**
 * Create text node with bulk allocation (OPTIMIZED)
 *
 * Allocates node structure and content string in single pool allocation.
 *
 * @param content      Text content
 * @param content_len  Length of content
 * @param pool         Memory pool for allocation
 * @return New text node, or NULL on failure
 */
TaurusTextNode* taurus_text_create_fast(
    const char* content,
    size_t content_len,
    TaurusMemoryPool* pool
) {
    if (!content || !pool) return NULL;

    /* Single allocation: node + content */
    size_t total_size = sizeof(TaurusTextNode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusTextNode* node = (TaurusTextNode*)memory;
    char* content_storage = memory + sizeof(TaurusTextNode);

    /* Initialize base - note: parent/sibling pointers removed in compact architecture */
    node->base.type = TAURUS_NODE_TYPE_TEXT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling = NULL;  /* Initialize sibling pointer */

    /* Copy content to adjacent storage */
    memcpy(content_storage, content, content_len);
    content_storage[content_len] = '\0';
    node->content = content_storage;

    return node;
}

/* Free text node */
void taurus_text_free(TaurusTextNode* text) {
    if (!text) return;

    if (text->content) free(text->content);
    free(text);
}

/* Get text content */
const char* taurus_text_get_content(TaurusTextNode* text) {
    return text ? text->content : NULL;
}

/* Set text content */
void taurus_text_set_content(TaurusTextNode* text, const char* content) {
    if (!text) return;

    if (text->content) free(text->content);
    text->content = content ? taurus_strdup(content) : NULL;
}