/* lib/src/dom/cdata.c - CDATA node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "cdata.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create CDATA node.  Single pool-routed entry point via
 * leptris_pool_alloc_node_with_content — keeps the struct
 * pool-resident even for oversized content (TODO 90 Phase 2b fix). */
LeptrisCDATANode* leptris_cdata_create(const char* content,
                                      size_t content_len,
                                      LeptrisMemoryPool* pool) {
    if (!pool) return NULL;

    char* content_storage;
    LeptrisCDATANode* node = (LeptrisCDATANode*)leptris_pool_alloc_node_with_content(
        pool, sizeof(LeptrisCDATANode), content_len, &content_storage);
    if (!node) return NULL;

    node->base.type = LEPTRIS_NODE_TYPE_CDATA;
    node->base.frozen = 0;
    node->base.version = 0;
      node->base.binding_wrapper = NULL;
    node->parent_off = 0;
    node->next_sibling_off = 0;

    if (content && content_len > 0) {
        memcpy(content_storage, content, content_len);
    }
    content_storage[content_len] = '\0';
    node->content = content_storage;

    return node;
}

/* Free CDATA node — pool-owned (TODO 17); no-op. */
void leptris_cdata_free(LeptrisCDATANode* cdata) {
    (void)cdata;
}

/* Get CDATA content */
const char* leptris_cdata_get_content(LeptrisCDATANode* cdata) {
    return cdata ? cdata->content : NULL;
}
