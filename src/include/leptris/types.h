/* libleptris - Core Type Definitions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains opaque type definitions and enums used throughout
 * the libleptris API. Include this file when you need type definitions
 * without pulling in the entire API.
 */

#ifndef LEPTRIS_TYPES_H
#define LEPTRIS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types - Hide implementation details
 *
 * Guarded so re-including this header alongside leptris.h doesn't
 * produce a typedef-redefinition warning.  See TODO 12.
 * ============================================================================ */

#ifndef LEPTRIS_INTERNAL_TYPES_DEFINED
#define LEPTRIS_INTERNAL_TYPES_DEFINED
typedef struct leptris_node*            LeptrisNodeRef;
typedef struct leptris_document*     LeptrisDocument;
typedef struct leptris_element*      LeptrisElement;
typedef struct leptris_attribute*    LeptrisAttribute;
typedef struct leptris_doctype*      LeptrisDoctype;
typedef const char*                 LeptrisNamespace;
typedef struct leptris_xpath_result* LeptrisXPathResult;
#endif

/* ============================================================================
 * Status Codes
 * ============================================================================ */

typedef enum {
    LEPTRIS_OK = 0,
    LEPTRIS_ERROR_MEMORY = -1,      /* Memory allocation failed */
    LEPTRIS_ERROR_PARSE = -2,       /* XML parsing error */
    LEPTRIS_ERROR_XPATH = -3,       /* XPath evaluation error */
    LEPTRIS_ERROR_NULL_ARG = -4,    /* NULL argument passed */
    LEPTRIS_ERROR_INVALID_ARG = -5, /* Invalid argument */
    LEPTRIS_ERROR_NOT_FOUND = -6,   /* Resource not found */
    LEPTRIS_ERROR_IO = -7,          /* I/O error (file not found, etc.) */
    LEPTRIS_ERROR_NOT_IMPLEMENTED = -8 /* Feature not yet implemented */
} LeptrisStatus;

/* ============================================================================
 * XPath Result Types
 * ============================================================================ */

typedef enum {
    LEPTRIS_XPATH_NODESET,
    LEPTRIS_XPATH_BOOLEAN,
    LEPTRIS_XPATH_NUMBER,
    LEPTRIS_XPATH_STRING,
    /* XPath 3.0 function item: a closure or named function
     * reference. Internally carried as a single-member synthetic
     * nodeset; classified at this public boundary. Appended —
     * existing values keep their ABI numbers. */
    LEPTRIS_XPATH_FUNCTION
} LeptrisXPathResultType;

/* ============================================================================
 * Node Kinds
 * ============================================================================ */

/* Kind of a tree node, as returned by leptris_node_get_type.
 * XPath RESULT nodes carry a separate internal tag space — use
 * LeptrisXPathNodeKind (below) for those. */
typedef enum {
    LEPTRIS_NODE_TYPE_ELEMENT = 0,
    LEPTRIS_NODE_TYPE_TEXT = 1,
    LEPTRIS_NODE_TYPE_COMMENT = 2,
    LEPTRIS_NODE_TYPE_CDATA = 3,
    LEPTRIS_NODE_TYPE_PI = 4,
    LEPTRIS_NODE_TYPE_DOCTYPE = 5,
    LEPTRIS_NODE_TYPE_ATTRIBUTE = 6, /* reserved; not produced by the parser */
    /* Document node (XPath root). Values 7/8 are reserved for the
     * XPath-synthetic internal kinds (namespace/text) — see
     * leptris_internal.h — so the public document node sits at 9. */
    LEPTRIS_NODE_TYPE_DOCUMENT = 9
} LeptrisNodeKind;

/* Kind of a node inside an XPath nodeset result. Nodesets are mixed:
 * element nodes alongside synthetic attribute nodes (from \@attr /
 * attribute:: axes). Consume with leptris_xpath_result_node_kind —
 * leptris_xpath_result_get returns elements only. */
typedef enum {
    LEPTRIS_XPATH_NODE_ELEMENT = 0,
    LEPTRIS_XPATH_NODE_ATTRIBUTE,
    LEPTRIS_XPATH_NODE_TEXT,
    LEPTRIS_XPATH_NODE_OTHER   /* comment, namespace, ... */
} LeptrisXPathNodeKind;

/* ============================================================================
 * Pull (StAX-style) Parsing (TODO.bindings/04)
 * ============================================================================ */

typedef enum {
    LEPTRIS_PULL_START_ELEMENT = 0,
    LEPTRIS_PULL_END_ELEMENT,
    LEPTRIS_PULL_TEXT,
    LEPTRIS_PULL_COMMENT,
    LEPTRIS_PULL_CDATA,
    LEPTRIS_PULL_PI,
    LEPTRIS_PULL_END_DOCUMENT,
    LEPTRIS_PULL_ERROR,
    LEPTRIS_PULL_START_PREFIX,
    LEPTRIS_PULL_END_PREFIX
} LeptrisPullEventType;

typedef struct {
    LeptrisPullEventType type;
    const char* name;   /* element name / PI target; NULL otherwise */
    const char* text;   /* text / comment / CDATA / PI data / error
                         * message; NULL otherwise (NUL-terminated) */
    size_t text_len;    /* text length in bytes */
} LeptrisPullEvent;

typedef struct leptris_pull_parser* LeptrisPullParser;

/* ============================================================================
 * SAX event recorder (issue #585)
 * ============================================================================
 * Fixed-size records + a packed string arena, drained in bulk per fed
 * chunk — callback count becomes O(chunks), not O(events), for FFI
 * hosts (ffi's generic callback dispatch cost more per event than the
 * parse itself). */

typedef enum {
    LEPTRIS_SAX_EVENT_START_DOCUMENT = 0,
    LEPTRIS_SAX_EVENT_END_DOCUMENT,
    LEPTRIS_SAX_EVENT_START_ELEMENT,
    LEPTRIS_SAX_EVENT_END_ELEMENT,
    LEPTRIS_SAX_EVENT_CHARACTERS,
    LEPTRIS_SAX_EVENT_COMMENT,
    LEPTRIS_SAX_EVENT_CDATA,
    LEPTRIS_SAX_EVENT_PI,
    LEPTRIS_SAX_EVENT_START_PREFIX,
    LEPTRIS_SAX_EVENT_END_PREFIX,
    LEPTRIS_SAX_EVENT_ERROR
} LeptrisSaxEventKind;

/* One buffered event. Strings are NOT inline: name/text/attrs slice
 * the arena returned by leptris_sax_recorder_arena — the host reads
 * the whole arena in one bulk transfer, then slices in host code.
 *
 * START_ELEMENT: name = element name; attrs_off addresses
 * name\0value\0… pairs, attr_count pairs.
 * PI: name = target, text = data. START_PREFIX: name = prefix (may
 * be ""), text = URI. ERROR: text = message, line/column set.
 * All other kinds: only the fields their event carries. */
typedef struct LeptrisSaxEventRecord {
    uint8_t kind;
    uint8_t reserved[7];
    uint32_t name_off, name_len;
    uint32_t text_off, text_len;
    uint32_t attrs_off;
    uint32_t attr_count;
    uint32_t line, column;
} LeptrisSaxEventRecord;   /* fixed layout — FFI mirrors this */

typedef struct leptris_sax_recorder* LeptrisSaxRecorder;

/* ============================================================================
 * Incremental (iterparse) Parsing (TODO.bindings/02)
 * ============================================================================ */

typedef enum {
    LEPTRIS_ITERPARSE_TOP_LEVEL = 0,       /* v1: root's children only */
    LEPTRIS_ITERPARSE_FULL_DOCUMENT = 1    /* v2: every element */
} LeptrisIterparseMode;

typedef struct leptris_iterparse* LeptrisIterparse;

/* ============================================================================
 * Per-parse Options (TODO.bindings/05)
 * ============================================================================ */

/* Parse flags for leptris_parse_string_flags().
 *
 * LEPTRIS_PARSE_DROP_WS_TEXT discards whitespace-ONLY text nodes
 * (runs between tags that contain nothing but spaces/tabs/newlines).
 * This matches pugixml's default behavior (their parse_ws_pcdata is
 * opt-in) and libxml2's XML_PARSE_NOBLANKS. By default leptris KEEPS
 * these nodes — the faithful-DOM behavior of libxml2/Nokogiri and
 * the only way to round-trip pretty-printed XML byte-for-byte.
 * Pretty-printed documents carry one ws-only node per element;
 * dropping them removes ~6ns of create+wire per element and wins
 * the whitespace-heavy parse shapes outright. */
/* LEPTRIS_PARSE_DTDATTR applies DTD ATTLIST default (and #FIXED)
 * attribute values to matching elements at parse time. OPT-IN, the
 * libxml2 XML_PARSE_DTDATTR default: without it a non-validating
 * parse leaves defaulted attributes out of the tree (XML 1.0 §5
 * permits either; the ecosystem compares against libxml2/Nokogiri,
 * and W3C C14N 1.1 example 3.3's canonical form excludes them —
 * issue #606). */
typedef enum {
    LEPTRIS_PARSE_DEFAULT     = 0,
    LEPTRIS_PARSE_DROP_WS_TEXT = 1u,
    LEPTRIS_PARSE_DTDATTR     = 2u
} LeptrisParseFlags;

typedef struct {
    LeptrisParseFlags flags;   /* passthrough to the parser */
    int strict_mode;           /* -1 = keep thread default (recommended),
                                * 0 = lenient, 1 = strict W3C */
    int max_depth;             /* 0 = engine default (256), >0 = cap */
    /* Issue #547: when non-zero, a parse failure returns an empty
     * document (with the failure recorded via leptris_last_error /
     * leptris_last_error_position) instead of returning NULL. Mirrors
     * the recovery semantics moxml + the libxml2 adapter emulate;
     * partial-tree recovery lands with a future parser rework. */
    int recover;
} LeptrisParseOptions;

/* ============================================================================
 * Serialization Options
 * ============================================================================ */

/* ABI-FROZEN (issue #568): callers allocate this struct. The
 * 1.9.1 release appended fields here and every binding compiled
 * against the 1.9.0 layout segfaulted (the serializer read past
 * the caller's allocation). Layout is now pinned by static
 * assert; new options go through the internal extended-options
 * entry, never by growing this struct. */
typedef struct {
    int indent;              /* 0 = compact, >0 = pretty-print with N spaces */
    int xml_declaration;     /* 1 = include <?xml?>, 0 = omit */
    const char* encoding;    /* "UTF-8" or NULL for default */
} LeptrisSerializeOptions;
/* C89-portable size pin: a negative array type fails to compile on
 * every compiler (clang, gcc, MSVC C and C++) the moment the struct
 * grows. _Static_assert is C11-only and static_assert C++-only. */
typedef char leptris_serialize_options_abi_frozen_[
    sizeof(LeptrisSerializeOptions) ==
            2 * sizeof(int) + sizeof(void*)
        ? 1 : -1];

/* Post-1.9.x serialization extensions (issue #129): the frozen
 * options struct cannot grow, so newer knobs live here and ride the
 * leptris_document_serialize_ext entry. Zero-initialize; every field
 * defaults to the historical behavior. This struct MAY grow — pass
 * its size-correct allocation (sizeof this type at your compile
 * time) via leptris_document_serialize_ext. */
typedef struct {
    /* 1 = the formatter also indents TEXT and mixed content (the
     * pretty-printer normally keeps mixed elements on one line to
     * guarantee byte-exact round-trips — #534). Display-oriented:
     * output is NOT guaranteed to round-trip. */
    int indent_text;
    /* Indent unit string — one copy per depth level, replacing the
     * default spaces (issue #633; libxml2 xmlTreeIndentString /
     * Nokogiri indent_text). NULL = options->indent spaces per
     * level. Requires options->indent > 0. */
    const char* indent_unit;
} LeptrisSerializeExtOptions;

/* ============================================================================
 * C14N (Canonical XML) Types
 * ============================================================================ */

typedef enum {
    LEPTRIS_C14N_1_0 = 0,      /* Canonical XML 1.0 */
    LEPTRIS_C14N_1_1 = 1       /* Canonical XML 1.1 */
} LeptrisC14NVersion;

/* C14N mode (issue #183).
 *
 * CANONICAL: standard Canonical XML 1.0/1.1 (the original algorithm).
 *   Keeps all namespace declarations visible in the output.
 *
 * EXCLUSIVE: Exclusive Canonical XML (http://www.w3.org/2001/10/xml-exc-c14n#).
 *   Drops namespace declarations that are not visibly used by the
 *   canonicalized subtree. Used by XML Digital Signature to avoid
 *   signature breakage when enveloped XML carries extra namespace
 *   context.
 *
 * Pair with `inclusive_ns_prefixes` on the `_ex` variants to add
 * prefixes to the visible-namespace set even when exclusive mode
 * would otherwise drop them. */
typedef enum {
    LEPTRIS_C14N_MODE_CANONICAL  = 0,
    LEPTRIS_C14N_MODE_EXCLUSIVE  = 1
} LeptrisC14NMode;

/* ============================================================================
 * XPath Variable Types
 * ============================================================================ */

typedef enum {
    LEPTRIS_XPATH_VAR_TYPE_NONE = 0,      /* Invalid type */
    LEPTRIS_XPATH_VAR_TYPE_BOOLEAN,       /* Boolean value */
    LEPTRIS_XPATH_VAR_TYPE_NUMBER,        /* Floating-point number */
    LEPTRIS_XPATH_VAR_TYPE_STRING,        /* String value */
    LEPTRIS_XPATH_VAR_TYPE_NODE_SET       /* Node set */
} LeptrisXPathVariableType;

/* Opaque variable set type */
typedef struct leptris_xpath_variable_set* LeptrisXPathVariableSet;

/* External namespace bindings for XPath/XPointer evaluation
 * (leptris_xpath_ns_set_* below). */
typedef struct leptris_xpath_ns_map* LeptrisXPathNsSet;

/* Compiled XPath expression handle (TODO.bindings/03). Opaque; created
 * by leptris_xpath_compile, freed by leptris_xpath_compiled_free. */
typedef struct leptris_xpath_compiled* LeptrisXPathCompiled;

/* Compiled XSLT stylesheet handle (TODO.transform). Opaque; created by
 * leptris_xslt_parse, freed by leptris_xslt_free. Immutable once
 * compiled — apply it to any number of documents, from any threads. */
typedef struct leptris_xslt* LeptrisXslt;

/* ============================================================================
 * Memory Allocation Function Types
 * ============================================================================ */

typedef void* (*leptris_allocation_function)(size_t size);
typedef void (*leptris_deallocation_function)(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_TYPES_H */
