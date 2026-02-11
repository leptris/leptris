/* lib/src/dom/cdata.c - CDATA node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "cdata.h"
#include <stdlib.h>
#include <string.h>

/* Create CDATA node */
TaurusCDATANode* taurus_cdata_create(const char* content) {
    TaurusCDATANode* cdata = (TaurusCDATANode*)taurus_node_create(
        TAURUS_NODE_TYPE_CDATA,
        sizeof(TaurusCDATANode)
    );

    if (!cdata) return NULL;

    cdata->content = content ? taurus_strdup(content) : NULL;
    cdata->next_sibling = NULL;  /* Initialize sibling pointer */

    return cdata;
}

/**
 * Create CDATA node with bulk allocation (OPTIMIZED)
 *
 * Allocates node structure and content string in single pool allocation
 * for better performance and cache locality.
 *
 * @param content      CDATA content
 * @param content_len  Length of content (avoid strlen call)
 * @param pool         Memory pool for allocation
 * @return New CDATA node, or NULL on failure
 */
TaurusCDATANode* taurus_cdata_create_fast(
    const char* content,
    size_t content_len,
    TaurusMemoryPool* pool
) {
    if (!content || !pool) return NULL;

    /* Single allocation: node + content */
    size_t total_size = sizeof(TaurusCDATANode) + content_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    /* CDATA structure at start of memory */
    TaurusCDATANode* node = (TaurusCDATANode*)memory;

    /* Content string immediately after structure */
    char* content_storage = memory + sizeof(TaurusCDATANode);
    memcpy(content_storage, content, content_len);
    content_storage[content_len] = '\0';

    /* Initialize base node - note: parent/sibling pointers removed in compact architecture */
    node->base.type = TAURUS_NODE_TYPE_CDATA;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling = NULL;  /* Initialize sibling pointer */

    /* Set content pointer to adjacent storage */
    node->content = content_storage;

    return node;
}

/* Free CDATA node */
void taurus_cdata_free(TaurusCDATANode* cdata) {
    if (!cdata) return;

    if (cdata->content) free(cdata->content);
    free(cdata);
}

/* Get CDATA content */
const char* taurus_cdata_get_content(TaurusCDATANode* cdata) {
    return cdata ? cdata->content : NULL;
}