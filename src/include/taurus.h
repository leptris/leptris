/* libtaurus - Pure C XML/XPath library
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Public API header - No Ruby dependencies
 */

#ifndef LIBTAURUS_H
#define LIBTAURUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Export macro for Windows DLL support and binding generators.
 *
 * On Unix the build sets default symbol visibility to hidden
 * (CMAKE_C_VISIBILITY_PRESET=hidden in CMakeLists.txt); TAURUS_API
 * opts back into default visibility for the public surface.
 * Internal helpers stay hidden and never appear in the .so export
 * table. See TODO 80.
 *
 * When TAURUS_FOR_BINDGEN is defined (by bindgen/cffi/ctypes),
 * the macro expands to nothing so the header parses cleanly.
 * See TODO 84. */
#ifdef TAURUS_FOR_BINDGEN
#define TAURUS_API
#else
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
#    define TAURUS_API __attribute__((visibility("default")))
#  endif
#endif
#endif  /* TAURUS_FOR_BINDGEN */

/* ============================================================================
 * Public types — single canonical source in taurus/types.h.
 *
 * taurus.h is the "full API" header; taurus/types.h is the lightweight
 * types-only header.  All shared typedefs, enums, and option structs
 * live in types.h.  See TODO 99.
 * ============================================================================ */
#include "taurus/types.h"

/* ABI sanity asserts — catches accidental struct-field exposure
 * that would change opaque-handle sizes.  See TODO 84.
 * Uses _Static_assert (works on GCC/Clang as extension in C99,
 * standard in C11, and in C++ via static_assert). */
#ifdef __cplusplus
static_assert(sizeof(TaurusDocument)  == sizeof(void*), "ABI");
static_assert(sizeof(TaurusElement)   == sizeof(void*), "ABI");
static_assert(sizeof(TaurusAttribute) == sizeof(void*), "ABI");
static_assert(sizeof(TaurusXPathResult) == sizeof(void*), "ABI");
#else
_Static_assert(sizeof(TaurusDocument)  == sizeof(void*), "ABI");
_Static_assert(sizeof(TaurusElement)   == sizeof(void*), "ABI");
_Static_assert(sizeof(TaurusAttribute) == sizeof(void*), "ABI");
_Static_assert(sizeof(TaurusXPathResult) == sizeof(void*), "ABI");
#endif

/* ============================================================================
 * Node Operations
 * ============================================================================ */

/**
 * Get node type
 *
 * @param node Node handle
 * @return Node type code (0=Element, 1=Text, 2=Comment, 3=CDATA, 4=PI, 5=DOCTYPE, 6=Attribute)
 */
TAURUS_API int taurus_node_get_type(TaurusNodeRef node);

/**
 * Get first child node (any type)
 *
 * @param node Parent node
 * @return First child node (element, text, comment, CDATA, or PI), or NULL if no children
 *
 * Memory: Node is owned by document. Do not free.
 */
TAURUS_API TaurusNodeRef taurus_node_first_child(TaurusNodeRef node);

/**
 * Get last child node (any type)
 *
 * @param node Parent node
 * @return Last child node, or NULL if no children
 *
 * Memory: Node is owned by document. Do not free.
 */
TAURUS_API TaurusNodeRef taurus_node_last_child(TaurusNodeRef node);

/**
 * Get next sibling node (any type)
 *
 * @param node Current node
 * @return Next sibling node, or NULL if node is last child
 *
 * Memory: Node is owned by document. Do not free.
 */
TAURUS_API TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node);

/**
 * Get previous sibling node (any type)
 *
 * @param node Current node
 * @return Previous sibling node, or NULL if node is first child
 *
 * Memory: Node is owned by document. Do not free.
 */
TAURUS_API TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node);

/**
 * Get child count (all node types)
 *
 * @param node Parent node
 * @return Number of child nodes
 */
TAURUS_API size_t taurus_node_child_count(TaurusNodeRef node);

/**
 * Cast node to element (if node is an element)
 *
 * @param node Node handle
 * @return Element handle, or NULL if node is not an element
 *
 * Memory: Element is owned by document. Do not free.
 */
TAURUS_API TaurusElement taurus_node_as_element(TaurusNodeRef node);

/**
 * Cast element to its base node handle (for traversal with the
 * taurus_node_* family of operations).
 *
 * Every element IS-A node (the element struct begins with a TaurusNode
 * header), so this cast is always safe.  The reverse direction is
 * taurus_node_as_element(), which returns NULL for non-element nodes.
 *
 * @param elem Element handle
 * @return Node handle, or NULL if elem is NULL
 */
TAURUS_API TaurusNodeRef taurus_element_as_node(TaurusElement elem);

/**
 * Get text content from text node
 *
 * @param node Node handle (must be TAURUS_NODE_TYPE_TEXT or TAURUS_NODE_TYPE_CDATA)
 * @return Text content, or NULL if node is not a text/CDATA node
 *
 * Memory: String is owned by node. Do not free or modify.
 */
TAURUS_API const char* taurus_text_node_get_content(TaurusNodeRef node);

/**
 * Create a new Text node owned by the given document (issue #167).
 *
 * @param doc Document that will own the new node
 * @param content Text content (NUL-terminated). May be empty ("").
 * @return New node handle, or NULL on allocation failure
 *
 * Memory: Node is owned by doc; released by taurus_document_free.
 *         content is pool-copied.
 */
TAURUS_API TaurusNodeRef taurus_text_node_create(TaurusDocument doc,
                                                  const char* content);

/**
 * Create a new Comment node owned by the given document (issue #167).
 *
 * @param doc Owning document
 * @param content Comment body (NUL-terminated)
 * @return New node handle, or NULL on allocation failure
 */
TAURUS_API TaurusNodeRef taurus_comment_node_create(TaurusDocument doc,
                                                     const char* content);

/**
 * Create a new CDATA node owned by the given document (issue #167).
 *
 * @param doc Owning document
 * @param content CDATA section content (NUL-terminated)
 * @return New node handle, or NULL on allocation failure
 */
TAURUS_API TaurusNodeRef taurus_cdata_node_create(TaurusDocument doc,
                                                   const char* content);

/**
 * Create a new Processing Instruction node (issue #167).
 *
 * @param doc Owning document
 * @param target PI target (e.g. "xml-stylesheet")
 * @param data PI data (may be NULL or empty)
 * @return New node handle, or NULL on allocation failure
 */
TAURUS_API TaurusNodeRef taurus_pi_node_create(TaurusDocument doc,
                                                const char* target,
                                                const char* data);

/**
 * Replace a Text node's content (issue #167).
 *
 * @param node Node handle (must be TEXT or CDATA)
 * @param content New content (NUL-terminated, pool-copied)
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG or
 *         TAURUS_ERROR_INVALID_ARG on bad input
 */
TAURUS_API TaurusStatus taurus_text_node_set_content(TaurusNodeRef node,
                                                      const char* content);

/**
 * Replace a Comment node's content (issue #167).
 */
TAURUS_API TaurusStatus taurus_comment_node_set_content(TaurusNodeRef node,
                                                         const char* content);

/**
 * Replace a CDATA node's content (issue #167).
 */
TAURUS_API TaurusStatus taurus_cdata_node_set_content(TaurusNodeRef node,
                                                       const char* content);

/**
 * Replace a PI node's target (issue #167).
 */
TAURUS_API TaurusStatus taurus_pi_node_set_target(TaurusNodeRef node,
                                                   const char* target);

/**
 * Replace a PI node's data (issue #167).
 */
TAURUS_API TaurusStatus taurus_pi_node_set_data(TaurusNodeRef node,
                                                 const char* data);

/**
 * Get the parent of any node type (issue #168). Works on Text, Comment,
 * CDATA, PI, and Element nodes. Returns NULL if node is detached or is
 * the document root.
 *
 * @param node Node handle
 * @return Parent element, or NULL if no parent
 */
TAURUS_API TaurusElement taurus_node_parent(TaurusNodeRef node);

/**
 * Detach a node from its parent (issue #168). The node remains owned
 * by its document and can be re-attached via taurus_element_append_child.
 *
 * @param node Node to unlink
 * @return TAURUS_OK on success,
 *         TAURUS_ERROR_NULL_ARG if node is NULL,
 *         TAURUS_ERROR_NOT_FOUND if node has no parent
 */
TAURUS_API TaurusStatus taurus_node_unlink(TaurusNodeRef node);

/**
 * Get the source line number where the node's opening token appeared
 * (issue #172). Returns 0 if the node was created programmatically
 * (no source info) or the parser did not record line numbers.
 */
TAURUS_API int taurus_node_line(TaurusNodeRef node);

/**
 * Document-order comparison (issue #172).
 *
 * @return -1 if a precedes b, 0 if equal, 1 if a follows b.
 *         Returns 0 if either is NULL or they are in different documents.
 */
TAURUS_API int taurus_node_compare(TaurusNodeRef a, TaurusNodeRef b);

/**
 * Build a canonical unique XPath string identifying `node` within
 * its document (TODO 148 Phase 3).
 *
 * Format matches Nokogiri's `Node#path`:
 *   - Element: `/{qname}[N]` where N is 1-based position among
 *     same-named element siblings. If the qname is unique among
 *     siblings, `[N]` is omitted.
 *   - Text / CDATA: `/text()`
 *   - Comment: `/comment()`
 *   - Processing Instruction: `/processing-instruction()`
 *   - Root: `/` (a single slash)
 *
 * @param node Node to identify (must not be NULL)
 * @return Newly allocated NUL-terminated string. Caller frees via
 *         `taurus_free_string`. Returns NULL on NULL node or
 *         allocation failure.
 *
 * Memory: Caller-owned; release with `taurus_free_string`.
 */
TAURUS_API char* taurus_node_get_xpath(TaurusNodeRef node);

/**
 * Get comment content
 *
 * @param node Node handle (must be TAURUS_NODE_TYPE_COMMENT)
 * @return Comment content, or NULL if node is not a comment
 *
 * Memory: String is owned by node. Do not free or modify.
 */
TAURUS_API const char* taurus_comment_node_get_content(TaurusNodeRef node);

/**
 * Get CDATA content
 *
 * @param node Node handle (must be TAURUS_NODE_TYPE_CDATA)
 * @return CDATA content, or NULL if node is not a CDATA node
 *
 * Memory: String is owned by node. Do not free or modify.
 */
TAURUS_API const char* taurus_cdata_node_get_content(TaurusNodeRef node);

/**
 * Get processing instruction target
 *
 * @param node Node handle (must be TAURUS_NODE_TYPE_PI)
 * @return PI target, or NULL if node is not a PI
 *
 * Memory: String is owned by node. Do not free or modify.
 */
TAURUS_API const char* taurus_pi_node_get_target(TaurusNodeRef node);

/**
 * Get processing instruction data
 *
 * @param node Node handle (must be TAURUS_NODE_TYPE_PI)
 * @return PI data, or NULL if node is not a PI or has no data
 *
 * Memory: String is owned by node. Do not free or modify.
 */
TAURUS_API const char* taurus_pi_node_get_data(TaurusNodeRef node);

/* ============================================================================
 * Document Operations
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
 * Parse an XML fragment (multiple top-level children allowed) into
 * a synthetic container element owned by `dest_doc` (TODO 148
 * Phase 4).
 *
 * @param xml Fragment source (NUL-terminated not required; length used)
 * @param length Byte length of `xml`
 * @param dest_doc Document that will own the synthetic container
 *                 and its children. May NOT be NULL.
 * @param status Output status (may be NULL)
 * @return Newly created synthetic element named `#document-fragment`,
 *         or NULL on error. The synthetic has no parent reference;
 *         caller may attach or discard it.
 *
 * The synthetic container's children are the parsed top-level
 * nodes. Fragment parsing differs from full-document parsing in
 * that multiple top-level elements are allowed (e.g. `"<a/><b/>"`).
 *
 * Backs `Document#fragment` and `Node#fragment` in the Ruby
 * binding. Caller moves children via `taurus_element_append_child`
 * (which unlinks from the synthetic, see #217).
 *
 * Memory: Synthetic + children are owned by `dest_doc`; released
 *         by `taurus_document_free`.
 */
TAURUS_API TaurusElement taurus_parse_fragment(const char* xml,
                                                size_t length,
                                                TaurusDocument dest_doc,
                                                TaurusStatus* status);

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

/**
 * Free document and all its elements
 *
 * @param doc Document to free (can be NULL)
 */
TAURUS_API void taurus_document_free(TaurusDocument doc);

/**
 * Adopt a child document into the parent's lifecycle.
 *
 * TODO 117: used by xi:include parse="xml" to transfer ownership
 * of a freshly-parsed included document so its pool (which now
 * owns nodes spliced into the parent tree) survives until the
 * parent is freed.  Sets `child->child_docs` to NULL so the child
 * doesn't recursively carry its own adopted docs.
 *
 * @param parent Owning document (must outlive child)
 * @param child  Adopted document (its pool is kept alive by `parent`)
 */
TAURUS_API void taurus_document_adopt_child(TaurusDocument parent,
                                           TaurusDocument child);

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
 * Get document encoding string
 *
 * @param doc Document
 * @return Encoding string (e.g. "UTF-8") or NULL if not set
 *
 * Memory: String is owned by document. Do not free.
 */
TAURUS_API const char* taurus_document_encoding(TaurusDocument doc);

/**
 * Get the document's internal DTD subset — the DOCTYPE declaration
 * (TODO 148 Phase 2).
 *
 * @param doc Document handle
 * @return Opaque `TaurusDoctype` handle, or NULL if the document
 *         has no DOCTYPE.
 *
 * Use the `taurus_doctype_*` accessors below to read the name,
 * public identifier, system identifier, and internal subset.
 *
 * Memory: Handle is owned by the document; released by
 *         `taurus_document_free`.
 */
TAURUS_API TaurusDoctype taurus_document_internal_subset(TaurusDocument doc);

/**
 * Get the DOCTYPE's root element name (the name following
 * `<!DOCTYPE`).
 *
 * For `<!DOCTYPE html ...>`, returns `"html"`.
 *
 * @param dt DOCTYPE handle (must not be NULL)
 * @return Root element name, or NULL if `dt` is NULL
 *
 * Memory: String is owned by the document; do not free.
 */
TAURUS_API const char* taurus_doctype_get_name(TaurusDoctype dt);

/**
 * Get the DOCTYPE's root element name (alias matching the libxml2
 * / Nokogiri `DocType#name` convention).
 *
 * @param dt DOCTYPE handle
 * @return Same value as `taurus_doctype_get_name`
 */
TAURUS_API const char* taurus_doctype_get_root_name(TaurusDoctype dt);

/**
 * Get the DOCTYPE's PUBLIC identifier.
 *
 * For `<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0//EN" "...">`
 * returns `"-//W3C//DTD XHTML 1.0//EN"`. Returns NULL for SYSTEM
 * declarations and bare `<!DOCTYPE html>` declarations.
 *
 * @param dt DOCTYPE handle
 * @return Public identifier, or NULL if not declared
 */
TAURUS_API const char* taurus_doctype_get_public_id(TaurusDoctype dt);

/**
 * Get the DOCTYPE's SYSTEM identifier.
 *
 * For `<!DOCTYPE html SYSTEM "html.dtd">` returns `"html.dtd"`.
 * Returns NULL if not declared.
 *
 * @param dt DOCTYPE handle
 * @return System identifier, or NULL if not declared
 */
TAURUS_API const char* taurus_doctype_get_system_id(TaurusDoctype dt);

/**
 * Get the DOCTYPE's internal DTD subset — the contents of the
 * `[...]` block following the name and external identifiers.
 *
 * For `<!DOCTYPE root [<!ENTITY foo "bar">]>` returns
 * `<!ENTITY foo "bar">`. Returns NULL if no internal subset.
 *
 * @param dt DOCTYPE handle
 * @return Internal subset source, or NULL if empty
 */
TAURUS_API const char* taurus_doctype_get_internal_subset(TaurusDoctype dt);

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
 * Thread safety: __thread (TODO 27 phase 1) — each thread has its
 * own default.  Documents created after this call inherit the value
 * at creation time.  Per-document override: taurus_document_set_strict.
 */
TAURUS_API void taurus_set_strict_mode(int strict);

/**
 * Set strict mode on a specific document (TODO 38).
 *
 * Overrides the thread-default for this document only.  Useful when
 * an application wants to mix strict and lenient parsing in the same
 * thread.
 *
 * @param doc Document to modify.
 * @param strict 1 for strict mode, 0 for lenient mode.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if doc is NULL.
 *
 * Memory: No allocation.
 */
TAURUS_API TaurusStatus taurus_document_set_strict(TaurusDocument doc, int strict);

/**
 * Get strict mode for a specific document.
 *
 * Returns the per-document setting (set via taurus_document_set_strict)
 * or the thread-default if never explicitly set.
 *
 * @param doc Document to query.
 * @return 1 if strict, 0 if lenient, 0 if doc is NULL.
 */
TAURUS_API int taurus_document_get_strict(TaurusDocument doc);

/**
 * Get the thread-default strict mode.
 *
 * Documents inherit this value at creation time unless
 * taurus_document_set_strict overrides it.
 *
 * @return 1 if strict default, 0 if lenient.
 */
TAURUS_API int taurus_get_strict_mode(void);

/* ============================================================================
 * Document Freeze API (TODO 88)
 *
 * A frozen document is marked immutable. Mutation functions SHOULD
 * check the frozen flag and refuse to modify a frozen document.
 * (Currently the flag is advisory — mutation functions do not yet
 * enforce it. COW deep-copy on mutation is a future enhancement.)
 * ============================================================================ */

/**
 * Freeze a document, marking all its nodes as immutable.
 *
 * The freeze flag is **advisory only** — it does not cause mutation
 * functions (set_attribute, append_child, etc.) to return an error.
 * Callers who want to honor the freeze should check
 * `taurus_document_is_frozen` before mutating.
 *
 * Rationale: the alternative ("freeze = read-only, mutations reject")
 * breaks the common pattern of parse-then-modify, since every
 * freshly-parsed document is auto-frozen by the parser.  Users who
 * want true immutability should keep the document pointer private
 * and check `is_frozen` at their own API boundaries.
 *
 * @param doc Document to freeze. Must not be NULL.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if doc is NULL.
 */
TAURUS_API TaurusStatus taurus_document_freeze(TaurusDocument doc);

/**
 * Check if a document has been frozen (advisory — see
 * taurus_document_freeze for the full contract).
 *
 * @param doc Document to query. NULL returns 0.
 * @return 1 if frozen, 0 if mutable.
 */
TAURUS_API int taurus_document_is_frozen(TaurusDocument doc);

/**
 * Set the thread-default maximum element-nesting depth.
 *
 * Documents deeper than this are rejected with TAURUS_ERROR_PARSE to
 * prevent stack-overflow crashes.  Default: 256 (matches libxml2).
 *
 * Set to 0 to restore the default.
 *
 * @param max_depth Maximum depth, or 0 for default.
 *
 * Thread safety: __thread — each thread has its own default.
 */
TAURUS_API void taurus_set_max_depth(int max_depth);

/**
 * Get the thread-default maximum element-nesting depth.
 *
 * @return The effective depth (always > 0; returns the default if unset).
 */
TAURUS_API int taurus_get_max_depth(void);

/* ============================================================================
 * Element Operations
 * ============================================================================ */

/**
 * Get element name
 *
 * @param elem Element
 * @return Element name or NULL if elem is NULL
 *
 * Memory: String is owned by element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_name(TaurusElement elem);

/**
 * Get element text content (concatenation of all text nodes)
 *
 * @param elem Element
 * @return Text content, or "" if elem is NULL or has no text
 *
 * Memory: String is owned by the document. Do not free or modify. It stays
 * valid until taurus_document_free(). When the element's only child is a text
 * or CDATA node the node's own storage is returned; mixed content is
 * concatenated into the document pool.
 */
TAURUS_API const char* taurus_element_text(TaurusElement elem);

/**
 * Get element text content as integer
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid integer
 * @return Text content as integer, or default_value
 *
 * Converts element text content to int using strtol(). Supports decimal and hexadecimal (0x) formats.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
TAURUS_API int taurus_element_text_int(TaurusElement elem, int default_value);

/**
 * Get element text content as unsigned integer
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid integer
 * @return Text content as unsigned integer, or default_value
 *
 * Converts element text content to unsigned int using strtoul(). Supports decimal and hexadecimal (0x) formats.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
TAURUS_API unsigned int taurus_element_text_uint(TaurusElement elem, unsigned int default_value);

/**
 * Get element text content as double
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid number
 * @return Text content as double, or default_value
 *
 * Converts element text content to double using strtod().
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
TAURUS_API double taurus_element_text_double(TaurusElement elem, double default_value);

/**
 * Get element text content as float
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid number
 * @return Text content as float, or default_value
 *
 * Converts element text content to float using strtof().
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
TAURUS_API float taurus_element_text_float(TaurusElement elem, float default_value);

/**
 * Get element text content as boolean
 *
 * @param elem Element
 * @param default_value Value to return if element is NULL, has no text, or text is not a valid boolean
 * @return Text content as boolean (1 for true, 0 for false), or default_value
 *
 * Parses text content as boolean. Accepts: "true", "1" (case-insensitive) for true;
 * "false", "0" (case-insensitive) for false.
 * Returns default_value if element is NULL, has no text, or text cannot be parsed.
 */
TAURUS_API int taurus_element_text_bool(TaurusElement elem, int default_value);

/**
 * Get attribute value by name
 *
 * @param elem Element
 * @param name Attribute name
 * @return Attribute value or NULL if not found
 *
 * Memory: String is owned by element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_attribute(TaurusElement elem, const char* name);

/**
 * Test whether an attribute is present on this element.
 *
 * @param elem Element
 * @param name Attribute name
 * @return 1 if the attribute exists, 0 otherwise
 *
 * Memory: None. Convenient alternative to
 * `taurus_element_attribute(elem, name) != NULL` for callers that
 * only need the boolean answer (issue #166-class visibility gap
 * reported in the v0.5.13 audit).
 */
TAURUS_API int taurus_element_has_attribute(TaurusElement elem, const char* name);

/**
 * Get attribute value as integer
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as integer, or default_value if not found
 *
 * Converts attribute value to int using atoi(). Returns default_value
 * if attribute doesn't exist or value is empty.
 */
TAURUS_API int taurus_element_attribute_int(TaurusElement elem, const char* name, int default_value);

/**
 * Get attribute value as double
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as double, or default_value if not found
 *
 * Converts attribute value to double using atof(). Returns default_value
 * if attribute doesn't exist or value is empty.
 */
TAURUS_API double taurus_element_attribute_double(TaurusElement elem, const char* name, double default_value);

/**
 * Get attribute value as boolean
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as boolean, or default_value if not found
 *
 * Returns true if attribute exists and value is "true" or "1" (case-insensitive).
 * Returns false if attribute exists and value is "false" or "0" (case-insensitive).
 * Returns default_value if attribute doesn't exist.
 */
TAURUS_API int taurus_element_attribute_bool(TaurusElement elem, const char* name, int default_value);

/**
 * Get attribute value as unsigned integer
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as unsigned int, or default_value if not found
 *
 * Converts attribute value to unsigned int using strtoul(). Returns default_value
 * if attribute doesn't exist or value is empty/invalid.
 */
TAURUS_API unsigned int taurus_element_attribute_uint(TaurusElement elem, const char* name, unsigned int default_value);

/**
 * Get attribute value as float
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value as float, or default_value if not found
 *
 * Converts attribute value to float using strtof(). Returns default_value
 * if attribute doesn't exist or value is empty/invalid.
 */
TAURUS_API float taurus_element_attribute_float(TaurusElement elem, const char* name, float default_value);

/**
 * Get attribute value as string with default
 *
 * @param elem Element
 * @param name Attribute name
 * @param default_value Value to return if attribute not found
 * @return Attribute value, or default_value if not found
 *
 * Memory: String is owned by element. Do not free or modify.
 * The default_value string must remain valid for the duration of use.
 */
TAURUS_API const char* taurus_element_attribute_string(TaurusElement elem, const char* name, const char* default_value);

/**
 * Get number of attributes on an element
 *
 * @param elem Element
 * @return Attribute count or 0 if elem is NULL
 */
TAURUS_API size_t taurus_element_attribute_count(TaurusElement elem);

/**
 * Get name of attribute by index
 *
 * @param elem Element
 * @param index Attribute index (0-based)
 * @return Attribute name or NULL if index out of range
 *
 * Memory: String is owned by element. Do not free.
 */
TAURUS_API const char* taurus_element_attribute_name_at(TaurusElement elem, size_t index);

/**
 * Get value of attribute by index
 *
 * @param elem Element
 * @param index Attribute index (0-based)
 * @return Attribute value or NULL if index out of range
 *
 * Memory: String is owned by element. Do not free.
 */
TAURUS_API const char* taurus_element_attribute_value_at(TaurusElement elem, size_t index);

/**
 * Get number of child elements
 *
 * @param elem Element
 * @return Number of children or 0 if elem is NULL
 */
TAURUS_API size_t taurus_element_child_count(TaurusElement elem);

/**
 * Get child element by index
 *
 * @param elem Element
 * @param index Child index (0-based)
 * @return Child element or NULL if index out of bounds
 *
 * Memory: Element is owned by document. Do not free separately.
 *
 * Performance: **O(index)**.  Walks the linked-list of children from
 * `first_child` to reach the requested index.  The legacy index-cache
 * (`children_array`) was removed in TODO 90 Phase 1 to shrink the
 * element struct; sequential iteration via
 * `taurus_element_first_child_any` + `next_sibling_any` is O(1) per
 * step and is the recommended pattern when iterating all children.
 */
TAURUS_API TaurusElement taurus_element_child(TaurusElement elem, size_t index);

/**
 * Get parent element
 *
 * @param elem Element
 * @return Parent element or NULL if elem is root or NULL
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_parent(TaurusElement elem);

/**
 * Get root element from any element in the document
 *
 * @param elem Any element in the document
 * @return Root element of the document, or NULL if elem is NULL or not in a document
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_root(TaurusElement elem);

/**
 * Get text value of first child text node
 *
 * @param elem Element
 * @return Text content of first child text node, or NULL if no text child
 *
 * Returns the text content of the first child text node.
 * If the element has no children or the first child is not a text node, returns NULL.
 *
 * Memory: String is owned by element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_child_value(TaurusElement elem);

/**
 * Remove all children from element
 *
 * @param elem Element to remove children from
 * @return TAURUS_OK on success, error code otherwise
 *
 * Removes all child elements and nodes from the element.
 */
TAURUS_API TaurusStatus taurus_element_remove_children(TaurusElement elem);

/**
 * Get hash value of element for comparison
 *
 * @param elem Element
 * @return Hash value based on element name and attributes
 *
 * Computes a hash value for the element based on its name and attributes.
 * This can be used for quick comparison between elements.
 * Returns 0 if elem is NULL.
 */
TAURUS_API size_t taurus_element_hash_value(TaurusElement elem);

/* ============================================================================
 * Element Modification Operations
 * ============================================================================ */

/**
 * Create new element in document
 *
 * @param doc Document that will own the element
 * @param name Element name (without namespace prefix)
 * @return New element or NULL on error
 *
 * Memory: Element owned by document, freed when document freed
 */
TAURUS_API TaurusElement taurus_element_create(TaurusDocument doc, const char* name);

/**
 * Set element name (rename element tag)
 *
 * @param elem Element to rename
 * @param new_name New element name (will be copied)
 * @return TAURUS_OK on success, error code otherwise
 *
 * Example:
 *   // Change <old> to <new>
 *   taurus_element_set_name(elem, "new");
 *
 * Memory: Name is copied (pooled for pool documents)
 */
TAURUS_API TaurusStatus taurus_element_set_name(TaurusElement elem, const char* new_name);

/**
 * Append child element
 *
 * @param parent Parent element
 * @param child Child element to append
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_append_child(TaurusElement parent, TaurusElement child);

/**
 * Prepend child element at the beginning
 *
 * @param parent Parent element
 * @param child Child element to prepend
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_prepend_child(TaurusElement parent, TaurusElement child);

/**
 * Insert new node before a sibling
 *
 * @param sibling Sibling element to insert before
 * @param new_node New element to insert
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_insert_before(TaurusElement sibling, TaurusElement new_node);

/**
 * Insert new node after a sibling
 *
 * @param sibling Sibling element to insert after
 * @param new_node New element to insert
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_insert_after(TaurusElement sibling, TaurusElement new_node);

/**
 * Remove child element
 *
 * @param parent Parent element
 * @param child Child element to remove
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_remove_child(TaurusElement parent, TaurusElement child);

/**
 * Remove all children from element
 *
 * @param elem Element to remove children from
 * @return TAURUS_OK on success, error code otherwise
 *
 * Removes all child nodes from the element, making it empty.
 * The removed nodes are freed and should not be accessed afterwards.
 */
TAURUS_API TaurusStatus taurus_element_remove_children(TaurusElement elem);

/**
 * Set element text content
 *
 * @param elem Element
 * @param text New text content (will be copied)
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_set_text(TaurusElement elem, const char* text);

/**
 * Set attribute value (creates if doesn't exist, updates if exists)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Attribute value (will be copied)
 * @return TAURUS_OK on success, error code otherwise
 */
TAURUS_API TaurusStatus taurus_element_set_attribute(TaurusElement elem,
                                                       const char* name,
                                                       const char* value);

/**
 * Set attribute value as double (converts to string)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Numeric value to set
 * @return TAURUS_OK on success, error code otherwise
 *
 * Converts the double value to a string and sets it as an attribute.
 */
TAURUS_API TaurusStatus taurus_element_set_attribute_double(TaurusElement elem,
                                                             const char* name,
                                                             double value);

/**
 * Set attribute value as float (converts to string)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Numeric value to set
 * @return TAURUS_OK on success, error code otherwise
 *
 * Converts the float value to a string and sets it as an attribute.
 */
TAURUS_API TaurusStatus taurus_element_set_attribute_float(TaurusElement elem,
                                                            const char* name,
                                                            float value);

/**
 * Set attribute value as boolean (converts to string)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Boolean value to set
 * @return TAURUS_OK on success, error code otherwise
 *
 * Converts the boolean value to "true" or "false" and sets it as an attribute.
 */
TAURUS_API TaurusStatus taurus_element_set_attribute_bool(TaurusElement elem,
                                                           const char* name,
                                                           int value);

/**
 * Set attribute value as integer (converts to string)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Integer value to set
 * @return TAURUS_OK on success, error code otherwise
 *
 * Converts the integer value to a string and sets it as an attribute.
 */
TAURUS_API TaurusStatus taurus_element_set_attribute_int(TaurusElement elem,
                                                         const char* name,
                                                         int value);

/**
 * Set attribute value as unsigned integer (converts to string)
 *
 * @param elem Element
 * @param name Attribute name
 * @param value Unsigned integer value to set
 * @return TAURUS_OK on success, error code otherwise
 *
 * Converts the unsigned integer value to a string and sets it as an attribute.
 */
TAURUS_API TaurusStatus taurus_element_set_attribute_uint(TaurusElement elem,
                                                          const char* name,
                                                          unsigned int value);

/**
 * Remove attribute
 *
 * @param elem Element
 * @param name Attribute name
 * @return TAURUS_OK on success, TAURUS_ERROR_NOT_FOUND if attribute doesn't exist
 */
TAURUS_API TaurusStatus taurus_element_remove_attribute(TaurusElement elem, const char* name);

/**
 * Remove all attributes from element
 *
 * @param elem Element
 * @return TAURUS_OK on success
 */
TAURUS_API TaurusStatus taurus_element_remove_all_attributes(TaurusElement elem);

/**
 * Find first child element with given tag name
 *
 * @param elem Element to search in
 * @param name Tag name to find
 * @return First matching child element or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_find_child(TaurusElement elem, const char* name);

/**
 * Find first child with given name and attribute value
 *
 * @param elem Element to search in
 * @param child_name Child tag name (NULL to match any tag)
 * @param attr_name Attribute name to check
 * @param attr_value Attribute value to match
 * @return First matching child element or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_find_child_by_attr(TaurusElement elem,
                                                             const char* child_name,
                                                             const char* attr_name,
                                                             const char* attr_value);

/**
 * Get next sibling element with specified name
 *
 * @param elem Element to start from
 * @param name Element name to find (NULL to get next sibling regardless of name)
 * @return Next sibling with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_next_sibling(TaurusElement elem, const char* name);

/**
 * Get previous sibling element with specified name
 *
 * @param elem Element to start from
 * @param name Element name to find (NULL to get previous sibling regardless of name)
 * @return Previous sibling with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_previous_sibling(TaurusElement elem, const char* name);

/**
 * Get first child element with specified name
 *
 * @param elem Element to search in
 * @param name Element name to find (NULL to get first child regardless of name)
 * @return First child with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_first_child(TaurusElement elem, const char* name);

/**
 * Get last child element with specified name
 *
 * @param elem Element to search in
 * @param name Element name to find (NULL to get last child regardless of name)
 * @return Last child with matching name, or NULL if not found
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_last_child(TaurusElement elem, const char* name);

/**
 * Get first child element regardless of name
 *
 * @param elem Element to search in
 * @return First child element or NULL if elem has no children
 *
 * Convenience function that returns the first child element
 * regardless of its name. Same as taurus_element_first_child(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_first_child_any(TaurusElement elem);

/**
 * Get last child element regardless of name
 *
 * @param elem Parent element
 * @return Last child element or NULL if elem has no children
 *
 * Convenience function that returns the last child element
 * regardless of its name.
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_last_child_any(TaurusElement elem);

/**
 * Get next sibling element regardless of name
 *
 * @param elem Element to start from
 * @return Next sibling element or NULL if elem is last child
 *
 * Convenience function that returns the next sibling element
 * regardless of its name. Same as taurus_element_next_sibling(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_next_sibling_any(TaurusElement elem);

/**
 * Get previous sibling element regardless of name
 *
 * @param elem Current element
 * @return Previous sibling element, or NULL if not found
 *
 * Convenience function that returns the previous sibling element
 * regardless of its name. Same as taurus_element_previous_sibling(elem, NULL).
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_element_previous_sibling_any(TaurusElement elem);

/**
 * Get child element text content
 *
 * @param elem Parent element
 * @return Text content of first child text node, or NULL if not found
 *
 * Returns the text content of the first text node child.
 * This is a convenience function for accessing element text.
 *
 * Memory: String is owned by element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_child_value(TaurusElement elem);

/**
 * Deep copy element into a destination document (detached).
 *
 * @param src Element to copy (subtree copied recursively)
 * @param dest_doc Document that will own the copy. May be the same
 *                 document or a different one (cross-document copy).
 * @return Newly created copy detached from any parent, or NULL on
 *         error (bad args or pool allocation failure).
 *
 * Issue #148 Phase 1: backs `Node#dup` / `#clone` in the Ruby
 * binding. The copy carries no parent reference and the caller
 * owns attaching it via `taurus_element_append_child`.
 *
 * Memory: Copy is owned by `dest_doc`; released by
 *         `taurus_document_free`.
 */
TAURUS_API TaurusElement taurus_element_copy(TaurusElement src,
                                              TaurusDocument dest_doc);

/**
 * Deep copy an entire document.
 *
 * @param src Source document
 * @return Newly created document, or NULL on error
 *
 * Copies the tree (root element + all descendants), the XML
 * declaration (version/encoding/standalone), document-level
 * processing instructions, and (if present) the DOCTYPE.
 *
 * Memory: Caller owns the result; release with
 *         `taurus_document_free`.
 */
TAURUS_API TaurusDocument taurus_document_copy(TaurusDocument src);

/**
 * Deep copy element and append to parent
 *
 * @param parent Parent element to append to
 * @param source Element to copy (can be from different document)
 * @return Newly created copy, or NULL on error
 *
 * Creates a deep copy including all children and attributes.
 * The copy belongs to parent's document.
 *
 * Memory: Copy is owned by parent's document
 */
TAURUS_API TaurusElement taurus_element_append_copy(TaurusElement parent, TaurusElement source);

/**
 * Deep copy element and prepend to parent
 *
 * @param parent Parent element to prepend to
 * @param source Element to copy (can be from different document)
 * @return Newly created copy, or NULL on error
 *
 * Creates a deep copy and inserts at the beginning of parent's children.
 * The copy belongs to parent's document.
 *
 * Memory: Copy is owned by parent's document
 */
TAURUS_API TaurusElement taurus_element_prepend_copy(TaurusElement parent, TaurusElement source);

/**
 * Deep copy element and insert before sibling
 *
 * @param sibling Sibling element to insert before
 * @param source Element to copy (can be from different document)
 * @return Newly created copy, or NULL on error
 *
 * Creates a deep copy and inserts it before the sibling.
 * The copy belongs to sibling's document.
 *
 * Memory: Copy is owned by sibling's document
 */
TAURUS_API TaurusElement taurus_element_insert_copy_before(TaurusElement sibling, TaurusElement source);

/**
 * Deep copy element and insert after sibling
 *
 * @param sibling Sibling element to insert after
 * @param source Element to copy (can be from different document)
 * @return Newly created copy, or NULL on error
 *
 * Creates a deep copy and inserts it after the sibling.
 * The copy belongs to sibling's document.
 *
 * Memory: Copy is owned by sibling's document
 */
TAURUS_API TaurusElement taurus_element_insert_copy_after(TaurusElement sibling, TaurusElement source);

/* ============================================================================
 * Serialization Operations
 * ============================================================================ */

/**
 * Options for XML serialization
 *
 * Definition lives in taurus/types.h.
 */

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
 *       printf("Failed to save: %d\n", status);
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
TAURUS_API char* taurus_c14n_canonicalize(struct taurus_document* doc,
                                          int version,
                                          int flags);

/**
 * Canonicalize a subtree rooted at the given element (issue #169).
 * Same algorithm as taurus_c14n_canonicalize but limited to elem
 * and its descendants. Pairs of PIs and the document's XML
 * declaration are NOT included (subtree C14N is element-scoped).
 *
 * @param elem Subtree root
 * @param version C14N version (TAURUS_C14N_1_0 or TAURUS_C14N_1_1)
 * @param flags Reserved for future use (pass 0)
 * @return Canonicalized XML string (caller must free with
 *         taurus_free_string), or NULL on error
 */
TAURUS_API char* taurus_c14n_canonicalize_subtree(TaurusElement elem,
                                                   int version,
                                                   int flags);

/**
 * Extended canonicalization with mode, inclusive namespaces, and
 * comments toggle (issue #183).
 *
 * @param doc Document
 * @param version TAURUS_C14N_1_0 or TAURUS_C14N_1_1
 * @param mode TAURUS_C14N_MODE_CANONICAL or TAURUS_C14N_MODE_EXCLUSIVE
 * @param inclusive_ns_prefixes NULL or NULL-terminated array of
 *        namespace prefixes to include even under EXCLUSIVE mode.
 *        Pass NULL when not needed.
 * @param with_comments 0 to strip comments, 1 to preserve
 * @return Canonicalized XML string (caller frees with
 *         taurus_free_string), or NULL on error
 */
TAURUS_API char* taurus_c14n_canonicalize_ex(
    struct taurus_document* doc,
    int version,
    TaurusC14NMode mode,
    const char** inclusive_ns_prefixes,
    int with_comments);

/**
 * Extended subtree canonicalization (issue #183). Same parameters
 * as taurus_c14n_canonicalize_ex but limited to elem + descendants.
 */
TAURUS_API char* taurus_c14n_canonicalize_subtree_ex(
    TaurusElement elem,
    int version,
    TaurusC14NMode mode,
    const char** inclusive_ns_prefixes,
    int with_comments);

/* ============================================================================
 * Namespace Operations
 * ============================================================================ */

/**
 * Get element's active namespace
 *
 * @param elem Element
 * @return Namespace or NULL if elem has no namespace
 *
 * Memory: Namespace is owned by element. Do not free separately.
 */
TAURUS_API TaurusNamespace taurus_element_namespace(TaurusElement elem);

/**
 * Get namespace URI
 *
 * @param ns Namespace
 * @return URI string or NULL if ns is NULL
 *
 * Memory: String is owned by namespace. Do not free or modify.
 */
TAURUS_API const char* taurus_namespace_uri(TaurusNamespace ns);

/**
 * Get namespace prefix
 *
 * @param ns Namespace
 * @return Prefix string or NULL if default namespace or ns is NULL
 *
 * Memory: String is owned by namespace. Do not free or modify.
 */
TAURUS_API const char* taurus_namespace_prefix(TaurusNamespace ns);

/**
 * Resolve namespace prefix (with inheritance)
 *
 * @param elem Element to start search from
 * @param prefix Prefix to resolve (NULL for default namespace)
 * @return Namespace URI or NULL if not found
 *
 * Memory: String is owned by element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_namespace_for_prefix(TaurusElement elem, const char* prefix);

/**
 * Get number of namespaces declared on an element
 *
 * Counts xmlns and xmlns:* attributes. O(n) over the attribute list.
 *
 * @param elem Element
 * @return Namespace count or 0 if elem is NULL
 */
TAURUS_API size_t taurus_element_namespace_count(TaurusElement elem);

/**
 * Get the prefix of the namespace declaration at the given index
 * (issue #171). Use with taurus_element_namespace_count to enumerate
 * all declarations on an element.
 *
 * @param elem Element
 * @param index 0-based index, must be < namespace_count
 * @return Prefix string (NULL for the default namespace),
 *         or NULL if index is out of range
 *
 * Memory: String is owned by the element. Do not free or modify.
 */
TAURUS_API const char* taurus_element_namespace_decl_prefix(TaurusElement elem,
                                                              size_t index);

/**
 * Get the URI of the namespace declaration at the given index
 * (issue #171). Pairs with taurus_element_namespace_decl_prefix.
 *
 * @param elem Element
 * @param index 0-based index
 * @return URI string, or NULL if index is out of range
 */
TAURUS_API const char* taurus_element_namespace_decl_uri(TaurusElement elem,
                                                          size_t index);

/**
 * Add a namespace declaration to an element (issue #186).
 *
 * @param elem Element to receive the declaration
 * @param prefix Namespace prefix. NULL or "" means default namespace.
 * @param href Namespace URI (required)
 * @return TAURUS_OK on success,
 *         TAURUS_ERROR_NULL_ARG if elem or href is NULL,
 *         TAURUS_ERROR_MEMORY on allocation failure
 *
 * Memory: prefix and href are pool-copied; caller may free or
 * modify the inputs immediately.
 */
TAURUS_API TaurusStatus taurus_element_add_namespace_definition(
    TaurusElement elem, const char* prefix, const char* href);

/**
 * Set the default namespace on an element (issue #186).
 * Equivalent to add_namespace_definition(elem, NULL, href).
 */
TAURUS_API TaurusStatus taurus_element_set_default_namespace(
    TaurusElement elem, const char* href);

/**
 * Remove the namespace declaration matching prefix (issue #186).
 *
 * @param elem Element
 * @param prefix Prefix to match. NULL means default namespace.
 * @return TAURUS_OK on success,
 *         TAURUS_ERROR_NULL_ARG if elem is NULL,
 *         TAURUS_ERROR_NOT_FOUND if no matching declaration exists
 */
TAURUS_API TaurusStatus taurus_element_remove_namespace_definition(
    TaurusElement elem, const char* prefix);

/**
 * Convert status code to human-readable string
 *
 * @param status Status code from taurus_parse_string or other API
 * @return Static string (never NULL, never freed)
 */
TAURUS_API const char* taurus_status_string(TaurusStatus status);

/* ============================================================================
 * XPath Operations
 * ============================================================================ */

/**
 * Evaluate XPath expression
 *
 * @param doc Document (required)
 * @param context Context element (NULL = document root)
 * @param expression XPath expression string
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 * Thread safety: Not thread-safe. One evaluation per thread.
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 * - All 13 axes: child, descendant, parent, ancestor, sibling, etc.
 * - All 27 functions: string(), count(), position(), etc.
 * - All operators: =, !=, <, <=, >, >=, +, -, *, div, mod, |, and, or
 * - Predicates: [1], [@attr], [position() > 2], etc.
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression
);

/**
 * Custom XPath function handler (string-valued).
 *
 * The handler receives the string representations of each XPath
 * argument (XPath node-sets are flattened to concatenated text
 * content; numbers and booleans are stringified). It returns a
 * newly-allocated NUL-terminated string, or NULL on error.
 *
 * The caller (libtaurus) frees the returned string. The `args`
 * array is owned by libtaurus; do not free or modify.
 *
 * Use this with `taurus_xpath_register_function` to expose Ruby
 * callbacks via `Searchable#xpath(expr, ..., handler)` in the
 * Nokogiri-compatible API.
 */
typedef char* (*TaurusXPathFn)(const char* const* args,
                                int argc,
                                void* user_data);

/**
 * Register a custom XPath function on a document.
 *
 * @param doc Document that will own the registration
 * @param name Function name as it appears in XPath expressions.
 *             Must not conflict with the standard XPath 1.0
 *             function names (count, position, last, etc.) — the
 *             standard library wins ties.
 * @param fn Handler. Must not be NULL.
 * @param user_data Opaque pointer passed back to `fn` on each
 *                  call. May be NULL.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG on NULL
 *         doc/name/fn.
 *
 * Registered functions are scoped to `doc`; they live for the
 * document's lifetime and are released by
 * `taurus_document_free`. Within an XPath expression they are
 * callable by name — no namespace prefix needed. The handler
 * runs only when the function is invoked during
 * `taurus_xpath_eval` against this document.
 *
 * Memory: the document owns the registration; user_data is owned
 *         by the caller.
 */
TAURUS_API TaurusStatus taurus_xpath_register_function(
    TaurusDocument doc,
    const char* name,
    TaurusXPathFn fn,
    void* user_data
);

/**
 * Get XPath result type
 *
 * @param result XPath result
 * @return Result type or -1 if result is NULL
 */
TAURUS_API TaurusXPathResultType taurus_xpath_result_type(TaurusXPathResult result);

/**
 * Get nodeset size (for NODESET results)
 *
 * @param result XPath result
 * @return Number of nodes or 0 if not a nodeset or result is NULL
 */
TAURUS_API size_t taurus_xpath_result_count(TaurusXPathResult result);

/**
 * Get node from nodeset by index
 *
 * @param result XPath result
 * @param index Node index (0-based)
 * @return Element or NULL if index out of bounds or not a nodeset
 *
 * Memory: Element is owned by document. Do not free separately.
 */
TAURUS_API TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index);

/**
 * Get boolean value (for BOOLEAN results or type conversion)
 *
 * @param result XPath result
 * @return Boolean value (1 = true, 0 = false)
 *
 * Type conversion rules:
 * - BOOLEAN: Direct value
 * - NUMBER: true if non-zero and not NaN
 * - STRING: true if non-empty
 * - NODESET: true if non-empty
 */
TAURUS_API int taurus_xpath_result_boolean(TaurusXPathResult result);

/**
 * Get number value (for NUMBER results or type conversion)
 *
 * @param result XPath result
 * @return Number value (NaN if conversion fails)
 *
 * Type conversion rules:
 * - NUMBER: Direct value
 * - BOOLEAN: 1.0 or 0.0
 * - STRING: Parsed as number (NaN if invalid)
 * - NODESET: First node's string value converted to number
 */
TAURUS_API double taurus_xpath_result_number(TaurusXPathResult result);

/**
 * Get string value (for STRING results or type conversion)
 *
 * @param result XPath result
 * @return String value or NULL if result is NULL
 *
 * Memory: Caller must call taurus_free_string() when done
 *
 * Type conversion rules:
 * - STRING: Direct value
 * - BOOLEAN: "true" or "false"
 * - NUMBER: String representation of number
 * - NODESET: String value of first node (recursive text concatenation)
 */
TAURUS_API char* taurus_xpath_result_string(TaurusXPathResult result);

/**
 * Free XPath result
 *
 * @param result Result to free (can be NULL)
 */
TAURUS_API void taurus_xpath_result_free(TaurusXPathResult result);

/* ============================================================================
 * XPath Variables (XPath 1.0)
 * ============================================================================ */

/**
 * XPath variable value types
 *
 * Definition lives in taurus/types.h.
 */

/**
 * Opaque variable set type — defined in taurus/types.h.
 */

/**
 * Create a new variable set
 *
 * @return New variable set, or NULL on error
 *
 * Memory: Caller must call taurus_xpath_variable_set_free() when done
 */
TAURUS_API TaurusXPathVariableSet taurus_xpath_variable_set_new(void);

/**
 * Free a variable set
 *
 * @param set Variable set to free (can be NULL)
 */
TAURUS_API void taurus_xpath_variable_set_free(TaurusXPathVariableSet set);

/**
 * Add a boolean variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Boolean value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_boolean(TaurusXPathVariableSet set, const char* name, int value);

/**
 * Add a number variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value Number value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_number(TaurusXPathVariableSet set, const char* name, double value);

/**
 * Add a string variable to the set
 *
 * @param set Variable set
 * @param name Variable name (must be valid XPath identifier)
 * @param value String value
 * @return TAURUS_OK on success, error code on failure
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_string(TaurusXPathVariableSet set, const char* name, const char* value);

/**
 * Evaluate XPath expression with variables
 *
 * @param doc Document to evaluate against
 * @param expression XPath expression string
 * @param variables Variable set (can be NULL)
 * @return XPath result or NULL on error
 *
 * Memory: Caller must call taurus_xpath_result_free() when done
 *
 * XPath 1.0 compliance: Full XPath 1.0 specification
 *
 * Variables are referenced in expressions using $name syntax:
 *   taurus_xpath_variable_set_number(vars, "x", 42);
 *   taurus_xpath_eval(doc, "//item[@id = $x]", vars);
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars(
    TaurusDocument doc,
    const char* expression,
    TaurusXPathVariableSet variables
);

/**
 * Evaluate an XPath expression with both a context node and a variable
 * set (issue #170). Use this when the receiver is not the document
 * root (e.g. relative paths like ".//item[@id = $x]").
 *
 * @param doc Document
 * @param context Context element (NULL = document root)
 * @param expression XPath expression
 * @param variables Variable set (may be NULL)
 * @return XPath result (caller frees with taurus_xpath_result_free)
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars_context(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression,
    TaurusXPathVariableSet variables
);

/* ============================================================================
 * Memory Management Helpers
 * ============================================================================ */

/**
 * Free string returned by libtaurus
 *
 * @param str String to free (can be NULL)
 *
 * Use this to free strings returned by:
 * - taurus_xpath_result_string()
 */
TAURUS_API void taurus_free_string(char* str);

/**
 * Cleanup function to free internal memory structures (for testing)
 *
 * This function cleans up the compact pointer overflow table and other
 * thread-local structures that may accumulate stale entries across
 * multiple document operations.
 *
 * IMPORTANT: This should ONLY be called between test runs or when
 * you're certain no documents are active. Calling this while documents
 * are active will cause crashes.
 *
 * This function is primarily intended for test suites to prevent
 * state accumulation between tests.
 */
TAURUS_API void taurus_explicit_cleanup(void);

/* ============================================================================
 * Memory Allocation Hooks (for testing and custom allocators)
 *
 * The taurus_allocation_function / taurus_deallocation_function typedefs
 * come from taurus/types.h (included above).  Only the API entry points
 * live here.
 * ============================================================================ */

/**
 * Set custom memory management functions for all allocations
 *
 * By default, Taurus uses malloc/free. This function allows you to
 * specify custom allocation functions for testing (memory leak detection)
 * or custom memory management.
 *
 * WARNING: This function affects ALL Taurus operations globally.
 * Set custom functions BEFORE any parsing operations and restore
 * to defaults before program exit.
 *
 * @param alloc_function Function to use for memory allocation (NULL = use malloc)
 * @param dealloc_function Function to use for memory deallocation (NULL = use free)
 *
 * Example:
 *   void* my_alloc(size_t size) { return calloc(1, size); }
 *   void my_free(void* ptr) { free(ptr); }
 *   taurus_set_memory_management_functions(my_alloc, my_free);
 */
TAURUS_API void taurus_set_memory_management_functions(taurus_allocation_function alloc_function,
                                                         taurus_deallocation_function dealloc_function);

/**
 * Get current memory allocation function
 *
 * @return Current allocation function (NULL if using default malloc)
 */
TAURUS_API taurus_allocation_function taurus_get_memory_allocation_function(void);

/**
 * Get current memory deallocation function
 *
 * @return Current deallocation function (NULL if using default free)
 */
TAURUS_API taurus_deallocation_function taurus_get_memory_deallocation_function(void);

/* ============================================================================
 * Per-Document Allocator Hooks (TODO 74)
 *
 * Set allocator hooks on a specific document before parsing it.  Useful
 * when an app needs different allocators for different documents in
 * the same thread.
 *
 * Must be called BEFORE taurus_parse_string.  Changes after parse
 * have no effect on already-allocated memory.
 *
 * To set thread-default hooks (applies to all documents in the
 * current thread), use taurus_set_memory_management_functions().
 * ============================================================================ */

TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc);

/* ============================================================================
 * XInclude 1.0 Support
 * ============================================================================ */

/**
 * Process XInclude elements in document
 *
 * Replaces all <xi:include> elements with their included content.
 * Follows W3C XInclude 1.0 specification (https://www.w3.org/TR/xinclude/).
 *
 * @param doc Document to process
 * @param base_url Base URL for resolving relative hrefs (can be NULL)
 * @return TAURUS_OK on success, error code on failure
 *
 * XInclude namespace: http://www.w3.org/2001/XInclude
 *
 * Example:
 *   <root xmlns:xi="http://www.w3.org/2001/XInclude">
 *     <xi:include href="chapter1.xml"/>
 *   </root>
 */
TAURUS_API TaurusStatus taurus_xinclude_process(TaurusDocument doc, const char* base_url);

/**
 * Check if element is an XInclude include element
 *
 * @param elem Element to check
 * @return 1 if element is <xi:include>, 0 otherwise
 */
TAURUS_API int taurus_xinclude_is_include_element(TaurusElement elem);

/**
 * Check if element is an XInclude fallback element
 *
 * @param elem Element to check
 * @return 1 if element is <xi:fallback>, 0 otherwise
 */
TAURUS_API int taurus_xinclude_is_fallback_element(TaurusElement elem);

/**
 * Get href attribute value from include element
 *
 * @param include_elem Include element
 * @return href value or NULL if not found
 */
TAURUS_API const char* taurus_xinclude_get_href(TaurusElement include_elem);

/**
 * Get parse attribute value from include element
 *
 * @param include_elem Include element
 * @return "xml" or "text" (defaults to "xml" if not specified)
 */
TAURUS_API const char* taurus_xinclude_get_parse(TaurusElement include_elem);

/**
 * Get xpointer attribute value from include element
 *
 * @param include_elem Include element
 * @return xpointer value or NULL if not specified
 */
TAURUS_API const char* taurus_xinclude_get_xpointer(TaurusElement include_elem);

/**
 * Get encoding attribute value from include element (for parse="text")
 *
 * @param include_elem Include element
 * @return encoding value or NULL if not specified
 */
TAURUS_API const char* taurus_xinclude_get_encoding(TaurusElement include_elem);

/* ============================================================================
 * SAX (Simple API for XML) - Event-based Parsing
 * ============================================================================ */

/**
 * Include SAX parser support
 *
 * SAX provides event-driven XML parsing without building a DOM tree.
 * Useful for streaming large XML files or when only specific data is needed.
 *
 * Include <taurus/sax.h> for SAX parser API.
 */

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define LIBTAURUS_VERSION_MAJOR 0
#define LIBTAURUS_VERSION_MINOR 1
#define LIBTAURUS_VERSION_PATCH 0
#define LIBTAURUS_VERSION_STRING "0.1.0"

/**
 * Get libtaurus version string
 *
 * @return Version string (e.g., "0.2.0")
 */
TAURUS_API const char* taurus_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBTAURUS_H */