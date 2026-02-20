/* xpath_fast_path.h - Fast path XPath evaluation for common patterns
 * Copyright (c) 2024, Ribose Inc.
 *
 * PERFORMANCE: Detects common XPath patterns and bypasses full parsing.
 * Provides 10-50x speedup for simple expressions like "child::*" or "@attr".
 */

#ifndef TAURUS_XPATH_FAST_PATH_H
#define TAURUS_XPATH_FAST_PATH_H

#include <stddef.h>
#include "../include/taurus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct taurus_document;
struct taurus_xpath_result;
struct XPathContext;

/**
 * Fast path pattern types
 *
 * These are patterns that can be evaluated without full parsing.
 */
typedef enum {
    XPATH_FAST_PATH_NONE = 0,        /* Not a fast path pattern */

    /* Child axis patterns */
    XPATH_FAST_PATH_CHILD_STAR,      /* child::* or * */
    XPATH_FAST_PATH_CHILD_NAME,      /* child:tagname or tagname */
    XPATH_FAST_PATH_CHILD_TEXT,      /* child::text() or text() */

    /* Attribute patterns */
    XPATH_FAST_PATH_ATTR_STAR,       /* attribute::* or @* */
    XPATH_FAST_PATH_ATTR_NAME,       /* attribute::name or @name */

    /* Parent axis patterns */
    XPATH_FAST_PATH_PARENT,          /* parent::* or .. */
    XPATH_FAST_PATH_PARENT_NAME,     /* parent:tagname */

    /* Self axis patterns */
    XPATH_FAST_PATH_SELF,            /* self::* or . */
    XPATH_FAST_PATH_SELF_NAME,       /* self:tagname or .tagname */

    /* Descendant patterns */
    XPATH_FAST_PATH_DESCENDANT_STAR, /* descendant::* or //* */
    XPATH_FAST_PATH_DESCENDANT_NAME, /* descendant::name or //name */

    /* Ancestor patterns */
    XPATH_FAST_PATH_ANCESTOR_STAR,   /* ancestor::* */
    XPATH_FAST_PATH_ANCESTOR_NAME,   /* ancestor::name */

    /* Simple name test (for root element lookup) */
    XPATH_FAST_PATH_ROOT_NAME,       /* /rootname */

    /* Count function on simple axis */
    XPATH_FAST_PATH_COUNT_CHILDREN,  /* count(*) or count(child::*) */

    /* String functions */
    XPATH_FAST_PATH_STRING_VALUE,    /* string() or string(.) */

    /* Boolean checks */
    XPATH_FAST_PATH_BOOLEAN_CHECK,   /* boolean(node) pattern */

    /* Multi-step patterns (common combinations) */
    XPATH_FAST_PATH_DESCENDANT_SELF,      /* //name/self::* or //name/. */
    XPATH_FAST_PATH_DESCENDANT_ATTR,      /* //name/@attr or //name/attribute::name */
    XPATH_FAST_PATH_DESCENDANT_CHILD,     /* //name/* or //name/child::* */
    XPATH_FAST_PATH_DESCENDANT_CHILD_NAME /* //name/child */
} XPathFastPathType;

/**
 * Parsed fast path pattern
 *
 * Contains extracted information for direct evaluation.
 */
typedef struct {
    XPathFastPathType type;
    const char* name;         /* Node name (for NAME patterns), NULL for STAR */
    size_t name_len;          /* Length of name */
    int is_absolute;          /* Starts with / */
    /* For multi-step patterns */
    const char* second_name;  /* Second step name (for multi-step patterns) */
    size_t second_name_len;   /* Length of second step name */
} XPathFastPathPattern;

/**
 * Detect fast path pattern from expression string
 *
 * @param expr Expression string
 * @param len Length of expression
 * @param pattern Output: parsed pattern info
 * @return 1 if fast path detected, 0 if not
 */
int xpath_fast_path_detect(const char* expr, size_t len, XPathFastPathPattern* pattern);

/**
 * Evaluate fast path pattern directly
 *
 * @param doc Document
 * @param context Context element (NULL for document root)
 * @param pattern Parsed pattern
 * @return Result or NULL if evaluation failed
 */
struct taurus_xpath_result* xpath_fast_path_eval(
    struct taurus_document* doc,
    TaurusElement context,
    const XPathFastPathPattern* pattern
);

/**
 * Combined detect-and-evaluate for convenience
 *
 * @param doc Document
 * @param context Context element (NULL for document root)
 * @param expr Expression string
 * @param len Length of expression
 * @return Result or NULL if not a fast path or evaluation failed
 */
struct taurus_xpath_result* xpath_fast_path_try(
    struct taurus_document* doc,
    TaurusElement context,
    const char* expr,
    size_t len
);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_XPATH_FAST_PATH_H */
