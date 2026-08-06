/* lib/src/dom/text.c - Text node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "text.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create text node: single pool-routed entry point.
 *
 * Struct and content come from the pool. For typical small content
 * they share a single pool page (cache-friendly); for oversized
 * content the struct stays pool-resident (so int32_t compact
 * pointers from the parent element remain valid) and the content
 * lives in a separate oversized allocation referenced via pointer.
 *
 * CRITICAL: Content is NEVER trimmed — preserved exactly as given. */
TaurusTextNode* taurus_text_create(const char* content,
                                    size_t content_len,
                                    TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    char* content_storage;
    TaurusTextNode* node = (TaurusTextNode*)taurus_pool_alloc_node_with_content(
        pool, sizeof(TaurusTextNode), content_len, &content_storage);
    if (!node) return NULL;

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