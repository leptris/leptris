/* libleptris - Serialization Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains document and element serialization operations.
 */

#ifndef LEPTRIS_DOM_SERIALIZE_H
#define LEPTRIS_DOM_SERIALIZE_H

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support */
#ifndef LEPTRIS_API
#  ifdef _WIN32
#    ifdef LEPTRIS_BUILD_SHARED
#      define LEPTRIS_API __declspec(dllexport)
#    elif defined(LEPTRIS_USE_SHARED)
#      define LEPTRIS_API __declspec(dllimport)
#    else
#      define LEPTRIS_API
#    endif
#  else
#    define LEPTRIS_API
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
 * @return XML string or NULL on error (caller must free with leptris_free_string)
 *
 * Example (compact):
 *   char* xml = leptris_document_serialize(doc, NULL);
 *   printf("%s\n", xml);
 *   leptris_free_string(xml);
 *
 * Example (pretty-print with declaration):
 *   LeptrisSerializeOptions opts = { .indent = 2, .xml_declaration = 1, .encoding = "UTF-8" };
 *   char* xml = leptris_document_serialize(doc, &opts);
 *   printf("%s\n", xml);
 *   leptris_free_string(xml);
 *
 * Memory: Caller must free returned string with leptris_free_string()
 */
LEPTRIS_API char* leptris_document_serialize(LeptrisDocument doc,
                                             LeptrisSerializeOptions* options);

/**
 * Serialize element subtree to XML string
 *
 * @param elem Element to serialize
 * @param options Serialization options (NULL for defaults)
 * @return XML string or NULL on error (caller must free with leptris_free_string)
 *
 * Memory: Caller must free returned string with leptris_free_string()
 */
LEPTRIS_API char* leptris_element_serialize(LeptrisElement elem,
                                            LeptrisSerializeOptions* options);

/**
 * Save document to file
 *
 * @param doc Document to save
 * @param filepath Path to output file
 * @param options Serialization options (NULL for defaults: compact, no declaration)
 * @return LEPTRIS_OK on success, error code on failure
 *
 * Thread safety: Thread-safe (multiple threads can save different files)
 *
 * Example:
 *   LeptrisStatus status = leptris_document_save_file(doc, "output.xml", NULL);
 *   if (status != LEPTRIS_OK) {
 *     printf("Failed to save: %d\n", status);
 *   }
 */
LEPTRIS_API LeptrisStatus leptris_document_save_file(LeptrisDocument doc,
                                                  const char* filepath,
                                                  LeptrisSerializeOptions* options);

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
 * 5. Empty element normalization (&lt;tag&gt;&lt;/tag&gt; not &lt;tag/&gt;)
 * 6. Entity/character reference expansion
 * 7. Attribute value quoting with double quotes
 *
 * @param doc Document to canonicalize
 * @param version C14N version (LEPTRIS_C14N_1_0 or LEPTRIS_C14N_1_1)
 * @param flags Reserved for future use (pass 0)
 * @return Canonicalized XML string (caller must free with leptris_free_string())
 *
 * Memory: Caller must free returned string with leptris_free_string()
 */
LEPTRIS_API char* leptris_c14n_canonicalize(LeptrisDocument doc,
                                          int version,
                                          int flags);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_DOM_SERIALIZE_H */
