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

/* How a user simpleType is derived: restriction of a base (with
 * facets), a whitespace-separated list of an item type, or a
 * union of member types. */
typedef enum {
    XSD_SIMPLE_RESTRICT = 0,
    XSD_SIMPLE_LIST,
    XSD_SIMPLE_UNION
} XsdSimpleDeriv;

/* A user simpleType captured at compile time: name, restriction
 * base (a built-in reference like "xs:integer" or another local
 * simpleType name), and the facets the restriction carries.
 * List item types and union members arrive either as NAMES
 * (resolved through the schema at validation) or as inline
 * anonymous captures riding this type (freed with it). */
typedef struct xsd_simple {
    char* name;
    char* base;
    XsdSimpleDeriv deriv;
    char* item_type;         /* LIST: builtin or local type name */
    struct xsd_simple* item_def; /* LIST: inline anonymous item type */
    char** members;          /* UNION: named member type names */
    size_t member_count;
    struct xsd_simple** member_defs; /* UNION: inline members */
    size_t member_def_count;
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

/* Instance-path entry to the chain walker: resolves list item
 * types and union member names through the schema. */
int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v);

/* Content-model node: a particle (element ref / wildcard) or a
 * group (sequence | choice | all). Owned by the schema; freed
 * with the schema. */
typedef enum {
    XSD_CM_ELEMENT = 0,
    XSD_CM_ANY,
    XSD_CM_SEQ,
    XSD_CM_CHOICE,
    XSD_CM_ALL,
    XSD_CM_GROUP_REF /* name holds the ref; expanded at NFA build */
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
    char* text_type; /* simpleContent wrapper: the text's type ref */
} XsdCm;

/* A top-level xs:group definition: name + the model its refs
 * splice in (the definition's single sequence/choice/all). */
typedef struct xsd_group_def {
    char* name;
    XsdCm* model;
    struct xsd_group_def* next;
} XsdGroupDef;

/* Group refs resolve through the schema at NFA-build time —
 * forward references need no capture order. */
XsdGroupDef* xsd_find_group(struct leptris_xsd_schema* s,
                            const char* name);

/* A top-level xs:element declaration (slice 3: name + type ref). */
typedef struct xsd_element_decl {
    char* name;
    char* type;     /* "xs:string"-style builtin or a local type name */
    char* fixed;    /* fixed element content: text must string-equal it */
    char* sub_head; /* substitutionGroup head (local name after ':') */
    struct xsd_element_decl* next;
} XsdElementDecl;

/* Instance element `name` may bind a particle written for `head`
 * when its substitutionGroup chain (depth-capped) reaches head. */
int xsd_is_substitute(struct leptris_xsd_schema* s,
                      const char* name, const char* head);

typedef struct xsd_attr_decl {
    char* name;  /* attribute local name */
    char* type;  /* builtin ref or local simpleType name (NULL=anySimple) */
    char* fixed; /* #FIXED value: present values must string-equal it */
    int required;
    struct xsd_attr_decl* next;
} XsdAttrDecl;

/* complexType name -> its attribute declarations (hash-free: a
 * parallel list keyed the same way as complex model roots). */
/* ---- slice 6: identity constraints -------------------------------- */

/* xs:key | xs:unique | xs:keyref captured on an element decl. */
typedef struct xsd_ic {
    char* name;    /* constraint name */
    int kind;      /* 0 = key, 1 = unique, 2 = keyref */
    char* selector; /* XPath relative to the scoping element */
    char* fields[8]; /* XPaths relative to each selected node */
    size_t field_count;
    char* refer;   /* keyref: the referenced constraint's name */
    struct xsd_ic* next;
} XsdIc;

/* Per-declaration constraint list: entries keyed by element name. */
typedef struct xsd_element_ics {
    char* element_name;
    XsdIc* constraints;
    struct xsd_element_ics* next;
} XsdElementIcs;

/* A top-level xs:attributeGroup definition: name + its attribute
 * rows (deep-copied into each referencing type at capture). */
typedef struct xsd_attrgroup_def {
    char* name;
    XsdAttrDecl* attrs;
    struct xsd_attrgroup_def* next;
} XsdAttrGroupDef;

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
