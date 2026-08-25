/* dom/document_node.h — the XPath document node (root).
 *
 * One lazily-created, pool-owned singleton per document. It is the
 * XPath/XSLT "root node": the initial XSLT context, the "/" pattern
 * target, and the child-axis source for document-level selects.
 * It never appears in serialized output. */
#ifndef LEPTRIS_DOM_DOCUMENT_NODE_H
#define LEPTRIS_DOM_DOCUMENT_NODE_H

#include "node.h"
#include "../leptris_internal.h"

typedef struct leptris_document_node {
    LeptrisNode base;               /* type == LEPTRIS_NODE_TYPE_DOCUMENT */
    struct leptris_document* doc;   /* child axis reads doc->new_dom_root */
} LeptrisDocumentNode;

/* The document's root node (creates on first call, pool-owned). */
LeptrisNode* leptris_document_get_node(struct leptris_document* doc);

#endif /* LEPTRIS_DOM_DOCUMENT_NODE_H */
