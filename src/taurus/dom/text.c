/* lib/src/dom/text.c - Text node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "text.h"
#include "../memory/pool.h"
#include "../common/entities.h"
#include "../common/string_view.h"
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
    node->pool = NULL;       /* content is pool-resident + NUL-terminated */
    node->borrowed = 0;
    node->parent_off = 0;
    node->next_sibling_cp = 0;

    if (content && content_len > 0) {
        memcpy(content_storage, content, content_len);
    }
    content_storage[content_len] = '\0';
    node->content = content_storage;
    node->content_len = content_len;

    return node;
}

/* Create a borrowed text node (TODO 115 Phase B).
 *
 * Allocates only sizeof(TaurusTextNode) from the pool — no content
 * copy. Stores the caller's pointer + length; content is NOT
 * NUL-terminated. The pool is kept on the node so a later
 * taurus_text_get_content call can materialize a NUL-terminated copy. */
TaurusTextNode* taurus_text_create_borrowed(const char* content,
                                             size_t content_len,
                                             TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    TaurusTextNode* node = (TaurusTextNode*)taurus_pool_alloc(pool, sizeof(TaurusTextNode));
    if (!node) return NULL;

    node->base.type = TAURUS_NODE_TYPE_TEXT;
    node->base.frozen = 0;
    node->base.version = 0;
    node->content = (char*)content;  /* Non-owning; caller guarantees lifetime. */
    node->content_len = content_len;
    node->pool = pool;
    node->borrowed = 1;
    node->parent_off = 0;
    node->next_sibling_cp = 0;

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

/* Get text content.
 *
 * For a borrowed (non-NUL-terminated) node this lazily materializes a
 * NUL-terminated copy into the node's pool and flips the node out of
 * borrowed mode, so subsequent calls are O(1). The materialized copy
 * is pool-owned and freed by taurus_document_free. */
const char* taurus_text_get_content(TaurusTextNode* text) {
    if (!text) return NULL;

    if (text->borrowed && text->pool) {
        /* Entity expansion: if the borrowed content contains '&',
         * expand predefined XML entities (&amp;, &lt;, etc.) and
         * numeric character references (&#65;, &#x42;) into a new
         * pool allocation. DTD-defined entities are NOT expanded
         * here — inputs with DOCTYPE internal subsets are routed to
         * the legacy parser which handles them eagerly. This lazy
         * path lets the fast direct_parse/flat_parse parsers handle
         * the common case of predefined-entity-only inputs at full
         * speed (zero entity cost on the parse hot path; expansion
         * only happens when text content is actually read). */
        if (text->content_len > 0 &&
            memchr(text->content, '&', text->content_len) != NULL) {
            TaurusStringView sv = taurus_sv_from_ptr(text->content,
                                                      text->content_len);
            char* expanded = taurus_decode_entities_view(&sv, text->pool);
            if (expanded) {
                text->content = expanded;
                text->content_len = strlen(expanded);
                text->borrowed = 0;
                return text->content;
            }
            /* Expansion failed (malformed entity, OOM) — fall
             * through to raw materialization so callers still get
             * a NUL-terminated string. */
        }
        char* storage = (char*)taurus_pool_alloc(text->pool, text->content_len + 1);
        if (!storage) return text->content;  /* Out of pool; return what we have. */
        if (text->content_len > 0) {
            memcpy(storage, text->content, text->content_len);
        }
        storage[text->content_len] = '\0';
        text->content = storage;
        text->borrowed = 0;
    }

    return text->content;
}

/* Set text content */
void taurus_text_set_content(TaurusTextNode* text, const char* content) {
    if (!text) return;

    if (text->content && !text->borrowed) free(text->content);
    if (content) {
        text->content_len = strlen(content);
        text->content = taurus_strdup(content);
    } else {
        text->content_len = 0;
        text->content = NULL;
    }
    text->borrowed = 0;
    text->pool = NULL;
}