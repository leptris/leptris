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

/* Content-model node: a particle (element ref / wildcard) or a
 * group (sequence | choice | all). Owned by the schema; freed
 * with the schema. */
typedef enum {
    XSD_CM_ELEMENT = 0,
    XSD_CM_ANY,
    XSD_CM_SEQ,
    XSD_CM_CHOICE,
    XSD_CM_ALL
} XsdCmKind;

typedef struct xsd_cm {
    XsdCmKind kind;
    char* name;   /* CM_ELEMENT: child element local name */
    char* type;   /* CM_ELEMENT: the particle's type ref, if any */
    char* ns;     /* CM_ELEMENT: expected namespace (NULL = unqualified) */
    char* any_ns; /* CM_ANY: the namespace attribute grammar */
    int process_skip; /* CM_ANY: processContents="skip" or "lax" */
    int min, max; /* occurrence bounds; max < 0 = unbounded */
    struct xsd_cm* first_child; /* group children list */
    struct xsd_cm* next;        /* sibling link within the parent group */
} XsdCm;

/* A top-level xs:element declaration (slice 3: name + type ref). */
typedef struct xsd_element_decl {
    char* name;
    char* type; /* "xs:string"-style builtin or a local type name */
    struct xsd_element_decl* next;
} XsdElementDecl;

typedef struct xsd_attr_decl {
    char* name;  /* attribute local name */
    char* type;  /* builtin ref or local simpleType name (NULL=anySimple) */
    int required;
    struct xsd_attr_decl* next;
} XsdAttrDecl;

/* complexType name -> its attribute declarations (hash-free: a
 * parallel list keyed the same way as complex model roots). */
typedef struct xsd_type_attrs {
    char* type_name;
    XsdAttrDecl* attrs;
    struct xsd_type_attrs* next;
} XsdTypeAttrs;

#ifdef __cplusplus
}
#endif

/* ---- slice 4: attribute declarations + instance validation ------- */

#endif /* LEPTRIS_XSD_INTERNAL_H */
