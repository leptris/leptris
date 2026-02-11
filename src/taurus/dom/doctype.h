/* lib/src/dom/doctype.h - DOCTYPE node type
 * Copyright (c) 2024, Ribose Inc.
 *
 * DOCTYPE nodes contain <!DOCTYPE ...> declarations.
 */

#ifndef TAURUS_DOM_DOCTYPE_H
#define TAURUS_DOM_DOCTYPE_H

#include "node.h"

/* DOCTYPE node - inherits from TaurusNode */
typedef struct taurus_doctype_node {
    TaurusNode base;                   /* MUST be first */
    char* name;                        /* DOCTYPE name (e.g., "html") */
    char* public_id;                   /* Public identifier (optional) */
    char* system_id;                   /* System identifier (optional) */
    char* internal_subset;             /* Internal DTD subset (optional) */
} TaurusDoctypeNode;

/* DOCTYPE node creation and destruction */
TaurusDoctypeNode* taurus_doctype_create(const char* name);
void taurus_doctype_free(TaurusDoctypeNode* doctype);

/* Content access */
const char* taurus_doctype_get_name(TaurusDoctypeNode* doctype);
void taurus_doctype_set_public_id(TaurusDoctypeNode* doctype, const char* public_id);
void taurus_doctype_set_system_id(TaurusDoctypeNode* doctype, const char* system_id);
void taurus_doctype_set_internal_subset(TaurusDoctypeNode* doctype, const char* subset);

/* Casting helpers */
#define TAURUS_NODE_AS_DOCTYPE(node) \
    (TAURUS_NODE_IS_DOCTYPE(node) ? (TaurusDoctypeNode*)(node) : NULL)

#define TAURUS_DOCTYPE_AS_NODE(doctype) \
    ((TaurusNode*)(doctype))

#endif /* TAURUS_DOM_DOCTYPE_H */