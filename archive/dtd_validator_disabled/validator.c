/**
 * @file dtd/validator.c
 * @brief DTD validator implementation
 *
 * Validates documents against DTD rules.
 */

#include "../../include/taurus/dtd.h"
#include "../../include/taurus.h"
#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Helper: Find element declaration */
static DTDElementDecl* find_element_decl(TaurusDTD* dtd, const char* name) {
    for (size_t i = 0; i < dtd->element_count; i++) {
        if (strcmp(dtd->elements[i]->name, name) == 0) {
            return dtd->elements[i];
        }
    }
    return NULL;
}

/* Helper: Find attribute declarations for element */
static int find_attribute_decls(TaurusDTD* dtd, const char* element_name,
                                DTDAttributeDecl*** out_decls, size_t* out_count) {
    *out_decls = NULL;
    *out_count = 0;

    for (size_t i = 0; i < dtd->attribute_count; i++) {
        if (strcmp(dtd->attributes[i]->element_name, element_name) == 0) {
            DTDAttributeDecl** new_decls = (DTDAttributeDecl**)realloc(
                *out_decls, (*out_count + 1) * sizeof(DTDAttributeDecl*));
            if (!new_decls) {
                free(*out_decls);
                return -1;
            }
            *out_decls = new_decls;
            (*out_decls)[*out_count] = dtd->attributes[i];
            (*out_count)++;
        }
    }

    return 0;
}

/* Validate element */
static int validate_element(TaurusElement elem, TaurusDTD* dtd, TaurusDTDError* error) {
    const char* elem_name = taurus_element_name(elem);
    if (!elem_name) {
        if (error) {
            error->message = strdup("Element has no name");
            error->element_name = NULL;
        }
        return 0;
    }

    /* Find element declaration */
    DTDElementDecl* elem_decl = find_element_decl(dtd, elem_name);

    /* Validate content model (basic) - only if element declaration exists */
    if (elem_decl) {
        size_t child_count = taurus_element_child_count(elem);

        if (elem_decl->content_type == DTD_CONTENT_EMPTY && child_count > 0) {
            if (error) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Element '%s' must be empty but has children", elem_name);
                error->message = strdup(msg);
                error->element_name = strdup(elem_name);
            }
            return 0;
        }
    }

    /* Find attribute declarations - check even without element declaration */
    DTDAttributeDecl** attr_decls = NULL;
    size_t attr_count = 0;
    if (find_attribute_decls(dtd, elem_name, &attr_decls, &attr_count) < 0) {
        if (error) {
            error->message = strdup("Memory allocation error");
        }
        return -1;
    }

    /* Check required attributes */
    for (size_t i = 0; i < attr_count; i++) {
        if (attr_decls[i]->default_type == DTD_ATTR_REQUIRED) {
            const char* attr_value = taurus_element_attribute(elem, attr_decls[i]->attr_name);
            if (!attr_value) {
                if (error) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                            "Element '%s' missing required attribute '%s'",
                            elem_name, attr_decls[i]->attr_name);
                    error->message = strdup(msg);
                    error->element_name = strdup(elem_name);
                }
                free(attr_decls);
                return 0;
            }
        }
    }

    free(attr_decls);

    /* Recursively validate children */
    size_t child_count = taurus_element_child_count(elem);
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(elem, i);
        if (child) {
            int result = validate_element(child, dtd, error);
            if (result != 1) {
                return result;
            }
        }
    }

    return 1;
}

/**
 * Validate document against DTD
 */
int taurus_dtd_validate(TaurusDocument doc, TaurusDTD* dtd, TaurusDTDError* error) {
    if (!doc || !dtd) return -1;

    /* Initialize error if provided */
    if (error) {
        error->message = NULL;
        error->element_name = NULL;
        error->line = 0;
        error->column = 0;
    }

    /* Get root element */
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        if (error) {
            error->message = strdup("Document has no root element");
        }
        return 0;
    }

    /* Validate from root */
    return validate_element(root, dtd, error);
}

/**
 * Free DTD error
 */
void taurus_dtd_error_free(TaurusDTDError* error) {
    if (!error) return;

    free(error->message);
    free(error->element_name);

    error->message = NULL;
    error->element_name = NULL;
}