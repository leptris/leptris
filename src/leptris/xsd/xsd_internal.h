/* xsd/xsd_internal.h — XSD subsystem internals (#1075 tier 1).
 * Copyright (c) 2024, Ribose Inc. */
#ifndef LEPTRIS_XSD_INTERNAL_H
#define LEPTRIS_XSD_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One xs:enumeration / facet value, schema-owned. */
typedef struct xsd_facet_value {
    char* text;
    struct xsd_facet_value* next;
} XsdFacetValue;

/* A user simpleType captured at compile time: name, restriction
 * base (a built-in reference like "xs:integer" or another local
 * simpleType name), and the facets the restriction carries. */
typedef struct xsd_simple {
    char* name;
    char* base;
    char* pattern;
    XsdFacetValue* enum_values;
    size_t enum_count;
    char* min_inclusive;
    char* min_exclusive;
    char* max_inclusive;
    char* max_exclusive;
    long length;
    long min_length;
    long max_length; /* -1 = absent */
    long total_digits;
    long fraction_digits;
    char* whitespace; /* "preserve" | "replace" | "collapse" */
    struct xsd_simple* next;
} XsdSimple;

#ifdef __cplusplus
}
#endif
#endif /* LEPTRIS_XSD_INTERNAL_H */
