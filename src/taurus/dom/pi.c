/* lib/src/dom/pi.c - Processing Instruction node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "pi.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create PI node.  Single pool-routed entry point: struct + target +
 * optional data, all contiguous (TODO 18 consolidated _create and
 * _create_fast). */
TaurusPINode* taurus_pi_create(const char* target,
                                size_t target_len,
                                const char* data,
                                size_t data_len,
                                TaurusMemoryPool* pool) {
    if (!target || target_len == 0 || !pool) return NULL;

    /* Single allocation: struct + target + NUL + (data + NUL if present) */
    size_t total_size = sizeof(TaurusPINode) + target_len + 1;
    if (data && data_len > 0) {
        total_size += data_len + 1;
    }

    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusPINode* node = (TaurusPINode*)memory;
    char* target_storage = memory + sizeof(TaurusPINode);

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
    node->next_sibling = NULL;
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
