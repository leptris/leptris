/**
 * @file sax.h
 * @brief SAX (Simple API for XML) event-based parsing interface
 *
 * Provides event-driven XML parsing without building a DOM tree.
 * Useful for streaming large XML files or when only specific data is needed.
 */

#ifndef LEPTRIS_SAX_H
#define LEPTRIS_SAX_H

#include <stddef.h>

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
#    define LEPTRIS_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct LeptrisSAXHandler LeptrisSAXHandler;
typedef struct LeptrisSAXParser LeptrisSAXParser;

/**
 * SAX event handler callbacks
 *
 * All callbacks are optional (can be NULL). Set only the events you need.
 * The user_data pointer is passed to all callbacks for custom context.
 */
struct LeptrisSAXHandler {
    /**
     * Called when parsing starts
     * @param user_data User-provided context pointer
     */
    void (*start_document)(void* user_data);

    /**
     * Called when parsing completes successfully
     * @param user_data User-provided context pointer
     */
    void (*end_document)(void* user_data);

    /**
     * Called when an element opening tag is encountered
     *
     * @param user_data User-provided context pointer
     * @param name Element name (UTF-8, null-terminated)
     * @param attrs NULL-terminated array of attribute name-value pairs
     *              Format: [name1, value1, name2, value2, ..., NULL]
     *              Empty array if no attributes
     *
     * Example:
     *   &lt;book id="123" title="XML Guide"&gt;
     *   -> start_element(data, "book", ["id", "123", "title", "XML Guide", NULL])
     */
    void (*start_element)(void* user_data, const char* name, const char** attrs);

    /**
     * Called when an element closing tag is encountered
     *
     * @param user_data User-provided context pointer
     * @param name Element name (UTF-8, null-terminated)
     *
     * Example:
     *   &lt;/book&gt;
     *   -> end_element(data, "book")
     */
    void (*end_element)(void* user_data, const char* name);

    /**
     * Called for character data (text content)
     *
     * Entity and character references are expanded before delivery:
     * "&amp;" arrives as "&", "&#65;" as "A" (XML 1.0 section 2.4).
     * Attribute values in start_element are expanded the same way
     * (XML 1.0 section 3.3.3).
     *
     * May be called multiple times for a single text node if the text
     * is long, contains references, or spans chunk boundaries in
     * incremental parsing. Concatenate all calls between start_element
     * and end_element to get complete text.
     *
     * @param user_data User-provided context pointer
     * @param text Text content (UTF-8, NOT null-terminated)
     * @param len Length of text in bytes
     *
     * Note: text is NOT null-terminated. Use len to determine bounds.
     *
     * Example:
     *   &lt;book&gt;XML Programming&lt;/book&gt;
     *   -> characters(data, "XML Programming", 15)
     */
    void (*characters)(void* user_data, const char* text, size_t len);

    /**
     * Called for XML comments
     *
     * @param user_data User-provided context pointer
     * @param comment Comment content (UTF-8, null-terminated)
     *
     * Example:
     *   <!-- This is a comment -->
     *   -> comment(data, " This is a comment ")
     */
    void (*comment)(void* user_data, const char* comment);

    /**
     * Called for CDATA sections
     *
     * @param user_data User-provided context pointer
     * @param cdata CDATA content (UTF-8, null-terminated)
     *
     * Example:
     *   <![CDATA[<html>]]>
     *   -> cdata(data, "<html>")
     */
    void (*cdata)(void* user_data, const char* cdata);

    /**
     * Called for processing instructions
     *
     * @param user_data User-provided context pointer
     * @param target PI target name (UTF-8, null-terminated)
     * @param data PI data (UTF-8, null-terminated, may be NULL)
     *
     * Example:
     *   <?xml-stylesheet type="text/xsl" href="style.xsl"?>
     *   -> processing_instruction(data, "xml-stylesheet", "type=\"text/xsl\" href=\"style.xsl\"")
     */
    void (*processing_instruction)(void* user_data, const char* target, const char* data);

    /**
     * Called when a namespace prefix mapping starts
     *
     * This is called before the start_element callback for the element
     * that declares the namespace.
     *
     * @param user_data User-provided context pointer
     * @param prefix Namespace prefix (UTF-8, null-terminated, "" for default namespace)
     * @param uri Namespace URI (UTF-8, null-terminated)
     *
     * Example:
     *   &lt;root xmlns:foo="http://example.com"&gt;
     *   -> start_prefix_mapping(data, "foo", "http://example.com")
     *
     *   &lt;root xmlns="http://example.com"&gt;
     *   -> start_prefix_mapping(data, "", "http://example.com")
     */
    void (*start_prefix_mapping)(void* user_data, const char* prefix, const char* uri);

    /**
     * Called when a namespace prefix mapping ends
     *
     * This is called after the end_element callback for the element
     * that declared the namespace.
     *
     * @param user_data User-provided context pointer
     * @param prefix Namespace prefix (UTF-8, null-terminated, "" for default namespace)
     *
     * Example:
     *   &lt;/root&gt;  (where root declared xmlns:foo)
     *   -> end_prefix_mapping(data, "foo")
     */
    void (*end_prefix_mapping)(void* user_data, const char* prefix);

    /**
     * Called for parse errors
     *
     * @param user_data User-provided context pointer
     * @param message Error message (UTF-8, null-terminated)
     * @param line Line number where error occurred (1-based)
     * @param column Column number where error occurred (1-based)
     */
    void (*error)(void* user_data, const char* message, int line, int column);
};

/**
 * Parse XML using SAX event-based interface
 *
 * This function parses XML without building a DOM tree, calling handler
 * callbacks as parsing events occur. More memory-efficient than DOM parsing
 * for large files or when only specific data is needed.
 *
 * @param xml XML string (must be valid UTF-8)
 * @param len Length of XML string in bytes
 * @param handler Event handler with callbacks (required)
 * @param user_data User context pointer passed to all callbacks (can be NULL)
 * @return 0 on success, -1 on error
 *
 * Example:
 *   LeptrisSAXHandler handler = {0};
 *   handler.start_element = my_start_handler;
 *   handler.characters = my_text_handler;
 *
 *   MyContext ctx = {0};
 *   int result = leptris_sax_parse(xml, len, &handler, &ctx);
 *
 * Memory:
 * - No DOM tree is built (memory-efficient)
 * - Callback strings are temporary (copy if you need to keep them)
 * - user_data lifetime managed by caller
 *
 * Thread safety: Not thread-safe. One parse per thread.
 */
LEPTRIS_API int leptris_sax_parse(const char* xml, size_t len,
                                LeptrisSAXHandler* handler,
                                void* user_data);

/**
 * Create SAX parser for streaming/incremental parsing
 *
 * Allows parsing XML in chunks (useful for network streams or large files).
 *
 * @param handler Event handler with callbacks (required)
 * @param user_data User context pointer passed to all callbacks (can be NULL)
 * @return Parser instance or NULL on error
 *
 * Memory: Caller must call leptris_sax_parser_free() when done
 */
LEPTRIS_API LeptrisSAXParser* leptris_sax_parser_create(LeptrisSAXHandler* handler, void* user_data);

/**
 * Feed XML chunk to incremental parser
 *
 * @param parser SAX parser instance
 * @param xml XML chunk (must be valid UTF-8)
 * @param len Length of chunk in bytes
 * @param is_final True if this is the last chunk
 * @return 0 on success, -1 on error
 *
 * Example:
 *   LeptrisSAXParser* parser = leptris_sax_parser_create(&handler, &ctx);
 *
 *   // Feed in chunks
 *   leptris_sax_parser_feed(parser, chunk1, len1, 0);
 *   leptris_sax_parser_feed(parser, chunk2, len2, 0);
 *   leptris_sax_parser_feed(parser, chunk3, len3, 1); // Final chunk
 *
 *   leptris_sax_parser_free(parser);
 */
LEPTRIS_API int leptris_sax_parser_feed(LeptrisSAXParser* parser,
                                      const char* xml,
                                      size_t len,
                                      int is_final);

/**
 * Free SAX parser
 *
 * @param parser Parser to free (can be NULL)
 */
LEPTRIS_API void leptris_sax_parser_free(LeptrisSAXParser* parser);

/**
 * Opt into the streaming state-machine parser (TODO 116).
 *
 * The legacy parser buffers the entire document between feed()
 * calls, then parses on `is_final=1`.  The streaming path emits
 * events as chunks arrive and uses constant memory bounded by
 * maximum nesting depth, not by document size.
 *
 * Call this *before* the first feed().  When enabled, each
 * leptris_sax_parser_feed() call advances the state machine and
 * emits events immediately; chunks whose token straddles the chunk
 * boundary are buffered internally and resumed on the next feed().
 *
 * @param parser Parser instance
 * @param streaming 1 to enable streaming, 0 to use legacy buffering
 * @return 0 on success, -1 if parser is NULL
 *
 * Thread safety: Not thread-safe. Set once before the first feed().
 */
LEPTRIS_API int leptris_sax_parser_set_streaming(LeptrisSAXParser* parser, int streaming);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_SAX_H */