/* lib/src/dom/cdata.c - CDATA node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "cdata.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create CDATA node.  Single pool-routed entry point via
 * taurus_pool_alloc_node_with_content — keeps the struct
 * pool-resident even for oversized content (TODO 90 Phase 2b fix). */
TaurusCDATANode* taurus_cdata_create(const char* content,
                                      size_t content_len,
                                      TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    char* content_storage;
    TaurusCDATANode* node = (TaurusCDATANode*)taurus_pool_alloc_node_with_content(
        pool, sizeof(TaurusCDATANode), content_len, &content_storage);
    if (!node) return NULL;

    node->base.type = TAURUS_NODE_TYPE_CDATA;
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

/* Free CDATA node — pool-owned (TODO 17); no-op. */
void taurus_cdata_free(TaurusCDATANode* cdata) {
    (void)cdata;
}

/* Get CDATA content */
const char* taurus_cdata_get_content(TaurusCDATANode* cdata) {
    return cdata ? cdata->content : NULL;
}
