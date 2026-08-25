/* lib/src/dom/node_vtable.c — Node vtable registry (TODO 29 phase 3).
 *
 * Defines one LeptrisNodeVTable per node type and exposes a lookup
 * function.  Dispatch sites (serializer, future free path) call
 * leptris_node_vtable_for(type) to get the per-type operations.
 *
 * Why a registry array (not a per-node pointer)?
 *  - Preserves the compact 4-byte LeptrisNode layout that the
 *    compact-pointer system relies on.
 *  - One indirection per dispatch — branch predictor learns it.
 *  - Adding a new node type = new entry here, no struct change.
 */

#include "node.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"
#include "element.h"
#include "doctype.h"
#include "document_node.h"
/* Include serialize.h for the full SerializeBuffer type — TODO 43. */
#include "../serialize/serialize.h"

static void document_serialize_impl(LeptrisNode* self, struct SerializeBuffer* buf);

/* Forward declarations for per-type serialize wrappers.  Each wraps
 * the existing serialize_*_internal function with the vtable signature. */
static void element_serialize (LeptrisNode* self, struct SerializeBuffer* buf);
static void text_serialize    (LeptrisNode* self, struct SerializeBuffer* buf);
static void comment_serialize (LeptrisNode* self, struct SerializeBuffer* buf);
static void cdata_serialize   (LeptrisNode* self, struct SerializeBuffer* buf);
static void pi_serialize      (LeptrisNode* self, struct SerializeBuffer* buf);
static void doctype_serialize (LeptrisNode* self, struct SerializeBuffer* buf);

static const LeptrisNodeVTable kElementVtable = {
    .serialize  = element_serialize,
    .type_name  = "element",
    .type_enum  = LEPTRIS_NODE_TYPE_ELEMENT,
};
static const LeptrisNodeVTable kTextVtable = {
    .serialize  = text_serialize,
    .type_name  = "text",
    .type_enum  = LEPTRIS_NODE_TYPE_TEXT,
};
static const LeptrisNodeVTable kCommentVtable = {
    .serialize  = comment_serialize,
    .type_name  = "comment",
    .type_enum  = LEPTRIS_NODE_TYPE_COMMENT,
};
static const LeptrisNodeVTable kCdataVtable = {
    .serialize  = cdata_serialize,
    .type_name  = "cdata",
    .type_enum  = LEPTRIS_NODE_TYPE_CDATA,
};
static const LeptrisNodeVTable kPiVtable = {
    .serialize  = pi_serialize,
    .type_name  = "pi",
    .type_enum  = LEPTRIS_NODE_TYPE_PI,
};
static const LeptrisNodeVTable kDoctypeVtable = {
    .serialize  = doctype_serialize,
    .type_name  = "doctype",
    .type_enum  = LEPTRIS_NODE_TYPE_DOCTYPE,
};

static const LeptrisNodeVTable kDocumentVtable = {
    .serialize  = document_serialize_impl,
    .type_name  = "document",
    .type_enum  = LEPTRIS_NODE_TYPE_DOCUMENT,
};

/* Indexed by LeptrisNodeTypeEnum.  NULL slots = no vtable (e.g.,
 * LEPTRIS_NODE_TYPE_ATTRIBUTE — XPath-internal, not serialized). */
static const LeptrisNodeVTable* const g_node_vtables[LEPTRIS_NODE_TYPE_COUNT] = {
    &kElementVtable,   /* 0 */
    &kTextVtable,      /* 1 */
    &kCommentVtable,   /* 2 */
    &kCdataVtable,     /* 3 */
    &kPiVtable,        /* 4 */
    &kDoctypeVtable,   /* 5 */
    NULL,              /* 6: ATTRIBUTE — XPath-internal */
    NULL,              /* 7: NAMESPACE — XPath-synthetic */
    NULL,              /* 8: TEXT — XPath-synthetic */
    &kDocumentVtable,  /* 9: DOCUMENT */
};

const LeptrisNodeVTable* leptris_node_vtable_for(LeptrisNodeTypeEnum type) {
    if (type < 0 || type >= LEPTRIS_NODE_TYPE_COUNT) return NULL;
    return g_node_vtables[type];
}

/* ---- per-type serialize wrappers ------------------------------------ */

static void element_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    /* serialize_element_internal takes (elem, buf, is_root) — for vtable
     * dispatch we always treat as non-root; the document-level entry
     * point (leptris_document_serialize) handles the root case directly. */
    serialize_element_internal((LeptrisElement)self, buf, 0);
}

static void text_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    serialize_text_internal((LeptrisTextNode*)self, buf);
}

static void comment_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    serialize_comment_internal((LeptrisCommentNode*)self, buf);
}

static void cdata_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    serialize_cdata_internal((LeptrisCDATANode*)self, buf);
}

static void pi_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    serialize_pi_internal((LeptrisPINode*)self, buf);
}

static void doctype_serialize(LeptrisNode* self, struct SerializeBuffer* buf) {
    serialize_doctype_internal((LeptrisDoctypeNode*)self, buf);
}

static void document_serialize_impl(LeptrisNode* self,
                                    struct SerializeBuffer* buf) {
    (void)self; (void)buf;   /* source-side only: never serialized */
}
