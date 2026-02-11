/* libtaurus - Document Operations
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains document-level operations: parsing, freeing,
 * and accessing document roots.
 */

#ifndef TAURUS_DOM_DOCUMENT_H
#define TAURUS_DOM_DOCUMENT_H

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
 * Document Parsing Operations
 * ============================================================================ */

/**
 * Parse XML string into document
 *
 * @param xml XML string (must be valid UTF-8)
 * @param length Length of XML string in bytes
 * @param status Output status code (can be NULL)
 * @return Document handle or NULL on error
 *
 * Memory: Caller must call taurus_document_free() when done
 * Thread safety: Not thread-safe. One document per thread.
 */
TAURUS_API TaurusDocument taurus_parse_string(const char* xml, size_t length, TaurusStatus* status);

/**
 * Parse XML string into document with zero-copy optimization
 *
 * This function modifies the input buffer in-place by NULL-terminating strings.
 * The document retains a reference to the input buffer, which must remain valid
 * for the lifetime of the document.
 *
 * @param xml Writable XML string (must be valid UTF-8, will be modified)
 * @param length Length of XML string in bytes
 * @param status Output status code (can be NULL)
 * @return Document handle or NULL on error
 *
 * Memory:
 * - Caller must call taurus_document_free() when done
 * - Caller must keep xml buffer alive until document is freed
 * - xml buffer will be modified (NULL terminators inserted)
 *
 * Performance: 3-5x faster than taurus_parse_string() due to zero allocations
 * Thread safety: Not thread-safe. One document per thread.
 */
TAURUS_API TaurusDocument taurus_parse_string_inplace(char* xml, size_t length, TaurusStatus* status);

/**
 * Parse XML string with automatic encoding detection and conversion
 *
 * This function automatically detects the encoding from:
 * 1. XML declaration encoding="..." attribute
 * 2. Byte Order Mark (BOM) if present
 * 3. Heuristic detection (UTF-8, UTF-16, ISO-8859-1, etc.)
 *
 * If the encoding is not UTF-8, the input is converted to UTF-8 before parsing.
 * Requires iconv support (enabled by default via vcpkg).
 *
 * @param xml XML string (any encoding)
 * @param length Length of XML string in bytes
 * @param status Output status code (can be NULL)
 * @return Document handle or NULL on error
 *
 * Supported encodings:
 * - UTF-8, UTF-16LE, UTF-16BE, UTF-32LE, UTF-32BE
 * - ISO-8859-1, ISO-8859-2, ISO-8859-15
 * - Windows-1252
 * - Shift_JIS, EUC-JP, GB18030
 *
 * Memory: Caller must call taurus_document_free() when done
 * Thread safety: Not thread-safe. One document per thread.
 */
TAURUS_API TaurusDocument taurus_parse_string_with_encoding(const char* xml, size_t length, TaurusStatus* status);

/**
 * Parse XML string using high-performance compact mode
 *
 * This function uses an optimized internal parser that stores DOM nodes in
 * contiguous memory blocks for maximum parsing speed, then converts to regular
 * format for use.
 *
 * @param xml XML string (must be valid UTF-8)
 * @param length Length of XML string in bytes
 * @param error_out Output error flag (0=success, 1=error)
 * @return Document handle or NULL on error
 *
 * Performance: Significantly faster than taurus_parse_string() for large documents
 * Memory: Caller must call taurus_document_free() when done
 * Thread safety: Not thread-safe. One document per thread.
 */
TAURUS_API TaurusDocument taurus_parse_string_compact(const char* xml, size_t length, int* error_out);

/**
 * Load file into memory buffer
 *
 * Reads entire file into a newly allocated buffer. Caller must free the buffer.
 *
 * @param filepath Path to file to load
 * @param out_size Output parameter for file size (can be NULL)
 * @return Newly allocated buffer containing file contents, or NULL on error
 *
 * Memory: Caller must free the returned buffer with TAURUS_FREE()
 * Thread safety: Thread-safe (multiple threads can load different files)
 */
TAURUS_API char* taurus_load_file(const char* filepath, size_t* out_size);

/**
 * Parse XML file directly
 *
 * Convenience function that loads a file and parses it as XML.
 *
 * @param filepath Path to XML file to parse
 * @param status Output status code (can be NULL)
 * @return Document handle or NULL on error
 *
 * Memory: Caller must call taurus_document_free() when done
 * Thread safety: Not thread-safe. One document per thread.
 */
TAURUS_API TaurusDocument taurus_parse_file(const char* filepath, TaurusStatus* status);

/* ============================================================================
 * Document Management Operations
 * ============================================================================ */

/**
 * Free document and all its elements
 *
 * @param doc Document to free (can be NULL)
 */
TAURUS_API void taurus_document_free(TaurusDocument doc);

/**
 * Get root element of document
 *
 * @param doc Document
 * @return Root element or NULL if document is NULL or empty
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_document_root(TaurusDocument doc);

/**
 * Eagerly convert all StringViews to NULL-terminated strings
 *
 * PERFORMANCE OPTIMIZATION: Call this after parsing to optimize for
 * query-heavy workloads. Eliminates lazy conversion overhead during
 * attribute/element access.
 *
 * For parse-once-serialize workflows, this can be skipped to avoid
 * unnecessary string conversions.
 *
 * @param doc Document to finalize strings for
 * @return 0 on success, -1 on failure
 *
 * Thread safety: Not thread-safe if document is shared between threads.
 * Memory: Strings are allocated from document's memory pool.
 */
TAURUS_API int taurus_document_finalize_strings(TaurusDocument doc);

/**
 * Set strict parsing mode
 *
 * Controls whether the parser should be strict or lenient when parsing XML.
 * In strict mode, the parser will reject malformed XML and return errors.
 * In lenient mode, the parser may attempt to recover from certain errors.
 *
 * @param strict 1 for strict mode, 0 for lenient mode
 *
 * Thread safety: Not thread-safe. Affects all subsequent parsing operations.
 * Note: This is a global setting. Use with caution in multi-threaded environments.
 */
TAURUS_API void taurus_set_strict_mode(int strict);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_DOM_DOCUMENT_H */
