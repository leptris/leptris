/* dom/node_public.c — Public TaurusNodeRef API.
 *
 * Extracted from taurus.c (TODO 42 phase 2). These are the public-facing
 * wrappers around the node-navigation helpers in dom/node.c.
 */

#include "../include/taurus.h"
#include "../taurus_internal.h"
#include "element.h"
#include "node.h"
#include "text.h"
#include "comment.h"
#include "cdata.h"
#include "pi.h"

TAURUS_API int taurus_node_get_type(TaurusNodeRef node) {
    if (!node) return 0; /* TAURUS_NODE_TYPE_ELEMENT */
    return (int)node->type;
}

TAURUS_API TaurusNodeRef taurus_node_first_child(TaurusNodeRef node) {
    /* Use the internal accessor that returns ANY child type (text,
     * element, CDATA, etc.) — the public taurus_element_get_first_child
     * would skip non-element children and break node-level traversal. */
    return (TaurusNodeRef)taurus_node_first_child_internal((TaurusNode*)node);
}

TAURUS_API TaurusNodeRef taurus_node_last_child(TaurusNodeRef node) {
    return (TaurusNodeRef)taurus_node_last_child_internal((TaurusNode*)node);
}

TAURUS_API TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    return (TaurusNodeRef)taurus_node_get_next_sibling(node);
}

TAURUS_API TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
        TaurusElement elem = (TaurusElement)node;
        TaurusElement parent = taurus_element_get_parent(elem);
        if (!parent) return NULL;

        TaurusNodeRef prev = NULL;
        TaurusNodeRef child = (TaurusNodeRef)taurus_elem_first_child(parent);
        while (child && child != node) {
            prev = child;
            child = (TaurusNodeRef)taurus_node_get_next_sibling(child);
        }
        return prev;
    }
    return NULL;
}

TAURUS_API size_t taurus_node_child_count(TaurusNodeRef node) {
    if (!node) return 0;
    return taurus_node_child_count_internal(node);
}

TAURUS_API TaurusElement taurus_node_as_element(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;
    return (TaurusElement)node;
}

TAURUS_API TaurusNodeRef taurus_element_as_node(TaurusElement elem) {
    return (TaurusNodeRef)elem;
}

TAURUS_API const char* taurus_text_node_get_content(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_TEXT) {
        return taurus_text_get_content((TaurusTextNode*)node);
    }
    if (node->type == TAURUS_NODE_TYPE_CDATA) {
        return ((TaurusCDATANode*)node)->content;
    }
    return NULL;
}

TAURUS_API const char* taurus_comment_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_COMMENT) return NULL;
    return ((TaurusCommentNode*)node)->content;
}

TAURUS_API const char* taurus_cdata_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_CDATA) return NULL;
    return ((TaurusCDATANode*)node)->content;
}

TAURUS_API const char* taurus_pi_node_get_target(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->target;
}

TAURUS_API const char* taurus_pi_node_get_data(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->data;
}
