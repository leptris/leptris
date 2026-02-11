/* libtaurus - Serialization Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains document and element serialization operations.
 */

#ifndef TAURUS_DOM_SERIALIZE_H
#define TAURUS_DOM_SERIALIZE_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef TAURUS_API
#  ifdef _WIN32
#    ifdef TAURUS_BUILD_SHARED
#      define TAURUS_API __declspec(dllexport)
#    elif defined(TAURUS_USE_SHARED)
#      define TAURUS_API __declspec(dllimport)
#    else
#      define TAURUS_API
#    endif
#  else
#    define TAURUS_API
#  endif
#endif

/* ============================================================================
 * Serialization Operations
 * ============================================================================ */

/**
 * Serialize document to XML string
 *
 * @param doc Document to serialize
 * @param options Serialization options (NULL for defaults: compact, no declaration)
 * @return XML string or NULL on error (caller must free with taurus_free_string)
 *
 * Example (compact):
 *   char* xml = taurus_document_serialize(doc, NULL);
 *   printf("%s\n", xml);
 *   taurus_free_string(xml);
 *
 * Example (pretty-print with declaration):
 *   TaurusSerializeOptions opts = { .indent = 2, .xml_declaration = 1, .encoding = "UTF-8" };
 *   char* xml = taurus_document_serialize(doc, &opts);
 *   printf("%s\n", xml);
 *   taurus_free_string(xml);
 *
 * Memory: Caller must free returned string with taurus_free_string()
 */
TAURUS_API char* taurus_document_serialize(TaurusDocument doc,
                                             TaurusSerializeOptions* options);

/**
 * Serialize element subtree to XML string
 *
 * @param elem Element to serialize
 * @param options Serialization options (NULL for defaults)
 * @return XML string or NULL on error (caller must free with taurus_free_string)
 *
 * Memory: Caller must free returned string with taurus_free_string()
 */
TAURUS_API char* taurus_element_serialize(TaurusElement elem,
                                            TaurusSerializeOptions* options);

/**
 * Save document to file
 *
 * @param doc Document to save
 * @param filepath Path to output file
 * @param options Serialization options (NULL for defaults: compact, no declaration)
 * @return TAURUS_OK on success, error code on failure
 *
 * Thread safety: Thread-safe (multiple threads can save different files)
 *
 * Example:
 *   TaurusStatus status = taurus_document_save_file(doc, "output.xml", NULL);
 *   if (status != TAURUS_OK) {
 *     printf("Failed to save: %d\n", status);
 *   }
 */
TAURUS_API TaurusStatus taurus_document_save_file(TaurusDocument doc,
                                                  const char* filepath,
                                                  TaurusSerializeOptions* options);

/* ============================================================================
 * Canonical XML (C14N) Operations
 * ============================================================================ */

/**
 * Canonicalize document to C14N format
 *
 * C14N generates a canonical form of an XML document for:
 * - Digital signatures
 * - Cryptographic hashing
 * - Semantic XML comparison
 *
 * Key C14N rules applied:
 * 1. UTF-8 encoding
 * 2. Normalized line endings (\n)
 * 3. Lexicographic attribute ordering
 * 4. Namespace declaration ordering
 * 5. Empty element normalization (<tag></tag> not <tag/>)
 * 6. Entity/character reference expansion
 * 7. Attribute value quoting with double quotes
 *
 * @param doc Document to canonicalize
 * @param version C14N version (TAURUS_C14N_1_0 or TAURUS_C14N_1_1)
 * @param flags Reserved for future use (pass 0)
 * @return Canonicalized XML string (caller must free with taurus_free_string())
 *
 * Memory: Caller must free returned string with taurus_free_string()
 */
TAURUS_API char* taurus_c14n_canonicalize(TaurusDocument doc,
                                          int version,
                                          int flags);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_DOM_SERIALIZE_H */
