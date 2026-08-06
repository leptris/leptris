/* lib/src/dom/pi.c - Processing Instruction node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "pi.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create PI node.  Single pool-routed entry point via
 * taurus_pool_alloc_node_with_content so oversized PI data doesn't
 * drag the struct out of the pool's compact-pointer range (TODO 90
 * Phase 2b silent-drop fix). Target is always small; data may be
 * arbitrarily large. */
TaurusPINode* taurus_pi_create(const char* target,
                                size_t target_len,
                                const char* data,
                                size_t data_len,
                                TaurusMemoryPool* pool) {
    if (!target || target_len == 0 || !pool) return NULL;

    /* Combined "content" buffer holds target\0 + data\0 (if data). */
    size_t content_size = target_len;  /* NUL added by helper */
    if (data && data_len > 0) {
        content_size += 1 + data_len;  /* +1 for target's NUL */
    }

    char* content_storage;
    TaurusPINode* node = (TaurusPINode*)taurus_pool_alloc_node_with_content(
        pool, sizeof(TaurusPINode), content_size, &content_storage);
    if (!node) return NULL;

    char* target_storage = content_storage;
    memcpy(target_storage, target, target_len);
    target_storage[target_len] = '\0';

    if (data && data_len > 0) {
        char* data_storage = target_storage + target_len + 1;
        memcpy(data_storage, data, data_len);
        data_storage[data_len] = '\0';
        node->data = data_storage;
    } else {
        node->data = NULL;
    }

    node->base.type = TAURUS_NODE_TYPE_PI;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling_off = 0;
    node->target = target_storage;

    return node;
}

/* Free PI node — pool-owned (TODO 17); no-op. */
void taurus_pi_free(TaurusPINode* pi) {
    (void)pi;
}

/* Get PI target */
const char* taurus_pi_get_target(TaurusPINode* pi) {
    return pi ? pi->target : NULL;
}

/* Get PI data */
const char* taurus_pi_get_data(TaurusPINode* pi) {
    return pi ? pi->data : NULL;
}
