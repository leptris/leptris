/* flat/flat_fast.c — Flat-mode query fast paths (TODO 139 Phase E).
 *
 * See flat_fast.h for the architectural rationale. These helpers
 * walk the FlatDoc arrays directly to answer simple queries without
 * triggering the promote pass.
 */
#include "flat_fast.h"
#include "flat_doc.h"

#include <string.h>

size_t flat_fast_count_elements_named(struct taurus_document* doc,
                                       const char* name) {
    if (!doc || !doc->flat_doc || doc->flat_promoted) return 0;
    if (!name) return 0;

    FlatDoc* flat = doc->flat_doc;
    const char* xml = flat->xml_buffer;
    size_t name_len = strlen(name);
    size_t count = 0;

    for (size_t i = 0; i < flat->node_count; i++) {
        const FlatNode* n = &flat->nodes[i];
        if ((FlatNodeType)n->type != FLAT_NODE_ELEMENT) continue;
        if (n->name_len != name_len) continue;
        if (memcmp(xml + n->name_offset, name, name_len) == 0) {
            count++;
        }
    }
    return count;
}

size_t flat_fast_count_elements_all(struct taurus_document* doc) {
    if (!doc || !doc->flat_doc || doc->flat_promoted) return 0;

    FlatDoc* flat = doc->flat_doc;
    size_t count = 0;
    for (size_t i = 0; i < flat->node_count; i++) {
        if ((FlatNodeType)flat->nodes[i].type == FLAT_NODE_ELEMENT) {
            count++;
        }
    }
    return count;
}

const char* flat_fast_root_name(struct taurus_document* doc) {
    if (!doc || !doc->flat_doc || doc->flat_promoted) return NULL;
    FlatDoc* flat = doc->flat_doc;
    if (flat->root_index == FLAT_INDEX_NULL) return NULL;
    /* NOTE: returns a non-NUL-terminated view! Caller must copy if
     * they need a C string. We expose length via flat->nodes. */
    return flat->xml_buffer + flat->nodes[flat->root_index].name_offset;
}
