/* flat/flat_promote.c — FlatDoc → TaurusDocument promote pass
 * (TODO 139 Phases C + D).
 *
 * Two entry points:
 *
 *   flat_promote(FlatDoc* flat)
 *       Convenience for tests and one-shot use: builds a fresh
 *       TaurusDocument from the FlatDoc, frees the FlatDoc, returns
 *       the document. The Phase C / Phase F test suite uses this.
 *
 *   flat_promote_into(struct taurus_document* doc)
 *       Phase D lazy-promote entry: builds the compact-pointer tree
 *       into an existing doc shell that already holds flat_doc.
 *       Used by taurus_parse_string to defer the pool-alloc cost
 *       until the first call to taurus_document_root.
 *
 * Single linear walk over the FlatDoc node array (preorder DFS).
 * Parents always precede children, so a parallel mapping array
 * (flat_idx → TaurusNode*) resolves edges in one pass.
 *
 * Memory: the document owns its own writable copy of the XML buffer
 * (taken from the FlatDoc at promote time). The FlatDoc itself is
 * freed after promote_into.
 */
#include "flat_promote.h"
#include "flat_doc.h"
#include "../dom/element.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../dom/compact.h"
#include "../common/string_view.h"

#include <string.h>

extern __thread int g_taurus_strict_mode;
extern void taurus_compact_set_current_document(struct taurus_document* doc);

/* Forward decls from taurus_memory.c — avoid pulling taurus_memory.h
 * directly because it conflicts with pi.h's taurus_pi_free. */
int taurus_element_add_namespace(struct taurus_element* elem,
                                  struct taurus_namespace* ns);
struct taurus_namespace* taurus_namespace_new_pooled(const char* prefix,
                                                      const char* uri,
                                                      TaurusMemoryPool* pool);

/* TODO 141 Phase A: hot-path inliner for promote.
 *
 * Append `child` to parent's child chain in O(1). The general-purpose
 * taurus_element_append_child_internal does type validation, type
 * dispatch, and child_count maintenance that promote doesn't need
 * (we know the child type — we just created it; we know parent is
 * an element — we resolved it from the mapping array).
 *
 * The wire logic for elements vs non-elements differs only in
 * which compact-pointer setter we call. We dispatch on type
 * directly here. */
static inline void promote_wire_child(TaurusElement parent,
                                       TaurusNode* child) {
    /* Set child's parent pointer. */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        taurus_element_set_parent((TaurusElement)child, parent);
        ((TaurusElement)child)->document = parent->document;
    } else {
        switch (child->type) {
            case TAURUS_NODE_TYPE_TEXT:
                taurus_textnode_set_parent((TaurusTextNode*)child, parent);
                break;
            case TAURUS_NODE_TYPE_COMMENT:
                taurus_comment_set_parent((TaurusCommentNode*)child, parent);
                break;
            case TAURUS_NODE_TYPE_CDATA:
                taurus_cdata_set_parent((TaurusCDATANode*)child, parent);
                break;
            case TAURUS_NODE_TYPE_PI:
                taurus_pi_set_parent((TaurusPINode*)child, parent);
                break;
            default:
                break;
        }
    }

    /* Splice into parent's child chain as the new last child. Since
     * promote walks in preorder DFS, the next node we encounter at
     * any level is the next sibling — never an earlier one. */
    TaurusNode* last = taurus_elem_last_child(parent);
    if (last) {
        taurus_node_set_next_sibling(last, child);
    } else {
        taurus_elem_set_first_child(parent, child);
    }
    taurus_elem_set_last_child(parent, child);
}

/* Copy a length-bounded slice of the flat XML buffer into a heap-
 * allocated NUL-terminated string. Use for document-level fields
 * (encoding, version) that taurus_document_free releases via free(). */
static char* flat_promote_strdup(const char* xml_buffer,
                                  uint32_t offset, uint32_t len) {
    char* s = (char*)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, xml_buffer + offset, len);
    s[len] = '\0';
    return s;
}

/* Promote all FlatAttr records in [start, start+count) into
 * attributes on the given element. Returns 0 on success, -1 on
 * alloc failure.
 *
 * TODO 145: handles xmlns / xmlns:prefix declarations — moves
 * them from the regular attribute list to elem->namespaces, and
 * splits the element name on ':' for prefix:local form. Mirrors
 * what the legacy parser does inline during parse. */
static int flat_promote_attrs(FlatDoc* flat, TaurusElement elem,
                               uint32_t start, uint16_t count,
                               TaurusMemoryPool* pool,
                               const char* xml_buffer) {
    for (uint16_t i = 0; i < count; i++) {
        const FlatAttr* a = &flat->attrs[start + i];
        TaurusStringView name_view = taurus_sv_from_ptr(
            xml_buffer + a->name_offset, a->name_len);
        TaurusStringView value_view = taurus_sv_from_ptr(
            xml_buffer + a->value_offset, a->value_len);

        /* Detect xmlns declarations. */
        if (name_view.length >= 5 && name_view.data &&
            name_view.data[0] == 'x' && name_view.data[1] == 'm' &&
            name_view.data[2] == 'l' && name_view.data[3] == 'n' &&
            name_view.data[4] == 's') {
            /* Either "xmlns" (default ns) or "xmlns:prefix". */
            const char* prefix = NULL;
            if (name_view.length > 5) {
                /* Skip "xmlns:" — record the prefix portion. */
                prefix = xml_buffer + a->name_offset + 6;
                size_t prefix_len = name_view.length - 6;
                /* Pool-copy the prefix so it's NUL-terminated. */
                char* pbuf = (char*)taurus_pool_alloc(pool, prefix_len + 1);
                if (!pbuf) return -1;
                memcpy(pbuf, prefix, prefix_len);
                pbuf[prefix_len] = '\0';
                prefix = pbuf;
            }
            /* Pool-copy the URI. */
            char* uri_buf = (char*)taurus_pool_alloc(pool, value_view.length + 1);
            if (!uri_buf) return -1;
            memcpy(uri_buf, value_view.data, value_view.length);
            uri_buf[value_view.length] = '\0';

            struct taurus_namespace* ns =
                taurus_namespace_new_pooled(prefix, uri_buf, pool);
            if (ns) taurus_element_add_namespace(elem, ns);
            continue;
        }

        /* Regular attribute. */
        if (taurus_element_add_attribute(elem, name_view, value_view, pool) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Build the compact-pointer tree into the given doc shell.
 *
 * Pre: doc->flat_doc != NULL, doc->flat_promoted == 0,
 *      doc->pool == NULL (no tree built yet).
 * Post: doc->pool allocated, tree built and stored in
 *       doc->new_dom_root, doc->flat_doc freed and cleared,
 *       doc->flat_promoted == 1.
 *
 * Returns 0 on success, -1 on allocation failure (in which case the
 * caller must free the doc shell + flat_doc itself). */
static int flat_promote_build_tree(struct taurus_document* doc) {
    FlatDoc* flat = doc->flat_doc;
    if (!flat || !flat->xml_buffer) return -1;

    /* Take a writable copy of the XML buffer. The document owns this
     * copy; the FlatDoc's borrow ends when we free it below. */
    char* xml_buffer_owned = (char*)malloc(flat->xml_len + 1);
    if (!xml_buffer_owned) return -1;
    memcpy(xml_buffer_owned, flat->xml_buffer, flat->xml_len);
    xml_buffer_owned[flat->xml_len] = '\0';
    const char* xml_buffer = xml_buffer_owned;

    /* Pool page size heuristic, matching the legacy parser. */
    size_t page_size;
    if (flat->xml_len < 4096) page_size = 4096;
    else if (flat->xml_len < 65536) page_size = 16384;
    else page_size = 32768;

    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(page_size);
    if (!pool) {
        free(xml_buffer_owned);
        return -1;
    }
    if (flat->xml_len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 128);
    }

    /* Commit pool + xml_buffer to doc so taurus_document_free can
     * release them on failure past this point. */
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);
    doc->xml_buffer = xml_buffer_owned;
    doc->xml_buffer_len = flat->xml_len;
    doc->xml_buffer_needs_free = 1;

    /* Set this document as current for compact-pointer overflow
     * tracking. Overflow entries created during promote associate
     * with this doc and are released in taurus_document_free. */
    taurus_compact_set_current_document(doc);

    /* Parallel mapping: flat_idx → TaurusNode*. */
    TaurusNode** mapping = NULL;
    if (flat->node_count > 0) {
        mapping = (TaurusNode**)calloc(flat->node_count, sizeof(TaurusNode*));
        if (!mapping) return -1;
    }

    /* TODO 146 Phase 4a: bulk-allocate element nodes in one
     * pool_alloc + memset. Saves N-1 function calls and N-1
     * memset invocations for the element type (the most common).
     * Text/comment/cdata/pi stay per-element — they're rarer and
     * the savings are marginal. */
    size_t n_elem = 0;
    for (size_t i = 0; i < flat->node_count; i++) {
        if ((FlatNodeType)flat->nodes[i].type == FLAT_NODE_ELEMENT) n_elem++;
    }
    TaurusElement elem_block = NULL;
    size_t elem_idx = 0;
    if (n_elem > 0) {
        elem_block = (TaurusElement)taurus_pool_alloc(
            pool, n_elem * sizeof(struct taurus_element));
        if (!elem_block) { free(mapping); return -1; }
        memset(elem_block, 0, n_elem * sizeof(struct taurus_element));
    }

    TaurusElement root_elem = NULL;
    /* Doc-level PI list (PIs that appeared before the root element).
     * The legacy parser stores these in doc->pis; promote must do
     * the same so the serializer emits them. */
    struct taurus_processing_instruction* pis_head = NULL;
    struct taurus_processing_instruction* pis_tail = NULL;

    for (size_t i = 0; i < flat->node_count; i++) {
        const FlatNode* fn = &flat->nodes[i];

        switch ((FlatNodeType)fn->type) {
            case FLAT_NODE_ELEMENT: {
                /* TODO 145: split qualified name "prefix:local" if
                 * present. The flat parser stores the full name as
                 * one byte range; the legacy parser splits inline. */
                const char* name_start = xml_buffer + fn->name_offset;
                size_t name_len = fn->name_len;
                const char* colon = (const char*)memchr(name_start, ':', name_len);
                TaurusStringView name_view;
                TaurusStringView prefix_view = taurus_sv_empty();
                if (colon && colon > name_start) {
                    size_t prefix_len = (size_t)(colon - name_start);
                    prefix_view = taurus_sv_from_ptr(name_start, prefix_len);
                    name_view = taurus_sv_from_ptr(colon + 1,
                                                     name_len - prefix_len - 1);
                } else {
                    name_view = taurus_sv_from_ptr(name_start, name_len);
                }
                /* Phase 4a: take from pre-allocated block instead of
                 * calling taurus_element_create_with_view. The block
                 * was bulk-allocated and zeroed above. */
                TaurusElement elem = &elem_block[elem_idx++];
                elem->base.type = TAURUS_NODE_TYPE_ELEMENT;
                elem->name = taurus_sv_to_cstr_pooled(&name_view, pool);
                if (!elem->name) { free(mapping); return -1; }
                elem->document = doc;
                if (!taurus_sv_is_empty(&prefix_view)) {
                    elem->prefix = taurus_sv_to_cstr_pooled(&prefix_view, pool);
                }

                if (flat_promote_attrs(flat, elem, fn->attr_start,
                                        fn->attr_count, pool, xml_buffer) != 0) {
                    free(mapping);
                    return -1;
                }

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    promote_wire_child(parent, (TaurusNode*)elem);
                } else {
                    if (i == flat->root_index) {
                        root_elem = elem;
                    }
                }

                mapping[i] = (TaurusNode*)elem;
                break;
            }

            case FLAT_NODE_TEXT: {
                uint32_t off = flat_node_text_offset(fn);
                uint32_t len = flat_node_text_len(fn);
                TaurusTextNode* text = taurus_text_create_borrowed(
                    xml_buffer + off, len, pool);
                if (!text) { free(mapping); return -1; }

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    promote_wire_child(parent, (TaurusNode*)text);
                }
                mapping[i] = (TaurusNode*)text;
                break;
            }

            case FLAT_NODE_COMMENT: {
                uint32_t off = flat_node_text_offset(fn);
                uint32_t len = flat_node_text_len(fn);
                TaurusCommentNode* comment = taurus_comment_create(
                    xml_buffer + off, len, pool);
                if (!comment) { free(mapping); return -1; }

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    promote_wire_child(parent, (TaurusNode*)comment);
                }
                mapping[i] = (TaurusNode*)comment;
                break;
            }

            case FLAT_NODE_CDATA: {
                uint32_t off = flat_node_text_offset(fn);
                uint32_t len = flat_node_text_len(fn);
                TaurusCDATANode* cdata = taurus_cdata_create(
                    xml_buffer + off, len, pool);
                if (!cdata) { free(mapping); return -1; }

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    promote_wire_child(parent, (TaurusNode*)cdata);
                }
                mapping[i] = (TaurusNode*)cdata;
                break;
            }

            case FLAT_NODE_PI: {
                uint32_t data_off = flat_node_pi_data_offset(fn);
                uint32_t data_len = flat_node_pi_data_len(fn);
                TaurusPINode* pi = taurus_pi_create(
                    xml_buffer + fn->name_offset, fn->name_len,
                    xml_buffer + data_off, data_len, pool);
                if (!pi) { free(mapping); return -1; }

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    promote_wire_child(parent, (TaurusNode*)pi);
                } else {
                    /* Doc-level PI (appeared before root element).
                     * Build a taurus_processing_instruction node and
                     * chain it onto doc->pis so the serializer
                     * finds it. The TaurusPINode above stays orphan
                     * (not in any tree); its content is duplicated
                     * into the heap-allocated pis node. */
                    struct taurus_processing_instruction* pi_node =
                        (struct taurus_processing_instruction*)malloc(sizeof(*pi_node));
                    if (!pi_node) { free(mapping); return -1; }
                    pi_node->target = pi->target ? strdup(pi->target) : NULL;
                    pi_node->data = pi->data ? strdup(pi->data) : NULL;
                    pi_node->next = NULL;
                    if (pis_tail) pis_tail->next = pi_node;
                    else pis_head = pi_node;
                    pis_tail = pi_node;
                }
                mapping[i] = (TaurusNode*)pi;
                break;
            }

            default:
                free(mapping);
                return -1;
        }
    }

    free(mapping);

    /* XML declaration fields (heap-allocated, freed by taurus_document_free). */
    if (flat->version_len > 0) {
        doc->xml_version = flat_promote_strdup(
            xml_buffer, flat->version_offset, flat->version_len);
    }
    if (flat->encoding_len > 0) {
        doc->encoding = flat_promote_strdup(
            xml_buffer, flat->encoding_offset, flat->encoding_len);
    }
    doc->standalone      = flat->standalone;
    doc->had_declaration = (flat->version_len > 0) ? 1 : 0;
    doc->has_bom         = 0;

    doc->root = NULL;
    doc->new_dom_root = (void*)root_elem;
    doc->pis = pis_head;

    /* FlatDoc no longer needed; its borrow ends here. */
    flat_doc_free(flat);
    doc->flat_doc = NULL;
    doc->flat_promoted = 1;

    /* Match the legacy parser: freeze the tree after parse so COW
     * semantics see all parsed nodes as immutable. */
    extern void taurus_document_freeze_tree(struct taurus_document*);
    taurus_document_freeze_tree(doc);
    return 0;
}

/* Phase D entry point: build the tree into the existing doc shell.
 * Returns 0 on success, -1 on failure (doc is left in a
 * partially-built state and the caller must free it). */
int flat_promote_into(struct taurus_document* doc) {
    if (!doc || !doc->flat_doc || doc->flat_promoted) return -1;
    return flat_promote_build_tree(doc);
}

/* Phase C entry point: build a fresh TaurusDocument from a FlatDoc.
 * Used by the Phase F test suite. Frees the FlatDoc on both success
 * and failure paths. */
struct taurus_document* flat_promote(FlatDoc* flat) {
    if (!flat) return NULL;

    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(*doc));
    if (!doc) {
        flat_doc_free(flat);
        return NULL;
    }
    memset(doc, 0, sizeof(*doc));
    doc->strict_mode = g_taurus_strict_mode;
    doc->ref_count = 1;
    doc->flat_doc = flat;
    doc->flat_promoted = 0;

    if (flat_promote_build_tree(doc) != 0) {
        /* Failure: tear down whatever got built. */
        if (doc->pool) taurus_pool_destroy(doc->pool);
        free(doc->xml_buffer);
        free(doc->xml_version);
        free(doc->encoding);
        if (doc->flat_doc) flat_doc_free(doc->flat_doc);
        free(doc);
        return NULL;
    }
    return doc;
}
