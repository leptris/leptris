/**
 * @file dtd.h
 * @brief DTD (Document Type Definition) validation API
 *
 * Provides DTD parsing and document validation against DTD rules.
 */

#ifndef LEPTRIS_DTD_H
#define LEPTRIS_DTD_H

#include <stddef.h>

/* Export macro: mirrors leptris.h/sax.h so dtd.h is includable
 * standalone (bindings include sub-headers directly). See TODO 80. */
/* Export macro.  Defined fully in leptris.h; sax.h allows standalone
 * inclusion (leptris.h is not required) so we mirror the same logic.
 * See TODO 80 (visibility preset) and TODO 122 (SAX surface). */
#ifndef LEPTRIS_API
#  ifdef LEPTRIS_FOR_BINDGEN
#    define LEPTRIS_API
#  elif defined(_WIN32)
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_BUILDING_DLL)
       /* Mirrors leptris.h (issue #278): CMake defines
        * LEPTRIS_BUILDING_DLL on the objects that build the DLL. */
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
     /* Mirrors leptris.h: GCC LTO needs externally_visible or it
      * internalizes public symbols with no in-library caller
      * (#1154/#1196/#1204 class). */
#    if defined(__GNUC__) && !defined(__clang__)
#      define LEPTRIS_API __attribute__((visibility("default"), used, \
                                            externally_visible))
#    else
#      define LEPTRIS_API __attribute__((visibility("default"), used))
#    endif
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* LeptrisDocument comes from leptris/types.h (or leptris.h) via the
 * shared LEPTRIS_INTERNAL_TYPES_DEFINED guard.  No redefinition. */
#ifndef LEPTRIS_INTERNAL_TYPES_DEFINED
#define LEPTRIS_INTERNAL_TYPES_DEFINED
typedef struct leptris_document* LeptrisDocument;
typedef struct leptris_doctype*  LeptrisDoctype;
#endif

/* LeptrisDTD is opaque to the public API.  Guard matches the internal
 * common/types_internal.h so the two headers can be included in
 * either order without a C99 typedef-redefinition warning. */
#ifndef LEPTRIS_TYPEDEF_DTD_DECLARED
#define LEPTRIS_TYPEDEF_DTD_DECLARED
typedef struct LeptrisDTD LeptrisDTD;
#endif

/**
 * DTD validation error
 */
typedef struct {
    char* message;       /* Error message */
    char* element_name;  /* Element where error occurred (may be NULL) */
    int line;            /* Line number (if available) */
    int column;          /* Column number (if available) */
} LeptrisDTDError;

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
 *   LeptrisDTD* dtd_obj = leptris_dtd_parse(dtd, strlen(dtd));
 *
 * Memory: Caller must free with leptris_dtd_free()
 */
LEPTRIS_API LeptrisDTD* leptris_dtd_parse(const char* dtd_content, size_t len);

/**
 * Get the document's DTD (the declarations parsed from the DOCTYPE
 * internal subset)
 *
 * The returned DTD is owned by the document: it lives until
 * leptris_document_free and must NOT be passed to leptris_dtd_free.
 * When the document has no internal subset (or no DOCTYPE), an empty
 * DTD is created on the document's pool on first call — the handle
 * for attaching an external subset via
 * leptris_dtd_parse_external_subset.
 *
 * @param doc Document (must not be NULL)
 * @return The document's DTD (never NULL for a valid document)
 *
 * Memory: Owned by the document; freed by leptris_document_free
 */
LEPTRIS_API LeptrisDTD* leptris_document_get_dtd(LeptrisDocument doc);

/**
 * Parse an external subset into an existing DTD
 *
 * The application owns I/O: read the resource named by the DOCTYPE
 * system id (leptris_doctype_get_system_id) and pass the bytes here.
 * Declarations merge with the internal subset per XML 1.0 — the
 * first declaration of a name wins, so internal-subset declarations
 * are never overridden. Parameter entities and conditional sections
 * (<![INCLUDE[...]]> / <![IGNORE[...]]>) are processed as in the
 * external subset grammar.
 *
 * @param dtd DTD to extend (e.g. from leptris_document_get_dtd)
 * @param content External subset text (UTF-8)
 * @param len Length of content in bytes
 * @return 1 on success, -1 on invalid arguments
 *
 * Example:
 *   LeptrisDTD* dtd = leptris_document_get_dtd(doc);
 *   const char* sys = leptris_doctype_get_system_id(
 *       leptris_document_get_doctype(doc));
 *   // read the resource named by sys, then:
 *   leptris_dtd_parse_external_subset(dtd, buffer, len);
 *   int valid = leptris_dtd_validate(doc, dtd, &error);
 */
LEPTRIS_API int leptris_dtd_parse_external_subset(LeptrisDTD* dtd, const char* content,
                                      size_t len);

/**
 * External parameter-entity loader (application-owned I/O)
 *
 * External parameter entities (<!ENTITY % pe SYSTEM "uri">) carry no
 * inline value; when such a PE is referenced, the DTD parser asks
 * this loader for the resource named by the system id. Same pattern
 * as leptris_dtd_parse_external_subset: the library never does I/O.
 *
 * The loader returns a malloc'd (or calloc'd) buffer in *out_len
 * bytes, which the parser consumes and frees; return NULL for
 * "unavailable" (the reference is then skipped, leniently).
 *
 * The loader is consulted when DTD CONTENT IS PARSED — register it
 * before leptris_dtd_parse_external_subset (or before parsing a
 * document whose internal subset you want resolved). Registering it
 * on a DTD whose text was already consumed has nothing left to
 * resolve.
 *
 * @param dtd DTD the loader attaches to
 * @param loader Callback (NULL clears it)
 * @param user_data Opaque pointer handed to the loader
 */
LEPTRIS_API void leptris_dtd_set_pe_loader(LeptrisDTD* dtd,
                               char* (*loader)(void* user_data,
                                               const char* system_id,
                                               size_t* out_len),
                               void* user_data);

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
 *   LeptrisDTDError error = {0};
 *   int valid = leptris_dtd_validate(doc, dtd, &error);
 *   if (!valid) {
 *       printf("Validation failed: %s\n", error.message);
 *       leptris_dtd_error_free(&error);
 *   }
 */
LEPTRIS_API int leptris_dtd_validate(LeptrisDocument doc, LeptrisDTD* dtd,
                                        LeptrisDTDError* error);

/**
 * Free DTD object
 *
 * @param dtd DTD to free (can be NULL)
 */
LEPTRIS_API void leptris_dtd_free(LeptrisDTD* dtd);

/**
 * Free DTD error
 *
 * Frees any allocated strings in the error structure.
 *
 * @param error Error to free (can be NULL)
 */
LEPTRIS_API void leptris_dtd_error_free(LeptrisDTDError* error);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DTD_H */