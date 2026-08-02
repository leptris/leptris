/* libtaurus - Core Type Definitions
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * This file contains opaque type definitions and enums used throughout
 * the libtaurus API. Include this file when you need type definitions
 * without pulling in the entire API.
 */

#ifndef TAURUS_TYPES_H
#define TAURUS_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Opaque Types - Hide implementation details
 *
 * Guarded so re-including this header alongside taurus.h doesn't
 * produce a typedef-redefinition warning.  See TODO 12.
 * ============================================================================ */

#ifndef TAURUS_INTERNAL_TYPES_DEFINED
#define TAURUS_INTERNAL_TYPES_DEFINED
typedef struct taurus_document*     TaurusDocument;
typedef struct taurus_element*      TaurusElement;
typedef struct taurus_attribute*    TaurusAttribute;
typedef const char*                 TaurusNamespace;
typedef struct taurus_xpath_result* TaurusXPathResult;
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
    TAURUS_ERROR_IO = -7           /* I/O error (file not found, etc.) */
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
