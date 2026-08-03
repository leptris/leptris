/**
 * @file dtd.h
 * @brief DTD (Document Type Definition) validation API
 *
 * Provides DTD parsing and document validation against DTD rules.
 */

#ifndef TAURUS_DTD_H
#define TAURUS_DTD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TaurusDocument comes from taurus/types.h (or taurus.h) via the
 * shared TAURUS_INTERNAL_TYPES_DEFINED guard.  No redefinition. */
#ifndef TAURUS_INTERNAL_TYPES_DEFINED
#define TAURUS_INTERNAL_TYPES_DEFINED
typedef struct taurus_document* TaurusDocument;
#endif

/* TaurusDTD is opaque to the public API.  Guard matches the internal
 * common/types_internal.h so the two headers can be included in
 * either order without a C99 typedef-redefinition warning. */
#ifndef TAURUS_TYPEDEF_DTD_DECLARED
#define TAURUS_TYPEDEF_DTD_DECLARED
typedef struct TaurusDTD TaurusDTD;
#endif

/**
 * DTD validation error
 */
typedef struct {
    char* message;       /* Error message */
    char* element_name;  /* Element where error occurred (may be NULL) */
    int line;            /* Line number (if available) */
    int column;          /* Column number (if available) */
} TaurusDTDError;

/**
 * Parse DTD from string
 *
 * Parses internal DTD subset (declarations within DOCTYPE).
 *
 * @param dtd_content DTD content string (UTF-8)
 * @param len Length of DTD content in bytes
 * @return Parsed DTD object or NULL on error
 *
 * Example:
 *   const char* dtd =
 *       "<!ELEMENT book (title, author+, isbn?)>"
 *       "<!ATTLIST book id ID #REQUIRED>";
 *   TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));
 *
 * Memory: Caller must free with taurus_dtd_free()
 */
TaurusDTD* taurus_dtd_parse(const char* dtd_content, size_t len);

/**
 * Validate document against DTD
 *
 * Checks if document conforms to DTD rules:
 * - Required attributes present
 * - Element content matches declared model
 * - Attribute types valid
 *
 * @param doc Document to validate
 * @param dtd DTD to validate against
 * @param error Output error (if validation fails), can be NULL
 * @return 1 if valid, 0 if invalid, -1 on internal error
 *
 * Example:
 *   TaurusDTDError error = {0};
 *   int valid = taurus_dtd_validate(doc, dtd, &error);
 *   if (!valid) {
 *       printf("Validation failed: %s\n", error.message);
 *       taurus_dtd_error_free(&error);
 *   }
 */
int taurus_dtd_validate(TaurusDocument doc, TaurusDTD* dtd, TaurusDTDError* error);

/**
 * Free DTD object
 *
 * @param dtd DTD to free (can be NULL)
 */
void taurus_dtd_free(TaurusDTD* dtd);

/**
 * Free DTD error
 *
 * Frees any allocated strings in the error structure.
 *
 * @param error Error to free (can be NULL)
 */
void taurus_dtd_error_free(TaurusDTDError* error);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_DTD_H */