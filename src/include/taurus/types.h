/* libtaurus - Core Type Definitions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains opaque type definitions and enums used throughout
 * the libtaurus API. Include this file when you need type definitions
 * without pulling in the entire API.
 *
 * POINTER-BASED ARCHITECTURE:
 * TaurusElement points directly to ptr_element for maximum performance.
 * No offset calculations - direct pointer access only.
 */

#ifndef TAURUS_TYPES_H
#define TAURUS_TYPES_H
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types - Hide implementation details
 * ============================================================================ */

typedef struct taurus_document*     TaurusDocument;

/* POINTER-BASED: Direct pointer to ptr_element for maximum performance
 * The ptr_element structure is defined in ptr_element.h which is included
 * via taurus_internal.h in implementation files. */
typedef struct ptr_element*         TaurusElement;

/* Attribute type - opaque pointer */
typedef struct taurus_attribute*    TaurusAttribute;

/* Namespace is just the URI string */
typedef const char*                 TaurusNamespace;

typedef struct taurus_xpath_result* TaurusXPathResult;

/* Compiled XPath expression - pre-parsed for faster repeated evaluation */
typedef struct taurus_xpath_compiled* TaurusXPathCompiled;

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
    TAURUS_ERROR_INVALID_STATE = -8 /* Invalid state for operation */
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
 * Parsing Options
 * ============================================================================ */

/**
 * Parsing option flags for performance optimization
 *
 * These flags control trade-offs between correctness/validation and performance.
 * Use taurus_parse_string_ex() to specify these options.
 */
typedef enum {
    /* Default: Full XML 1.0 compliance with namespaces */
    TAURUS_PARSE_DEFAULT = 0,

    /* Skip namespace resolution (faster for documents without namespace queries)
     * Namespaces are still parsed but not resolved to URIs.
     * Use this when you don't need XPath namespace-aware queries. */
    TAURUS_PARSE_NO_NAMESPACE_RESOLUTION = (1 << 0),

    /* Skip entity expansion (faster, but returns entity references as-is)
     * Only use for documents without entities or when you'll handle entities yourself. */
    TAURUS_PARSE_NO_ENTITY_EXPANSION = (1 << 1),

    /* Fast mode: combines all skip flags for maximum speed
     * Suitable for trusted input where you don't need namespace/entity features. */
    TAURUS_PARSE_FAST = TAURUS_PARSE_NO_NAMESPACE_RESOLUTION | TAURUS_PARSE_NO_ENTITY_EXPANSION
} TaurusParseFlags;

/**
 * Parsing options structure
 */
typedef struct {
    TaurusParseFlags flags;    /* Combination of TaurusParseFlags */
    int strict;                /* 1 = strict validation, 0 = lenient (default: 0) */
} TaurusParseOptions;

/* ============================================================================
 * Serialization Options
 * ============================================================================ */

typedef struct {
    int indent;              /* 0 = compact, >0 = pretty-print with N spaces */
    int xml_declaration;     /* 1 = include <?xml?>, 0 = omit */
    const char* encoding;    /* "UTF-8" or NULL for default */
} TaurusSerializeOptions;

/* ============================================================================
 * C14N (Canonical XML) Types
 * ============================================================================ */

typedef enum {
    TAURUS_C14N_1_0 = 0,      /* Canonical XML 1.0 */
    TAURUS_C14N_1_1 = 1       /* Canonical XML 1.1 */
} TaurusC14NVersion;

/* ============================================================================
 * XPath Variable Types
 * ============================================================================ */

typedef enum {
    TAURUS_XPATH_VAR_TYPE_NONE = 0,      /* Invalid type */
    TAURUS_XPATH_VAR_TYPE_BOOLEAN,       /* Boolean value */
    TAURUS_XPATH_VAR_TYPE_NUMBER,        /* Floating-point number */
    TAURUS_XPATH_VAR_TYPE_STRING,        /* String value */
    TAURUS_XPATH_VAR_TYPE_NODE_SET       /* Node set */
} TaurusXPathVariableType;

/* Opaque variable set type */
typedef struct taurus_xpath_variable_set* TaurusXPathVariableSet;

/* ============================================================================
 * Memory Allocation Function Types
 * ============================================================================ */

typedef void* (*taurus_allocation_function)(size_t size);
typedef void (*taurus_deallocation_function)(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_TYPES_H */
