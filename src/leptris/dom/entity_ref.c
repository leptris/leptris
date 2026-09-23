/* src/dom/entity_ref.c - Entity reference node implementation (#1094) */

#include "entity_ref.h"
#include "element.h"
#include "../memory/pool.h"
#include "../dtd/model.h"
#include "../../src/include/leptris/dtd.h"
#include <string.h>

LeptrisEntityRefNode* leptris_entity_ref_create(const char* name,
                                                size_t name_len,
                                                LeptrisMemoryPool* pool) {
    if (!name || name_len == 0 || !pool) return NULL;

    char* name_storage;
    LeptrisEntityRefNode* node =
        (LeptrisEntityRefNode*)leptris_pool_alloc_node_with_content(
            pool, sizeof(LeptrisEntityRefNode), name_len, &name_storage);
    if (!node) return NULL;

    memcpy(name_storage, name, name_len);
    name_storage[name_len] = '\0';

    node->base.type = LEPTRIS_NODE_TYPE_ENTITY_REF;
    node->base.frozen = 0;
    node->base.version = 0;
    node->parent_off = 0;
    node->next_sibling_off = 0;
    node->owner_doc = NULL;
    node->name = name_storage;
    return node;
}

const char* leptris_entity_ref_get_name(LeptrisEntityRefNode* ref) {
    return ref ? ref->name : NULL;
}

const char* leptris_entity_ref_resolve(LeptrisEntityRefNode* ref) {
    if (!ref || !ref->name) return NULL;
    /* Predefined entities first — they exist with or without a DTD. */
    if (strcmp(ref->name, "amp") == 0) return "&";
    if (strcmp(ref->name, "lt") == 0) return "<";
    if (strcmp(ref->name, "gt") == 0) return ">";
    if (strcmp(ref->name, "apos") == 0) return "'";
    if (strcmp(ref->name, "quot") == 0) return "\"";
    /* Then the owning document's DTD entity table. */
    struct leptris_document* doc = ref->owner_doc;
    if (!doc) {
        LeptrisElement parent = leptris_entity_ref_parent(ref);
        if (parent) doc = leptris_element_get_document(parent);
    }
    if (!doc) return NULL;
    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    if (!dtd) return NULL;
    return leptris_dtd_lookup_entity(dtd, ref->name);
}
