#include "document_node.h"

LeptrisNode* leptris_document_get_node(struct leptris_document* doc) {
    if (!doc) return NULL;
    if (doc->document_node) return (LeptrisNode*)doc->document_node;
    LeptrisDocumentNode* n = (LeptrisDocumentNode*)
        leptris_node_create_pooled(LEPTRIS_NODE_TYPE_DOCUMENT,
                                   sizeof(*n), doc->pool);
    if (!n) return NULL;
    n->doc = doc;
    doc->document_node = n;
    return (LeptrisNode*)n;
}
