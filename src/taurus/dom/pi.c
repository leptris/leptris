/* lib/src/dom/pi.c - Processing Instruction node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "pi.h"
#include <stdlib.h>
#include <string.h>
#include "../memory/pool.h"

/* Create PI node */
TaurusPINode* taurus_pi_create(const char* target, const char* data) {
    if (!target) return NULL;

    TaurusPINode* pi = (TaurusPINode*)taurus_node_create(
        TAURUS_NODE_TYPE_PI,
        sizeof(TaurusPINode)
    );

    if (!pi) return NULL;

    pi->target = taurus_strdup(target);
    pi->data = data ? taurus_strdup(data) : NULL;
    pi->next_sibling = NULL;  /* Initialize sibling pointer */

    return pi;
}

/**
 * Create PI node with bulk allocation (OPTIMIZED)
 *
 * Allocates node structure, target string, and data string in single
 * pool allocation for better performance and cache locality.
 *
 * @param target      PI target
 * @param target_len  Length of target (avoid strlen call)
 * @param data        PI data (can be NULL)
 * @param data_len    Length of data (0 if NULL)
 * @param pool        Memory pool for allocation
 * @return New PI node, or NULL on failure
 */
TaurusPINode* taurus_pi_create_fast(
    const char* target,
    size_t target_len,
    const char* data,
    size_t data_len,
    TaurusMemoryPool* pool
) {
    if (!target || !pool) return NULL;

    /* Calculate total: structure + target + data (if present) */
    size_t total_size = sizeof(TaurusPINode) + target_len + 1;
    if (data && data_len > 0) {
        total_size += data_len + 1;
    }

    /* Single allocation for all three components */
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    /* PI structure at start of memory */
    TaurusPINode* node = (TaurusPINode*)memory;

    /* Target string immediately after structure */
    char* target_storage = memory + sizeof(TaurusPINode);
    memcpy(target_storage, target, target_len);
    target_storage[target_len] = '\0';

    /* Data string immediately after target (if present) */
    if (data && data_len > 0) {
        char* data_storage = target_storage + target_len + 1;
        memcpy(data_storage, data, data_len);
        data_storage[data_len] = '\0';
        node->data = data_storage;
    } else {
        node->data = NULL;
    }

    /* Initialize base node - note: parent/sibling pointers removed in compact architecture */
    node->base.type = TAURUS_NODE_TYPE_PI;
    node->base.frozen = 0;
    node->base.version = 0;
    node->next_sibling = NULL;  /* Initialize sibling pointer */

    /* Set target pointer to adjacent storage */
    node->target = target_storage;

    return node;
}

/* Free PI node */
void taurus_pi_free(TaurusPINode* pi) {
    if (!pi) return;

    if (pi->target) free(pi->target);
    if (pi->data) free(pi->data);
    free(pi);
}

/* Get PI target */
const char* taurus_pi_get_target(TaurusPINode* pi) {
    return pi ? pi->target : NULL;
}

/* Get PI data */
const char* taurus_pi_get_data(TaurusPINode* pi) {
    return pi ? pi->data : NULL;
}