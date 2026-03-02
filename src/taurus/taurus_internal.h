/* libtaurus - Internal data structures
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * INTERNAL HEADER - Not part of public API
 * These structures are implementation details and may change between versions.
 */

#ifndef TAURUS_INTERNAL_H
#define TAURUS_INTERNAL_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Memory pool for fast DOM allocation */
#include "memory/pool.h"

/* StringView for zero-copy string handling */
#include "common/string_view.h"

/* Forward declarations - actual typedefs in respective headers */
struct taurus_element;  /* typedef in dom/element.h */
struct taurus_compact_overflow_entry;  /* typedef in dom/compact.h */

/* ============================================================================
 * Error Codes (Internal)
 *
 * These map to public TaurusStatus codes in taurus/types.h.
 * Kept for backward compatibility with internal code.
 * ============================================================================ */

/* Include public error types */
#include "../include/taurus/error.h"

/* Internal error codes - map to public TaurusStatus */
/* DEPRECATED: Use TaurusStatus directly in new code */
#define TAURUS_ERROR_NONE            TAURUS_OK
#define TAURUS_ERROR_MEMORY_ALLOCATION TAURUS_ERROR_MEMORY
#define TAURUS_ERROR_PARSE_FAILED    TAURUS_ERROR_PARSE
#define TAURUS_ERROR_XPATH_EVALUATION TAURUS_ERROR_XPATH
#define TAURUS_ERROR_XPATH_SYNTAX    TAURUS_ERROR_XPATH
#define TAURUS_ERROR_XPATH_FUNCTION  TAURUS_ERROR_XPATH
#define TAURUS_ERROR_EVAL_CONTEXT    TAURUS_ERROR_XPATH
#define TAURUS_ERROR_INVALID_ARGUMENT TAURUS_ERROR_INVALID_ARG
#define TAURUS_ERROR_NULL_INPUT      TAURUS_ERROR_NULL_ARG
#define TAURUS_ERROR_EMPTY_INPUT     TAURUS_ERROR_INVALID_ARG
#define TAURUS_ERROR_OUT_OF_MEMORY   TAURUS_ERROR_MEMORY
#define TAURUS_ERROR_INVALID_XML     TAURUS_ERROR_PARSE

/* Legacy typedef - DEPRECATED: Use TaurusStatus */
typedef TaurusStatus taurus_error_code;

/* ============================================================================
 * Internal Structures - Match ext/taurus/taurus.h but without Ruby
 * ============================================================================ */

/* Node type enumeration - supports all XPath node types */
typedef enum {
    TAURUS_NODE_ELEMENT = 0,
    TAURUS_NODE_ATTRIBUTE = 1,
    TAURUS_NODE_TEXT = 2,      /* Future */
    TAURUS_NODE_COMMENT = 3,    /* Future */
    TAURUS_NODE_PI = 4,          /* Future */
    TAURUS_NODE_NAMESPACE = 5   /* For namespace axis */
} TaurusNodeType;

/* Processing instruction structure */
struct taurus_processing_instruction {
    char* target;                /* PI target (e.g., "xml-stylesheet") */
    char* data;                  /* PI data/content */
    struct taurus_processing_instruction* next; /* Linked list */
};

/* Document structure */
struct taurus_document {
    struct taurus_element* root;             /* Root element (legacy API) */
    char* encoding;                 /* UTF-8 assumed, but store if specified */
    struct taurus_processing_instruction* pis;  /* Processing instructions */
    size_t ref_count;               /* Reference counting for memory management */
    void* new_dom_root;             /* New DOM tree root (TaurusElement) for serialization */
    /* XML Declaration support */
    char* xml_version;              /* "1.0", "1.1", etc. or NULL if not present */
    int standalone;                 /* -1=not set, 0=no, 1=yes */
    int had_declaration;            /* 1 if input had <?xml?>, 0 otherwise */
    int has_bom;                    /* 1 if UTF-8 BOM was present, 0 otherwise */
    /* DOCTYPE support */
    void* doctype;                  /* TaurusDoctypeNode* or NULL */
    void* dtd;                      /* TaurusDTD* - Parsed DTD declarations */
    /* Memory pool for fast DOM node allocation */
    TaurusMemoryPool* pool;         /* Pool allocator (owns all DOM nodes) */
    /* Compact pointer support */
    void* page_base;                /* Base pointer for compact pointer decoding */
    struct taurus_compact_overflow_entry* overflow_entries; /* Per-document overflow entry list head */
    /* In-place parsing support (zero-copy optimization) */
    char* xml_buffer;               /* Owned writable XML buffer (NULL if not in-place) */
    size_t xml_buffer_len;       /* Length of xml_buffer */
    int xml_buffer_needs_free;   /* 1 if xml_buffer needs free(), 0 if stack/const */
    /* Per-document strict mode (thread-safe, no global state) */
    int strict_mode;                /* 1=strict XML 1.0, 0=lenient (pugixml compat) */
    /* Observer list for change tracking (lazy initialization) */
    void* observer_list;            /* ObserverList* - for document change events */
    /* Per-document allocator (NULL = use global allocator) */
    void* allocator;                /* TaurusAllocator* - document-specific memory allocator */
    /* Compact-only mode support (v5 parser with 16-byte elements) */
    void* compact_alloc;            /* ZeroCheckAlloc* - zero-check bump allocator */
    void* compact_base;             /* Base pointer for compact element resolution */
    uint32_t compact_root_offset;   /* Offset to root element in compact block */
    /* Compact wrapper cache - maps offsets to wrapper elements */
    void* wrapper_cache;            /* WrapperCache* - hash table for offset->wrapper mapping */
    /* Pointer-based mode support (v6 parser - 1.28-1.46x faster than pugixml!) */
    int is_ptr_mode;                /* 1 if using pointer-based structures */
    void* ptr_root;                 /* ptr_element* - root element in pointer mode */
    void* ptr_elem_pool;            /* Element pool for pointer mode */
    void* ptr_attr_pool;            /* Attribute pool for pointer mode */
    void* ptr_text_pool;            /* Text node pool for pointer mode */
};

/* Parse options structure */
typedef struct {
    int strict;                  /* Strict mode (1=enabled, 0=disabled) */
    int preserve_whitespace;     /* Preserve whitespace (1=enabled, 0=disabled) */
    int track_positions;         /* Track positions (1=enabled, 0=disabled) */
} taurus_parse_options;

/* taurus_parse() is now declared in the public header (taurus.h) */

/* Get current strict parsing mode */
extern int taurus_get_strict_mode(void);

/* Namespace structure - Matches ext/taurus/taurus.h _namespace
 *
 * OPTIMIZATION (Phase B): Added StringView fields for zero-copy namespace storage.
 * The prefix_view and uri_view fields store StringViews directly pointing into
 * the XML buffer, eliminating the need to copy strings during parsing.
 */
struct taurus_namespace {
    /* StringView storage (32 bytes) - zero-copy into XML buffer */
    TaurusStringView prefix_view;  /* Namespace prefix (empty = default namespace) */
    TaurusStringView uri_view;     /* Namespace URI (required) */

    /* Cached NULL-terminated strings (16 bytes) - lazy conversion */
    char* prefix;                  /* NULL until first access, or set directly */
    char* uri;                     /* NULL until first access, or set directly */

    struct taurus_namespace* next; /* Linked list for multiple declarations */
};

/* ============================================================================
 * XPath Node Type System
 * ============================================================================ */

/* Attribute node structure - dedicated type for attribute nodes in XPath */
typedef struct taurus_attribute_node {
    TaurusNodeType node_type;    /* Always TAURUS_NODE_ATTRIBUTE */
    char* name;                  /* Attribute name */
    char* value;                 /* Attribute value */
    char* namespace_uri;         /* Namespace URI (can be NULL) */
    struct taurus_element* owner;           /* Owner element */
} TaurusAttributeNode;

/* Namespace node structure - dedicated type for namespace nodes in XPath */
typedef struct taurus_namespace_node {
    TaurusNodeType node_type;      /* Always TAURUS_NODE_NAMESPACE */
    char* prefix;                  /* Namespace prefix (NULL = default) */
    char* uri;                     /* Namespace URI (required) */
    struct taurus_element* owner;           /* Owner element */
} TaurusNamespaceNode;

/* Text node structure - for XPath text() function results (virtual text nodes) */
typedef struct xpath_text_node {
    TaurusNodeType node_type;      /* Always TAURUS_NODE_TEXT */
    char* content;                 /* Text content */
    struct taurus_element* owner;           /* Parent element */
} XPathTextNode;

/* XPath node union - type-safe wrapper for all XPath node types */
typedef union xpath_node {
    TaurusNodeType type;           /* First field for type checking */
    struct {
        TaurusNodeType node_type;
        struct taurus_element* element;
    } as_element;
    TaurusAttributeNode* as_attribute;
} XPathNode;

/* Type checking macros */
#define XPATH_NODE_TYPE(node) (*(TaurusNodeType*)(node))
#define IS_ELEMENT_NODE(node) ((node) && XPATH_NODE_TYPE(node) == TAURUS_NODE_ELEMENT)
#define IS_ATTRIBUTE_NODE(node) ((node) && XPATH_NODE_TYPE(node) == TAURUS_NODE_ATTRIBUTE)
#define IS_TEXT_NODE(node) ((node) && XPATH_NODE_TYPE(node) == TAURUS_NODE_TEXT)

/* ============================================================================
 * XPath Internal Structures
 * ============================================================================ */

/* XPath token - Matches ext/taurus/xpath.h Token */
typedef struct xpath_token {
    int type;                    /* XPathTokenType */
    const char* value;           /* Token value (points into input, not owned) */
    size_t value_len;
    int line;
    int column;
} XPathToken;

/* XPath lexer - Matches ext/taurus/xpath.h Lexer */
typedef struct xpath_lexer {
    const char* input;           /* Input string (not owned) */
    const char* pos;             /* Current position */
    const char* end;             /* End of input */
    int line;
    int column;
    XPathToken current;
    char error_msg[256];
} XPathLexer;

/* XPath AST node types - From ext/taurus/xpath.h */
typedef enum {
    XPATH_AST_PATH_EXPR,
    XPATH_AST_ABSOLUTE_PATH,
    XPATH_AST_RELATIVE_PATH,
    XPATH_AST_STEP,
    XPATH_AST_AXIS_SPECIFIER,
    XPATH_AST_NODE_TEST,
    XPATH_AST_PREDICATE,
    XPATH_AST_FUNCTION_CALL,
    XPATH_AST_ARGUMENT,
    XPATH_AST_NUMBER,
    XPATH_AST_STRING,
    XPATH_AST_VARIABLE_REFERENCE,
    XPATH_AST_OPERATOR,
    XPATH_AST_NODE_TEST_NAME,
    XPATH_AST_NODE_TEST_TYPE,
    XPATH_AST_NODE_TEST_PI,
    XPATH_AST_NODE_TEST_ALL,
    XPATH_AST_NODE_TEST_ALL_IN_NS
} XPathASTType;

/* XPath AST node - Matches ext/taurus/xpath.h _xpath_ast_node */
typedef struct xpath_ast_node {
    XPathASTType type;
    char* value;                 /* String value (owned by node) */
    double number_value;         /* Number value */
    struct xpath_ast_node** children;
    size_t child_count;
    size_t child_capacity;

    /* Namespace support for node tests (v0.8.0) */
    char* prefix;                /* Namespace prefix (NULL if no prefix) */
    char* local_name;            /* Local name part (NULL if not applicable) */
} XPathASTNode;

/* XPath parser - Matches ext/taurus/xpath.h _xpath_parser */
typedef struct xpath_parser {
    XPathLexer* lexer;
    XPathToken* tokens;          /* Token array for lookahead */
    size_t token_count;
    size_t token_pos;
    char error_msg[256];
} XPathParser;

/* XPath nodeset - Holds typed node pointers (elements or attributes) */
typedef struct xpath_nodeset {
    void** nodes;                /* Typed node pointers (element* or TaurusAttributeNode*) */
    size_t count;
    size_t capacity;
    int owns_attributes;         /* If true, free attribute nodes on nodeset_free */
    int owns_namespaces;         /* If true, free namespace nodes on nodeset_free */
    struct xpath_nodeset* next_in_pool;  /* Next nodeset in pool free list (when pooled) */
} XPathNodeSet;

/* XPath result types - MUST match public enum in ext/taurus/xpath.h!
 * Order matters: NODESET=0, BOOLEAN=1, NUMBER=2, STRING=3 */
typedef enum {
    XPATH_RESULT_NODESET,
    XPATH_RESULT_BOOLEAN,
    XPATH_RESULT_NUMBER,
    XPATH_RESULT_STRING
} XPathResultType;

/* XPath result value union */
typedef union {
    int boolean_value;
    double number_value;
    char* string_value;          /* Owned by result */
    XPathNodeSet* nodeset_value; /* Owned by result */
} XPathResultValue;

/* XPath result - Matches ext/taurus/xpath.h _xpath_result */
struct taurus_xpath_result {
    XPathResultType type;
    XPathResultValue value;
};

/* Namespace mapping for XPath context (v0.8.0) */
typedef struct xpath_namespace_mapping {
    char* prefix;                /* Namespace prefix (NULL = default namespace) */
    char* uri;                   /* Namespace URI (required) */
} XPathNamespaceMapping;

/* XPath context - Matches ext/taurus/xpath.h _xpath_context */
typedef struct xpath_context {
    struct taurus_document* document;
    struct taurus_element* context_node;
    size_t context_position;     /* 1-based position in context nodeset */
    size_t context_size;         /* Total size of context nodeset */
    void* function_registry;     /* Opaque function registry */
    char error_msg[256];

    /* Current node in predicate evaluation (can be attribute or element) */
    void* current_predicate_node;  /* The actual node being filtered (for name() in predicates) */

    /* Namespace support (v0.8.0) */
    XPathNamespaceMapping* namespace_mappings;
    size_t namespace_count;
    size_t namespace_capacity;

    /* Variable support (v1.0.1) */
    void* variable_set;          /* TaurusXPathVariableSet - for $var references */

    /* Error context support (v1.0.0) */
    const char* input;           /* Original XPath expression for error context */
    size_t input_len;            /* Length of input expression */

    /* Optimization flags */
    int to_boolean;              /* Only checking existence */
    int max_results;             /* Stop after N results (0 = unlimited) */
    int enable_early_exit;       /* Master switch for early termination */

    /* PERFORMANCE: Nodeset pool for O(1) allocation */
    struct xpath_nodeset* nodeset_pool;  /* Free list of reusable nodesets */
    size_t nodesets_allocated;           /* Total nodesets allocated */
    size_t nodesets_reused;              /* Nodesets reused from pool */
} XPathContext;

/* Compiled XPath expression - pre-parsed AST for faster repeated evaluation */
struct taurus_xpath_compiled {
    XPathASTNode* ast;           /* Pre-parsed AST */
    char* expression;            /* Original expression string (for debugging) */
    char error_msg[256];         /* Compilation error if any */

    /* PERFORMANCE: Cache for literal-only expressions (constant-folded) */
    int is_literal;              /* 1 if AST is just a literal (STRING or NUMBER) */
    struct taurus_xpath_result* cached_result;  /* Pre-computed result for literals */
};

/* XPath operator types - From ext/taurus/xpath.h */
typedef enum {
    XPATH_OP_OR,
    XPATH_OP_AND,
    XPATH_OP_EQUAL,
    XPATH_OP_NOT_EQUAL,
    XPATH_OP_LESS,
    XPATH_OP_LESS_EQUAL,
    XPATH_OP_GREATER,
    XPATH_OP_GREATER_EQUAL,
    XPATH_OP_PLUS,
    XPATH_OP_MINUS,
    XPATH_OP_MULTIPLY,
    XPATH_OP_DIV,
    XPATH_OP_MOD,
    XPATH_OP_UNION,
    XPATH_OP_NEGATION
} XPathOperatorType;

/* XPath axis types - From ext/taurus/xpath.h */
typedef enum {
    XPATH_AXIS_ANCESTOR,
    XPATH_AXIS_ANCESTOR_OR_SELF,
    XPATH_AXIS_ATTRIBUTE,
    XPATH_AXIS_CHILD,
    XPATH_AXIS_DESCENDANT,
    XPATH_AXIS_DESCENDANT_OR_SELF,
    XPATH_AXIS_FOLLOWING,
    XPATH_AXIS_FOLLOWING_SIBLING,
    XPATH_AXIS_NAMESPACE,
    XPATH_AXIS_PARENT,
    XPATH_AXIS_PRECEDING,
    XPATH_AXIS_PRECEDING_SIBLING,
    XPATH_AXIS_SELF
} XPathAxisType;

/* ============================================================================
 * Memory Management Macros
 * ============================================================================ */

/* Forward declarations for memory allocation hooks */
void* taurus_alloc_hook(size_t size);
void taurus_free_hook(void* ptr);

/* Use standard C memory functions instead of Ruby macros */
#define TAURUS_ALLOC(type) \
    ((type*)malloc(sizeof(type)))

#define TAURUS_ALLOC_N(type, n) \
    ((type*)malloc(sizeof(type) * (n)))

#define TAURUS_REALLOC_N(ptr, type, n) \
    ((type*)realloc((ptr), sizeof(type) * (n)))

#define TAURUS_FREE(ptr) \
    do { if (ptr) { free(ptr); ptr = NULL; } } while(0)

/* Array growth helper - double capacity when full */
#define TAURUS_GROW_ARRAY(ptr, capacity) \
    do { \
        size_t new_cap = (capacity) == 0 ? 4 : (capacity) * 2; \
        (ptr) = realloc((ptr), new_cap * sizeof(*(ptr))); \
        (capacity) = new_cap; \
    } while(0)

/* ============================================================================
 * Generic Memory Allocation
 * ============================================================================ */

/* Generic malloc wrapper */
static inline void* taurus_malloc(size_t size) {
    return malloc(size);
}

/* Generic realloc wrapper */
static inline void* taurus_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

/* Generic free wrapper (renamed to avoid conflict with public API) */
static inline void taurus_internal_free(void* ptr) {
    free(ptr);
}

/* ============================================================================
 * String Helpers
 * ============================================================================ */

/* NULL-safe string duplication */
static inline char* taurus_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

/* NULL-safe string length */
static inline size_t taurus_strlen(const char* str) {
    return str ? strlen(str) : 0;
}

/* NULL-safe string comparison */
static inline int taurus_strcmp(const char* s1, const char* s2) {
    if (s1 == s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return strcmp(s1, s2);
}

/* ============================================================================
 * Internal Error Functions (from error.c)
 *
 * These are thin wrappers around the public error API in taurus/error.h.
 * New code should use the public API directly:
 *   - taurus_set_error_ex(code, line, col, context, fmt, ...)
 *   - taurus_set_error(code, fmt, ...)
 * ============================================================================ */

/* Legacy compatibility functions - use taurus_set_error_ex() instead */
void taurus_set_parse_error_position(int line, int column);

void taurus_set_error_with_context(
    TaurusStatus code,
    const char* message,
    const char* input,
    size_t byte_offset,
    int line,
    int column
);

void taurus_extract_context_snippet(
    const char* input,
    size_t offset,
    int error_line,
    char* out_buffer,
    size_t buffer_size
);

/* ============================================================================
 * Observer Integration (Internal API)
 *
 * These functions are used by DOM modification functions to emit events.
 * See taurus/observer.h for the public observer API.
 * ============================================================================ */

/* Include observer types (TaurusEventType is defined there) */
#include "../include/taurus/observer.h"

/* Emit an event to document observers (called by DOM modification functions) */
void taurus_emit_event(
    struct taurus_document* doc,
    TaurusEventType type,
    struct taurus_element* target,
    struct taurus_element* parent,
    struct taurus_element* sibling,
    const char* name,
    const char* old_value,
    const char* new_value
);

/* Initialize observer list for a document (called during document creation) */
void taurus_observer_init_document(struct taurus_document* doc);

/* Cleanup observer list for a document (called during document free) */
void taurus_observer_cleanup_document(struct taurus_document* doc);

#endif /* TAURUS_INTERNAL_H */