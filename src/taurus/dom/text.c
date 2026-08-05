/* lib/src/dom/text.c - Text node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "text.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create text node: single pool-routed entry point.
 *
 * Allocates struct + content contiguously in one pool bump (TODO 18
 * consolidated the legacy calloc-backed create with the pool-backed
 * _create_fast — both now flow through here).
 *
 * CRITICAL: Content is NEVER trimmed — preserved exactly as given. */
TaurusTextNode* taurus_text_create(const char* content,
                                    size_t content_len,
                                    TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    /* Single allocation: node struct + content + NUL */
    size_t total_size = sizeof(TaurusTextNode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusTextNode* node = (TaurusTextNode*)memory;
    char* content_storage = memory + sizeof(TaurusTextNode);

    node->base.type = TAURUS_NODE_TYPE_TEXT;
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

/* Free text node.
 *
 * As of TODO 17, individual node freeing is forbidden — the pool owns
 * all node lifetime.  This function is kept only for backwards source
 * compatibility with callers that explicitly call it; it's a no-op. */
void taurus_text_free(TaurusTextNode* text) {
    (void)text;
    /* Pool-owned; nothing to do. */
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