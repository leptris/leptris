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
#include "../types.h"

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

/* ============================================================================
 * Pull (StAX-style) API — TODO.bindings/04, issue #510 Tier 2
 * ============================================================================ */

/**
 * Create a host-driven pull parser over an in-memory document
 *
 * The host drives with leptris_pull_next() instead of receiving
 * callbacks — one event per call, no C->host dispatch. Memory stays
 * bounded by the internal input slice, not the document size.
 *
 * @param xml Input (must be valid UTF-8)
 * @param len Input length in bytes
 * @return New puller, or NULL on invalid input / OOM
 *
 * Memory: free with leptris_pull_free. Event strings are owned by
 * the puller and valid only until the next leptris_pull_next call.
 */
LEPTRIS_API LeptrisPullParser leptris_pull_new(const char* xml, size_t len);

/**
 * Create a pull parser streaming from a file (TODO.engine/01)
 *
 * Same event semantics as leptris_pull_new; input is read from disk
 * in bounded slices — no whole-document buffer. Huge documents
 * stream with memory bounded by the internal slice.
 *
 * @param path File path
 * @return New puller, or NULL when the file cannot be opened
 */
LEPTRIS_API LeptrisPullParser leptris_pull_new_file(const char* path);

/**
 * Return the next event, feeding input as needed
 *
 * @param pull Pull parser
 * @return The event (owned by the puller, valid until the next
 *         call), or NULL when the document is exhausted or pull is
 *         NULL. The final events are LEPTRIS_PULL_END_DOCUMENT on
 *         success; LEPTRIS_PULL_ERROR (text = message) on parse
 *         failure.
 */
LEPTRIS_API const LeptrisPullEvent* leptris_pull_next(LeptrisPullParser pull);

/* Attribute accessors — valid only during the START_ELEMENT event
 * most recently returned by leptris_pull_next. */
LEPTRIS_API size_t leptris_pull_attr_count(LeptrisPullParser pull);
LEPTRIS_API const char* leptris_pull_attr_name(LeptrisPullParser pull,
                                               size_t index);
LEPTRIS_API const char* leptris_pull_attr_value(LeptrisPullParser pull,
                                                size_t index);

/**
 * Free the pull parser
 *
 * Memory: invalidates every event and attribute string it handed out.
 */
LEPTRIS_API void leptris_pull_free(LeptrisPullParser pull);

/* ============================================================================
 * Chunked event recorder (issue #585)
 * ============================================================================ */

/**
 * Create a chunked SAX event recorder
 *
 * A SAX parser that BUFFERS events C-side instead of dispatching
 * callbacks: fixed-size LeptrisSaxEventRecord entries plus a packed
 * string arena. Feed a chunk, then read the accumulated records and
 * arena with the two accessors below — one bulk read per chunk, and
 * the host iterates events in host code. The callback count becomes
 * O(chunks), not O(events): through FFI, per-event callback dispatch
 * (the ffi gem's generic machinery) cost more than the parse itself.
 *
 * Each leptris_sax_recorder_feed starts a fresh chunk: records and
 * arena are reset at feed entry, so drain after every feed. Streamed
 * incrementally, memory stays bounded by one chunk's events plus the
 * parser's internal depth state — not the document.
 *
 * Event semantics are identical to the callback API (the recorder is
 * a handler on the same streaming state machine), including entity
 * expansion in text and attribute values.
 *
 * @return New recorder, or NULL on OOM
 *
 * Memory: free with leptris_sax_recorder_free.
 */
LEPTRIS_API LeptrisSaxRecorder leptris_sax_recorder_new(void);

/**
 * Feed one input chunk and buffer the events it produces
 *
 * @param r Recorder
 * @param xml Chunk (must be valid UTF-8)
 * @param len Chunk length in bytes
 * @param is_final 1 when this is the last chunk
 * @return 0 on success, -1 on error (an ERROR event is recorded with
 *         position; records/arena still expose everything parsed)
 */
LEPTRIS_API int leptris_sax_recorder_feed(LeptrisSaxRecorder r,
                                          const char* xml, size_t len,
                                          int is_final);

/**
 * Read the current chunk's buffered event records
 *
 * The pointer is valid until the next feed/free. Records reference
 * strings by offset into the arena from
 * leptris_sax_recorder_arena — read both in the same drain.
 *
 * @param r Recorder
 * @param count Out: number of records (0 when the chunk produced none)
 * @return Record array, or NULL for a NULL recorder
 */
LEPTRIS_API const LeptrisSaxEventRecord* leptris_sax_recorder_records(
    LeptrisSaxRecorder r, size_t* count);

/**
 * Read the current chunk's packed string arena
 *
 * One contiguous byte block holding every name/text/attribute string
 * of the chunk's records. Valid until the next feed/free.
 *
 * @param r Recorder
 * @param len Out: arena length in bytes
 * @return Arena pointer, or NULL for a NULL recorder
 */
LEPTRIS_API const char* leptris_sax_recorder_arena(LeptrisSaxRecorder r,
                                                   size_t* len);

/**
 * Free a recorder
 */
LEPTRIS_API void leptris_sax_recorder_free(LeptrisSaxRecorder r);

/* ============================================================================
 * Incremental (iterparse) API — TODO.bindings/02, issue #510 Tier 2
 * ============================================================================ */

/**
 * Create an incremental tree-iterator over an in-memory document
 *
 * Yields each TOP-LEVEL child element of the document root as it
 * completes. The subtree is materialized in its own pool; calling
 * leptris_iterparse_next releases the previous element and reclaims
 * its memory — bounded by the largest subtree, not the document.
 *
 * v1 limitation: element names are the QName as written; namespace
 * prefixes are not re-resolved (use the DOM path when namespace
 * URIs matter).
 *
 * @param xml Input (must be valid UTF-8)
 * @param len Input length in bytes
 * @return New iterator, or NULL on invalid input / OOM
 *
 * Memory: free with leptris_iterparse_free (releases any element
 * still held).
 */
LEPTRIS_API LeptrisIterparse leptris_iterparse_new(const char* xml,
                                                   size_t len);

/**
 * Create an incremental tree-iterator over a file (TODO.engine/01)
 *
 * Same yield semantics as leptris_iterparse_new, streaming from disk
 * — bounded by the largest subtree, not the file size.
 *
 * @param path File path
 * @return New iterator, or NULL when the file cannot be opened
 */
LEPTRIS_API LeptrisIterparse leptris_iterparse_new_file(const char* path);

/**
 * Return the next completed top-level element
 *
 * The previous element (its whole subtree) is released by this call.
 *
 * @param it Iterator
 * @return Borrowed element, or NULL when the document is exhausted
 *
 * Memory: element + subtree are owned by the iterator; valid until
 * the next leptris_iterparse_next / leptris_iterparse_free call.
 */
LEPTRIS_API LeptrisElement leptris_iterparse_next(LeptrisIterparse it);

LEPTRIS_API void leptris_iterparse_free(LeptrisIterparse it);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_SAX_H */
