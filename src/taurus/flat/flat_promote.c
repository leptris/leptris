/* flat/flat_promote.c — FlatDoc → TaurusDocument promote pass (TODO 139 Phase C).
 *
 * Single linear walk over the FlatDoc node array. For each FlatNode,
 * allocate the corresponding pool-owned TaurusNode and wire it into
 * the tree using a parallel mapping array.
 *
 * Why a mapping array (vs. recursion): the FlatDoc stores tree
 * edges as int32 indices. We need to resolve each index to a
 * pointer when wiring edges. Doing this in a single preorder walk
 * works because parents always appear before children in the flat
 * array (preorder DFS is how the parser built it). So when we
 * reach FlatNode[i], its parent FlatNode[parent] has already been
 * promoted, and mapping[parent] holds the TaurusElement*.
 *
 * Memory: the document gets its own writable copy of the XML
 * buffer (the legacy parser path mutates the buffer in-place for
 * NUL termination; we follow the same convention so consumers
 * that hold a TaurusElement see consistent string lifetimes).
 */
#include "flat_promote.h"
#include "flat_doc.h"
#include "../dom/element.h"
#include "../dom/text.h"
#include "../dom/comment.h"
#include "../dom/cdata.h"
#include "../dom/pi.h"
#include "../common/string_view.h"

#include <string.h>

/* Globals from core.c (already declared extern in taurus_internal.h
 * via the path taurus_internal.h → taurus_parse.h chain, but the
 * promote pass is intentionally minimal — re-declare here). */
extern __thread int g_taurus_strict_mode;
extern void taurus_compact_set_current_document(struct taurus_document* doc);

/* Allocate and zero-initialize a taurus_document with a pool
 * pre-sized for the FlatDoc's contents. Returns NULL on OOM.
 * Caller must populate root, xml_buffer, etc. */
static struct taurus_document* flat_promote_new_doc(FlatDoc* flat,
                                                     TaurusMemoryPool* pool,
                                                     char* xml_buffer_owned) {
    struct taurus_document* doc = (struct taurus_document*)malloc(sizeof(*doc));
    if (!doc) return NULL;
    memset(doc, 0, sizeof(*doc));
    doc->strict_mode = g_taurus_strict_mode;
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);
    doc->ref_count = 1;
    doc->xml_buffer = xml_buffer_owned;
    doc->xml_buffer_len = flat->xml_len;
    doc->xml_buffer_needs_free = 1;
    return doc;
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
 * alloc failure. */
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
        if (taurus_element_add_attribute(elem, name_view, value_view, pool) != 0) {
            return -1;
        }
    }
    return 0;
}

struct taurus_document* flat_promote(FlatDoc* flat) {
    if (!flat || !flat->xml_buffer) goto fail_no_doc;

    /* Take a writable copy of the XML buffer. The document owns
     * this copy; the FlatDoc's borrow ends when we free it below. */
    char* xml_buffer_owned = (char*)malloc(flat->xml_len + 1);
    if (!xml_buffer_owned) goto fail_no_doc;
    memcpy(xml_buffer_owned, flat->xml_buffer, flat->xml_len);
    xml_buffer_owned[flat->xml_len] = '\0';
    const char* xml_buffer = xml_buffer_owned;  /* read-only alias */

    /* Pool page size heuristic, matching the legacy parser. */
    size_t page_size;
    if (flat->xml_len < 4096) page_size = 4096;
    else if (flat->xml_len < 65536) page_size = 16384;
    else page_size = 32768;

    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(page_size);
    if (!pool) {
        free(xml_buffer_owned);
        goto fail_no_doc;
    }

    if (flat->xml_len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 128);
    }

    struct taurus_document* doc = flat_promote_new_doc(flat, pool, xml_buffer_owned);
    if (!doc) {
        taurus_pool_destroy(pool);
        free(xml_buffer_owned);
        goto fail_no_doc;
    }

    /* Set this document as current for compact-pointer overflow
     * tracking. All overflow entries created during promote
     * associate with this doc and are released in
     * taurus_document_free. */
    taurus_compact_set_current_document(doc);

    /* Parallel mapping: flat_idx → TaurusNode*. Used to resolve
     * parent / sibling edges during the walk. */
    TaurusNode** mapping = NULL;
    if (flat->node_count > 0) {
        mapping = (TaurusNode**)calloc(flat->node_count, sizeof(TaurusNode*));
        if (!mapping) goto fail;
    }

    TaurusElement root_elem = NULL;

    for (size_t i = 0; i < flat->node_count; i++) {
        const FlatNode* fn = &flat->nodes[i];

        switch ((FlatNodeType)fn->type) {
            case FLAT_NODE_ELEMENT: {
                TaurusStringView name_view = taurus_sv_from_ptr(
                    xml_buffer + fn->name_offset, fn->name_len);
                TaurusElement elem = taurus_element_create_with_view(name_view, pool);
                if (!elem) goto fail;
                elem->document = doc;

                if (flat_promote_attrs(flat, elem, fn->attr_start,
                                        fn->attr_count, pool, xml_buffer) != 0) {
                    goto fail;
                }

                /* Wire into parent. */
                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    taurus_element_append_child_internal(parent,
                                                          (TaurusNode*)elem);
                } else {
                    /* Top-level element. Should match doc->root_index. */
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
                if (!text) goto fail;

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    taurus_element_append_child_internal(parent,
                                                          (TaurusNode*)text);
                }
                mapping[i] = (TaurusNode*)text;
                break;
            }

            case FLAT_NODE_COMMENT: {
                uint32_t off = flat_node_text_offset(fn);
                uint32_t len = flat_node_text_len(fn);
                TaurusCommentNode* comment = taurus_comment_create(
                    xml_buffer + off, len, pool);
                if (!comment) goto fail;

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    taurus_element_append_child_internal(parent,
                                                          (TaurusNode*)comment);
                }
                mapping[i] = (TaurusNode*)comment;
                break;
            }

            case FLAT_NODE_CDATA: {
                uint32_t off = flat_node_text_offset(fn);
                uint32_t len = flat_node_text_len(fn);
                TaurusCDATANode* cdata = taurus_cdata_create(
                    xml_buffer + off, len, pool);
                if (!cdata) goto fail;

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    taurus_element_append_child_internal(parent,
                                                          (TaurusNode*)cdata);
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
                if (!pi) goto fail;

                if (fn->parent != FLAT_INDEX_NULL) {
                    TaurusElement parent =
                        (TaurusElement)mapping[fn->parent];
                    taurus_element_append_child_internal(parent,
                                                          (TaurusNode*)pi);
                }
                mapping[i] = (TaurusNode*)pi;
                break;
            }

            default:
                /* Unknown node type — should not happen with a
                 * FlatDoc produced by flat_parse(). */
                goto fail;
        }
    }

    /* XML declaration fields. These are heap-allocated (not pool)
     * because taurus_document_free releases them via free(). */
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
    doc->has_bom         = 0;  /* flat_parse strips BOM */

    doc->root = NULL;
    doc->new_dom_root = (void*)root_elem;

    free(mapping);
    flat_doc_free(flat);
    return doc;

fail:
    free(mapping);
    /* Tear down: destroy pool (frees all nodes), free xml buffer,
     * free doc struct, clear current document pointer. */
    taurus_compact_set_current_document(NULL);
    if (doc) {
        if (doc->pool) taurus_pool_destroy(doc->pool);
        free(doc->xml_buffer);
        free(doc);
    }
fail_no_doc:
    if (flat) flat_doc_free(flat);
    return NULL;
}
