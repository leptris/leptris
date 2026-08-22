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
    LEPTRIS_XPATH_STRING
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
    LEPTRIS_NODE_TYPE_ATTRIBUTE = 6  /* reserved; not produced by the parser */
} LeptrisNodeKind;

/* Kind of a node inside an XPath nodeset result. Nodesets are mixed:
 * element nodes alongside synthetic attribute nodes (from @attr /
 * attribute:: axes). Consume with leptris_xpath_result_node_kind —
 * leptris_xpath_result_get returns elements only. */
typedef enum {
    LEPTRIS_XPATH_NODE_ELEMENT = 0,
    LEPTRIS_XPATH_NODE_ATTRIBUTE,
    LEPTRIS_XPATH_NODE_TEXT,
    LEPTRIS_XPATH_NODE_OTHER   /* comment, namespace, ... */
} LeptrisXPathNodeKind;

/* ============================================================================
 * Serialization Options
 * ============================================================================ */

typedef struct {
    int indent;              /* 0 = compact, >0 = pretty-print with N spaces */
    int xml_declaration;     /* 1 = include <?xml?>, 0 = omit */
    const char* encoding;    /* "UTF-8" or NULL for default */
} LeptrisSerializeOptions;

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

/* ============================================================================
 * Memory Allocation Function Types
 * ============================================================================ */

typedef void* (*leptris_allocation_function)(size_t size);
typedef void (*leptris_deallocation_function)(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_TYPES_H */
