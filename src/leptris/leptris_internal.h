/* libleptris - Internal data structures
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * INTERNAL HEADER - Not part of public API
 * These structures are implementation details and may change between versions.
 */

#ifndef LEPTRIS_INTERNAL_H
#define LEPTRIS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* Linkage marker source: declarations mirrored between this header
 * and the public leptris.h must expand LEPTRIS_API identically in
 * every TU (C2375 on MSVC otherwise). Pull the real definition —
 * no fallback macro, which would poison leptris.h's #ifndef guard. */
#include "../include/leptris.h"
#include <stdlib.h>
#include <string.h>

/* Memory pool for fast DOM allocation */
#include "memory/pool.h"

/* StringView for zero-copy string handling */
#include "common/string_view.h"

/* Forward declarations - actual typedefs in respective headers */
struct leptris_element;  /* typedef in dom/element.h */
struct leptris_compact_overflow_entry;  /* typedef in dom/compact.h */
struct leptris_element_index;  /* typedef in dom/element_index.h */

/* ============================================================================
 * Error Codes (Internal)
 * ============================================================================ */

typedef enum {
    LEPTRIS_ERROR_NONE = 0,
    LEPTRIS_ERROR_MEMORY_ALLOCATION,
    LEPTRIS_ERROR_PARSE_FAILED,
    LEPTRIS_ERROR_XPATH_EVALUATION,
    LEPTRIS_ERROR_XPATH_SYNTAX,
    LEPTRIS_ERROR_XPATH_FUNCTION,
    LEPTRIS_ERROR_EVAL_CONTEXT,
    LEPTRIS_ERROR_INVALID_ARGUMENT,
    LEPTRIS_ERROR_NULL_INPUT,
    LEPTRIS_ERROR_EMPTY_INPUT,
    LEPTRIS_ERROR_OUT_OF_MEMORY,
    LEPTRIS_ERROR_INVALID_XML
} leptris_error_code;

/* ============================================================================
 * Internal Structures - Match ext/leptris/leptris.h but without Ruby
 * ============================================================================ */

/* Tag values for the XPath-SYNTHETIC node structs (LeptrisAttributeNode,
 * LeptrisNamespaceNode, XPathTextNode below). They share the value space
 * of the public LeptrisNodeKind carried by every real DOM node
 * (element=0, text=1, comment=2, cdata=3, pi=4, doctype=5): synthetic
 * kinds use values the public enum never produces, so reading the first
 * int of any entry in a mixed nodeset classifies it unambiguously
 * (issue #477). Do not renumber into 0..5. */
typedef enum {
    LEPTRIS_NODE_ELEMENT = 0,     /* == public LEPTRIS_NODE_TYPE_ELEMENT */
    LEPTRIS_NODE_ATTRIBUTE = 6,   /* == public reserved LEPTRIS_NODE_TYPE_ATTRIBUTE */
    LEPTRIS_NODE_NAMESPACE = 7,
    LEPTRIS_NODE_TEXT = 8         /* synthetic text() result node */
} LeptrisNodeType;

/* Document-level nodes (issue #580): real pool comment/PI nodes
 * chained with the root element as the document node's children —
 * [prolog..., root, epilog...] in document order (libxml2 model).
 * Every document-level consumer (serializer, c14n, the #526 flat
 * accessors, XSLT numbering, the XPath document-node axes) walks
 * this single chain; the separate side-list store is gone. */

/* Document structure */
/* Contiguous block of mutation elements (round 18). Chained via
 * next; freed with the document. Elements are carved from [base,
 * base+count). */
struct leptris_mut_elem_block {
    struct leptris_mut_elem_block* next;
    /* Raw storage for N contiguous leptris_element structs; typed
     * as bytes to avoid an incomplete-type dependency. */
    char bytes[];
};

/* Round 21 twin: chained blocks for mutation-created element NAME
 * storage. Names are short (element names, not content); carved
 * bump-style, freed with the document. */
struct leptris_mut_name_block {
    struct leptris_mut_name_block* next;
    char bytes[];
};

/* Round 22: chained blocks for mutation-created ATTRIBUTE structs.
 * Contiguous 40-byte stride keeps sequentially-created attrs of one
 * element adjacent — their cp16 `next` edges stay in-range instead
 * of hitting the compact-overflow path (the round-18 element-block
 * win, applied to attrs). */
struct leptris_mut_attr_block {
    struct leptris_mut_attr_block* next;
    char bytes[];
};

struct leptris_document {
    struct leptris_element* root;             /* Root element (legacy API) */
    /* Snapshot of this document's last failing parse message
     * (TODO.concurrency/01; thread-safe alternative to the
     * thread-local channel). */
    char last_error_message[256];

    /* EXSLT-style extension pack enabled via leptris_exslt_enable
     * (TODO.concurrency/06). */
    int exslt_enabled;
    /* Active XSLT transform state (TODO.transform 04/05): while a
     * transform runs on this document, leptris_xpath_build_custom_
     * registry registers the XSLT function bridge (current/key/
     * format-number/generate-id/system-property/document + EXSLT
     * node-set/regexp/date) with this pointer as the handlers'
     * user_data. Set only for the transform's duration (save/
     * restore in xslt_transform); NULL otherwise. Concurrent
     * transforms of the SAME document are not supported — one
     * document per mutating thread, per the README model. */
    void* xslt_state;
    char* encoding;                 /* UTF-8 assumed, but store if specified */
    /* Issue #580: the document node's child chain (see the comment
     * above) — LeptrisNodeRef comment/PI nodes + the root element. */
    void* doc_children_head;
    void* doc_children_tail;
    size_t ref_count;               /* Reference counting for memory management */
    void* new_dom_root;             /* New DOM tree root (LeptrisElement) for serialization */
    /* Lazy XPath document node (LEPTRIS_NODE_TYPE_DOCUMENT
     * singleton — dom/document_node.h). XSLT's initial context and
     * "/" pattern matching run on it. */
    void* document_node;
    /* XML Declaration support */
    char* xml_version;              /* "1.0", "1.1", etc. or NULL if not present */
    int standalone;                 /* -1=not set, 0=no, 1=yes */
    int had_declaration;            /* 1 if input had <?xml?>, 0 otherwise */
    /* 1 if ANY namespace declaration exists anywhere in the
     * document (parse or mutation). XPath 1.0 §2.3: an unprefixed
     * name test matches only no-namespace elements; when this is 0
     * every element is no-namespace and the hot name-match paths
     * skip the namespace check entirely (issue #525). */
    int has_namespaces;
    int has_bom;                    /* 1 if UTF-8 BOM was present, 0 otherwise */
    /* Issue #541 mem-cache: the last successful serialization. */
    unsigned ser_version;          /* bump on any mutation */
    char* ser_cache;
    size_t ser_cache_len;
    unsigned ser_cache_version;
    int ser_cache_text;
    int ser_cache_omit;
    int ser_cache_indent;
    /* DOCTYPE support */
    void* doctype;                  /* LeptrisDoctypeNode* or NULL */
    void* dtd;                      /* LeptrisDTD* - Parsed DTD declarations */
    /* Memory pool for fast DOM node allocation */
    LeptrisMemoryPool* pool;         /* Pool allocator (owns all DOM nodes) */
    /* Per-document allocator hooks (TODO 74) — set BEFORE parsing to
     * override the thread-default globals.  NULL = use defaults. */
    leptris_allocation_function  alloc_hook;
    leptris_deallocation_function dealloc_hook;
    /* Compact pointer support */
    void* page_base;                /* Base pointer for compact pointer decoding */
    struct leptris_compact_overflow_entry* overflow_entries; /* Per-document overflow entry list head */
    /* In-place parsing support (zero-copy optimization) */
    char* xml_buffer;               /* Owned writable XML buffer (NULL if not in-place) */
    size_t xml_buffer_len;       /* Length of xml_buffer */
    int xml_buffer_needs_free;   /* 1 if xml_buffer needs free(), 0 if stack/const */
    /* Extra allocation past len+1: the parser's owns-copy over-
     * allocates 64 zeroed bytes for slack-backed probe windows. The
     * release path must hand the retention free-list the TRUE size
     * or later best-fit reuses under-advertise the block. */
    unsigned xml_buffer_slack;
    /* Lazy line resolution (issue #223 follow-up): parse-created
     * nodes store byteOffset+1 in base.line; leptris_node_line builds
     * this newline-offset table ONCE per document (memchr hop over
     * xml_buffer) and resolves queries via binary search, caching
     * the result in the node with the high bit set. NULL = not yet
     * built. Freed with the document. */
    uint32_t* line_breaks;
    size_t line_break_count;
    /* Doc-level attribute-name index (mutation path): open-addressed
     * (element, name-hash) -> attr. Built lazily on the first
     * set/remove_attribute; each element bulk-registers its attrs on
     * first touch (one O(N) walk amortized per element) via the
     * (elem, hash=0) sentinel. Safe with raw pointers because nodes
     * are arena-backed and unlink-never-frees — entries outlive any
     * detach. remove_attribute tombstones its entry. NULL = unused
     * (never-mutated documents pay nothing). */
    struct leptris_attr_index* attr_index;
    /* Mutation attr-tail cache (the 195a child-tail twin): elements
     * carry no last-attribute edge (64-byte layout law), so appends
     * would otherwise walk the list — O(N^2) for programmatic attr
     * builds. Sequential set_attribute on one element is O(1). */
    struct leptris_element* mut_attr_elem;
    struct leptris_attribute* mut_attr_tail;
    /* Mutation element bump block (round 18): leptris_element_create
     * was allocating each mutation element via the pool extension
     * path — a separate malloc per element, scattered across heap
     * regions. Parent edges (root→child) crossed regions → every
     * edge hit the compact-overflow table; sibling edges (cp16)
     * missed when mallocs weren't adjacent. Measured: 33× behind
     * pugixml on sequential append.
     *
     * This bump block keeps mutation elements CONTIGUOUS: sibling
     * edges stay in cp16 range, parent edges in int32 range, zero
     * overflow-table hits. Grows geometrically; blocks are chained
     * and freed with the document. */
    struct leptris_mut_elem_block* mut_elem_blocks;
    struct leptris_element* mut_elem_cursor;
    struct leptris_element* mut_elem_end;
    /* Mutation name bump block (round 21): element_create's name
     * copy went through leptris_pool_strdup → arena_alloc (call
     * chain + strlen + slack checks, ~9ns for a 2-byte name).
     * Carving from a per-doc contiguous block is a bump + memcpy +
     * NUL. Freed with the document. */
    struct leptris_mut_name_block* mut_name_blocks;
    char* mut_name_cursor;
    char* mut_name_end;
    /* Mutation attr bump block (round 22). */
    struct leptris_mut_attr_block* mut_attr_blocks;
    struct leptris_attribute* mut_attr_cursor;
    struct leptris_attribute* mut_attr_end;
    /* Definition follows this struct (needed by document_free). */

    /* Document-scoped state (TODO 27/38 phase 2).
     *
     * Previously these were process-global (or __thread).  Moving
     * them to the document lets two documents in the same thread
     * have different settings — important for libraries that mix
     * trusted and untrusted XML.
     *
     * `strict_mode` defaults to the value of g_leptris_strict_mode
     * at document creation; callers can override via
     * leptris_document_set_strict().
     *
     * `alloc_hook` / `dealloc_hook` default to the thread-local
     * hooks; per-document overrides are TODO 38 phase 3. */
    int strict_mode;

    /* Element index (TODO 132) — lazily-built flat array + per-name
     * buckets for O(1) descendant queries. Built on SECOND axis
     * query (TODO 190): building costs two tree walks + per-name
     * bucket allocations, which a single-query document never
     * recovers — the first query walks directly and only repeat
     * queries pay for the index. NULL until then. */
    struct leptris_element_index* element_index;
    unsigned axis_query_count;  /* axis queries seen without an index */

    /* Document-order rank table (issue #485) — one preorder walk
     * assigns every node an integer rank used to sort merged
     * nodesets into document order. Cached here so repeated queries
     * don't rewalk the tree; rebuilt when mutation_version moves.
     * Bumped by leptris_element_index_invalidate, which every DOM
     * mutation path already calls. */
    void* doc_order_index;
    unsigned mutation_version;

    /* Mutation tail caches (TODO 195): the public API's append /
     * set-attribute walk to the tail because elements carry no
     * last-child edge (64 B layout law, TODO 155). Sequential
     * mutation — the dominant programmatic-build pattern — appends
     * to the SAME parent/element, so a one-entry cache makes the
     * common case O(1). Verified before use via the child's parent
     * back-pointer (stale entries fall back to the walk). */
    struct leptris_element* mut_tail_parent;
    struct leptris_node* mut_tail_child;

    /* FlatDoc + lazy-promote removed — direct_parse builds the
     * LeptrisElement tree eagerly. Retained as an always-zero field
     * so leptris_document_ensure_promoted (a public-facing no-op
     * chokepoint) can stay in the API without ABI churn. */
    int _unused_post_flat_removal;

    /* TODO 117: adopted child documents from xi:include parse="xml".
     * The included doc's pool is owned by this document -- the
     * included nodes were MOVED (not copied) into our tree, so they
     * live in the included doc's pool.  `child_docs` / `child_docs_tail`
     * are the head/tail of OUR adopted-children list (single-linked
     * via `next_adopted` on each child so we can append in O(1) without
     * needing a "next" pointer on the parent).  Typically very short
     * (a handful of xi:include directives). */
    struct leptris_document* child_docs;
    struct leptris_document* child_docs_tail;  /* Append in O(1). */
    struct leptris_document* next_adopted;    /* Singly-linked sibling. */

    /* Custom XPath function registrations (TODO 148 Phase 5).
     * Each entry stores a (name, fn, user_data) triple; the
     * evaluator merges them with the standard XPath 1.0 library
     * when building the per-context function registry. Standard
     * functions win name collisions. */
    struct leptris_custom_xpath_fn* custom_xpath_fns;

    /* Set when this doc struct was allocated from the document's
     * own pool. leptris_document_free must skip the LEPTRIS_FREE(doc)
     * call in that case — the pool destroy reclaims it. Heap-
     * allocated docs (leptris_document_copy, leptris_parse_fragment)
     * leave this 0 and get freed via LEPTRIS_FREE. TODO 154. */
    int doc_pool_allocated;

    /* Cached merged function registry (TODO.transform perf): built
     * once from custom_xpath_fns + exslt_enabled + xslt_state, then
     * REUSED by every evaluation context on this document. A
     * transform builds ~45 registrations per expression otherwise.
     * Invalidate (free + NULL) whenever any of the three inputs
     * changes; document_free releases it. Evaluation contexts mark
     * it borrowed so their cleanup leaves it alive. */
    void* cached_fn_registry;
};

/* Open-addressed (element, name-hash) -> attribute index for the
 * mutation path (see doc->attr_index). Defined here so document_free
 * can release it; all logic lives in dom/element_modify.c. */
struct leptris_attr_index_entry {
    struct leptris_element* elem;
    uint32_t name_hash;
    struct leptris_attribute* attr;   /* NULL: tombstone or sentinel */
};

struct leptris_attr_index {
    struct leptris_attr_index_entry* slots;
    size_t cap;   /* power of two */
    size_t used;
};

/* Parse options structure */
typedef struct {
    int strict;                  /* Strict mode (1=enabled, 0=disabled) */
    int preserve_whitespace;     /* Preserve whitespace (1=enabled, 0=disabled) */
    int track_positions;         /* Track positions (1=enabled, 0=disabled) */
    int max_depth;               /* Maximum element nesting depth (0 = default 256) */
} leptris_parse_options;

/* Internal parse function - implemented in parse_simple.c or parser_new.c */
extern struct leptris_document* leptris_parse(const char* xml, size_t len);

/* TODO 139 Phase D: trigger lazy promote if the doc has a parsed
 * FlatDoc that hasn't been built into the compact-pointer tree yet.
 * Safe to call multiple times. Internal helper. */
extern void leptris_document_ensure_promoted(struct leptris_document* doc);

/* Get current strict parsing mode */
extern LEPTRIS_API int leptris_get_strict_mode(void);

/* Namespace structure - Matches ext/leptris/leptris.h _namespace */
struct leptris_namespace {
    char* prefix;                /* Namespace prefix (NULL = default namespace) */
    char* uri;                   /* Namespace URI (required) */
    struct leptris_namespace* next; /* Linked list for multiple declarations */
};

/* Document-order rank cache teardown (issue #485). Defined in
 * xpath/evaluator_path.c. */
void leptris_doc_order_index_free(void* table);

/* Copy the thread-local error message into the document's snapshot
 * slot. error.c; called by the public parse entry points on
 * failure (TODO.concurrency/01). */
void leptris_doc_snapshot_error(struct leptris_document* doc);

/* Thread-local position of the most recent parse error (issue #510).
 * Set by the parse fail path next to leptris_set_error. */
void leptris_set_error_position(int line, int column);

/* TODO.concurrency/08: per-thread cache drains, called by the public
 * leptris_thread_cleanup from each worker thread before it exits. */
void leptris_xpath_drain_thread_caches(void);
void leptris_root_doc_drain_thread_caches(void);

/* ============================================================================
 * XPath Node Type System
 * ============================================================================ */

/* Attribute node structure - dedicated type for attribute nodes in XPath */
typedef struct leptris_attribute_node {
    LeptrisNodeType node_type;    /* Always LEPTRIS_NODE_ATTRIBUTE */
    char* name;                  /* Attribute name */
    char* value;                 /* Attribute value */
    char* namespace_uri;         /* Namespace URI (can be NULL) */
    struct leptris_element* owner;           /* Owner element */
} LeptrisAttributeNode;

/* Namespace node structure - dedicated type for namespace nodes in XPath */
typedef struct leptris_namespace_node {
    LeptrisNodeType node_type;      /* Always LEPTRIS_NODE_NAMESPACE */
    char* prefix;                  /* Namespace prefix (NULL = default) */
    char* uri;                     /* Namespace URI (required) */
    struct leptris_element* owner;           /* Owner element */
} LeptrisNamespaceNode;

/* Text node structure - for XPath text() function results (virtual text nodes) */
typedef struct xpath_text_node {
    LeptrisNodeType node_type;      /* Always LEPTRIS_NODE_TEXT */
    char* content;                 /* Text content */
    struct leptris_element* owner;           /* Parent element */
} XPathTextNode;

/* XPath node union - type-safe wrapper for all XPath node types */
typedef union xpath_node {
    LeptrisNodeType type;           /* First field for type checking */
    struct {
        LeptrisNodeType node_type;
        struct leptris_element* element;
    } as_element;
    LeptrisAttributeNode* as_attribute;
} XPathNode;

/* Type checking macros */
#define XPATH_NODE_TYPE(node) (*(LeptrisNodeType*)(node))
#define IS_ELEMENT_NODE(node) ((node) && XPATH_NODE_TYPE(node) == LEPTRIS_NODE_ELEMENT)
#define IS_ATTRIBUTE_NODE(node) ((node) && XPATH_NODE_TYPE(node) == LEPTRIS_NODE_ATTRIBUTE)
#define IS_TEXT_NODE(node) ((node) && XPATH_NODE_TYPE(node) == LEPTRIS_NODE_TEXT)

/* ============================================================================
 * XPath Internal Structures
 * ============================================================================ */

/* XPath token - Matches ext/leptris/xpath.h Token */
typedef struct xpath_token {
    int type;                    /* XPathTokenType */
    const char* value;           /* Token value (points into input, not owned) */
    size_t value_len;
    int line;
    int column;
} XPathToken;

/* XPath lexer - Matches ext/leptris/xpath.h Lexer */
typedef struct xpath_lexer {
    const char* input;           /* Input string (not owned) */
    const char* pos;             /* Current position */
    const char* end;             /* End of input */
    int line;
    int column;
    XPathToken current;
    char error_msg[256];
} XPathLexer;

/* XPath AST node types - From ext/leptris/xpath.h */
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

/* XPath AST node - Matches ext/leptris/xpath.h _xpath_ast_node */
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

/* XPath parser - Matches ext/leptris/xpath.h _xpath_parser */
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
    int owns_synthetic_text;     /* If true, free synthetic XPathTextNode
                                  * entries on nodeset_free (EXSLT
                                  * str:tokenize/split results) */
    /* XSLT 3.0 item sequence (for/range/sequence results): members are
     * one-item synthetic text nodes, and string-value consumers join
     * them with spaces instead of taking the first member. */
    int is_sequence;
    void* inline_data[XPATH_NODESET_INLINE_CAPACITY];
} XPathNodeSet;

/* XPath result types - MUST match public enum in ext/leptris/xpath.h!
 * Order matters: NODESET=0, BOOLEAN=1, NUMBER=2, STRING=3 */
typedef enum {
    XPATH_RESULT_NODESET,
    XPATH_RESULT_BOOLEAN,
    XPATH_RESULT_NUMBER,
    XPATH_RESULT_STRING,
    /* Internal-only sentinel: a result parked on the thread-local
     * free-list. The public enum never uses this value. Guards the
     * free-list against a double xpath_result_free — a cached entry
     * previously looked like a live NODESET (type 0), and freeing
     * it again would have treated the free-list next-pointer as a
     * nodeset and corrupted the list. */
    XPATH_RESULT_CACHED = 127
} XPathResultType;

/* XPath result value union */
typedef union {
    int boolean_value;
    double number_value;
    char* string_value;          /* Owned by result */
    XPathNodeSet* nodeset_value; /* Owned by result */
} XPathResultValue;

/* XPath result - Matches ext/leptris/xpath.h _xpath_result */
struct leptris_xpath_result {
    XPathResultType type;
    XPathResultValue value;
};

/* Namespace mapping for XPath context (v0.8.0) */
typedef struct xpath_namespace_mapping {
    char* prefix;                /* Namespace prefix (NULL = default namespace) */
    char* uri;                   /* Namespace URI (required) */
} XPathNamespaceMapping;

/* XPath context - Matches ext/leptris/xpath.h _xpath_context */
typedef struct xpath_context {
    struct leptris_document* document;
    struct leptris_element* context_node;
    size_t context_position;     /* 1-based position in context nodeset */
    size_t context_size;         /* Total size of context nodeset */
    void* function_registry;     /* Opaque function registry */
    char error_msg[256];

    /* Current node in predicate evaluation (can be attribute or element) */
    void* current_predicate_node;  /* The actual node being filtered (for name() in predicates) */

    /* Namespace support (v0.8.0)
     *
     * Lazy init (TODO 125): namespaces are NOT collected at context
     * creation. Collection runs on the first call to
     * xpath_context_resolve_prefix, gated by namespaces_collected.
     * Most XPath expressions never touch a namespace prefix, so
     * skipping the document walk saves 4-8 µs per eval on
     * medium and large docs. */
    XPathNamespaceMapping* namespace_mappings;
    size_t namespace_count;
    size_t namespace_capacity;
    int namespaces_collected;

    /* Variable support (v1.0.1) */
    void* variable_set;          /* LeptrisXPathVariableSet - for $var references */

    /* External namespace bindings (v1.2.0): expression prefix -> URI,
     * overriding literal prefix comparison in name tests when the
     * test prefix is bound here (XPointer xmlns() is the driver). */
    struct leptris_xpath_ns_map* ns_set;

    /* Error context support (v1.0.0) */
    const char* input;           /* Original XPath expression for error context */
    size_t input_len;            /* Length of input expression */

    /* Optimization flags */
    int to_boolean;              /* Only checking existence */
    int max_results;             /* Stop after N results (0 = unlimited) */
    int enable_early_exit;       /* Master switch for early termination */

    /* TODO 148 Phase 5: per-call user_data for custom XPath fns.
     * Set by the dispatch (evaluate_function_call_impl) before
     * invoking the handler; restored after so recursion works.
     * Standard handlers ignore the slot. */
    void* current_fn_user_data;

    /* Dynamic error code (XQuery try/catch, TODO 12): the last
     * error's QName local part (e.g. "FODC0002"); empty when the
     * failing path had no code. Named catches match it. */
    char error_code[32];

    /* The registry came from the document's cached_fn_registry —
     * context cleanup must NOT free it (the document owns it). */
    int registry_borrowed;

    /* Documents loaded by fn:doc() during this evaluation —
     * context-lifetime anchors (TODO.xslt-full/11): the returned
     * nodeset borrows the root element, so the owning document must
     * outlive the result. Cleanup frees them. */
    struct leptris_document** owned_docs;
    size_t n_owned_docs;
    size_t cap_owned_docs;
} XPathContext;

/* XPath operator types - From ext/leptris/xpath.h */
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
    XPATH_OP_NEGATION,
    /* XSLT 3.0 expression extensions (XPath 2.0+ forms), carried on
     * operator nodes: XPATH_OP_IF has 3 children (cond, then,
     * else); XPATH_OP_FOR has 2 children (bindings AST + return
     * expr) with ->value naming the loop variables in order;
     * XPATH_OP_RANGE has 2 children (from, to). */
    XPATH_OP_IF,
    XPATH_OP_FOR,
    XPATH_OP_RANGE,
    /* XPath 3.1 `let $x := E1, $y := E2 ... return B`: children
     * [0..n-1] are the binding value exprs, [n] is the body;
     * ->value carries the variable names space-joined, in order.
     * Each binding sees the earlier ones; inner lets shadow. */
    XPATH_OP_LET,
    /* XPath 3.0 `L ! R` (simple map): R evaluates once per item of
     * L with the context item/position/size set to that item's
     * slot; results concatenate in order. Arrow `=>` reuses the
     * function-call node (left side prepended as the first
     * argument). */
    XPATH_OP_MAP,
    /* XPath 3.0 `A || B`: string() both sides, concatenate. */
    XPATH_OP_CONCAT,
    /* XPath 3.1 `switch (E) { case T1 return R1 ... default return
     * RD }`: children = [operand, test1, res1, test2, res2, ...,
     * default]; the first eq-match wins (empty default = no
     * children pair). */
    XPATH_OP_SWITCH,
    /* Parenthesized item sequence `('a','b',expr)`: N children, one
     * per member — evaluates to the sequence (synthetic-text
     * nodeset). */
    XPATH_OP_SEQUENCE,
    /* 2.0 type operators (TODO.xslt-full/06): one child + the
     * SequenceType carried as node->value ("xs:integer", "node()+").
     * TREAT passes the operand through (value-level v1). */
    XPATH_OP_INSTANCE_OF,
    XPATH_OP_CASTABLE,
    XPATH_OP_CAST,
    XPATH_OP_TREAT,
    /* 3.1 map constructor `map { k: v, ... }`: children alternate
     * key/value; the value is a synthetic encoded map node. */
    XPATH_OP_MAP_CONSTRUCTOR,
    /* 3.1 square array constructor `[ a, b, ... ]`: one child per
     * member; the value rides the map representation with
     * positional keys "1".."n". */
    XPATH_OP_ARRAY_CONSTRUCTOR,
    /* 3.1 postfix lookup `V?k` / `V?2`: child[0] + the key in
     * node->value (array indices ARE the positional keys). */
    XPATH_OP_LOOKUP,
    /* 3.0 function items (07). INLINE_FN: value = params joined by
     * ' ', children[0] = body; the value is a closure node. FN_REF:
     * value = "name#arity". DYN_CALL: children[0] = callee,
     * children[1..] = args. */
    XPATH_OP_INLINE_FN,
    XPATH_OP_FN_REF,
    XPATH_OP_DYN_CALL,
    /* XQuery 1.0 constructors (TODO.xslt-full/11; value-level —
     * the result is the serialized XML string). ELEMENT_CTOR:
     * value = element name, children = ATTRIBUTE_CTOR nodes first,
     * then content expressions. ATTRIBUTE_CTOR: value = attr name,
     * children[0] = value expression. TEXT_CTOR: children[0] =
     * content expression (content escaping applies). */
    XPATH_OP_ELEMENT_CTOR,
    XPATH_OP_ATTRIBUTE_CTOR,
    XPATH_OP_TEXT_CTOR,
    /* document { content } — serializes its content with no
     * wrapper (Saxon). XPATH_OP_TRY: value = catch name-tests
     * joined by '\x01' ("*" catches all), children[0] = try body,
     * children[1..] = catch bodies in order. */
    XPATH_OP_DOCUMENT_CTOR,
    XPATH_OP_TRY,
    /* XQuery 3.0 typeswitch: value = case SequenceTypes joined
     * by '\x01' (a trailing empty entry = the default arm),
     * children = [operand, ret1, ret2, ..., defaultRet]. */
    XPATH_OP_TYPESWITCH
} XPathOperatorType;

/* XPathAxisType defined above (near XPathASTType) so it's in scope
 * for the XPathASTNode.axis_id field. */

/* ============================================================================
 * Memory Management Macros
 * ============================================================================ */

/* Forward declarations for memory allocation hooks */
void* leptris_alloc_hook(size_t size);
void leptris_free_hook(void* ptr);

/* Use standard C memory functions instead of Ruby macros */
#define LEPTRIS_ALLOC(type) \
    ((type*)malloc(sizeof(type)))

#define LEPTRIS_ALLOC_N(type, n) \
    ((type*)malloc(sizeof(type) * (n)))

#define LEPTRIS_REALLOC_N(ptr, type, n) \
    ((type*)realloc((ptr), sizeof(type) * (n)))

#define LEPTRIS_FREE(ptr) \
    do { if (ptr) { free(ptr); ptr = NULL; } } while(0)

/* Array growth helper - double capacity when full */
#define LEPTRIS_GROW_ARRAY(ptr, capacity) \
    do { \
        size_t new_cap = (capacity) == 0 ? 4 : (capacity) * 2; \
        (ptr) = realloc((ptr), new_cap * sizeof(*(ptr))); \
        (capacity) = new_cap; \
    } while(0)

/* ============================================================================
 * Generic Memory Allocation
 * ============================================================================ */

/* Generic malloc wrapper */
static inline void* leptris_malloc(size_t size) {
    return malloc(size);
}

/* Generic realloc wrapper */
static inline void* leptris_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

/* Generic free wrapper */
static inline void leptris_free(void* ptr) {
    free(ptr);
}

/* ============================================================================
 * String Helpers
 * ============================================================================ */

/* NULL-safe string duplication */
static inline char* leptris_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

/* NULL-safe string length */
static inline size_t leptris_strlen(const char* str) {
    return str ? strlen(str) : 0;
}

/* NULL-safe string comparison */
static inline int leptris_strcmp(const char* s1, const char* s2) {
    if (s1 == s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return strcmp(s1, s2);
}

/* ============================================================================
 * Internal Error Functions (from error.c)
 * ============================================================================ */

/* Set error with basic message */
void leptris_set_error(leptris_error_code code, const char* message);

/* Set error with line/column position */
void leptris_set_parse_error_position(int line, int column);

/* Set error with full context (message, input, position, snippet) */
void leptris_set_error_with_context(
    leptris_error_code code,
    const char* message,
    const char* input,
    size_t byte_offset,
    int line,
    int column
);

/* Extract context snippet from input around error position */
void leptris_extract_context_snippet(
    const char* input,
    size_t offset,
    int error_line,
    char* out_buffer,
    size_t buffer_size
);

#endif /* LEPTRIS_INTERNAL_H */