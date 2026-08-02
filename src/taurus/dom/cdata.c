/* lib/src/dom/cdata.c - CDATA node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "cdata.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create CDATA node.  Single pool-routed entry point: struct + content
 * contiguous (TODO 18 consolidated _create and _create_fast). */
TaurusCDATANode* taurus_cdata_create(const char* content,
                                      size_t content_len,
                                      TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    size_t total_size = sizeof(TaurusCDATANode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusCDATANode* node = (TaurusCDATANode*)memory;
    char* content_storage = memory + sizeof(TaurusCDATANode);

    node->base.type = TAURUS_NODE_TYPE_CDATA;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling = NULL;

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
