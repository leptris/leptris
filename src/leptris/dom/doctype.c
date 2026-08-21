/* lib/src/dom/doctype.c - DOCTYPE node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "../../include/leptris.h"
#include "doctype.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create DOCTYPE node.  Pool-allocated with contiguous name storage
 * (TODO 18).  Other string fields (public_id, system_id,
 * internal_subset) are added later via the setter functions, which
 * also route through the pool — see TODO 16. */
LeptrisDoctypeNode* leptris_doctype_create(const char* name,
                                          size_t name_len,
                                          LeptrisMemoryPool* pool) {
    if (!name || !pool) return NULL;

    size_t total_size = sizeof(LeptrisDoctypeNode) + name_len + 1;
    char* memory = (char*)leptris_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    LeptrisDoctypeNode* doctype = (LeptrisDoctypeNode*)memory;
    char* name_storage = memory + sizeof(LeptrisDoctypeNode);

    doctype->base.type = LEPTRIS_NODE_TYPE_DOCTYPE;
    doctype->base.frozen = 0;
    doctype->base.version = 0;
    doctype->public_id = NULL;
    doctype->system_id = NULL;
    doctype->internal_subset = NULL;

    memcpy(name_storage, name, name_len);
    name_storage[name_len] = '\0';
    doctype->name = name_storage;

    return doctype;
}

/* Free DOCTYPE node.
 *
 * Pool-ownership model (TODO 05/16/18):
 *   - node struct: pool-owned, released by leptris_pool_destroy.
 *   - name: pool-owned (stored contiguously after the struct).
 *   - public_id, system_id, internal_subset: pool-allocated via the
 *     setters below (TODO 16).
 *
 * This function is a no-op — kept only for backwards source
 * compatibility with callers that explicitly invoke it. */
void leptris_doctype_free(LeptrisDoctypeNode* doctype) {
    (void)doctype;
}

LEPTRIS_API const char* leptris_doctype_get_name(LeptrisDoctypeNode* doctype) {
    return doctype ? doctype->name : NULL;
}

/* Set public ID.  Pool-routed (TODO 16): the string is copied into the
 * document's pool so the caller's buffer can be freed/reused. */
void leptris_doctype_set_public_id(LeptrisDoctypeNode* doctype,
                                   const char* public_id,
                                   LeptrisMemoryPool* pool) {
    if (!doctype) return;
    /* Old value is pool-owned; we just overwrite the pointer.  No free. */
    doctype->public_id = public_id && pool
        ? leptris_pool_strdup(pool, public_id)
        : NULL;
}

void leptris_doctype_set_system_id(LeptrisDoctypeNode* doctype,
                                   const char* system_id,
                                   LeptrisMemoryPool* pool) {
    if (!doctype) return;
    doctype->system_id = system_id && pool
        ? leptris_pool_strdup(pool, system_id)
        : NULL;
}

void leptris_doctype_set_internal_subset(LeptrisDoctypeNode* doctype,
                                         const char* subset,
                                         LeptrisMemoryPool* pool) {
    if (!doctype) return;
    doctype->internal_subset = subset && pool
        ? leptris_pool_strdup(pool, subset)
        : NULL;
}

/* ---- Public API wrappers (TODO 148 Phase 2) ----
 *
 * Thin re-exports of the internal accessors under the public
 * `LeptrisDoctype` opaque typedef + a document-level entry point
 * that returns the doctype handle.
 */

LEPTRIS_API LeptrisDoctype leptris_document_internal_subset(LeptrisDocument doc) {
    if (!doc) return NULL;
    return (LeptrisDoctype)doc->doctype;
}

LEPTRIS_API const char* leptris_doctype_get_root_name(LeptrisDoctype dt) {
    return dt ? dt->name : NULL;
}

LEPTRIS_API const char* leptris_doctype_get_public_id(LeptrisDoctype dt) {
    return dt ? dt->public_id : NULL;
}

LEPTRIS_API const char* leptris_doctype_get_system_id(LeptrisDoctype dt) {
    return dt ? dt->system_id : NULL;
}

LEPTRIS_API const char* leptris_doctype_get_internal_subset(LeptrisDoctype dt) {
    return dt ? dt->internal_subset : NULL;
}
