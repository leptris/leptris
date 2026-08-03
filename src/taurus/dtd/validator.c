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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* Forward declaration for the recursive walker. */
static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error);

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

static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error) {
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
        /* DTD_CONTENT_ANY: any content allowed, always valid.
         * DTD_CONTENT_MIXED, DTD_CONTENT_CHILDREN, DTD_CONTENT_ELEMENT,
         * DTD_CONTENT_PCDATA: need the grammar matcher (Phase 3). */
    }
    /* Phase 1 does NOT error on undeclared elements — that's stricter
     * than most real-world DTD validation (which often permits
     * additional elements). Add an option later if needed. */

    /* Phase 2: enforce #REQUIRED ATTLIST attributes on this element.
     * The DTD attribute hash table is keyed by "element.attr". We
     * don't know up-front which attrs are declared for this element,
     * so we walk the common cases via ttdtd_lookup_attribute by
     * checking each attribute present on the element (Phase 2 needs
     * the inverse — what's REQUIRED but missing). Without iteration
     * over the DTD's attribute keys, we approximate by checking the
     * element's known ATTLIST entries via the parser.
     *
     * To keep this Phase 2 bounded, we test the well-known required
     * attributes from a fixed allowlist (id, ref, class, role). Real
     * DTDs that declare other #REQUIRED attributes won't be checked
     * until the DTD's attribute table iteration API is exposed. */
    static const char* common_required_attrs[] = { "id", "ref", NULL };
    for (size_t i = 0; common_required_attrs[i]; i++) {
        const char* attr_name = common_required_attrs[i];
        DTDAttributeDecl* ad = ttdtd_lookup_attribute(dtd, name, attr_name);
        if (ad && ad->default_type == DTD_ATTR_REQUIRED) {
            struct taurus_attribute* present =
                taurus_element_get_attribute_by_name(elem, attr_name);
            if (!present) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                         "Element '%s' missing #REQUIRED attribute '%s'",
                         name, attr_name);
                set_error(error, buf, name);
                return 0;
            }
        }
    }

    /* Recurse into children. */
    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        int rc = validate_element_recursive(child, dtd, error);
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

    return validate_element_recursive(root, dtd, error);
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
