/* lib/src/dom/doctype.c - DOCTYPE node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "../../include/taurus.h"
#include "doctype.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/* Create DOCTYPE node.  Pool-allocated with contiguous name storage
 * (TODO 18).  Other string fields (public_id, system_id,
 * internal_subset) are added later via the setter functions, which
 * also route through the pool — see TODO 16. */
TaurusDoctypeNode* taurus_doctype_create(const char* name,
                                          size_t name_len,
                                          TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    size_t total_size = sizeof(TaurusDoctypeNode) + name_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total_size);
    if (!memory) return NULL;

    TaurusDoctypeNode* doctype = (TaurusDoctypeNode*)memory;
    char* name_storage = memory + sizeof(TaurusDoctypeNode);

    doctype->base.type = TAURUS_NODE_TYPE_DOCTYPE;
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
 *   - node struct: pool-owned, released by taurus_pool_destroy.
 *   - name: pool-owned (stored contiguously after the struct).
 *   - public_id, system_id, internal_subset: pool-allocated via the
 *     setters below (TODO 16).
 *
 * This function is a no-op — kept only for backwards source
 * compatibility with callers that explicitly invoke it. */
void taurus_doctype_free(TaurusDoctypeNode* doctype) {
    (void)doctype;
}

TAURUS_API const char* taurus_doctype_get_name(TaurusDoctypeNode* doctype) {
    return doctype ? doctype->name : NULL;
}

/* Set public ID.  Pool-routed (TODO 16): the string is copied into the
 * document's pool so the caller's buffer can be freed/reused. */
void taurus_doctype_set_public_id(TaurusDoctypeNode* doctype,
                                   const char* public_id,
                                   TaurusMemoryPool* pool) {
    if (!doctype) return;
    /* Old value is pool-owned; we just overwrite the pointer.  No free. */
    doctype->public_id = public_id && pool
        ? taurus_pool_strdup(pool, public_id)
        : NULL;
}

void taurus_doctype_set_system_id(TaurusDoctypeNode* doctype,
                                   const char* system_id,
                                   TaurusMemoryPool* pool) {
    if (!doctype) return;
    doctype->system_id = system_id && pool
        ? taurus_pool_strdup(pool, system_id)
        : NULL;
}

void taurus_doctype_set_internal_subset(TaurusDoctypeNode* doctype,
                                         const char* subset,
                                         TaurusMemoryPool* pool) {
    if (!doctype) return;
    doctype->internal_subset = subset && pool
        ? taurus_pool_strdup(pool, subset)
        : NULL;
}

/* ---- Public API wrappers (TODO 148 Phase 2) ----
 *
 * Thin re-exports of the internal accessors under the public
 * `TaurusDoctype` opaque typedef + a document-level entry point
 * that returns the doctype handle.
 */

TAURUS_API TaurusDoctype taurus_document_internal_subset(TaurusDocument doc) {
    if (!doc) return NULL;
    return (TaurusDoctype)doc->doctype;
}

TAURUS_API const char* taurus_doctype_get_root_name(TaurusDoctype dt) {
    return dt ? dt->name : NULL;
}

TAURUS_API const char* taurus_doctype_get_public_id(TaurusDoctype dt) {
    return dt ? dt->public_id : NULL;
}

TAURUS_API const char* taurus_doctype_get_system_id(TaurusDoctype dt) {
    return dt ? dt->system_id : NULL;
}

TAURUS_API const char* taurus_doctype_get_internal_subset(TaurusDoctype dt) {
    return dt ? dt->internal_subset : NULL;
}
