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

    /* Issue #213: maintain child_count for element children, matching
     * the convention used by taurus_element_append_child_internal. */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        parent->child_count++;
    }
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

/* Inline attribute allocation helper. Mirrors direct_parse's
 * dp_add_attr_inline: takes the next slot off a pre-allocated
 * block, zero-copies name and value (both already NUL-terminated
 * in xml_buffer by the pre-pass above), computes the FNV hash
 * inline, and wires the attr-list offsets directly. Skips the
 * name interning + value pool_strdup + entity memchr that
 * taurus_element_add_attribute does per call.
 *
 * Fallbacks to per-attr pool_alloc if attr_idx >= attr_capacity
 * (the heuristic underestimated; rare in practice). */
static int promote_add_attr_inline(TaurusElement elem,
                                    struct taurus_attribute* attr_block,
                                    size_t* attr_idx, size_t attr_capacity,
                                    char* name, size_t name_len,
                                    char* val, size_t val_len,
                                    TaurusMemoryPool* pool) {
    struct taurus_attribute* attr;
    if (*attr_idx < attr_capacity) {
        attr = &attr_block[(*attr_idx)++];
    } else {
        attr = (struct taurus_attribute*)taurus_pool_alloc(
            pool, sizeof(struct taurus_attribute));
        if (!attr) return -1;
    }

    attr->name_view = taurus_sv_from_ptr(name, name_len);
    attr->value_view = taurus_sv_from_ptr(val, val_len);
    attr->prefix_view = taurus_sv_empty();
    attr->namespace_uri_view = taurus_sv_empty();
    attr->name = name;
    /* Leave value NULL for entity-containing attrs so the accessor
     * expands lazily. Non-entity values are zero-copy. */
    if (val_len > 0 && memchr(val, '&', val_len) != NULL) {
        attr->value = NULL;
        attr->has_entities = 1;
    } else {
        attr->value = val;
        attr->has_entities = 0;
    }
    attr->prefix = NULL;
    attr->namespace_uri = NULL;
    attr->next = NULL;

    uint32_t h = 2166136261u;
    for (size_t i = 0; i < name_len; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619u;
    }
    attr->name_hash = h;

    struct taurus_attribute* last = taurus_elem_last_attribute(elem);
    if (last) {
        last->next = attr;
    } else {
        taurus_elem_set_first_attribute(elem, attr);
    }
    taurus_elem_set_last_attribute(elem, attr);
    elem->attr_count++;
    return 0;
}

/* Promote all FlatAttr records in [start, start+count) into
 * attributes on the given element. Returns 0 on success, -1 on
 * alloc failure.
 *
 * TODO 145: handles xmlns / xmlns:prefix declarations — moves
 * them from the regular attribute list to elem->namespaces, and
 * splits the element name on ':' for prefix:local form. Mirrors
 * what the legacy parser does inline during parse.
 *
 * TODO 148 Phase 7: bulk-allocates attr structs from the shared
 * attr_block (sized from flat->attr_count) instead of calling
 * taurus_element_add_attribute per attr. Inline add skips name
 * interning + value pool_strdup + entity memchr. */
static int flat_promote_attrs(FlatDoc* flat, TaurusElement elem,
                               uint32_t start, uint16_t count,
                               TaurusMemoryPool* pool,
                               char* xml_buffer,
                               struct taurus_attribute* attr_block,
                               size_t* attr_idx, size_t attr_capacity) {
    for (uint16_t i = 0; i < count; i++) {
        const FlatAttr* a = &flat->attrs[start + i];
        char* name = xml_buffer + a->name_offset;
        size_t name_len = a->name_len;
        char* val = xml_buffer + a->value_offset;
        size_t val_len = a->value_len;

        /* Detect xmlns declarations. */
        if (name_len >= 5 &&
            name[0] == 'x' && name[1] == 'm' && name[2] == 'l' &&
            name[3] == 'n' && name[4] == 's') {
            /* Either "xmlns" (default ns) or "xmlns:prefix".
             * Zero-copy: pointers into the already-NUL-terminated
             * xml_buffer. No pool_strdup needed. */
            const char* prefix = NULL;
            if (name_len > 5) {
                /* "xmlns:foo" — NUL-terminate at ':' and read
                 * prefix from +6. The pre-pass above already
                 * NUL-terminated at name_offset + name_len; we
                 * additionally punch a NUL at the colon. */
                xml_buffer[a->name_offset + 5] = '\0';
                prefix = xml_buffer + a->name_offset + 6;
            }

            struct taurus_namespace* ns =
                (struct taurus_namespace*)taurus_pool_alloc(
                    pool, sizeof(struct taurus_namespace));
            if (!ns) return -1;
            ns->prefix = (char*)prefix;
            ns->uri = val;
            ns->next = NULL;
            taurus_element_add_namespace(elem, ns);
            continue;
        }

        /* Regular attribute. Name and value are already NUL-terminated
         * in xml_buffer by the pre-pass; zero-copy pointers + inline
         * alloc from the bulk block. */
        if (promote_add_attr_inline(elem, attr_block, attr_idx,
                                     attr_capacity,
                                     name, name_len, val, val_len,
                                     pool) != 0) {
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
    char* xml_buffer = xml_buffer_owned;

    /* pugixml trick: NUL-terminate every name and value in-place in
     * the buffer copy. Then element/attr names become zero-copy
     * pointers — no pool_strdup, no string interning hash lookups.
     *
     * Safety: each name/value in XML is followed by a delimiter byte
     * (space, '>', '=', quote, etc.) that's never part of any name.
     * Writing NUL there is safe because the flat parser has already
     * extracted all offsets. */
    for (size_t i = 0; i < flat->node_count; i++) {
        const FlatNode* fn = &flat->nodes[i];
        FlatNodeType ft = (FlatNodeType)fn->type;
        if (fn->name_len > 0 && fn->name_offset + fn->name_len < flat->xml_len) {
            xml_buffer_owned[fn->name_offset + fn->name_len] = '\0';
        }
        if (ft == FLAT_NODE_TEXT || ft == FLAT_NODE_COMMENT ||
            ft == FLAT_NODE_CDATA) {
            uint32_t to = flat_node_text_offset(fn);
            uint32_t tl = flat_node_text_len(fn);
            if (tl > 0 && to + tl < flat->xml_len) {
                xml_buffer_owned[to + tl] = '\0';
            }
        }
        if (ft == FLAT_NODE_PI) {
            uint32_t dl = flat_node_pi_data_len(fn);
            uint32_t dof = flat_node_pi_data_offset(fn);
            if (dl > 0 && dof + dl < flat->xml_len) {
                xml_buffer_owned[dof + dl] = '\0';
            }
        }
    }
    for (size_t i = 0; i < flat->attr_count; i++) {
        const FlatAttr* a = &flat->attrs[i];
        if (a->name_len > 0 && a->name_offset + a->name_len < flat->xml_len) {
            xml_buffer_owned[a->name_offset + a->name_len] = '\0';
        }
        if (a->value_len > 0 && a->value_offset + a->value_len < flat->xml_len) {
            xml_buffer_owned[a->value_offset + a->value_len] = '\0';
        }
    }

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

    /* TODO 148 Phase 7: bulk-allocate attribute structs from
     * flat->attr_count. Each non-xmlns attr takes the next slot
     * off the block (bump pointer). xmlns attrs go via the
     * namespace path, not the regular attr list, so the block
     * is sized for the worst case but a few slots go unused when
     * the doc has xmlns declarations. Per-attr pool_alloc
     * fallback handles the rare overflow. */
    struct taurus_attribute* attr_block = NULL;
    size_t attr_idx = 0;
    size_t attr_capacity = flat->attr_count;
    if (attr_capacity > 0) {
        attr_block = (struct taurus_attribute*)taurus_pool_alloc(
            pool, attr_capacity * sizeof(struct taurus_attribute));
        if (!attr_block) { free(mapping); return -1; }
        /* No memset — promote_add_attr_inline initializes every field. */
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
                elem->base.line = fn->line;
                /* Zero-copy name: points directly into xml_buffer
                 * (already NUL-terminated above). No pool_strdup,
                 * no hash interning. */
                elem->name = (char*)(xml_buffer + fn->name_offset);
                /* If name has a colon, split into prefix + local. */
                if (colon && colon > name_start) {
                    /* NUL-terminate at the colon position. */
                    xml_buffer_owned[fn->name_offset +
                        (size_t)(colon - name_start)] = '\0';
                    taurus_elem_set_prefix(elem, (char*)(name_start), pool);
                    elem->name = (char*)(colon + 1);
                }
                if (!elem->name) { free(mapping); return -1; }
                elem->document = doc;

                if (flat_promote_attrs(flat, elem, fn->attr_start,
                                        fn->attr_count, pool, xml_buffer,
                                        attr_block, &attr_idx,
                                        attr_capacity) != 0) {
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
                text->base.line = fn->line;

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
                comment->base.line = fn->line;

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
                cdata->base.line = fn->line;

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
                pi->base.line = fn->line;

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
