/* xsd/xsd_internal.h — XSD subsystem internals (#1075 tier 1).
 * Copyright (c) 2024, Ribose Inc. */
#ifndef LEPTRIS_XSD_INTERNAL_H
#define LEPTRIS_XSD_INTERNAL_H

#include <stddef.h>

#include "../../include/leptris/xquery/xquery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque here: compile.c owns the definition; prototypes below
 * take it by pointer only. */
struct leptris_xsd_schema;

/* One xs:enumeration / facet value, schema-owned. */
typedef struct xsd_facet_value {
    char* text;
    struct xsd_facet_value* next;
} XsdFacetValue;

/* XSD 1.1 (#2154): one xs:assertion facet bound to a simpleType —
 * the test plus its lazily-compiled query (§3.16.4). A restriction
 * may carry SEVERAL (the list accumulates), and the chain walker
 * enforces every level's list — assertion facets are inherited by
 * restriction (§3.16.6). Evaluation errors count as failure; an
 * expression the engine cannot COMPILE is not enforced (the facet
 * posture — never reject what we cannot assess). */
typedef struct xsd_assertion_facet {
    char* test;             /* the wire expression, for reporting */
    char* ns_prolog;        /* §3.13.2 {namespace bindings} +
                             * {xpathDefaultNamespace} as XQuery
                             * prolog declarations (capture-time) */
    LeptrisXQuery q;        /* compiled lazily at first use */
    int tried;              /* lazy-compile guard */
    struct xsd_assertion_facet* next;
} XsdAssertionFacet;

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
    char* ns;    /* leptris-ruby#440: declaring schema's targetNamespace */
    char* base;
    struct xsd_simple* base_def; /* RESTRICTION: inline anonymous base */
    XsdSimpleDeriv deriv;
    char* item_type;         /* LIST: builtin or local type name */
    struct xsd_simple* item_def; /* LIST: inline anonymous item type */
    char** members;          /* UNION: named member type names */
    size_t member_count;
    struct xsd_simple** member_defs; /* UNION: inline members */
    size_t member_def_count;
    XsdFacetValue* patterns; /* DISJUNCTION: any pattern may match */
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
    /* XSD 1.1 (#2155): the scale pair (decimal/precisionDecimal)
     * and the timezone facet (date/time family). -1/NULL = absent. */
    long min_scale;
    long max_scale;
    char* explicit_timezone; /* "required"|"optional"|"prohibited" */
    char* whitespace; /* "preserve" | "replace" | "collapse" */
    /* §4.3: the fixed flag per facet — a fixed base facet cannot be
     * re-specified with a different value down the ladder (#2155). */
    unsigned char fixed_min_inclusive, fixed_min_exclusive,
        fixed_max_inclusive, fixed_max_exclusive, fixed_total_digits,
        fixed_fraction_digits, fixed_length, fixed_min_length,
        fixed_max_length, fixed_min_scale, fixed_max_scale,
        fixed_whitespace, fixed_timezone;
    /* XSD 1.1 (#2154): the restriction's xs:assertion facet list —
     * accumulated, enforced across the chain by the validator. */
    XsdAssertionFacet* assertions;
    /* A single-occurrence facet repeated in one restriction (§4.3
     * XML representation; #2184 residue, simple060-084): the schema
     * fails — the capture no longer overwrites silently. */
    unsigned char dup_facet;
    struct xsd_simple* next;
} XsdSimple;

/* Instance-path entry to the chain walker: resolves list item
 * types and union member names through the schema. */
int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v);

/* XSD 1.1 (#1075 tier 2): one xs:assert bound to a complexType —
 * the compiled XPath 2 test plus its source text. Evaluation
 * errors count as assertion failure. */
typedef struct xsd_assert {
    char* test;             /* the wire expression, for reporting */
    char* ns_prolog;        /* §3.13.2 bindings as XQuery prolog */
    LeptrisXQuery compiled; /* NULL when compilation failed */
    struct xsd_assert* next;
} XsdAssert;

/* Negative-wildcard predicates (moxml#460): the schema struct is
 * compile.c-owned; the matchers see these two. */
int xsd_element_name_defined(struct leptris_xsd_schema* s,
                             const char* local, const char* uri);
int xsd_attr_name_defined(struct leptris_xsd_schema* s,
                          const char* local);
struct xsd_attr_decl;
const struct xsd_attr_decl* xsd_find_top_attr(
    struct leptris_xsd_schema* s, const char* local);

/* One validation finding (slice 4). text is malloc'd; kind and
 * component are BORROWED (static names / schema-owned component
 * names — both outlive the per-validate list). #1841 added the
 * structured fields beside the text. */
struct xsd_error {
    char* text;
    const char* kind;
    const char* component;
    int line;
    int column;
    struct xsd_error* next;
};

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

/* forward: defined below the content model — XsdCm particles carry
 * the alternative chain (#2149) */
typedef struct xsd_alternative XsdAlternative;

typedef struct xsd_cm {
    XsdCmKind kind;
    char* name;   /* CM_ELEMENT: child element local name */
    char* type;   /* CM_ELEMENT: the particle's type ref, if any */
    char* ns;     /* CM_ELEMENT: expected namespace (NULL = unqualified) */
    char* ext_base; /* extension whose base was not yet captured at
                     * capture time (forward ref) — folded by the
                     * post-capture resolver (leptris-ruby#440 A) */
    char* res_base; /* restriction whose base was not yet captured at
                     * capture time — the post-capture resolver only
                     * folds EXTENSION models; the restriction's model
                     * replaces the base's, so this rides along solely
                     * for strict-attribute ADMISSION (leptris#1923:
                     * libxml2 admits the restriction chain's declared
                     * attributes) */
    char* res_base_ns;
    char* ext_base_ns; /* #1881: the base QName's RESOLVED namespace
                        * (prefix resolved against the extension
                        * element's in-scope declarations) — a set
                        * compilation extends ACROSS units (g:foo1
                        * extends f:foo), so the fold resolves in
                        * the base's namespace, not the deriver's;
                        * NULL = the declaring unit's tns */
    char* any_ns; /* CM_ANY: the namespace attribute grammar */
    char* not_qname; /* XSD 1.1 xs:any/@notQName (moxml#460): space-
                      * separated ##defined / no-prefix literals;
                      * prefixed QNames need instance-ns resolution */
    /* #1735: the target namespace of the unit DECLARING the
     * wildcard — ##targetNamespace/##other are unit-relative and
     * the merged schema keeps only the main unit's tns */
    char* any_unit_tns;
    int process_skip; /* CM_ANY: processContents="skip" or "lax" */
    int pc_level; /* CM_ANY: 0 strict (default), 1 lax, 2 skip
                   * (#1879 — skip imposes no constraint on the
                   * matched element, even a declared one) */
    int min, max; /* occurrence bounds; max < 0 = unbounded */
    struct xsd_cm* first_child; /* group children list */
    struct xsd_cm* next;        /* sibling link within the parent group */
    char* text_type; /* simpleContent wrapper: the text's type ref */
    /* #2184 §3.2.2.3: a simpleContent EXTENSION's base simple type —
     * the ID bookkeeping's content classification reads it beside
     * text_type (whose synth-slot posture is pinned for the lexical
     * lane). NULL elsewhere. */
    char* text_ext_base;
    XsdAssert* asserts; /* XSD 1.1: the type's xs:assert list */
    struct xsd_cm* open_any; /* XSD 1.1: xs:openContent wildcard */
    int oc_mode;        /* 0 none/unset, 1 suffix, 2 interleave */
    int oc_owned;       /* the wildcard node is this model's own */
    /* #2151 (XSD 1.1 §3.4.2.3.3 clauses 5/6): how the open content
     * was established — 0 none declared, 1 declared with a wildcard
     * (mode suffix/interleave), 2 declared mode="none" (blocks the
     * schema default; an extension keeps the base's open content).
     * The unit id ties the type to the schema DOCUMENT that captured
     * it: a defaultOpenContent applies only to the types of its own
     * document (open040/open041/open042). */
    int oc_declared;
    int oc_unit_id;
    char* not_ns; /* XSD 1.1 xs:any/@notNamespace: whitespace-
                   * separated namespace exclusions (##targetNamespace
                   * / ##local resolve like @namespace lists) */
    /* The derivation base for the OPEN-CONTENT pass (post-capture):
     * an extension's explicit content type carries the base's open
     * content; the declared wildcard unions over it; the derivation-
     * ok ladder (cos-ct-extends.1.4.3.2.2.3/4) and the restriction
     * subset checks resolve the base here. Borrowed-name strings,
     * owned copies (freed with the node). */
    char* oc_ext_base;
    char* oc_ext_base_ns;
    int oc_unioned;   /* the base wildcard already joined */
    int oc_checked;   /* the derivation pass processed this type */
    int empty_content; /* #2151: explicit content EMPTY (variety
                        * empty, XSD 1.1 §3.4.2.3.3 cl. 4.1.1/3.1.2):
                        * no character content at all — not even
                        * whitespace (open012.n3) */
    int empty_upgraded; /* #2151: an open content UPGRADED the empty
                         * variety to element-only (cl. 6.2): the
                         * element-only text rule applies — non-
                         * whitespace character content is invalid
                         * (open010.n2), whitespace stays legal
                         * (open010.v2) */
    /* leptris-rs#35: particle value constraints + the type's mixed
     * flag. An empty element is satisfied BY its fixed/default value
     * (XSD 1.0 §3.12.2/§4.3.1.2 — isDefault005); mixed="true"
     * admits character data on a particle-less type (ctA033 and
     * idF025's inline mixed c). */
    char* fixed_value;
    char* default_value;
    int mixed;
    XsdAlternative* alternatives; /* XSD 1.1: the alternative chain on
                                   * a LOCAL element particle (#2149) */
    /* XSD 1.1 §3.13.6 (#2154): the derivation base the ASSERT chain
     * follows — the post-capture fold clears ext_base after
     * splicing the base's particles, but the derived type still
     * runs the base chain's asserts. Stolen pointers (freed here). */
    char* assert_base;
    char* assert_base_ns;
    /* #2182 §3.4.2.5: the complexType's defaultAttributesApply
     * tri-state (-1 absent = apply, 0 false, 1 true) and the unit
     * whose xs:schema/@defaultAttributes list applies to it — the
     * declaring document's own list only (a sub-document's types
     * ride their own document's list). -1 unit = none (override
     * bodies capture suppressed: §4.2.5 — the body rides the
     * OVERRIDDEN document's defaults). */
    int defattr_apply;
    int defattr_unit;
} XsdCm;



/* A top-level xs:group definition: name + the model its refs
 * splice in (the definition's single sequence/choice/all). */
typedef struct xsd_group_def {
    char* name;
    XsdCm* model;
    struct xsd_group_def* next;
} XsdGroupDef;

/* XSD 1.1 (xs:schema @version="1.1") gates 1.1 enforcement. */
int xsd_schema_version_11(struct leptris_xsd_schema* s);

/* #2184 (§3.4.4.3 dynamic EDC): is `derived` validly derived from
 * `base`? Builtin ladders, local simple chains, complex chains;
 * unresolved names stay posture (1). */
int xsd_type_derives_from(struct leptris_xsd_schema* s,
                          const char* derived, const char* base);

/* #2184: the author's explicit 1.0 pin (@version="1.0" /
 * vc:maxVersion="1.0") — 1.0-only rules stay armed for such a schema
 * whatever 1.1-only syntax it carries. */
int xsd_schema_pinned_10(struct leptris_xsd_schema* s);

/* #2183: the schema document's directory (file compile) — the
 * assertion lane anchors fn:doc's relative URIs here. */
const char* xsd_schema_base_dir(struct leptris_xsd_schema* s);

/* Group refs resolve through the schema at NFA-build time —
 * forward references need no capture order. */
XsdGroupDef* xsd_find_group(struct leptris_xsd_schema* s,
                            const char* name);

/* XSD 1.1 (#1075 tier 2): one xs:alternative on an element
 * declaration — the test plus the type it selects. test == NULL
 * marks the test-less DEFAULT alternative: the type table's
 * {default type definition} (§3.3.2.1), which closes the chain and
 * replaces the declared type as the no-match fallback. */
struct xsd_alternative {
    char* test;             /* wire text, for reporting */
    LeptrisXQuery compiled; /* NULL when compilation failed */
    /* #2184 (§3.13.2): the test's {namespace bindings} +
     * {xpathDefaultNamespace} as an XQuery prolog — prefixes bind
     * in the SCHEMA document (cta0002's @c2:min over the
     * instance's c: spelling), captured at the alternative
     * element like the assert/assertion lane. */
    char* ns_prolog;
    char* type;             /* the selected type name */
    int xs_error;           /* the selection is xs:error (no valid
                             * instances — always invalid, §2.2.1.1) */
    /* #2184 (XPST0080): the test's cast/castable target resolved to
     * a COMPLEX type of the schema — a static error (the cast target
     * must be a simple type); the gate rejects the schema. */
    char* cast_target;
    /* moxml#579: the attribute names the test references (@kind /
     * attribute::kind forms, string literals skipped) — the SELECTED
     * alternative's set is exempt from strict attribute assessment
     * (§3.12.4). Owned; freed with the alternative. */
    char** test_attrs;
    int test_attr_n;
    struct xsd_alternative* next;
};

/* A top-level xs:element declaration (slice 3: name + type ref). */
typedef struct xsd_element_decl {
    char* name;
    char* ns;       /* leptris-ruby#440: declaring schema's targetNamespace */
    char* type;     /* "xs:string"-style builtin or a local type name */
    XsdAlternative* alternatives; /* XSD 1.1 conditional types */
    char* fixed;    /* fixed element content: text must string-equal it */
    char* default_value; /* leptris-rs#35: supplies content when the
                              element is empty (stZ072) */
    char* sub_head; /* substitutionGroup head (local name after ':') */
    struct xsd_element_decl* next;
} XsdElementDecl;

/* Instance element `name` may bind a particle written for `head`
 * when its substitutionGroup chain (depth-capped) reaches head. */
int xsd_is_substitute(struct leptris_xsd_schema* s,
                      const char* name, const char* head);

/* elemZ020: the MEMBER declaration behind a substitute hit — the
 * member carries its OWN namespace (a member in ns g rides a head
 * particle written for ns f, XSD 1.1 cross-ns groups + schema-set
 * compilation), so ns gates must compare against the member's ns,
 * never the head particle's. */
const XsdElementDecl* xsd_substitute_member(
    struct leptris_xsd_schema* s, const char* name, const char* head);

typedef struct xsd_attr_decl {
    char* name;  /* attribute local name */
    char* type;  /* builtin ref or local simpleType name (NULL=anySimple) */
    char* fixed; /* #FIXED value: present values must string-equal it */
    /* #2184 §3.2.2.3: the row's {value constraint} default — applies
     * to absent attributes and joins the ID bookkeeping (id010's
     * defaulted para001, id016's defaulted idref). */
    char* def;
    int required;
    /* #2185: use="prohibited" — the row REMOVES the attribute from
     * the type's {attribute uses} (XSD 1.0 §3.4.2/§3.4.3): an
     * instance attribute it names is rejected, and the row is not a
     * match candidate for the derivation-ok comparison. */
    int prohibited;
    /* leptris-ruby#679: the row's {target namespace} (XSD 1.0
     * §3.2.3) — a qualified-form row (attributeFormDefault or the
     * declaration's own form) carries the declaring unit's
     * targetNamespace and binds only namespace-QUALIFIED instance
     * attributes; NULL names a NO-namespace attribute. Top-level
     * declarations are always qualified with their unit's tns. */
    char* ns;
    /* XSD 1.1 §3.3.5.6: the attribute value is inherited by
     * descendants for their CTA test contexts (#2149). -1 unset
     * (calloc-zero would conflate unset with an explicit false —
     * the use-override and the global-decl fallback both need the
     * distinction), 0 false, 1 true. */
    int inheritable;
    /* leptris-ruby#569: the row came from <xs:attribute ref=.../>
     * whose global declaration was not yet captured (the #1880
     * attributeGroup pre-pass runs before the import chase). The
     * row is kept — never silently dropped — and xsd_compile_impl
     * re-resolves it once the full merged capture has run
     * (xsd_resolve_pending_attr_refs). bit 0 = ref unresolved,
     * bit 1 = the ref carried its own use= (it overrides the
     * declaration's). */
    int pending_ref;
    /* #2182 §3.4.2.5: the row arrived as a copy of the DERIVATION
     * BASE's uses (the extension fold, at capture or in the
     * deferred resolver) — the default-attributes application
     * treats a same-(ns,name) collision with such a row as the
     * cos-st-extends merge (skip), while a collision with an
     * unmarked row is the type's own use and fails the schema
     * (c-props-correct.4 — the ibm si02/si03 shapes). */
    unsigned char from_base;
    struct xsd_attr_decl* next;
} XsdAttrDecl;

/* complexType name -> its attribute declarations (hash-free: a
 * parallel list keyed the same way as complex model roots). */
/* ---- slice 6: identity constraints -------------------------------- */

/* #2156: one normalized NameTest step of a §3.11.6.2/3 selector or
 * field path. The namespace is resolved at CAPTURE time against the
 * schema document's in-scope bindings + xpathDefaultNamespace
 * (§3.13.2) — instance namespaces never enter the picture. */
typedef struct xsd_ic_step {
    char* ns;    /* resolved namespace URI; NULL = no namespace */
    char* local; /* local name; NULL = wildcard local ('*' / 'p:*') */
    int any_ns;  /* the '*' form: matches any element namespace */
    int is_attr; /* '@' name test (field-final only, §3.11.6.3) */
    int is_self; /* the '.' step */
} XsdIcStep;

/* One path branch: steps + the leading './/' descendant flag. */
typedef struct xsd_ic_path {
    XsdIcStep steps[12];
    int step_count;
    int desc; /* leading './/' — branch from every descendant */
} XsdIcPath;

/* xs:key | xs:unique | xs:keyref captured on an element decl. */
typedef struct xsd_ic {
    char* name;    /* constraint name */
    int kind;      /* 0 = key, 1 = unique, 2 = keyref */
    char* selector; /* raw text, for reporting */
    XsdIcPath sel[4]; /* selector branches: Path ( '|' Path )* */
    int sel_count;
    char* fields[8]; /* raw field texts, for reporting */
    XsdIcPath fpaths[8]; /* normalized field paths */
    size_t field_count;
    char* refer;   /* keyref: referenced constraint's LOCAL name */
    char* ref_local; /* @ref form: the referenced constraint's local
                      * name, resolved post-capture (forward refs
                      * need the whole schema captured first) */
    int ref_pending; /* the constraint arrived via @ref: copy the
                      * target's shape once capture completes */
    int parse_error; /* selector/field outside the §3.11.6 subset or
                      * an unbound prefix — schema error (§3.13.2
                      * XPath Valid) */
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
    /* leptris#1957: the definition's own element — the ref
     * expansion RE-CAPTURES it lazily so an inner attributeGroup ref
     * naming a LATER-registered group resolves (document-order
     * capture used to leave level-2 members unexpanded). */
    LeptrisElement elem;
    struct xsd_attrgroup_def* next;
} XsdAttrGroupDef;

/* #2182 §3.4.2.5: one resolved QName of an xs:schema
 * @defaultAttributes list (document order). ns == NULL marks an
 * UNRESOLVABLE QName (a prefixed name whose prefix is not in
 * scope) — the post-capture check fails the schema; ns == "" is
 * the legitimate no-namespace form (an unprefixed QName in a
 * document with no default namespace / no targetNamespace). */
typedef struct xsd_defattr_ref {
    char* local;
    char* ns;
    struct xsd_defattr_ref* next;
} XsdDefAttrRef;

/* Per schema-DOCUMENT unit: the unit's own @defaultAttributes list.
 * Types stamp the unit they were captured in and look up exactly
 * that unit's list — an included document's types never ride the
 * includer's defaults (open040/open041 posture), while redefine
 * bodies stay in the including unit and do (open044). */
typedef struct xsd_unit_defattrs {
    int unit;
    XsdDefAttrRef* refs;
    struct xsd_unit_defattrs* next;
} XsdUnitDefAttrs;

/* #2184 §3.10.6.3: one attribute wildcard GATHERED into a
 * complexType from its attributeGroup refs (or a direct
 * xs:anyAttribute). A type gathering several wildcard-carrying
 * groups admits a name only when EVERY member admits (the
 * §3.10.6.3 intersection); across a DERIVATION the base and the
 * derived wildcard union instead. The gather folds the members
 * into one effective wildcard at capture, so this struct is the
 * accumulator only, not stored state. Canon forms:
 *   ns        concrete URIs / ##local / ##any (##targetNamespace
 *             resolved to the declaring unit's tns; ##other folded
 *             into ns=##any + not_ns {tns, ##local})
 *   not_ns    concrete URIs / ##local (##targetNamespace resolved)
 *   not_qname "{uri}local" canon / bare literals / ##defined
 *             (prefixed tokens resolved against the declaring
 *             element's in-scope bindings) */
typedef struct xsd_attr_wc {
    char* ns;        /* @namespace grammar; NULL = absent (##any) */
    char* unit_ns;   /* declaring unit's tns (##targetNamespace/##other) */
    char* not_ns;    /* @notNamespace exclusions */
    char* not_qname; /* canon @notQName exclusions */
    struct xsd_attr_wc* next;
} XsdAttrWc;

typedef struct xsd_type_attrs {
    char* type_name;
    char* ns;           /* leptris-ruby#440: declaring schema's tns */
    /* #2184: ONE effective wildcard per row — the canon grammar,
     * its exclusions, and the declaring unit's tns. Extension
     * unions the base's wildcard in at the fold; attributeGroup
     * refs intersect into the gather. */
    char* any_attr_ns; /* xs:anyAttribute grammar (canon form) */
    char* any_attr_not_qname; /* canon @notQName exclusions */
    char* any_attr_not_ns;    /* canon @notNamespace exclusions */
    char* any_attr_unit_ns;   /* declaring unit's tns (legacy) */
    XsdAttrDecl* attrs;
    struct xsd_type_attrs* next;
} XsdTypeAttrs;

#ifdef __cplusplus
}
#endif

/* ---- slice 4: attribute declarations + instance validation ------- */

/* Datatype facet gate (datatypes.c): is `v` a valid lexical value
 * for builtin type `type`? */
int xsd_builtin_valid(const char* type, const char* v);

/* Is `type` ("xs:"-spelled) in the tier-1 builtin table? The
 * unprefixed-reference fallback needs existence, not validity. */
int xsd_builtin_known(const char* type);

/* #2150: builtin EXISTENCE — xsd_builtin_known plus the names that
 * carry no lexical validator of their own (QName/NOTATION), the
 * list builtins, and the ur-types. */
int xsd_builtin_exists(const char* type);

/* #2155: the 1.1 facet table — terminal-builtin class, the
 * 1.1-only builtin marker, facet applicability, facet-value
 * lexical validity, and the ordered-facet VALUE-space comparison
 * (instants, durations, numerics; -2 = incomparable). datatypes.c
 * owns the knowledge; compile.c orchestrates the walk. */
int xsd_terminal_class(const char* type);
int xsd_builtin_is_11(const char* type);
int xsd_facet_applicable(const char* type, const char* facet,
                         int chain_is_list);
int xsd_facet_value_valid(const char* type, const char* facet,
                          const char* value, int chain_is_list);
int xsd_facet_value_compare(const char* type, const char* a,
                            const char* b);

#endif /* LEPTRIS_XSD_INTERNAL_H */
