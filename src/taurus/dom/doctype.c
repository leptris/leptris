/* lib/src/dom/doctype.c - DOCTYPE node implementation
 * Copyright (c) 2024, Ribose Inc.
 */

#include "doctype.h"
#include <stdlib.h>

/* Create DOCTYPE node */
TaurusDoctypeNode* taurus_doctype_create(const char* name) {
    if (!name) return NULL;

    TaurusDoctypeNode* doctype = (TaurusDoctypeNode*)taurus_node_create(
        TAURUS_NODE_TYPE_DOCTYPE,
        sizeof(TaurusDoctypeNode)
    );

    if (!doctype) return NULL;

    doctype->name = taurus_strdup(name);
    doctype->public_id = NULL;
    doctype->system_id = NULL;
    doctype->internal_subset = NULL;

    return doctype;
}

/* Free DOCTYPE node */
void taurus_doctype_free(TaurusDoctypeNode* doctype) {
    if (!doctype) return;

    if (doctype->name) free(doctype->name);
    if (doctype->public_id) free(doctype->public_id);
    if (doctype->system_id) free(doctype->system_id);
    if (doctype->internal_subset) free(doctype->internal_subset);
    free(doctype);
}

/* Get DOCTYPE name */
const char* taurus_doctype_get_name(TaurusDoctypeNode* doctype) {
    return doctype ? doctype->name : NULL;
}

/* Set public ID */
void taurus_doctype_set_public_id(TaurusDoctypeNode* doctype, const char* public_id) {
    if (!doctype) return;

    if (doctype->public_id) free(doctype->public_id);
    doctype->public_id = public_id ? taurus_strdup(public_id) : NULL;
}

/* Set system ID */
void taurus_doctype_set_system_id(TaurusDoctypeNode* doctype, const char* system_id) {
    if (!doctype) return;

    if (doctype->system_id) free(doctype->system_id);
    doctype->system_id = system_id ? taurus_strdup(system_id) : NULL;
}

/* Set internal subset */
void taurus_doctype_set_internal_subset(TaurusDoctypeNode* doctype, const char* subset) {
    if (!doctype) return;

    if (doctype->internal_subset) free(doctype->internal_subset);
    doctype->internal_subset = subset ? taurus_strdup(subset) : NULL;
}