/* flat/flat_doc.c — FlatDoc allocator implementation (TODO 139 Phase A).
 *
 * See flat_doc.h for the architectural rationale. This file is just
 * the array-append primitives and lifecycle bookkeeping.
 *
 * All allocations use malloc/realloc/free directly — NOT the pool
 * allocator. The pool allocator is for compact-pointer TaurusElement
 * nodes, which are 96 bytes and benefit from arena reuse. FlatDoc
 * arrays are large contiguous blocks that benefit from realloc in
 * place when the heuristic underestimates.
 */
#include "flat_doc.h"

/* Capacity heuristic. Approximately 1 node per 100 bytes of XML and
 * 2 attrs per node. Conservative: real XML averages closer to 1 node
 * per 80 bytes for verbose docs (config files) and 1 per 200 bytes
 * for data-shaped docs. We pick 100 as the middle ground; growth
 * handles underestimates cheaply. */
static size_t flat_doc_initial_node_capacity(size_t xml_len) {
    size_t est = xml_len / 100;
    if (est < 16) est = 16;          /* small doc floor */
    if (est > 1u << 20) est = 1u << 20; /* 1M-node ceiling; grow if exceeded */
    return est;
}

static size_t flat_doc_initial_attr_capacity(size_t xml_len) {
    size_t est = (xml_len / 100) * 2;
    if (est < 16) est = 16;
    if (est > 1u << 20) est = 1u << 20;
    return est;
}

/* Geometric growth factor. 1.5x is the usual sweet spot — it gives
 * amortized O(1) appends while bounding wasted memory to ~50%. */
static int flat_doc_grow_nodes(FlatDoc* doc) {
    size_t new_cap = doc->node_capacity + (doc->node_capacity >> 1);
    if (new_cap <= doc->node_capacity) return -1; /* overflow */
    FlatNode* new_nodes = (FlatNode*)realloc(doc->nodes,
                                              new_cap * sizeof(FlatNode));
    if (!new_nodes) return -1;
    doc->nodes = new_nodes;
    doc->node_capacity = new_cap;
    return 0;
}

static int flat_doc_grow_attrs(FlatDoc* doc) {
    size_t new_cap = doc->attr_capacity + (doc->attr_capacity >> 1);
    if (new_cap <= doc->attr_capacity) return -1;
    FlatAttr* new_attrs = (FlatAttr*)realloc(doc->attrs,
                                              new_cap * sizeof(FlatAttr));
    if (!new_attrs) return -1;
    doc->attrs = new_attrs;
    doc->attr_capacity = new_cap;
    return 0;
}

FlatDoc* flat_doc_new(const char* xml_buffer, size_t xml_len) {
    FlatDoc* doc = (FlatDoc*)malloc(sizeof(FlatDoc));
    if (!doc) return NULL;

    doc->node_count = 0;
    doc->node_capacity = flat_doc_initial_node_capacity(xml_len);
    doc->nodes = (FlatNode*)malloc(doc->node_capacity * sizeof(FlatNode));
    if (!doc->nodes) {
        free(doc);
        return NULL;
    }

    doc->attr_count = 0;
    doc->attr_capacity = flat_doc_initial_attr_capacity(xml_len);
    doc->attrs = (FlatAttr*)malloc(doc->attr_capacity * sizeof(FlatAttr));
    if (!doc->attrs) {
        free(doc->nodes);
        free(doc);
        return NULL;
    }

    doc->xml_buffer       = xml_buffer;
    doc->xml_buffer_owned = NULL;
    doc->xml_len          = xml_len;

    doc->version_offset = 0;
    doc->version_len    = 0;
    doc->encoding_offset = 0;
    doc->encoding_len   = 0;
    doc->standalone     = -1;
    doc->root_index     = FLAT_INDEX_NULL;
    return doc;
}

void flat_doc_free(FlatDoc* doc) {
    if (!doc) return;
    free(doc->nodes);
    free(doc->attrs);
    free(doc->xml_buffer_owned);
    free(doc);
}

uint32_t flat_doc_append_node(FlatDoc* doc, FlatNodeType type,
                              uint32_t name_offset, uint16_t name_len) {
    if (!doc) return FLAT_INDEX_NULL;
    if (doc->node_count == doc->node_capacity) {
        if (flat_doc_grow_nodes(doc) != 0) return FLAT_INDEX_NULL;
    }
    uint32_t idx = (uint32_t)doc->node_count;
    FlatNode* n = &doc->nodes[idx];
    n->type         = (uint16_t)type;
    n->name_len     = name_len;
    n->name_offset  = name_offset;
    n->parent       = FLAT_INDEX_NULL;
    n->first_child  = FLAT_INDEX_NULL;
    n->next_sibling = FLAT_INDEX_NULL;
    n->attr_start   = FLAT_INDEX_NULL;
    n->attr_count   = 0;
    n->depth        = 0;
    doc->node_count++;
    return idx;
}

uint32_t flat_doc_append_attr(FlatDoc* doc,
                              uint32_t name_offset, uint16_t name_len,
                              uint32_t value_offset, uint16_t value_len) {
    if (!doc) return FLAT_INDEX_NULL;
    if (doc->attr_count == doc->attr_capacity) {
        if (flat_doc_grow_attrs(doc) != 0) return FLAT_INDEX_NULL;
    }
    uint32_t idx = (uint32_t)doc->attr_count;
    FlatAttr* a = &doc->attrs[idx];
    a->name_offset  = name_offset;
    a->value_offset = value_offset;
    a->name_len     = name_len;
    a->value_len    = value_len;
    doc->attr_count++;
    return idx;
}

int flat_doc_dup_xml(FlatDoc* doc) {
    if (!doc) return -1;
    if (doc->xml_buffer_owned) return 0; /* already owned */

    char* copy = (char*)malloc(doc->xml_len + 1);
    if (!copy) return -1;
    if (doc->xml_buffer && doc->xml_len > 0) {
        memcpy(copy, doc->xml_buffer, doc->xml_len);
    }
    copy[doc->xml_len] = '\0';

    /* Fix up the borrow pointer to point into our copy. Offsets in
     * FlatNode/FlatAttr are byte offsets from the start of the XML
     * buffer, so swapping the base pointer preserves them. */
    doc->xml_buffer_owned = copy;
    doc->xml_buffer       = copy;
    return 0;
}
