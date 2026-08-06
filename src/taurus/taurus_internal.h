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
#include <stdint.h>
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
 * ============================================================================ */

typedef enum {
    TAURUS_ERROR_NONE = 0,
    TAURUS_ERROR_MEMORY_ALLOCATION,
    TAURUS_ERROR_PARSE_FAILED,
    TAURUS_ERROR_XPATH_EVALUATION,
    TAURUS_ERROR_XPATH_SYNTAX,
    TAURUS_ERROR_XPATH_FUNCTION,
    TAURUS_ERROR_EVAL_CONTEXT,
    TAURUS_ERROR_INVALID_ARGUMENT,
    TAURUS_ERROR_NULL_INPUT,
    TAURUS_ERROR_EMPTY_INPUT,
    TAURUS_ERROR_OUT_OF_MEMORY,
    TAURUS_ERROR_INVALID_XML
} taurus_error_code;

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
    /* Per-document allocator hooks (TODO 74) — set BEFORE parsing to
     * override the thread-default globals.  NULL = use defaults. */
    taurus_allocation_function  alloc_hook;
    taurus_deallocation_function dealloc_hook;
    /* Compact pointer support */
    void* page_base;                /* Base pointer for compact pointer decoding */
    struct taurus_compact_overflow_entry* overflow_entries; /* Per-document overflow entry list head */
    /* In-place parsing support (zero-copy optimization) */
    char* xml_buffer;               /* Owned writable XML buffer (NULL if not in-place) */
    size_t xml_buffer_len;       /* Length of xml_buffer */
    int xml_buffer_needs_free;   /* 1 if xml_buffer needs free(), 0 if stack/const */

    /* Document-scoped state (TODO 27/38 phase 2).
     *
     * Previously these were process-global (or __thread).  Moving
     * them to the document lets two documents in the same thread
     * have different settings — important for libraries that mix
     * trusted and untrusted XML.
     *
     * `strict_mode` defaults to the value of g_taurus_strict_mode
     * at document creation; callers can override via
     * taurus_document_set_strict().
     *
     * `alloc_hook` / `dealloc_hook` default to the thread-local
     * hooks; per-document overrides are TODO 38 phase 3. */
    int strict_mode;

    /* TODO 117: adopted child documents from xi:include parse="xml".
     * The included doc's pool is owned by this document -- the
     * included nodes were MOVED (not copied) into our tree, so they
     * live in the included doc's pool.  `child_docs` / `child_docs_tail`
     * are the head/tail of OUR adopted-children list (single-linked
     * via `next_adopted` on each child so we can append in O(1) without
     * needing a "next" pointer on the parent).  Typically very short
     * (a handful of xi:include directives). */
    struct taurus_document* child_docs;
    struct taurus_document* child_docs_tail;  /* Append in O(1). */
    struct taurus_document* next_adopted;    /* Singly-linked sibling. */
};

/* Parse options structure */
typedef struct {
    int strict;                  /* Strict mode (1=enabled, 0=disabled) */
    int preserve_whitespace;     /* Preserve whitespace (1=enabled, 0=disabled) */
    int track_positions;         /* Track positions (1=enabled, 0=disabled) */
    int max_depth;               /* Maximum element nesting depth (0 = default 256) */
} taurus_parse_options;

/* Internal parse function - implemented in parse_simple.c or parser_new.c */
extern struct taurus_document* taurus_parse(const char* xml, size_t len);

/* Get current strict parsing mode */
extern int taurus_get_strict_mode(void);

/* Namespace structure - Matches ext/taurus/taurus.h _namespace */
struct taurus_namespace {
    char* prefix;                /* Namespace prefix (NULL = default namespace) */
    char* uri;                   /* Namespace URI (required) */
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

/* XPath axis types (forward decl of the canonical enum — full
 * definition lives below; here so XPathASTNode can reference it
 * for the axis_id perf optimization in TODO 113 Phase 1). */
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

    /* Axis enum (TODO 113 Phase 1): populated on XPATH_AST_STEP nodes
     * to skip the strcmp dispatch chain in apply_axis. Defaults to
     * XPATH_AXIS_CHILD (the default axis per XPath spec). */
    XPathAxisType axis_id;
} XPathASTNode;

/* XPath parser - Matches ext/taurus/xpath.h _xpath_parser */
typedef struct xpath_parser {
    XPathLexer* lexer;
    XPathToken* tokens;          /* Token array for lookahead */
    size_t token_count;
    size_t token_pos;
    char error_msg[256];
} XPathParser;

/* XPath nodeset - Holds typed node pointers (elements or attributes).
 *
 * TODO 113 Phase 2 perf: small-buffer optimization. The first 16
 * entries live inline in the struct so the common case (small query
 * result) avoids the second heap allocation entirely. Larger
 * nodesets spill to a separately-allocated array. */
#define XPATH_NODESET_INLINE_CAPACITY 16
typedef struct xpath_nodeset {
    void** nodes;                /* Typed node pointers; points to
                                  * inline_data for small nodesets,
                                  * otherwise heap-allocated. */
    size_t count;
    size_t capacity;
    int owns_attributes;         /* If true, free attribute nodes on nodeset_free */
    int owns_namespaces;         /* If true, free namespace nodes on nodeset_free */
    void* inline_data[XPATH_NODESET_INLINE_CAPACITY];
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
} XPathContext;

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

/* XPathAxisType defined above (near XPathASTType) so it's in scope
 * for the XPathASTNode.axis_id field. */

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

/* Generic free wrapper */
static inline void taurus_free(void* ptr) {
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
 * ============================================================================ */

/* Set error with basic message */
void taurus_set_error(taurus_error_code code, const char* message);

/* Set error with line/column position */
void taurus_set_parse_error_position(int line, int column);

/* Set error with full context (message, input, position, snippet) */
void taurus_set_error_with_context(
    taurus_error_code code,
    const char* message,
    const char* input,
    size_t byte_offset,
    int line,
    int column
);

/* Extract context snippet from input around error position */
void taurus_extract_context_snippet(
    const char* input,
    size_t offset,
    int error_line,
    char* out_buffer,
    size_t buffer_size
);

#endif /* TAURUS_INTERNAL_H */