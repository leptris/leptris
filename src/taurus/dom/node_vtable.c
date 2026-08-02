/* lib/src/dom/node_vtable.c — Node vtable registry (TODO 29 phase 3).
 *
 * Defines one TaurusNodeVTable per node type and exposes a lookup
 * function.  Dispatch sites (serializer, future free path) call
 * taurus_node_vtable_for(type) to get the per-type operations.
 *
 * Why a registry array (not a per-node pointer)?
 *  - Preserves the compact 4-byte TaurusNode layout that the
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
/* Include serialize.h for the full SerializeBuffer type — TODO 43. */
#include "../serialize/serialize.h"

/* Forward declarations for per-type serialize wrappers.  Each wraps
 * the existing serialize_*_internal function with the vtable signature. */
static void element_serialize (TaurusNode* self, struct SerializeBuffer* buf);
static void text_serialize    (TaurusNode* self, struct SerializeBuffer* buf);
static void comment_serialize (TaurusNode* self, struct SerializeBuffer* buf);
static void cdata_serialize   (TaurusNode* self, struct SerializeBuffer* buf);
static void pi_serialize      (TaurusNode* self, struct SerializeBuffer* buf);
static void doctype_serialize (TaurusNode* self, struct SerializeBuffer* buf);

static const TaurusNodeVTable kElementVtable = {
    .serialize  = element_serialize,
    .type_name  = "element",
    .type_enum  = TAURUS_NODE_TYPE_ELEMENT,
};
static const TaurusNodeVTable kTextVtable = {
    .serialize  = text_serialize,
    .type_name  = "text",
    .type_enum  = TAURUS_NODE_TYPE_TEXT,
};
static const TaurusNodeVTable kCommentVtable = {
    .serialize  = comment_serialize,
    .type_name  = "comment",
    .type_enum  = TAURUS_NODE_TYPE_COMMENT,
};
static const TaurusNodeVTable kCdataVtable = {
    .serialize  = cdata_serialize,
    .type_name  = "cdata",
    .type_enum  = TAURUS_NODE_TYPE_CDATA,
};
static const TaurusNodeVTable kPiVtable = {
    .serialize  = pi_serialize,
    .type_name  = "pi",
    .type_enum  = TAURUS_NODE_TYPE_PI,
};
static const TaurusNodeVTable kDoctypeVtable = {
    .serialize  = doctype_serialize,
    .type_name  = "doctype",
    .type_enum  = TAURUS_NODE_TYPE_DOCTYPE,
};

/* Indexed by TaurusNodeTypeEnum.  NULL slots = no vtable (e.g.,
 * TAURUS_NODE_TYPE_ATTRIBUTE — XPath-internal, not serialized). */
static const TaurusNodeVTable* const g_node_vtables[TAURUS_NODE_TYPE_COUNT] = {
    &kElementVtable,   /* 0 */
    &kTextVtable,      /* 1 */
    &kCommentVtable,   /* 2 */
    &kCdataVtable,     /* 3 */
    &kPiVtable,        /* 4 */
    &kDoctypeVtable,   /* 5 */
    NULL,              /* 6: ATTRIBUTE — XPath-internal */
};

const TaurusNodeVTable* taurus_node_vtable_for(TaurusNodeTypeEnum type) {
    if (type < 0 || type >= TAURUS_NODE_TYPE_COUNT) return NULL;
    return g_node_vtables[type];
}

/* ---- per-type serialize wrappers ------------------------------------ */

static void element_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    /* serialize_element_internal takes (elem, buf, is_root) — for vtable
     * dispatch we always treat as non-root; the document-level entry
     * point (taurus_document_serialize) handles the root case directly. */
    serialize_element_internal((TaurusElement)self, buf, 0);
}

static void text_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    serialize_text_internal((TaurusTextNode*)self, buf);
}

static void comment_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    serialize_comment_internal((TaurusCommentNode*)self, buf);
}

static void cdata_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    serialize_cdata_internal((TaurusCDATANode*)self, buf);
}

static void pi_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    serialize_pi_internal((TaurusPINode*)self, buf);
}

static void doctype_serialize(TaurusNode* self, struct SerializeBuffer* buf) {
    serialize_doctype_internal((TaurusDoctypeNode*)self, buf);
}
