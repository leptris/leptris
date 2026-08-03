/* dtd/validator.c — DTD validation engine.
 *
 * Phase 1 (this file): EMPTY content model + unknown-element detection.
 * Walks the document tree, finds each element's declaration in the DTD,
 * and checks the simplest invariants:
 *   - EMPTY elements must have no element children.
 *   - Elements with declarations should have content matching the model
 *     (Phase 1 only enforces EMPTY; mixed/children models need the
 *     grammar matcher that's Phase 3+).
 *
 * Return codes:
 *   1 = document is valid (no violations found).
 *   0 = at least one violation found; `error` is populated with the first.
 *  -1 = internal error (couldn't run validation).
 *
 * Future phases (TODO 91):
 *   2: ATTLIST parsing + #REQUIRED enforcement.
 *   3: Element-content grammar matcher (sequences, choices, modifiers).
 *   4: Attribute type validation (ID, IDREF, NMTOKEN, enumerated).
 *   5: ENTITY-typed attribute resolution.
 */

#include "../../include/taurus.h"
#include "../../include/taurus/dtd.h"
#include "model.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward decl from content_check.c (Phase 4 of TODO 91). */
int taurus_content_model_match(const char* model, const char* elem_name,
                                const char** child_names, size_t child_count,
                                char* out_msg, size_t msg_size);

static char* dup_str(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1);
    if (out) {
        memcpy(out, src, len + 1);
    }
    return out;
}

static void set_error(TaurusDTDError* error, const char* msg, const char* elem_name) {
    if (!error) return;
    error->message = dup_str(msg);
    error->element_name = elem_name ? dup_str(elem_name) : NULL;
    error->line = 0;
    error->column = 0;
}

/* Phase 5: hash table for tracking ID values across the document. */
typedef struct {
    StringHashTable* ids;  /* maps id value → element name */
    TaurusDTDError* error;
    int found_violation;
} IdCheckContext;

/* IDs need to be tracked in a document-level table, not per-element.
 * The walker accumulates them as it visits each element with an
 * ID-typed attribute. */

static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error,
                                       StringHashTable* id_table);

/* Return 1 if this element has any element-type children, 0 otherwise.
 * Used to validate <!ELEMENT name EMPTY> — no element children allowed. */
static int element_has_element_children(TaurusElement elem) {
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)elem);
    while (child) {
        if (taurus_node_get_type(child) == TAURUS_NODE_TYPE_ELEMENT) {
            return 1;
        }
        child = taurus_node_next_sibling(child);
    }
    return 0;
}

/* Phase 3: check #REQUIRED ATTLIST attributes by iterating the DTD's
 * attribute hash table. The iterator context carries the element under
 * validation and the error struct so we can short-circuit on first
 * violation. */
typedef struct {
    TaurusElement elem;
    const char* elem_name;
    size_t elem_name_len;
    TaurusDTDError* error;
    int found_violation;
} AttrCheckContext;

static int attr_check_iter(const char* key, size_t key_len,
                           void* value, void* user_data) {
    AttrCheckContext* ctx = (AttrCheckContext*)user_data;
    DTDAttributeDecl* decl = (DTDAttributeDecl*)value;

    /* Hash key is "element.attr"; does this entry belong to our element? */
    if (key_len <= ctx->elem_name_len + 1) return 1;  /* continue */
    if (memcmp(key, ctx->elem_name, ctx->elem_name_len) != 0) return 1;
    if (key[ctx->elem_name_len] != '.') return 1;

    /* This declaration belongs to the element. If #REQUIRED and the
     * attribute is missing from the document, that's a violation. */
    if (decl->default_type == DTD_ATTR_REQUIRED) {
        const char* attr_name = key + ctx->elem_name_len + 1;
        size_t attr_name_len = key_len - ctx->elem_name_len - 1;
        /* Build a temporary null-terminated name so the lookup helper
         * can use it. Pool the alloc to avoid OOM bookkeeping. */
        char attr_buf[256];
        if (attr_name_len < sizeof(attr_buf)) {
            memcpy(attr_buf, attr_name, attr_name_len);
            attr_buf[attr_name_len] = '\0';
            struct taurus_attribute* present =
                taurus_element_get_attribute_by_name(ctx->elem, attr_buf);
            if (!present) {
                char msg_buf[200];
                snprintf(msg_buf, sizeof(msg_buf),
                         "Element '%s' missing #REQUIRED attribute '%s'",
                         ctx->elem_name, attr_buf);
                set_error(ctx->error, msg_buf, ctx->elem_name);
                ctx->found_violation = 1;
                return 0;  /* stop iteration */
            }
        }
    }
    return 1;  /* continue */
}

static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error,
                                       StringHashTable* id_table) {
    if (!elem) return 1;

    const char* name = taurus_element_get_name(elem);
    if (!name) return 1;

    /* Look up the element declaration in the DTD. */
    DTDElementDecl* decl = ttdtd_lookup_element(dtd, name);

    if (decl) {
        /* Phase 1: only EMPTY is enforced. EMPTY means NO element
         * children. Text content (whitespace) is tolerated per common
         * XML processor behavior, since whitespace is typically
         * formatting-only. */
        if (decl->content_type == DTD_CONTENT_EMPTY) {
            if (element_has_element_children(elem)) {
                set_error(error,
                          "Element declared EMPTY has element children",
                          name);
                return 0;
            }
        }
        /* DTD_CONTENT_ANY: any content allowed, always valid. */
        if (decl->content_type == DTD_CONTENT_ANY) {
            /* Valid by definition; fall through. */
        } else if (decl->content_model && decl->content_model[0]) {
            /* Phase 4: walk the element's actual element-type
             * children and match them against the parsed content
             * model. Phase 1's EMPTY check already handles the empty
             * case; the matcher handles the other content types
             * (CHILDREN, MIXED, ELEMENT) where a model is stored. */
            size_t child_count = 0;
            for (TaurusNodeRef c = taurus_node_first_child((TaurusNodeRef)elem);
                 c; c = taurus_node_next_sibling(c)) {
                if (taurus_node_get_type(c) == TAURUS_NODE_TYPE_ELEMENT) {
                    child_count++;
                }
            }
            if (child_count > 0) {
                const char** child_names = (const char**)malloc(
                    child_count * sizeof(const char*));
                if (child_names) {
                    size_t i = 0;
                    for (TaurusNodeRef c = taurus_node_first_child((TaurusNodeRef)elem);
                         c && i < child_count; c = taurus_node_next_sibling(c)) {
                        if (taurus_node_get_type(c) == TAURUS_NODE_TYPE_ELEMENT) {
                            child_names[i++] = taurus_element_get_name((TaurusElement)c);
                        }
                    }
                    char msg_buf[256];
                    int ok = taurus_content_model_match(
                        decl->content_model, name, child_names, child_count,
                        msg_buf, sizeof(msg_buf));
                    if (!ok) {
                        set_error(error, msg_buf, name);
                        free((void*)child_names);
                        return 0;
                    }
                    free((void*)child_names);
                }
            }
        }
        /* DTD_CONTENT_PCDATA (text-only): unmatched here; Phase 4
         * only validates element-type children. */
    }
    /* Phase 1 does NOT error on undeclared elements — that's stricter
     * than most real-world DTD validation (which often permits
     * additional elements). Add an option later if needed. */

    /* Phase 3: walk attribute declarations for this element. */
    AttrCheckContext ctx = {
        .elem = elem,
        .elem_name = name,
        .elem_name_len = strlen(name),
        .error = error,
        .found_violation = 0,
    };
    taurus_hash_table_for_each((StringHashTable*)dtd->tables.attributes,
                               attr_check_iter, &ctx);
    if (ctx.found_violation) return 0;

    /* Phase 5: ID uniqueness. Walk the element's attributes; if any
     * is declared as type "ID", record its value in id_table. The
     * first occurrence of a duplicate triggers a violation. */
    uint8_t ac = taurus_element_attribute_count(elem);
    for (uint8_t i = 0; i < ac; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(elem, i);
        if (!attr) continue;
        const char* attr_name = attr->name;
        if (!attr_name || !attr->value) continue;
        DTDAttributeDecl* ad = ttdtd_lookup_attribute(dtd, name, attr_name);
        if (!ad || !ad->attr_type) continue;
        if (strcmp(ad->attr_type, "ID") != 0) continue;
        const char* id_value = attr->value;
        size_t id_len = strlen(id_value);
        if (taurus_hash_table_get(id_table, id_value, id_len) != NULL) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "Duplicate ID value '%s'", id_value);
            set_error(error, msg, name);
            return 0;
        }
        if (!taurus_hash_table_set(id_table, id_value, id_len,
                                    (void*)(uintptr_t)1, NULL)) {
            /* OOM — would need proper error reporting. */
        }
    }

    /* Recurse into children. */
    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        int rc = validate_element_recursive(child, dtd, error, id_table);
        if (rc != 1) return rc;  /* propagate first violation */
        child = taurus_element_next_sibling_any(child);
    }
    return 1;
}

int taurus_dtd_validate(TaurusDocument doc, TaurusDTD* dtd, TaurusDTDError* error) {
    if (!doc || !dtd) {
        if (error) {
            set_error(error, "NULL document or DTD passed to taurus_dtd_validate", NULL);
        }
        return -1;
    }

    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        /* Empty document is valid by definition. */
        return 1;
    }

    /* Phase 5: allocate the ID-tracking hash table on the DTD's
     * pool so it shares the DTD's lifetime. */
    StringHashTable* id_table = taurus_hash_table_create(
        (TaurusMemoryPool*)dtd->pool, 16);
    if (!id_table) {
        if (error) set_error(error, "Failed to allocate ID table", NULL);
        return -1;
    }
    int rc = validate_element_recursive(root, dtd, error, id_table);
    taurus_hash_table_destroy(id_table);
    return rc;
}

void taurus_dtd_error_free(TaurusDTDError* error) {
    if (!error) return;
    if (error->message) {
        free(error->message);
        error->message = NULL;
    }
    if (error->element_name) {
        free(error->element_name);
        error->element_name = NULL;
    }
    error->line = 0;
    error->column = 0;
}
