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
#    define TAURUS_API
#  endif
#endif
#endif  /* TAURUS_FOR_BINDGEN */

/* ============================================================================
 * Opaque Types - Hide implementation details
 *
 * These mirror the definitions in taurus/types.h.  When both headers
 * are included in the same TU, C99's typedef-redefinition rule fires.
 * Guard each typedef so only the first definition wins (the definitions
 * are intentionally identical).  See TODO 12.
 * ============================================================================ */

#ifndef TAURUS_INTERNAL_TYPES_DEFINED
#define TAURUS_INTERNAL_TYPES_DEFINED
typedef struct taurus_node*            TaurusNodeRef;
typedef struct taurus_document*        TaurusDocument;
typedef struct taurus_element*         TaurusElement;
typedef struct taurus_attribute*       TaurusAttribute;
typedef const char*                    TaurusNamespace;
typedef struct taurus_xpath_result*    TaurusXPathResult;
#endif

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
 * Status Codes
 * ============================================================================ */

typedef enum {
    TAURUS_OK = 0,
    TAURUS_ERROR_MEMORY = -1,      /* Memory allocation failed */
    TAURUS_ERROR_PARSE = -2,       /* XML parsing error */
    TAURUS_ERROR_XPATH = -3,       /* XPath evaluation error */
    TAURUS_ERROR_NULL_ARG = -4,    /* NULL argument passed */
    TAURUS_ERROR_INVALID_ARG = -5, /* Invalid argument */
    TAURUS_ERROR_NOT_FOUND = -6,   /* Resource not found */
    TAURUS_ERROR_IO = -7,          /* I/O error (file not found, etc.) */
    TAURUS_ERROR_NOT_IMPLEMENTED = -8 /* Feature not yet implemented */
} TaurusStatus;

/* ============================================================================
 * XPath Result Types
 * ============================================================================ */

typedef enum {
    TAURUS_XPATH_NODESET,
    TAURUS_XPATH_BOOLEAN,
    TAURUS_XPATH_NUMBER,
    TAURUS_XPATH_STRING
} TaurusXPathResultType;

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
 * After freezing, mutation functions (set_attribute, append_child,
 * etc.) SHOULD return an error instead of modifying the tree.
 * Currently the flag is advisory — callers can check
 * taurus_document_is_frozen before mutating.
 *
 * @param doc Document to freeze. Must not be NULL.
 * @return TAURUS_OK on success, TAURUS_ERROR_NULL_ARG if doc is NULL.
 */
TAURUS_API TaurusStatus taurus_document_freeze(TaurusDocument doc);

/**
 * Check if a document is frozen.
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
 */
typedef struct {
    int indent;              /* 0 = compact, >0 = pretty-print with N spaces */
    int xml_declaration;     /* 1 = include <?xml?>, 0 = omit */
    const char* encoding;    /* "UTF-8" or NULL for default */
} TaurusSerializeOptions;

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
 * C14N (Canonical XML) version flags
 */
typedef enum {
    TAURUS_C14N_1_0 = 0,      /* Canonical XML 1.0 */
    TAURUS_C14N_1_1 = 1       /* Canonical XML 1.1 */
} TaurusC14NVersion;

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
 */
typedef enum {
    TAURUS_XPATH_VAR_TYPE_NONE = 0,      /* Invalid type */
    TAURUS_XPATH_VAR_TYPE_BOOLEAN,       /* Boolean value */
    TAURUS_XPATH_VAR_TYPE_NUMBER,        /* Floating-point number */
    TAURUS_XPATH_VAR_TYPE_STRING,        /* String value */
    TAURUS_XPATH_VAR_TYPE_NODE_SET       /* Node set */
} TaurusXPathVariableType;

/**
 * Opaque variable set type
 */
typedef struct taurus_xpath_variable_set* TaurusXPathVariableSet;

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
 * ============================================================================ */

/**
 * Function pointer type for memory allocation (compatible with malloc)
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory or NULL on failure
 */
#ifndef TAURUS_ALLOCATION_FUNCTION_DEFINED
#define TAURUS_ALLOCATION_FUNCTION_DEFINED
typedef void* (*taurus_allocation_function)(size_t size);
#endif

/**
 * Function pointer type for memory deallocation (compatible with free)
 * @param ptr Pointer to memory to deallocate
 */
#ifndef TAURUS_DEALLOCATION_FUNCTION_DEFINED
#define TAURUS_DEALLOCATION_FUNCTION_DEFINED
typedef void (*taurus_deallocation_function)(void* ptr);
#endif

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