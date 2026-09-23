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
LeptrisTextNode* leptris_text_create(const char* content,
                                    size_t content_len,
                                    LeptrisMemoryPool* pool) {
    if (!pool) return NULL;

    char* content_storage;
    LeptrisTextNode* node = (LeptrisTextNode*)leptris_pool_alloc_node_with_content(
        pool, sizeof(LeptrisTextNode), content_len, &content_storage);
    if (!node) return NULL;

    node->base.type = LEPTRIS_NODE_TYPE_TEXT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->base.raw = 0;   /* uninitialized: see create_borrowed */
    node->parent_off = 0;
    node->next_sibling_off = 0;
    node->owner_doc_off = 0;   /* #1320: only the public creator stamps */

    if (content && content_len > 0) {
        memcpy(content_storage, content, content_len);
    }
    content_storage[content_len] = '\0';
    node->content = content_storage;
    node->content_len = (uint32_t)content_len;

    return node;
}

/* Create a borrowed text node (TODO 115 Phase B).
 *
 * Allocates only sizeof(LeptrisTextNode) from the pool — no content
 * copy. Stores the caller's pointer + length; content is NOT
 * run is already NUL-terminated at content[content_len] and outlives
 * the document (every parse lane terminates the run in the doc-owned
 * input copy before carving). The node struct itself still comes from
 * the parser's pool. */
LeptrisTextNode* leptris_text_create_borrowed(const char* content,
                                             size_t content_len,
                                             LeptrisMemoryPool* pool) {
    if (!pool) return NULL;

    LeptrisTextNode* node = (LeptrisTextNode*)leptris_pool_alloc(pool, sizeof(LeptrisTextNode));
    if (!node) return NULL;

    node->base.type = LEPTRIS_NODE_TYPE_TEXT;
    node->base.frozen = 0;
    node->base.version = 0;
    /* Pool memory is dirty (and ASAN's allocator fills 0xbe): an
     * uninitialized raw bit silently flips DOE/cdata behavior by
     * build configuration. */
    node->base.raw = 0;
    node->content = (char*)content;  /* Non-owning; caller guarantees lifetime + termination. */
    node->content_len = (uint32_t)content_len;
    node->parent_off = 0;
    node->next_sibling_off = 0;
    node->owner_doc_off = 0;   /* #1320: only the public creator stamps */

    return node;
}

/* Free text node.
 *
 * As of TODO 17, individual node freeing is forbidden — the pool owns
 * all node lifetime.  This function is kept only for backwards source
 * compatibility with callers that explicitly call it; it's a no-op. */
void leptris_text_free(LeptrisTextNode* text) {
    (void)text;
    /* Pool-owned; nothing to do. */
}

/* Get text content.
 *
 * #1285 slice 4: content is ALWAYS NUL-terminated — pooled copies and
 * in-place parse-buffer runs alike — so this is a plain field read.
 * Entities are expanded at parse time (the XML dp decodes '&' runs
 * eagerly; the HTML builder routes them to its decode path), never
 * here. */
const char* leptris_text_get_content(LeptrisTextNode* text) {
    if (!text) return NULL;
    return text->content ? text->content : "";
}
