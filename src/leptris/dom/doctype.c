/* lib/src/dom/doctype.c - DOCTYPE node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "../../include/leptris.h"
#include "doctype.h"
#include "node.h"
#include "../leptris_internal.h"
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

/* #1094: programmatic DOCTYPE — create on a document (name +
 * external identifiers), symmetric with what parsing accepts. The
 * serializer emits it in document position once set. */
LEPTRIS_API LeptrisDoctype leptris_document_set_doctype(
    LeptrisDocument doc, const char* name,
    const char* public_id, const char* system_id) {
    if (!doc || !name || !*name) return NULL;
    leptris_document_ensure_promoted(doc);
    if (!doc->pool) return NULL;
    LeptrisDoctypeNode* dt =
        leptris_doctype_create(name, strlen(name), doc->pool);
    if (!dt) return NULL;
    if (public_id && *public_id)
        leptris_doctype_set_public_id(dt, public_id, doc->pool);
    if (system_id && *system_id)
        leptris_doctype_set_system_id(dt, system_id, doc->pool);
    doc->doctype = dt;
    return (LeptrisDoctype)dt;
}

/* #1229: the remove-half of set_doctype. The node is pool-owned, so
 * it is unlinked (document children chain, when linked) — never
 * freed; it stays readable until leptris_document_free. */
LEPTRIS_API LeptrisStatus leptris_document_remove_doctype(
    LeptrisDocument doc) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    LeptrisDoctypeNode* dt = (LeptrisDoctypeNode*)doc->doctype;
    if (!dt) return LEPTRIS_ERROR_NOT_FOUND;
    LeptrisNode* prev = NULL;
    for (LeptrisNode* c = (LeptrisNode*)doc->doc_children_head; c; ) {
        LeptrisNode* next = leptris_node_get_next_sibling(c);
        if (c == (LeptrisNode*)dt) {
            if (prev)
                leptris_node_set_next_sibling(prev, next);
            else
                doc->doc_children_head = next;
            if (doc->doc_children_tail == c)
                doc->doc_children_tail = prev;
            break;
        }
        prev = c;
        c = next;
    }
    doc->doctype = NULL;
    return LEPTRIS_OK;
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
