/**
 * @file dtd/model.c
 * @brief DTD model implementation
 *
 * Implementation of DTD model creation/destruction.
 */

#include "model.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create element declaration
 */
DTDElementDecl* dtd_element_decl_create(const char* name) {
    if (!name) return NULL;

    DTDElementDecl* decl = (DTDElementDecl*)calloc(1, sizeof(DTDElementDecl));
    if (!decl) return NULL;

    decl->name = strdup(name);
    if (!decl->name) {
        free(decl);
        return NULL;
    }

    decl->content_type = DTD_CONTENT_ANY;
    decl->content_model = NULL;

    return decl;
}

/**
 * Free element declaration
 */
void dtd_element_decl_free(DTDElementDecl* decl) {
    if (!decl) return;

    free(decl->name);
    free(decl->content_model);
    free(decl);
}

/**
 * Create attribute declaration
 */
DTDAttributeDecl* dtd_attribute_decl_create(const char* element, const char* attr) {
    if (!element || !attr) return NULL;

    DTDAttributeDecl* decl = (DTDAttributeDecl*)calloc(1, sizeof(DTDAttributeDecl));
    if (!decl) return NULL;

    decl->element_name = strdup(element);
    decl->attr_name = strdup(attr);

    if (!decl->element_name || !decl->attr_name) {
        free(decl->element_name);
        free(decl->attr_name);
        free(decl);
        return NULL;
    }

    decl->attr_type = strdup("CDATA");
    decl->default_type = DTD_ATTR_IMPLIED;
    decl->default_value = NULL;

    return decl;
}

/**
 * Free attribute declaration
 */
void dtd_attribute_decl_free(DTDAttributeDecl* decl) {
    if (!decl) return;

    free(decl->element_name);
    free(decl->attr_name);
    free(decl->attr_type);
    free(decl->default_value);
    free(decl);
}

/* ============================================================================
 * DTD Container Functions
 * ============================================================================*/

/**
 * Create a DTD container
 */
TaurusDTD* taurus_dtd_create(void) {
    /* Allocate DTD structure */
    TaurusDTD* dtd = (TaurusDTD*)calloc(1, sizeof(TaurusDTD));
    if (!dtd) return NULL;

    /* Create memory pool for DTD allocations */
    dtd->pool = taurus_pool_create();
    if (!dtd->pool) {
        free(dtd);
        return NULL;
    }

    /* Create hash tables for O(1) lookup */
    dtd->tables.entities = taurus_hash_table_create(dtd->pool, 128);
    dtd->tables.elements = taurus_hash_table_create(dtd->pool, 64);
    dtd->tables.notations = taurus_hash_table_create(dtd->pool, 32);
    dtd->tables.attributes = taurus_hash_table_create(dtd->pool, 128);

    /* Check that all hash tables were created successfully */
    if (!dtd->tables.entities || !dtd->tables.elements ||
        !dtd->tables.notations || !dtd->tables.attributes) {
        ttdtd_free(dtd);
        return NULL;
    }

    /* Initialize counts */
    dtd->entity_count = 0;
    dtd->element_count = 0;
    dtd->notation_count = 0;
    dtd->attribute_count = 0;

    return dtd;
}

/**
 * Free DTD container and all contained declarations
 */
void ttdtd_free(TaurusDTD* dtd) {
    if (!dtd) return;

    /* Destroy the pool, which frees all hash tables and entries */
    if (dtd->pool) {
        taurus_pool_destroy(dtd->pool);
    }

    /* Free the DTD structure itself */
    free(dtd);
}

/* ============================================================================
 * Entity Management
 * ============================================================================*/

/**
 * Create an entity declaration
 */
DTDEntityDecl* ttdtd_entity_create(const char* name) {
    if (!name) return NULL;

    /* Allocate entity structure */
    DTDEntityDecl* entity = (DTDEntityDecl*)calloc(1, sizeof(DTDEntityDecl));
    if (!entity) return NULL;

    /* Duplicate name */
    entity->name = strdup(name);
    if (!entity->name) {
        free(entity);
        return NULL;
    }

    /* Initialize fields */
    entity->type = DTD_ENTITY_INTERNAL;
    entity->value = NULL;
    entity->system_id = NULL;
    entity->public_id = NULL;
    entity->notation_name = NULL;

    return entity;
}

/**
 * Free an entity declaration
 */
void ttdtd_entity_free(DTDEntityDecl* entity) {
    if (!entity) return;

    free(entity->name);
    free(entity->value);
    free(entity->system_id);
    free(entity->public_id);
    free(entity->notation_name);
    free(entity);
}

/**
 * Add entity to DTD
 */
int ttdtd_add_entity(TaurusDTD* dtd, DTDEntityDecl* entity) {
    if (!dtd || !entity || !entity->name) return 0;

    size_t name_len = strlen(entity->name);

    /* Check if entity already exists */
    if (ttdtd_lookup_entity(dtd, entity->name) != NULL) {
        /* Entity already exists - don't add duplicate */
        return 0;
    }

    /* Add to hash table - the DTD pool will be used for hash table entries */
    if (taurus_hash_table_set(dtd->tables.entities, entity->name, name_len,
                              entity, dtd->pool)) {
        dtd->entity_count++;
        return 1;
    }

    return 0;
}

/**
 * Lookup entity by name
 */
DTDEntityDecl* ttdtd_lookup_entity(const TaurusDTD* dtd, const char* name) {
    if (!dtd || !name) return NULL;

    size_t name_len = strlen(name);
    return (DTDEntityDecl*)taurus_hash_table_get(dtd->tables.entities, name, name_len);
}

/* ============================================================================
 * Element Management
 * ============================================================================*/

/**
 * Create an element declaration
 */
DTDElementDecl* ttdtd_element_create(const char* name) {
    if (!name) return NULL;

    /* Allocate element structure */
    DTDElementDecl* element = (DTDElementDecl*)calloc(1, sizeof(DTDElementDecl));
    if (!element) return NULL;

    /* Duplicate name */
    element->name = strdup(name);
    if (!element->name) {
        free(element);
        return NULL;
    }

    /* Initialize fields */
    element->content_type = DTD_CONTENT_ANY;
    element->content_model = NULL;

    return element;
}

/**
 * Free an element declaration
 */
void ttdtd_element_free(DTDElementDecl* element) {
    if (!element) return;

    free(element->name);
    free(element->content_model);
    free(element);
}

/**
 * Add element to DTD
 */
int ttdtd_add_element(TaurusDTD* dtd, DTDElementDecl* element) {
    if (!dtd || !element || !element->name) return 0;

    size_t name_len = strlen(element->name);

    /* Check if element already exists */
    if (ttdtd_lookup_element(dtd, element->name) != NULL) {
        /* Element already exists - don't add duplicate */
        return 0;
    }

    /* Add to hash table */
    if (taurus_hash_table_set(dtd->tables.elements, element->name, name_len,
                              element, dtd->pool)) {
        dtd->element_count++;
        return 1;
    }

    return 0;
}

/**
 * Lookup element by name
 */
DTDElementDecl* ttdtd_lookup_element(const TaurusDTD* dtd, const char* name) {
    if (!dtd || !name) return NULL;

    size_t name_len = strlen(name);
    return (DTDElementDecl*)taurus_hash_table_get(dtd->tables.elements, name, name_len);
}

/* ============================================================================
 * Notation Management
 * ============================================================================*/

/**
 * Create a notation declaration
 */
DTDNotationDecl* ttdtd_notation_create(const char* name) {
    if (!name) return NULL;

    /* Allocate notation structure */
    DTDNotationDecl* notation = (DTDNotationDecl*)calloc(1, sizeof(DTDNotationDecl));
    if (!notation) return NULL;

    /* Duplicate name */
    notation->name = strdup(name);
    if (!notation->name) {
        free(notation);
        return NULL;
    }

    /* Initialize fields */
    notation->public_id = NULL;
    notation->system_id = NULL;

    return notation;
}

/**
 * Free a notation declaration
 */
void ttdtd_notation_free(DTDNotationDecl* notation) {
    if (!notation) return;

    free(notation->name);
    free(notation->public_id);
    free(notation->system_id);
    free(notation);
}

/**
 * Add notation to DTD
 */
int ttdtd_add_notation(TaurusDTD* dtd, DTDNotationDecl* notation) {
    if (!dtd || !notation || !notation->name) return 0;

    size_t name_len = strlen(notation->name);

    /* Check if notation already exists */
    if (ttdtd_lookup_notation(dtd, notation->name) != NULL) {
        /* Notation already exists - don't add duplicate */
        return 0;
    }

    /* Add to hash table */
    if (taurus_hash_table_set(dtd->tables.notations, notation->name, name_len,
                              notation, dtd->pool)) {
        dtd->notation_count++;
        return 1;
    }

    return 0;
}

/**
 * Lookup notation by name
 */
DTDNotationDecl* ttdtd_lookup_notation(const TaurusDTD* dtd, const char* name) {
    if (!dtd || !name) return NULL;

    size_t name_len = strlen(name);
    return (DTDNotationDecl*)taurus_hash_table_get(dtd->tables.notations, name, name_len);
}