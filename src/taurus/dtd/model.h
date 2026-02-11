/**
 * @file dtd/model.h
 * @brief Internal DTD model structures
 *
 * Internal representation of DTD declarations.
 */

#ifndef TAURUS_DTD_MODEL_H
#define TAURUS_DTD_MODEL_H

#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct taurus_memory_pool TaurusMemoryPool;

/**
 * DTD content type
 */
typedef enum {
    DTD_CONTENT_EMPTY,      /* <!ELEMENT foo EMPTY> */
    DTD_CONTENT_ANY,        /* <!ELEMENT foo ANY> */
    DTD_CONTENT_MIXED,      /* <!ELEMENT foo (#PCDATA | a | b)*> */
    DTD_CONTENT_CHILDREN,   /* <!ELEMENT foo (a, b+, c?)> */
    DTD_CONTENT_PCDATA,      /* #PCDATA (text-only) */
    DTD_CONTENT_ELEMENT     /* Single element (child) */
} DTDContentType;

/**
 * DTD attribute default type
 */
typedef enum {
    DTD_ATTR_REQUIRED,      /* #REQUIRED */
    DTD_ATTR_IMPLIED,       /* #IMPLIED */
    DTD_ATTR_FIXED,         /* #FIXED "value" */
    DTD_ATTR_DEFAULT        /* "value" */
} DTDAttrDefault;

/**
 * Entity declaration type
 */
typedef enum {
    DTD_ENTITY_INTERNAL,    /* <!ENTITY name "value"> */
    DTD_ENTITY_EXTERNAL     /* <!ENTITY name SYSTEM "uri"> or PUBLIC "pub" "uri"> */
} DTDEntityType;

/**
 * Element declaration
 */
typedef struct DTDElementDecl {
    char* name;                 /* Element name */
    DTDContentType content_type;
    char* content_model;        /* String representation of content model */
} DTDElementDecl;

/**
 * Attribute declaration
 */
typedef struct DTDAttributeDecl {
    char* element_name;         /* Element this attribute belongs to */
    char* attr_name;            /* Attribute name */
    char* attr_type;            /* "CDATA", "ID", "IDREF", "NMTOKEN", etc. */
    DTDAttrDefault default_type;
    char* default_value;        /* Default/fixed value (may be NULL) */
} DTDAttributeDecl;

/**
 * Entity declaration
 */
typedef struct DTDEntityDecl {
    char* name;                 /* Entity name */
    DTDEntityType type;         /* INTERNAL or EXTERNAL */
    char* value;                /* Internal entity value */
    char* system_id;            /* External entity system ID */
    char* public_id;            /* External entity public ID */
    char* notation_name;        /* For unparsed entities */
} DTDEntityDecl;

/**
 * Notation declaration
 */
typedef struct DTDNotationDecl {
    char* name;                 /* Notation name */
    char* public_id;            /* Public identifier */
    char* system_id;            /* System identifier */
} DTDNotationDecl;

/**
 * DTD structure (internal) - Container for all DTD declarations
 *
 * This is the main container that holds all parsed DTD declarations
 * for a document. Provides O(1) hash-based lookup for entities, elements,
 * and notations.
 */
typedef struct TaurusDTD {
    /* Memory pool for all DTD allocations */
    void* pool;  /* TaurusMemoryPool* */

    /* Hash tables for O(1) lookup by name */
    struct {
        void* entities;    /* StringHashTable: name -> DTDEntityDecl* */
        void* elements;    /* StringHashTable: name -> DTDElementDecl* */
        void* notations;   /* StringHashTable: name -> DTDNotationDecl* */
        void* attributes;  /* StringHashTable: "element.attr" -> DTDAttributeDecl* */
    } tables;

    /* Counts for iteration (optional) */
    size_t entity_count;
    size_t element_count;
    size_t notation_count;
    size_t attribute_count;
} TaurusDTD;

/* Creation/destruction functions */

/**
 * Create a DTD container
 *
 * @return New DTD, or NULL on failure
 */
TaurusDTD* taurus_dtd_create(void);

/**
 * Free DTD container and all contained declarations
 *
 * @param dtd DTD to free (NULL is safe)
 */
void ttdtd_free(TaurusDTD* dtd);

/* Entity management */

/**
 * Create an entity declaration
 *
 * @param name Entity name (must not be NULL)
 * @return New entity declaration, or NULL on failure
 */
DTDEntityDecl* ttdtd_entity_create(const char* name);

/**
 * Free an entity declaration
 *
 * @param entity Entity to free (NULL is safe)
 */
void ttdtd_entity_free(DTDEntityDecl* entity);

/**
 * Add entity to DTD
 *
 * @param dtd DTD container
 * @param entity Entity to add (consumed on success)
 * @return 1 on success, 0 on failure
 */
int ttdtd_add_entity(TaurusDTD* dtd, DTDEntityDecl* entity);

/**
 * Lookup entity by name
 *
 * @param dtd DTD container
 * @param name Entity name to lookup
 * @return Entity declaration, or NULL if not found
 */
DTDEntityDecl* ttdtd_lookup_entity(const TaurusDTD* dtd, const char* name);

/* Element management */

/**
 * Create an element declaration
 *
 * @param name Element name (must not be NULL)
 * @return New element declaration, or NULL on failure
 */
DTDElementDecl* ttdtd_element_create(const char* name);

/**
 * Free an element declaration
 *
 * @param element Element to free (NULL is safe)
 */
void ttdtd_element_free(DTDElementDecl* element);

/**
 * Add element to DTD
 *
 * @param dtd DTD container
 * @param element Element to add (consumed on success)
 * @return 1 on success, 0 on failure
 */
int ttdtd_add_element(TaurusDTD* dtd, DTDElementDecl* element);

/**
 * Lookup element by name
 *
 * @param dtd DTD container
 * @param name Element name to lookup
 * @return Element declaration, or NULL if not found
 */
DTDElementDecl* ttdtd_lookup_element(const TaurusDTD* dtd, const char* name);

/* Notation management */

/**
 * Create a notation declaration
 *
 * @param name Notation name (must not be NULL)
 * @return New notation declaration, or NULL on failure
 */
DTDNotationDecl* ttdtd_notation_create(const char* name);

/**
 * Free a notation declaration
 *
 * @param notation Notation to free (NULL is safe)
 */
void ttdtd_notation_free(DTDNotationDecl* notation);

/**
 * Add notation to DTD
 *
 * @param dtd DTD container
 * @param notation Notation to add (consumed on success)
 * @return 1 on success, 0 on failure
 */
int ttdtd_add_notation(TaurusDTD* dtd, DTDNotationDecl* notation);

/**
 * Lookup notation by name
 *
 * @param dtd DTD container
 * @param name Notation name to lookup
 * @return Notation declaration, or NULL if not found
 */
DTDNotationDecl* ttdtd_lookup_notation(const TaurusDTD* dtd, const char* name);

/* Legacy functions for backward compatibility */

DTDElementDecl* dtd_element_decl_create(const char* name);
void dtd_element_decl_free(DTDElementDecl* decl);

DTDAttributeDecl* dtd_attribute_decl_create(const char* element, const char* attr);
void dtd_attribute_decl_free(DTDAttributeDecl* decl);

/* DTD Parser functions */

/**
 * Parse DTD internal subset
 *
 * Parses DTD declarations from a string (ENTITY, ELEMENT, NOTATION, ATTLIST).
 * Uses hash tables for O(1) lookup of parsed declarations.
 *
 * @param dtd_content DTD content string (UTF-8)
 * @param len Length of DTD content in bytes
 * @return Parsed DTD object, or NULL on error
 *
 * Note: Caller must free with ttdtd_free()
 */
TaurusDTD* taurus_dtd_parse_internal_subset(const char* dtd_content, size_t len);

/* Entity Resolver functions */

/**
 * Expand entities in text content
 *
 * Expands entity references like &name; with their declared values.
 * Handles nested entities with cycle detection and max recursion depth.
 *
 * @param dtd DTD container (can be NULL for predefined entities only)
 * @param text Text content with entity references
 * @param len Length of text content
 * @param result_len Output: length of expanded text (can be NULL)
 * @return Expanded text, or NULL on error (caller must free)
 *
 * Two-tier lookup: predefined entities → custom entities
 * Max recursion depth: 10 levels
 */
char* taurus_dtd_expand_entities(const TaurusDTD* dtd,
                                  const char* text,
                                  size_t len,
                                  size_t* result_len);

/**
 * Lookup entity by name (two-tier: predefined → custom)
 *
 * @param dtd DTD container (can be NULL for predefined only)
 * @param name Entity name to lookup
 * @return Entity value (predefined or custom), or NULL if not found
 *
 * Note: Returns pointer to internal storage - do not free
 */
const char* taurus_dtd_lookup_entity(const TaurusDTD* dtd, const char* name);

#endif /* TAURUS_DTD_MODEL_H */