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

/* _POSIX_C_SOURCE=200809L is defined project-wide (see src/CMakeLists.txt)
 * so strdup() is properly declared. See xpath/xpath_variables.c note
 * for the bug this prevents (TODOs 94, 95). */

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
 * Create a DTD container backed by the given document pool.
 *
 * As of TODO 16, the DTD no longer allocates a private pool — every
 * byte (struct, hash tables, future entity declarations) comes from
 * the document's pool, so taurus_document_free releases everything
 * in one taurus_pool_destroy call.
 */
TaurusDTD* taurus_dtd_create(TaurusMemoryPool* pool) {
    if (!pool) return NULL;

    TaurusDTD* dtd = (TaurusDTD*)taurus_pool_calloc(pool, sizeof(TaurusDTD));
    if (!dtd) return NULL;

    /* Remember the pool so callers that grow the DTD later (e.g.,
     * ttdtd_entity_create) can route through it. */
    dtd->pool = pool;

    /* Hash tables for O(1) lookup — pool-allocated. */
    dtd->tables.entities   = taurus_hash_table_create(pool, 128);
    dtd->tables.elements   = taurus_hash_table_create(pool, 64);
    dtd->tables.notations  = taurus_hash_table_create(pool, 32);
    dtd->tables.attributes = taurus_hash_table_create(pool, 128);

    if (!dtd->tables.entities || !dtd->tables.elements ||
        !dtd->tables.notations || !dtd->tables.attributes) {
        return NULL;  /* Pool owns the partial allocation; nothing to free. */
    }

    dtd->entity_count = 0;
    dtd->element_count = 0;
    dtd->notation_count = 0;
    dtd->attribute_count = 0;

    return dtd;
}

/**
 * Free DTD container.
 *
 * Pool-ownership model (TODO 16): the DTD struct, hash tables, and
 * all declarations live in the document's pool.  They are released by
 * taurus_pool_destroy when the document is freed.  This function is a
 * no-op, kept for backwards source compatibility with callers that
 * explicitly invoke it.
 */
void ttdtd_free(TaurusDTD* dtd) {
    if (!dtd) return;
    /* Only destroy the pool when this DTD owns it (i.e., it was
     * created via the public taurus_dtd_parse API). Document-pool
     * DTDs (created internally by the parser) leave pool ownership
     * to the document. */
    if (dtd->owns_pool && dtd->pool) {
        taurus_pool_destroy((TaurusMemoryPool*)dtd->pool);
    }
}

/* ============================================================================
 * Entity Management
 * ============================================================================*/

/**
 * Create an entity declaration, pool-allocated (TODO 16).
 */
DTDEntityDecl* ttdtd_entity_create(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    size_t name_len = strlen(name);

    /* Single pool allocation: struct + name + NUL (contiguous). */
    size_t total = sizeof(DTDEntityDecl) + name_len + 1;
    char* memory = (char*)taurus_pool_alloc(pool, total);
    if (!memory) return NULL;

    DTDEntityDecl* entity = (DTDEntityDecl*)memory;
    char* name_storage = memory + sizeof(DTDEntityDecl);
    memcpy(name_storage, name, name_len);
    name_storage[name_len] = '\0';

    entity->name = name_storage;
    entity->type = DTD_ENTITY_INTERNAL;
    entity->value = NULL;
    entity->system_id = NULL;
    entity->public_id = NULL;
    entity->notation_name = NULL;

    return entity;
}

/**
 * Free an entity declaration.
 *
 * Pool-ownership (TODO 16): the entity struct is pool-allocated;
 * taurus_pool_destroy releases it.  This function is a no-op kept
 * for backwards source compatibility.
 */
void ttdtd_entity_free(DTDEntityDecl* entity) {
    (void)entity;
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

    /* Copy name */
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

/* Pool-allocated variant — the returned declaration is released when
 * the pool is destroyed. Callers that don't have a pool use the
 * non-pooled variant above and must call ttdtd_element_free. */
DTDElementDecl* ttdtd_element_create_pooled(const char* name, TaurusMemoryPool* pool) {
    if (!name || !pool) return NULL;

    DTDElementDecl* element = (DTDElementDecl*)taurus_pool_calloc(pool, sizeof(DTDElementDecl));
    if (!element) return NULL;

    size_t name_len = strlen(name);
    char* name_copy = (char*)taurus_pool_alloc(pool, name_len + 1);
    if (!name_copy) return NULL;
    memcpy(name_copy, name, name_len + 1);
    element->name = name_copy;

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
 * Attribute Management
 * ============================================================================*/

/* Build the "element.attr" composite key used by the attribute hash
 * table. Returns a malloc'd string the caller must free, or NULL on
 * OOM. */
static char* build_attr_key(const char* element_name, const char* attr_name) {
    size_t elen = strlen(element_name);
    size_t alen = strlen(attr_name);
    char* key = (char*)malloc(elen + 1 + alen + 1);
    if (!key) return NULL;
    memcpy(key, element_name, elen);
    key[elen] = '.';
    memcpy(key + elen + 1, attr_name, alen);
    key[elen + 1 + alen] = '\0';
    return key;
}

int ttdtd_add_attribute(TaurusDTD* dtd, DTDAttributeDecl* attr) {
    if (!dtd || !attr || !attr->element_name || !attr->attr_name) return 0;

    char* key = build_attr_key(attr->element_name, attr->attr_name);
    if (!key) return 0;
    size_t key_len = strlen(key);

    /* Replace existing declaration if present. */
    int rc = taurus_hash_table_set(dtd->tables.attributes, key, key_len,
                                   attr, dtd->pool);
    free(key);
    if (rc) {
        dtd->attribute_count++;
        return 1;
    }
    return 0;
}

DTDAttributeDecl* ttdtd_lookup_attribute(const TaurusDTD* dtd,
                                          const char* element_name,
                                          const char* attr_name) {
    if (!dtd || !element_name || !attr_name) return NULL;

    char* key = build_attr_key(element_name, attr_name);
    if (!key) return NULL;
    size_t key_len = strlen(key);
    DTDAttributeDecl* result = (DTDAttributeDecl*)taurus_hash_table_get(
        dtd->tables.attributes, key, key_len);
    free(key);
    return result;
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