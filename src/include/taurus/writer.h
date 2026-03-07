/**
 * @file writer.h
 * @brief StAX-style streaming XML writer API
 *
 * Provides a streaming XML writer interface for memory-efficient generation
 * of XML documents. Complementary to the SAX parser's pull-style reading.
 *
 * Key features:
 * - Streaming output (no DOM tree built)
 * - Configurable encoding (UTF-8, ASCII)
 * - Pretty-printing support
 * - Namespace-aware element/attribute writing
 * - Entity escaping with lookup table optimization
 *
 * Performance characteristics:
 * - 8KB output buffer with small write coalescing
 * - Pre-computed escape lookup tables (branch-free)
 * - Direct byte output for UTF-8 (no encoding conversion)
 *
 * Example usage:
 * @code
 *   TaurusWriterOptions opts = {
 *       .indent = 2,
 *       .pretty_print = 1,
 *       .xml_declaration = 1
 *   };
 *   TaurusXMLWriter* w = taurus_writer_create_file_ex("output.xml", &opts);
 *
 *   taurus_writer_start_document(w, "1.0", "UTF-8", 0);
 *   taurus_writer_start_element(w, "root");
 *   taurus_writer_attribute(w, "id", "123");
 *   taurus_writer_characters(w, "Hello, World!");
 *   taurus_writer_end_element(w);
 *   taurus_writer_end_document(w);
 *   taurus_writer_free(w);
 * @endcode
 */

#ifndef TAURUS_WRITER_H
#define TAURUS_WRITER_H

#include <stddef.h>
#include <stdio.h>

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
 * Opaque Types
 * ============================================================================ */

/**
 * Opaque XML writer handle
 */
typedef struct TaurusXMLWriter TaurusXMLWriter;

/* ============================================================================
 * Writer Options
 * ============================================================================ */

/**
 * Writer configuration options
 */
typedef struct {
    int indent;                 /* Spaces per indent level (0 = compact output) */
    int pretty_print;           /* Add newlines and indentation (0 = no, 1 = yes) */
    int xml_declaration;        /* Write XML declaration (0 = no, 1 = yes) */
    const char* encoding;       /* Output encoding (default: "UTF-8") */
    int validate_names;         /* Check XML name validity (0 = no, 1 = yes) */
    int escape_cr;              /* Escape \r as &#xD; (0 = no, 1 = yes) */
} TaurusWriterOptions;

/**
 * Default options initializer
 *
 * Usage:
 *   TaurusWriterOptions opts = TAURUS_WRITER_OPTIONS_DEFAULT;
 *   opts.indent = 4;
 */
#define TAURUS_WRITER_OPTIONS_DEFAULT { \
    .indent = 0, \
    .pretty_print = 0, \
    .xml_declaration = 1, \
    .encoding = "UTF-8", \
    .validate_names = 0, \
    .escape_cr = 0 \
}

/* ============================================================================
 * Writer Creation / Destruction
 * ============================================================================ */

/**
 * Create writer for file path
 *
 * Opens the specified file for writing. The file is closed when the writer
 * is freed.
 *
 * @param filepath Path to output file (UTF-8 encoded)
 * @param encoding Output encoding (NULL = "UTF-8")
 * @return Writer handle or NULL on error
 *
 * Memory: Caller must call taurus_writer_free() when done
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_file(const char* filepath,
                                                       const char* encoding);

/**
 * Create writer for FILE* stream
 *
 * Writes to the provided FILE* stream. The caller owns the FILE* and must
 * close it after freeing the writer.
 *
 * @param stream FILE* to write to (must be opened for writing)
 * @param encoding Output encoding (NULL = "UTF-8")
 * @return Writer handle or NULL on error
 *
 * Memory: Caller owns FILE*, must close after taurus_writer_free()
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_stream(FILE* stream,
                                                          const char* encoding);

/**
 * Custom output callback function type
 *
 * @param ctx User-provided context pointer
 * @param data Data to write
 * @param len Number of bytes to write
 * @return Number of bytes written, or negative on error
 */
typedef size_t (*TaurusWriteCallback)(void* ctx, const char* data, size_t len);

/**
 * Create writer for custom output callback
 *
 * Allows writing to any destination via callback function.
 *
 * @param cb Callback function for writing data
 * @param ctx User context passed to callback
 * @param encoding Output encoding (NULL = "UTF-8")
 * @return Writer handle or NULL on error
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_callback(TaurusWriteCallback cb,
                                                            void* ctx,
                                                            const char* encoding);

/**
 * Create writer for custom output callback with extended options
 *
 * Allows writing to any destination via callback function with full options.
 *
 * @param cb Callback function for writing data
 * @param ctx User context passed to callback
 * @param opts Writer options (NULL = defaults)
 * @return Writer handle or NULL on error
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_callback_ex(TaurusWriteCallback cb,
                                                               void* ctx,
                                                               TaurusWriterOptions* opts);

/**
 * Create writer for file path with extended options
 *
 * @param filepath Path to output file
 * @param opts Writer options (NULL = defaults)
 * @return Writer handle or NULL on error
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_file_ex(const char* filepath,
                                                           TaurusWriterOptions* opts);

/**
 * Create writer for FILE* stream with extended options
 *
 * @param stream FILE* to write to
 * @param opts Writer options (NULL = defaults)
 * @return Writer handle or NULL on error
 */
TAURUS_API TaurusXMLWriter* taurus_writer_create_stream_ex(FILE* stream,
                                                             TaurusWriterOptions* opts);

/**
 * Free writer and release all resources
 *
 * Flushes any remaining buffered data before freeing.
 *
 * @param writer Writer to free (can be NULL)
 */
TAURUS_API void taurus_writer_free(TaurusXMLWriter* writer);

/* ============================================================================
 * Document Structure
 * ============================================================================ */

/**
 * Write XML declaration
 *
 * Must be called first if XML declaration is desired.
 *
 * @param writer Writer handle
 * @param version XML version ("1.0" or "1.1", NULL = "1.0")
 * @param encoding Encoding declaration (NULL = use writer's encoding)
 * @param standalone Standalone declaration (-1 = omit, 0 = "no", 1 = "yes")
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_document(w, "1.0", "UTF-8", -1);  // <?xml version="1.0" encoding="UTF-8"?>
 *   taurus_writer_start_document(w, "1.0", NULL, 1);      // <?xml version="1.0" standalone="yes"?>
 */
TAURUS_API int taurus_writer_start_document(TaurusXMLWriter* writer,
                                             const char* version,
                                             const char* encoding,
                                             int standalone);

/**
 * End document
 *
 * Closes any open elements and flushes the buffer.
 * After calling this, no more content can be written.
 *
 * @param writer Writer handle
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_end_document(TaurusXMLWriter* writer);

/* ============================================================================
 * Elements
 * ============================================================================ */

/**
 * Write start element tag
 *
 * Opens a new element. After calling this, you can write attributes,
 * content, or nested elements. Must be paired with taurus_writer_end_element().
 *
 * @param writer Writer handle
 * @param name Element name (must be valid XML name)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_element(w, "book");
 *   // ... write attributes, content, children ...
 *   taurus_writer_end_element(w);
 *   // Output: <book ...>...</book>
 */
TAURUS_API int taurus_writer_start_element(TaurusXMLWriter* writer,
                                            const char* name);

/**
 * Write start element with namespace
 *
 * Opens a new element with explicit namespace prefix and/or URI.
 *
 * @param writer Writer handle
 * @param prefix Namespace prefix (NULL or "" for default namespace)
 * @param uri Namespace URI (NULL to omit)
 * @param localname Local element name
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_element_ns(w, "xs", "http://www.w3.org/2001/XMLSchema", "schema");
 *   // Output: <xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
 */
TAURUS_API int taurus_writer_start_element_ns(TaurusXMLWriter* writer,
                                               const char* prefix,
                                               const char* uri,
                                               const char* localname);

/**
 * Write end element tag
 *
 * Closes the most recently opened element. Must match a previous
 * start_element call.
 *
 * @param writer Writer handle
 * @return 0 on success, negative on error (e.g., no open element)
 */
TAURUS_API int taurus_writer_end_element(TaurusXMLWriter* writer);

/**
 * Write empty element (self-closing)
 *
 * Writes an element with no content in one call.
 *
 * @param writer Writer handle
 * @param name Element name
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_empty_element(w, "br");
 *   // Output: <br/>
 */
TAURUS_API int taurus_writer_empty_element(TaurusXMLWriter* writer,
                                            const char* name);

/**
 * Write empty element with namespace
 *
 * @param writer Writer handle
 * @param prefix Namespace prefix (NULL or "" for default)
 * @param uri Namespace URI (NULL to omit)
 * @param localname Local element name
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_empty_element_ns(TaurusXMLWriter* writer,
                                               const char* prefix,
                                               const char* uri,
                                               const char* localname);

/* ============================================================================
 * Attributes
 * ============================================================================ */

/**
 * Write attribute
 *
 * Must be called immediately after start_element() and before any content
 * or end_element().
 *
 * @param writer Writer handle
 * @param name Attribute name
 * @param value Attribute value (will be escaped)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_element(w, "book");
 *   taurus_writer_attribute(w, "id", "123");
 *   taurus_writer_attribute(w, "title", "XML & <Quotes>");
 *   taurus_writer_end_element(w);
 *   // Output: <book id="123" title="XML &amp; &lt;Quotes&gt;"/>
 */
TAURUS_API int taurus_writer_attribute(TaurusXMLWriter* writer,
                                        const char* name,
                                        const char* value);

/**
 * Write attribute with explicit length
 *
 * Like taurus_writer_attribute() but allows non-null-terminated values.
 *
 * @param writer Writer handle
 * @param name Attribute name
 * @param value Attribute value (need not be null-terminated)
 * @param value_len Length of value in bytes
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_attribute_len(TaurusXMLWriter* writer,
                                            const char* name,
                                            const char* value,
                                            size_t value_len);

/**
 * Write attribute with namespace
 *
 * @param writer Writer handle
 * @param prefix Namespace prefix (NULL or "" for default)
 * @param uri Namespace URI (NULL to omit)
 * @param localname Local attribute name
 * @param value Attribute value
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_attribute_ns(TaurusXMLWriter* writer,
                                           const char* prefix,
                                           const char* uri,
                                           const char* localname,
                                           const char* value);

/* ============================================================================
 * Text Content
 * ============================================================================ */

/**
 * Write text content
 *
 * Writes text with entity escaping (<, >, &, etc.).
 * Can be called multiple times for the same element.
 *
 * @param writer Writer handle
 * @param text Text to write (null-terminated)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_element(w, "p");
 *   taurus_writer_characters(w, "Hello <world> & friends!");
 *   taurus_writer_end_element(w);
 *   // Output: <p>Hello &lt;world&gt; &amp; friends!</p>
 */
TAURUS_API int taurus_writer_characters(TaurusXMLWriter* writer,
                                         const char* text);

/**
 * Write text content with explicit length
 *
 * Like taurus_writer_characters() but allows non-null-terminated text.
 *
 * @param writer Writer handle
 * @param text Text to write
 * @param len Length of text in bytes
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_characters_len(TaurusXMLWriter* writer,
                                             const char* text,
                                             size_t len);

/**
 * Write CDATA section
 *
 * Writes content wrapped in <![CDATA[...]]> without escaping.
 * The content must not contain "]]>".
 *
 * @param writer Writer handle
 * @param data CDATA content (not escaped)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_cdata(w, "<script>alert('hello');</script>");
 *   // Output: <![CDATA[<script>alert('hello');</script>]]>
 */
TAURUS_API int taurus_writer_cdata(TaurusXMLWriter* writer,
                                    const char* data);

/**
 * Write CDATA section with explicit length
 *
 * @param writer Writer handle
 * @param data CDATA content
 * @param len Length of data in bytes
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_cdata_len(TaurusXMLWriter* writer,
                                        const char* data,
                                        size_t len);

/* ============================================================================
 * Other Node Types
 * ============================================================================ */

/**
 * Write XML comment
 *
 * The comment text must not contain "--" or end with "-".
 *
 * @param writer Writer handle
 * @param text Comment text (without <!-- and -->)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_comment(w, " This is a comment ");
 *   // Output: <!-- This is a comment -->
 */
TAURUS_API int taurus_writer_comment(TaurusXMLWriter* writer,
                                      const char* text);

/**
 * Write processing instruction
 *
 * @param writer Writer handle
 * @param target PI target name
 * @param data PI data (can be NULL)
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_processing_instruction(w, "xml-stylesheet", "type=\"text/xsl\" href=\"style.xsl\"");
 *   // Output: <?xml-stylesheet type="text/xsl" href="style.xsl"?>
 */
TAURUS_API int taurus_writer_processing_instruction(TaurusXMLWriter* writer,
                                                     const char* target,
                                                     const char* data);

/* ============================================================================
 * Namespace Declarations
 * ============================================================================ */

/**
 * Write namespace declaration
 *
 * Must be called after start_element() and before end_element() or content.
 *
 * @param writer Writer handle
 * @param prefix Namespace prefix (NULL or "" for default namespace)
 * @param uri Namespace URI
 * @return 0 on success, negative on error
 *
 * Example:
 *   taurus_writer_start_element(w, "root");
 *   taurus_writer_namespace(w, "xs", "http://www.w3.org/2001/XMLSchema");
 *   // Output: <root xmlns:xs="http://www.w3.org/2001/XMLSchema">
 */
TAURUS_API int taurus_writer_namespace(TaurusXMLWriter* writer,
                                        const char* prefix,
                                        const char* uri);

/* ============================================================================
 * Flush and Error Handling
 * ============================================================================ */

/**
 * Flush buffered output
 *
 * Forces any buffered data to be written to the output.
 *
 * @param writer Writer handle
 * @return 0 on success, negative on error
 */
TAURUS_API int taurus_writer_flush(TaurusXMLWriter* writer);

/**
 * Get last error code
 *
 * @param writer Writer handle
 * @return Error code (0 = no error, negative = error)
 */
TAURUS_API int taurus_writer_get_error(TaurusXMLWriter* writer);

/**
 * Get error message for last error
 *
 * @param writer Writer handle
 * @return Error message string (NULL if no error)
 */
TAURUS_API const char* taurus_writer_get_error_message(TaurusXMLWriter* writer);

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define TAURUS_WRITER_OK                    0    /* No error */
#define TAURUS_WRITER_ERROR_MEMORY         -1    /* Memory allocation failed */
#define TAURUS_WRITER_ERROR_IO             -2    /* I/O error */
#define TAURUS_WRITER_ERROR_INVALID_ARG    -3    /* Invalid argument */
#define TAURUS_WRITER_ERROR_INVALID_STATE  -4    /* Invalid state for operation */
#define TAURUS_WRITER_ERROR_INVALID_NAME   -5    /* Invalid XML name */
#define TAURUS_WRITER_ERROR_NESTING        -6    /* Element nesting error */
#define TAURUS_WRITER_ERROR_CDATA          -7    /* Invalid CDATA content (contains ]]> ) */
#define TAURUS_WRITER_ERROR_COMMENT        -8    /* Invalid comment content */

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_WRITER_H */
