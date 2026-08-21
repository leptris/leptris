/* lib/src/dom/doctype.h - DOCTYPE node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * DOCTYPE nodes contain <!DOCTYPE ...> declarations.
 */

#ifndef LEPTRIS_DOM_DOCTYPE_H
#define LEPTRIS_DOM_DOCTYPE_H

#include "node.h"

/* DOCTYPE node - inherits from LeptrisNode.
 * Tagged `leptris_doctype` so the public opaque typedef
 * (LeptrisDoctype = struct leptris_doctype*) matches. The struct is
 * internal-only; the public API only sees the opaque pointer. */
typedef struct leptris_doctype {
    LeptrisNode base;                   /* MUST be first */
    char* name;                        /* DOCTYPE name (e.g., "html") */
    char* public_id;                   /* Public identifier (optional) */
    char* system_id;                   /* System identifier (optional) */
    char* internal_subset;             /* Internal DTD subset (optional) */
} LeptrisDoctypeNode;

/* DOCTYPE node creation.  Pool-allocated; struct + name contiguous
 * (TODO 18 consolidated naming with other node types). */
LeptrisDoctypeNode* leptris_doctype_create(const char* name,
                                          size_t name_len,
                                          struct leptris_memory_pool* pool);
void leptris_doctype_free(LeptrisDoctypeNode* doctype);

/* Content access.  Setters are pool-routed (TODO 16) — the input
 * string is copied into the pool so the caller's buffer can be
 * freed/reused immediately. */
LEPTRIS_API const char* leptris_doctype_get_name(LeptrisDoctypeNode* doctype);
void leptris_doctype_set_public_id(LeptrisDoctypeNode* doctype,
                                   const char* public_id,
                                   struct leptris_memory_pool* pool);
void leptris_doctype_set_system_id(LeptrisDoctypeNode* doctype,
                                   const char* system_id,
                                   struct leptris_memory_pool* pool);
void leptris_doctype_set_internal_subset(LeptrisDoctypeNode* doctype,
                                         const char* subset,
                                         struct leptris_memory_pool* pool);

/* Casting helpers */
#define LEPTRIS_NODE_AS_DOCTYPE(node) \
    (LEPTRIS_NODE_IS_DOCTYPE(node) ? (LeptrisDoctypeNode*)(node) : NULL)

#define LEPTRIS_DOCTYPE_AS_NODE(doctype) \
    ((LeptrisNode*)(doctype))

#endif /* LEPTRIS_DOM_DOCTYPE_H */