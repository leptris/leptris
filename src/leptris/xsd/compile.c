/* lib/src/leptris/xsd/compile.c — #1075 XSD tier 1, slice 1: the
 * compilation surface. Parses schema text through the standard XML
 * parser and models the top-level declaration set. Later slices
 * grow the model (datatypes, facets, content models, identity
 * constraints) — this slice's contract is the API shape, the
 * xs:schema identity check, and the declaration enumeration. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "../common/port.h" /* strndup shim for MSVC's CRT */
#include "xsd_internal.h"
#include "xsd_internal.h"
#include "../dom/element.h" /* attribute walk for the clause checks */

#define XSD_NS "http://www.w3.org/2001/XMLSchema"

void xsd_capture_ics(struct leptris_xsd_schema* s,
                     LeptrisElement elem, const char* element_name);
extern int xsd_simple_valid(const XsdSimple* simple, const char* v,
                            const char* terminal_base, int chain_is_list);
extern struct xsd_validator* xsd_validator_new(
    struct leptris_xsd_schema* s);
extern void xsd_validator_free(struct xsd_validator* v);
extern int xsd_validator_run(struct xsd_validator* v, LeptrisDocument doc);
extern size_t xsd_validator_error_count(struct xsd_validator* v);
extern const struct xsd_error* xsd_validator_error_entry(
    struct xsd_validator* v, size_t i);
extern const char* xsd_validator_error_at(struct xsd_validator* v,
                                          size_t i);

extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count,
                             int* oc_from);

struct leptris_xsd_schema {
    size_t declaration_count;
    XsdSimple* simple_types;
    XsdElementDecl* elements;
    XsdCm* complex_types; /* name-carrying model roots */
    XsdGroupDef* groups;  /* top-level xs:group definitions */
    XsdCm* default_open;  /* xs:defaultOpenContent wildcard */
    int default_oc_empty; /* appliesToEmpty="true" */
    /* #2151: sub-document defaults are captured during include
     * processing and must outlive the capture (types borrow the
     * wildcard) — discarded ones chain here and free with the
     * schema (the leaks --atExit gate). */
    XsdCm* oc_default_pool;
    /* #2151: the default's declared mode (0 unset, 1 suffix,
     * 2 interleave — the S4SD default) and the unit it belongs to:
     * a defaultOpenContent applies only to the types of its own
     * schema document (open040/open041/open042). */
    int default_oc_mode;
    int default_oc_unit_id;
    /* #2151: schema DOCUMENT units — one id per compiled document
     * (main + includes/imports). Type capture stamps the current
     * unit; the defaultOpenContent application compares ids. */
    char** units;
    size_t unit_count, unit_cap;
    int current_unit;
    /* #2151: xs:ID values seen on openContent elements (open038). */
    char** oc_ids;
    size_t oc_id_count, oc_id_cap;
    /* #2151: the schema carries xs:openContent / xs:defaultOpenContent
     * elements — 1.1 syntax evidence for schemas unmarked or marked
     * 1.0 (the saxonData/Open corpus carries no version marker). The
     * openContent capture keys on version_11 || saw_open_content,
     * the same posture as saw_alternative (#2149) / saw_assert. */
    int saw_open_content;
    /* #2151: the open-content pass (capture checks + the derivation
     * pass) rejected the schema. A dedicated flag — s->error is the
     * parked-error channel other capture paths write without failing
     * the compile, and flipping those would reject schemas the gate
     * pins as clean. */
    int oc_failed;
    /* #2184: the alternative-table capture rejected the schema —
     * src-element.5 (a test-less xs:alternative mid-table; the
     * capture truncates the chain there, so only the element walk can
     * see it). Surfaced post-capture like oc_failed. */
    int ct_failed;
    /* #2184: the redefine/override location graph closed back on
     * itself (§4.2.2 — the IBM cyclic-redefine family; a target
     * equal to the CONTAINING document is the legal self-reference
     * hook, a target closing an ancestor chain is the error).
     * Flagged at chase time, surfaced post-capture. */
    int ro_cyclic;
    /* #2184: the chain of redefine/override target documents being
     * chased (canonical paths, strdup'd) — the ancestor set the
     * cycle check matches against. */
    char** ro_stack;
    size_t ro_len, ro_cap;
    /* #2184: the wildcard negative-set capture rejected the schema —
     * namespace + notNamespace co-occurrence (src-wildcard), a
     * notQName token that is malformed or binds an undeclared
     * prefix, and a notQName exclusion whose namespace the wildcard
     * can never admit (vacuous, wild031-039). Surfaced post-capture
     * like oc_failed / ct_failed. */
    int wc_failed;
    XsdAttrGroupDef* attr_groups;
    XsdAttrDecl* top_attrs; /* top-level xs:attribute declarations */
    XsdTypeAttrs* type_attrs;
    /* #2182 §3.4.2.5: xs:schema/@defaultAttributes — one list per
     * schema document unit, applied to that unit's complexTypes
     * post-capture (xsd_apply_default_attrs). */
    XsdUnitDefAttrs* unit_defattrs;
    /* #2182: while an OVERRIDE body captures, its types ride the
     * OVERRIDDEN document's defaults (§4.2.5) — captured with the
     * application suppressed (open045). */
    int defattr_suppressed;
    XsdElementIcs* element_ics;
    char* target_ns;
    int version_11;    /* xs:schema @version = "1.1" */
    /* #2149: the schema carries xs:alternative chains — 1.1 syntax
     * that appears on schemas marked version="1.0" (23 of the 24 cta
     * schemas). Asserts inside such a schema belong to the CTA
     * semantics (the selected type's asserts MUST evaluate), so the
     * assert capture keys on version_11 || saw_alternative. */
    int saw_alternative;
    /* #2154: the schema carries xs:assert / xs:assertion elements —
     * the assert-keyed-on-syntax-presence posture extended to every
     * assertion form: schemas marked 1.0 (or unmarked) whose authors
     * WROTE asserts must have those asserts evaluate, or the facet
     * is decorative. */
    int saw_assert;
    char* base_dir;    /* schemaLocation resolution (file compile) */
    int include_depth;
    /* xs:override body capture replaces same-named components: the
     * attributeGroup pre-pass must re-register defs the base
     * already captured (leptris#1913). */
    int capture_force; /* include/import recursion guard */
    /* #2150 §4.3: while an override BODY captures, the (kind, name)
     * pairs of the overridden document's merged component set — body
     * components outside the set override nothing and are IGNORED
     * (§4.3.2): they are dropped from the merged schema. */
    struct xsd_ovr_name* override_set;
    size_t override_set_len, override_set_cap;
    int override_active;
    /* #2150: how many override bodies this schema captured — the
     * post-capture derivation-cycle check is OVERRIDE-scoped (the
     * redefine hook legitimately re-derives its own name from the
     * original, so a base==own-name derivation is only circular
     * under override's post-merge re-resolution). */
    int override_bodies;
    /* #2150 §4.3.2: the compiled file's normalized path — the
     * mutual-override conflict check matches an inner override's
     * target against it. NULL for string-compiled schemas (no file
     * identity). */
    char* self_path;
    /* leptris#1644: already-captured schema units (canonical
     * location per entry) — the include/import DAG is dense and
     * cyclic; without this set every DAG path re-parses and
     * re-captures the whole subtree (multi-GB / minutes). */
    char** visited;
    size_t visited_len, visited_cap;
    /* leptris#1957: captured sub-documents stay ALIVE until schema
     * teardown — attributeGroup defs hold LeptrisElement pointers
     * into their own unit for the lazy ref expansion, and freeing
     * the sub-document at chase time left those dangling (ASAN
     * heap-use-after-free in the corpus sweep). */
    struct xsd_sub_doc* sub_docs;
    void* validator; /* slice-4 parked error list */
    char* error;
    /* #1841: materialized structured report — rebuilt per
     * leptris_xsd_error_report call, freed with the schema.
     * void* to avoid a public-header dependency here. */
    void* report;
    size_t report_count;
};

/* ---- slice 3: content-model capture ------------------------------ */

static char* xsd_strdup(const char* s);
/* #2184: the entry document joins the visited set at setup — the
 * redefine/override chase must not re-enter it. */
static void xsd_visited_add(struct leptris_xsd_schema* s,
                            const char* path);

/* #1592: capture an INLINE anonymous xs:complexType / xs:simpleType
 * under a synthesized name ("element:NAME" — ':' cannot appear in
 * xs: NCName type names, so the slot is collision-free). Returns 1
 * when a capture happened. Forward decls below; definitions later
 * in this file. */
static int xsd_capture_inline(struct leptris_xsd_schema* s,
                              LeptrisElement elem, const char* synth);
static XsdCm* xsd_cm_new(XsdCmKind kind);
static void xsd_cm_occurrence(XsdCm* cm, LeptrisElement e);
static XsdCm* xsd_cm_parse(struct leptris_xsd_schema* s,
                           LeptrisElement group, XsdCm** tail_out);
static XsdAttrDecl* xsd_capture_attr_rows(struct leptris_xsd_schema* s,
                                          LeptrisElement container,
                                          int depth);
static XsdAttrDecl* xsd_capture_attr_rows_ex(struct leptris_xsd_schema* s,
                                             LeptrisElement container,
                                             int depth,
                                             const XsdAttrGroupDef* owner);
XsdCm* xsd_find_complex_ns(struct leptris_xsd_schema* s,
                           const char* type_name, const char* uri);
extern XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,
                               const char* type_name);
extern const XsdTypeAttrs* xsd_find_type_attrs_ns(
    struct leptris_xsd_schema* s, const char* type_name,
    const char* uri);
extern const XsdTypeAttrs* xsd_find_type_attrs(
    struct leptris_xsd_schema* s, const char* type_name);
static XsdCm* xsd_cm_copy_list(const XsdCm* head);
static void xsd_resolve_extension_bases(struct leptris_xsd_schema* s);
static int xsd_oc_finalize(struct leptris_xsd_schema* s);
static void xsd_capture_root(struct leptris_xsd_schema* s,
                             LeptrisElement root);
static char* xsd_read_file(const char* path, size_t* out_len);
static XsdAttrDecl* xsd_copy_attr_rows(const XsdAttrDecl* src,
                                       int from_base);
static void xsd_resolve_pending_attr_refs(struct leptris_xsd_schema* s);
static int xsd_apply_default_attrs(struct leptris_xsd_schema* s);
static int xsd_resolve_ic_refs(struct leptris_xsd_schema* s);
static void xsd_register_complex(struct leptris_xsd_schema* s,
                                 XsdCm* root, XsdAttrDecl* attrs,
                                 const char* name, char* any_attr_ns,
                                 char* any_attr_not_qname,
                                 char* any_attr_not_ns,
                                 char* any_attr_unit_ns);
static XsdCm* xsd_capture_complex_model(struct leptris_xsd_schema* s,
                                        LeptrisElement ct,
                                        const char* name);
static XsdAssert* xsd_capture_assert(struct leptris_xsd_schema* s,
                                     LeptrisElement e);
static XsdCm* xsd_capture_any(struct leptris_xsd_schema* s,
                              LeptrisElement any_child);
static char* xsd_capture_unit_tns(LeptrisElement e);
static char* xsd_capture_ns_prolog(LeptrisElement e, const char* base_dir);
static void xsd_resolve_qname(LeptrisElement e, const char* qname,
                              char* uri, size_t urisz, char* local,
                              size_t localsz);
static int xsd_capture_attr_wc(struct leptris_xsd_schema* s,
                               LeptrisElement container,
                               XsdAttrWc* eff);
static void xsd_wc_fold_union(XsdTypeAttrs* mine,
                              const XsdTypeAttrs* base);
static void xsd_wc_fold_union_locals(char** ns, char** nq, char** nns,
                                     char** unit,
                                     const XsdAttrWc* b);
static void xsd_wc_expand_defined_sibling_pass(
    struct leptris_xsd_schema* s);
static int xsd_check_attr_wildcards(struct leptris_xsd_schema* s);
/* #2184 wildcard canon helpers (defined past the capture driver) */
static void xsd_wc_canon_ns(const char* raw, const char* tns,
                            char** out_ns, char** out_other);
static char* xsd_wc_canon_not_ns(const char* raw, const char* tns);
static char* xsd_wc_canon_not_qname(struct leptris_xsd_schema* s,
                                    LeptrisElement decl,
                                    const char* raw);
static int xsd_wc_check(struct leptris_xsd_schema* s,
                        const char* raw_ns, const char* raw_not_ns,
                        const char* canon_ns, const char* canon_not_ns,
                        const char* canon_nq);
static void xsd_wc_append(char** dst, const char* tok, size_t len);
static void xsd_path_normalize(const char* in, char* out, size_t outsz);

/* #2150 §4.3: the override lane. */
struct xsd_ovr_name {
    char kind[16];
    char name[128];
};
static int xsd_check_override_docs(struct leptris_xsd_schema* s,
                                   LeptrisElement root);
static int xsd_check_override_refs(struct leptris_xsd_schema* s,
                                   LeptrisElement root);
static int xsd_check_derivation_cycles(struct leptris_xsd_schema* s);
static int xsd_attr_rows_same_name(const XsdAttrDecl* a,
                                   const XsdAttrDecl* b);
/* validate.c's wildcard namespace-set matcher (##any/##other/
 * ##targetNamespace/##local/URI lists) — shared with the compile-side
 * derivation-ok attribute check (#2185: a derived use with no
 * matching base use is legal when the base's attribute wildcard
 * admits its namespace). */
extern int xsd_any_attr_matches(const char* grammar, const char* uri,
                                const char* target_ns);
extern const char* xsd_target_ns(struct leptris_xsd_schema* s);
static int xsd_check_derivation_ok_attrs(struct leptris_xsd_schema* s);

/* #1626: prefixed QName refs (type="w:CT_Border") resolve by local
 * name — the engine's tables key on local names; the prefix maps
 * to a namespace the name-based resolution folds away. Returns the
 * local part after the colon when present, else the argument. */
static char xsd_qname_buf[8][512];
static int xsd_qname_slot;

static const char* xsd_local_name(const char* qname) {
    if (!qname) return NULL;
    const char* colon = strchr(qname, ':');
    if (!colon) return qname;
    /* xs:/xsd: are builtin references — canonize to "xs:local" so
     * text_valid's dispatch matches both spellings (xsd:-spelled
     * types were silently untyped before #1626). */
    size_t pl = (size_t)(colon - qname);
    if ((pl == 2 && strncmp(qname, "xs", 2) == 0) ||
        (pl == 3 && strncmp(qname, "xsd", 3) == 0)) {
        if (pl == 2) return qname;
        char* dst = xsd_qname_buf[xsd_qname_slot++ & 7];
        dst[0] = 'x';
        dst[1] = 's';
        dst[2] = ':';
        snprintf(dst + 3, sizeof(xsd_qname_buf[0]) - 3, "%s",
                 colon + 1);
        return dst;
    }
    /* a foreign prefix maps to a namespace; the name-based tables
     * resolve by local name */
    return colon + 1;
}
static XsdAlternative* xsd_capture_alternatives(
    struct leptris_xsd_schema* s, LeptrisElement decl_elem);
static int xsd_bool_attr(LeptrisElement e, const char* name, int dflt);
static int xsd_doc_has_alternative(LeptrisElement e, int depth);
static int xsd_doc_has_open_content(LeptrisElement e, int depth);
static int xsd_doc_has_assert(LeptrisElement e, int depth);
static XsdSimple* xsd_capture_simple(LeptrisElement st);


static XsdCm* xsd_cm_new(XsdCmKind kind) {
    XsdCm* cm = (XsdCm*)calloc(1, sizeof(*cm));
    if (!cm) return NULL;
    cm->kind = kind;
    cm->min = 1;
    cm->max = 1;
    return cm;
}

static void xsd_cm_free(XsdCm* cm) {
    while (cm) {
        XsdCm* next = cm->next;
        XsdAssert* a = cm->asserts;
        while (a) {
            XsdAssert* an = a->next;
            if (a->compiled) leptris_xquery_free(a->compiled);
            free(a->test);
            free(a->ns_prolog);
            free(a);
            a = an;
        }
        if (cm->oc_owned && cm->open_any) xsd_cm_free(cm->open_any);
        xsd_cm_free(cm->first_child);
        XsdAlternative* alt = cm->alternatives;
        while (alt) {
            XsdAlternative* an = alt->next;
            if (alt->compiled) leptris_xquery_free(alt->compiled);
            free(alt->test);
            free(alt->type);
            for (int i = 0; i < alt->test_attr_n; i++)
                free(alt->test_attrs[i]);
            free(alt->test_attrs);
            free(alt);
            alt = an;
        }
        free(cm->name);
        free(cm->type);
        free(cm->ns);
        free(cm->res_base);
    free(cm->res_base_ns);
    free(cm->ext_base);
        free(cm->ext_base_ns);
        free(cm->assert_base);
        free(cm->assert_base_ns);
        free(cm->any_ns);
        free(cm->not_qname);
        free(cm->not_ns);
        free(cm->any_unit_tns);
        free(cm->oc_ext_base);
        free(cm->oc_ext_base_ns);
        free(cm->text_type);
        free(cm->fixed_value);
        free(cm->default_value);
        free(cm);
        cm = next;
    }
}

static void xsd_cm_occurrence(XsdCm* cm, LeptrisElement e) {
    const char* mn = leptris_element_attribute(e, "minOccurs");
    if (mn) cm->min = (strcmp(mn, "unbounded") == 0) ? 0 : atoi(mn);
    const char* mx = leptris_element_attribute(e, "maxOccurs");
    if (mx) cm->max = (strcmp(mx, "unbounded") == 0) ? -1 : atoi(mx);
}

/* Recursive model build over sequence | choice | all | element |
 * any children. Returns the child list head; *tail_out receives
 * the list tail for sibling chaining. */
/* #1592: capture an INLINE anonymous xs:complexType / xs:simpleType
 * under a synthesized name ("element:NAME" — ':' cannot appear in
 * xs: NCName type names, so the slot is collision-free). Returns 1
 * when a capture happened. */
static int xsd_capture_inline(struct leptris_xsd_schema* s,
                              LeptrisElement elem, const char* synth) {
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(elem));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
        if (strcmp(local, "complexType") == 0) {
            return xsd_capture_complex_model(s, e, synth) ? 1 : 0;
        }
        if (strcmp(local, "simpleType") == 0) {
            XsdSimple* captured = xsd_capture_simple(e);
            if (!captured) return 0;
            free(captured->name);
            captured->name = xsd_strdup(synth);
            if (s->target_ns) captured->ns = xsd_strdup(s->target_ns);
            captured->next = s->simple_types;
            s->simple_types = captured;
            return 1;
        }
    }
    return 0;
}

static XsdCm* xsd_cm_parse(struct leptris_xsd_schema* s,
                           LeptrisElement group, XsdCm** tail_out) {
    XsdCm* head = NULL;
    XsdCm* tail = NULL;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(group));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || (uri && strcmp(uri, XSD_NS) != 0)) continue;

        XsdCm* cm = NULL;
        if (strcmp(local, "sequence") == 0 || strcmp(local, "choice") == 0 ||
            strcmp(local, "all") == 0) {
            cm = xsd_cm_new(strcmp(local, "sequence") == 0 ? XSD_CM_SEQ
                            : strcmp(local, "choice") == 0 ? XSD_CM_CHOICE
                                                           : XSD_CM_ALL);
            if (cm) cm->first_child = xsd_cm_parse(s, e, NULL);
        } else if (strcmp(local, "element") == 0) {
            cm = xsd_cm_new(XSD_CM_ELEMENT);
            if (cm) {
                const char* ref_raw =
                    leptris_element_attribute(e, "ref");
                const char* nm = NULL;
                if (ref_raw) {
                    /* elemZ020: record the particle's namespace — a
                     * ref binds in the REFERENCED DECLARATION's ns
                     * (same-tns ref → tns, cross-ns → the foreign
                     * ns). The old capture kept the local name only,
                     * so the NFA and the typing DFS matched by name
                     * alone. */
                    nm = xsd_local_name(ref_raw);
                    char ruri[512], rlocal[256];
                    xsd_resolve_qname(e, ref_raw, ruri, sizeof(ruri),
                                      rlocal, sizeof(rlocal));
                    if (ruri[0])
                        cm->ns = xsd_strdup(ruri);
                    else
                        cm->ns = xsd_capture_unit_tns(e);
                } else {
                    nm = leptris_element_attribute(e, "name");
                    /* name-form: @form wins; otherwise the unit's
                     * elementFormDefault decides */
                    const char* form =
                        leptris_element_attribute(e, "form");
                    const char* efd = NULL;
                    if (!form) {
                        for (LeptrisElement up =
                                 leptris_element_parent(e);
                             up; up = leptris_element_parent(up)) {
                            const char* ul = NULL;
                            leptris_element_expanded_name(
                                up, &ul, NULL, NULL);
                            if (ul && strcmp(ul, "schema") == 0) {
                                efd = leptris_element_attribute(
                                    up, "elementFormDefault");
                                break;
                            }
                        }
                    }
                    int qualified =
                        form ? strcmp(form, "qualified") == 0
                             : (efd &&
                                strcmp(efd, "qualified") == 0);
                    if (qualified) {
                        char* tns = xsd_capture_unit_tns(e);
                        if (tns && tns[0])
                            cm->ns = tns;
                        else
                            free(tns);
                    }
                }
                cm->name = xsd_strdup(nm);
                const char* pty =
                    leptris_element_attribute(e, "type");
                if (pty) {
                    cm->type = xsd_strdup(xsd_local_name(pty));
                } else if (s && nm) {
                    /* #1592: inline anonymous type on the particle */
                    char synth[512];
                    snprintf(synth, sizeof(synth), "element:%s", nm);
                    if (xsd_capture_inline(s, e, synth))
                        cm->type = xsd_strdup(synth);
                }
                /* leptris-rs#35 (isDefault005): particle value
                 * constraints — an empty child is satisfied BY its
                 * fixed/default value (§3.12.2). */
                cm->fixed_value = xsd_strdup(
                    leptris_element_attribute(e, "fixed"));
                cm->default_value = xsd_strdup(
                    leptris_element_attribute(e, "default"));
                /* #2149: a name-form particle may carry the 1.1
                 * alternative chain (§3.3.2.3 — the local declaration
                 * maps to a full Element Declaration, type table
                 * included). A ref-form particle resolves to a global
                 * declaration, which carries its own table. */
                if (!ref_raw)
                    cm->alternatives =
                        xsd_capture_alternatives(s, e);
            }
        } else if (strcmp(local, "group") == 0) {
            /* <xs:group ref="g"/> — expanded at NFA build so
             * forward references need no capture order */
            cm = xsd_cm_new(XSD_CM_GROUP_REF);
            if (cm)
                cm->name = xsd_strdup(xsd_local_name(
                    leptris_element_attribute(e, "ref")));
        } else if (strcmp(local, "any") == 0) {
            cm = xsd_cm_new(XSD_CM_ANY);
            if (cm) {
                cm->any_ns = xsd_strdup(
                    leptris_element_attribute(e, "namespace"));
                /* #2184: prefixed notQName tokens canonize at
                 * capture ({uri}local) — malformed spellings and
                 * undeclared prefixes fail the schema (wild036-039)
                 * and the matcher's canon arm bites (wild028) */
                cm->not_qname = xsd_wc_canon_not_qname(
                    s, e, leptris_element_attribute(e, "notQName"));
                cm->not_ns = xsd_strdup(
                    leptris_element_attribute(e, "notNamespace"));
                cm->any_unit_tns = xsd_capture_unit_tns(e);
                /* #2184 src-wildcard sweep on the element wildcard:
                 * co-occurrence + vacuous exclusions (wild031/037,
                 * wild007's xs:any arm) — canon views, raw fields */
                {
                    char* cns = NULL;
                    char* cother = NULL;
                    xsd_wc_canon_ns(cm->any_ns, cm->any_unit_tns,
                                    &cns, &cother);
                    char* cnns = xsd_wc_canon_not_ns(
                        cm->not_ns, cm->any_unit_tns);
                    if (cother) {
                        xsd_wc_append(&cnns, cother, strlen(cother));
                        free(cother);
                    }
                    xsd_wc_check(s, cm->any_ns, cm->not_ns, cns,
                                 cnns, cm->not_qname);
                    free(cns);
                    free(cnns);
                }
                const char* pc = leptris_element_attribute(
                    e, "processContents");
                cm->process_skip =
                    (pc && (strcmp(pc, "skip") == 0 ||
                            strcmp(pc, "lax") == 0)) ? 1 : 0;
                cm->pc_level =
                    (pc && strcmp(pc, "skip") == 0) ? 2
                    : (pc && strcmp(pc, "lax") == 0) ? 1 : 0;
            }
        } else {
            continue; /* annotation & friends */
        }
        if (!cm) continue;
        xsd_cm_occurrence(cm, e);
        if (tail)
            tail->next = cm;
        else
            head = cm;
        tail = cm;
    }
    if (tail_out) *tail_out = tail;
    return head;
}

static char* xsd_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

/* Capture one xs:simpleType's restriction model (slice 2). */
static XsdSimple* xsd_capture_simple(LeptrisElement st) {
    const char* name = leptris_element_attribute(st, "name");
    /* Anonymous types ride their element — allowed: the caller
     * renames the captured entry into its synthesized slot
     * (#1592). */

    XsdSimple* simple = (XsdSimple*)calloc(1, sizeof(*simple));
    if (!simple) return NULL;
    simple->name = xsd_strdup(name);
    simple->length = simple->min_length = simple->max_length = -1;
    simple->total_digits = simple->fraction_digits = -1;
    simple->min_scale = simple->max_scale = -1;

    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(st));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (uri && strcmp(uri, XSD_NS) != 0) continue;
        if (!local) continue;
        if (strcmp(local, "list") == 0) {
            simple->deriv = XSD_SIMPLE_LIST;
            const char* it = xsd_local_name(
                leptris_element_attribute(e, "itemType"));
            if (it && *it) {
                simple->item_type = xsd_strdup(it);
            } else {
                /* inline anonymous item type rides this capture */
                for (LeptrisNodeRef q = leptris_node_first_child(
                         leptris_element_as_node(e));
                     q && !simple->item_def;
                     q = leptris_node_next_sibling(q)) {
                    if (leptris_node_get_type(q) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement qe = (LeptrisElement)q;
                    const char* ql = NULL;
                    leptris_element_expanded_name(qe, &ql, NULL, NULL);
                    if (ql && strcmp(ql, "simpleType") == 0)
                        simple->item_def = xsd_capture_simple(qe);
                }
            }
        } else if (strcmp(local, "union") == 0) {
            simple->deriv = XSD_SIMPLE_UNION;
            const char* mt =
                leptris_element_attribute(e, "memberTypes");
            if (mt && *mt) {
                size_t n = 0;
                for (const char* p = mt; *p;) {
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (!*p) break;
                    while (*p && !isspace((unsigned char)*p)) p++;
                    n++;
                }
                if (n) {
                    simple->members = (char**)calloc(n, sizeof(char*));
                    if (simple->members) {
                        size_t i = 0;
                        const char* p = mt;
                        while (*p && i < n) {
                            while (*p && isspace((unsigned char)*p)) p++;
                            if (!*p) break;
                            const char* start = p;
                            while (*p && !isspace((unsigned char)*p)) p++;
                            size_t len = (size_t)(p - start);
                            char* tok = (char*)malloc(len + 1);
                            if (!tok) break;
                            memcpy(tok, start, len);
                            tok[len] = 0;
                            simple->members[i++] = tok;
                        }
                        simple->member_count = i;
                    }
                }
            }
            /* inline anonymous members ride this capture */
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (!ql || strcmp(ql, "simpleType") != 0) continue;
                XsdSimple* def = xsd_capture_simple(qe);
                if (!def) continue;
                XsdSimple** grown = (XsdSimple**)realloc(
                    simple->member_defs,
                    (simple->member_def_count + 1) *
                        sizeof(XsdSimple*));
                if (!grown) break;
                simple->member_defs = grown;
                simple->member_defs[simple->member_def_count++] = def;
            }
        } else if (strcmp(local, "restriction") == 0) {
            simple->base = xsd_strdup(xsd_local_name(
                leptris_element_attribute(e, "base")));
            /* #1936 (stG/stH): an inline anonymous BASE rides this
             * capture — without it the restriction's facets apply
             * with no base (length counted characters, list
             * semantics vanished). Mirrors the union member_defs. */
            for (LeptrisNodeRef b = leptris_node_first_child(
                     leptris_element_as_node(e));
                 b && !simple->base_def;
                 b = leptris_node_next_sibling(b)) {
                if (leptris_node_get_type(b) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement be = (LeptrisElement)b;
                const char* bl = NULL;
                leptris_element_expanded_name(be, &bl, NULL, NULL);
                if (bl && strcmp(bl, "simpleType") == 0)
                    simple->base_def = xsd_capture_simple(be);
            }
            for (LeptrisNodeRef f = leptris_node_first_child(
                     leptris_element_as_node(e));
                 f; f = leptris_node_next_sibling(f)) {
                if (leptris_node_get_type(f) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement fe = (LeptrisElement)f;
                const char* fl = NULL, *fp = NULL, *fu = NULL;
                leptris_element_expanded_name(fe, &fl, &fp, &fu);
                if (!fl || (fu && strcmp(fu, XSD_NS) != 0)) continue;
                const char* val =
                    leptris_element_attribute(fe, "value");
                /* §4.3: the facet's fixed flag — a fixed base
                 * facet cannot be re-specified with a different
                 * value down the ladder (#2155). */
                const char* fxv =
                    leptris_element_attribute(fe, "fixed");
                int is_fixed = fxv && (strcmp(fxv, "true") == 0 ||
                                       strcmp(fxv, "1") == 0);
                if (strcmp(fl, "pattern") == 0) {
                    XsdFacetValue* fv =
                        (XsdFacetValue*)calloc(1, sizeof(*fv));
                    if (fv) {
                        fv->text = xsd_strdup(val);
                        fv->next = simple->patterns;
                        simple->patterns = fv;
                    }
                } else if (strcmp(fl, "enumeration") == 0) {
                    XsdFacetValue* fv =
                        (XsdFacetValue*)calloc(1, sizeof(*fv));
                    if (fv) {
                        fv->text = xsd_strdup(val);
                        fv->next = simple->enum_values;
                        simple->enum_values = fv;
                        simple->enum_count++;
                    }
                } else if (strcmp(fl, "minInclusive") == 0) {
                    /* #2148: the suite ships schemas with the same
                     * facet repeated in one restriction — overwrite
                     * must free the previous capture or it leaks
                     * (LSan on the xsdtests 1.1 lane). */
                    free(simple->min_inclusive);
                    simple->min_inclusive = xsd_strdup(val);
                    simple->fixed_min_inclusive = (unsigned char)is_fixed;
                } else if (strcmp(fl, "minExclusive") == 0) {
                    free(simple->min_exclusive);
                    simple->min_exclusive = xsd_strdup(val);
                    simple->fixed_min_exclusive = (unsigned char)is_fixed;
                } else if (strcmp(fl, "maxInclusive") == 0) {
                    free(simple->max_inclusive);
                    simple->max_inclusive = xsd_strdup(val);
                    simple->fixed_max_inclusive = (unsigned char)is_fixed;
                } else if (strcmp(fl, "maxExclusive") == 0) {
                    free(simple->max_exclusive);
                    simple->max_exclusive = xsd_strdup(val);
                    simple->fixed_max_exclusive = (unsigned char)is_fixed;
                } else if (strcmp(fl, "length") == 0) {
                    simple->length = val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_length = (unsigned char)is_fixed;
                } else if (strcmp(fl, "minLength") == 0) {
                    simple->min_length = val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_min_length = (unsigned char)is_fixed;
                } else if (strcmp(fl, "maxLength") == 0) {
                    simple->max_length = val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_max_length = (unsigned char)is_fixed;
                } else if (strcmp(fl, "totalDigits") == 0) {
                    simple->total_digits =
                        val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_total_digits = (unsigned char)is_fixed;
                } else if (strcmp(fl, "fractionDigits") == 0) {
                    simple->fraction_digits =
                        val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_fraction_digits =
                        (unsigned char)is_fixed;
                } else if (strcmp(fl, "minScale") == 0) {
                    /* XSD 1.1: the scale pair (decimal family,
                     * #2155) */
                    simple->min_scale = val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_min_scale = (unsigned char)is_fixed;
                } else if (strcmp(fl, "maxScale") == 0) {
                    simple->max_scale = val ? strtol(val, NULL, 10) : -1;
                    simple->fixed_max_scale = (unsigned char)is_fixed;
                } else if (strcmp(fl, "explicitTimezone") == 0) {
                    /* XSD 1.1: the timezone facet (date/time
                     * family, #2155) */
                    free(simple->explicit_timezone);
                    simple->explicit_timezone = xsd_strdup(val);
                    simple->fixed_timezone = (unsigned char)is_fixed;
                } else if (strcmp(fl, "whiteSpace") == 0) {
                    free(simple->whitespace);
                    simple->whitespace = xsd_strdup(val);
                    simple->fixed_whitespace = (unsigned char)is_fixed;
                } else if (strcmp(fl, "assertion") == 0) {
                    /* #2154: assertion facets ACCUMULATE — a
                     * restriction may carry several and the validator
                     * enforces every one (§3.16.6); the old single
                     * slot silently dropped every facet past the
                     * last. */
                    const char* test =
                        leptris_element_attribute(fe, "test");
                    if (test && *test) {
                        XsdAssertionFacet* af =
                            (XsdAssertionFacet*)calloc(1, sizeof(*af));
                        if (af) {
                            af->test = xsd_strdup(test);
                            /* §3.13.2: the facet's bindings ride
                             * the schema document too — the base
                             * URI rides the validator-side wrap
                             * (this capture has no schema) */
                            af->ns_prolog = xsd_capture_ns_prolog(fe, NULL);
                            if (!simple->assertions) {
                                simple->assertions = af;
                            } else {
                                XsdAssertionFacet* t =
                                    simple->assertions;
                                while (t->next) t = t->next;
                                t->next = af;
                            }
                        }
                    }
                }
            }
        }
    }

    return simple;
}

/* Depth-capped local-base resolution with a cycle guard: the
 * restriction chain walks to its built-in, facets of EVERY hop
 * apply (XSD semantics). */
/* ---- the reference-resolution chokepoint (the #440 family) --------
 *
 * Every declaration table lookup rides ONE generated ladder instead
 * of a per-table copy of the same three loops. The ladder, in order:
 *   1. exact (declaring targetNamespace, name)
 *   2. unstamped entries (unknown-namespace declarations)
 *   3. name-only across the merged table — ONLY when the policy
 *      allows it (allow_name_only)
 * Policy: STRICT (allow_name_only = 0) for compile-side derivation
 * lookups — extension bases and group refs are same-namespace by
 * XSD rules, and a foreign same-name decl must never win
 * (leptris-ruby#440). LADDER (allow_name_only = 1) for instance-
 * side type references — OOXML types wp:/pic: particles with
 * a:-prefixed main-namespace types, and prefix-stripped names
 * legitimately cross namespaces (#1687). */
#define XSD_DEFINE_NS_LADDER(ret_type, fn_name, list_field, name_field,    \
                             ns_field)                                     \
static ret_type fn_name(struct leptris_xsd_schema* s, const char* name,    \
                        const char* uri, int allow_name_only) {            \
    if (!s || !name)                                                       \
        return NULL;                                                       \
    if (uri && uri[0]) {                                                   \
        for (ret_type t = s->list_field; t; t = t->next)                   \
            if (t->ns_field && strcmp(t->name_field, name) == 0 &&         \
                strcmp(t->ns_field, uri) == 0)                             \
                return t;                                                  \
        for (ret_type t = s->list_field; t; t = t->next)                   \
            if (!t->ns_field && strcmp(t->name_field, name) == 0)          \
                return t;                                                  \
        if (!allow_name_only)                                              \
            return NULL;                                                   \
        for (ret_type t = s->list_field; t; t = t->next)                   \
            if (strcmp(t->name_field, name) == 0)                          \
                return t;                                                  \
        return NULL;                                                       \
    }                                                                      \
    /* NULL uri: the reference itself is namespace-free — unstamped    \
     * declarations bind FIRST (leptris#1958: an imported               \
     * no-targetNamespace type must win its own reference over the      \
     * importing namespace's same-name type), then the policy allows    \
     * the name-only sweep across the merged table. */                  \
    for (ret_type t = s->list_field; t; t = t->next)                       \
        if (strcmp(t->name_field, name) == 0)                              \
            return t;                                                      \
    return NULL;                                                           \
}

XSD_DEFINE_NS_LADDER(const XsdElementDecl*, xsd_ladder_element, elements,
                     name, ns)

/* leptris#1958: a NULL-namespace INSTANCE element binds its own
 * no-targetNamespace declaration FIRST — the importing namespace's
 * same-name global (registered later, prepended earlier in the
 * merged list) must not shadow it. Stamped references keep the
 * macro's ladder. */
const XsdElementDecl* xsd_ladder_element_ns_free_first(
    struct leptris_xsd_schema* s, const char* name, const char* uri,
    int allow_name_only) {
    if (!s || !name) return NULL;
    if (uri && uri[0])
        return xsd_ladder_element(s, name, uri, allow_name_only);
    for (XsdElementDecl* t = s->elements; t; t = t->next)
        if (!t->ns && strcmp(t->name, name) == 0) return t;
    if (!allow_name_only) return NULL;
    for (XsdElementDecl* t = s->elements; t; t = t->next)
        if (strcmp(t->name, name) == 0) return t;
    return NULL;
}
XSD_DEFINE_NS_LADDER(XsdCm*, xsd_ladder_complex, complex_types, name, ns)
XSD_DEFINE_NS_LADDER(const XsdSimple*, xsd_ladder_simple, simple_types,
                     name, ns)
XSD_DEFINE_NS_LADDER(const XsdTypeAttrs*, xsd_ladder_type_attrs,
                     type_attrs, type_name, ns)

static const XsdSimple* xsd_find_simple(struct leptris_xsd_schema* s,
                                        const char* name) {
    for (XsdSimple* t = s->simple_types; t; t = t->next)
        if (strcmp(t->name, name) == 0) return t;
    return NULL;
}

/* leptris-ruby#440 shape B: prefer the (ns, name) exact match —
 * imported namespaces carry same-name simpleTypes whose value
 * spaces differ (shared-math ST_Jc has centerGroup, wml ST_Jc
 * does not). A resolving namespace is in hand: a DIFFERENT
 * namespace's same-name type must not win; but prefix-stripped
 * type names legitimately cross namespaces (CT_TwipsMeasure in
 * math types its val with sharedTypes' ST_TwipsMeasure), so the
 * name-only match remains the last resort. */
const XsdSimple* xsd_find_simple_ns_pub(
    struct leptris_xsd_schema* s, const char* name, const char* uri) {
    /* LADDER: prefix-stripped type names legitimately cross
     * namespaces (sharedTypes' ST_TwipsMeasure under a math prefix). */
    return xsd_ladder_simple(s, name, uri, 1);
}

extern int xsd_builtin_valid(const char* type, const char* v);
extern int rng_regex_matches(const char* pat, const char* text);
static int xsd_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v, int depth);

/* One list item or union member: a builtin reference, a local
 * schema type (chain-walked), or an inline capture. Unresolved
 * names accept (the compile-time leniency the corpus gate pins). */
static int xsd_item_valid(struct leptris_xsd_schema* s,
                          const char* name, const XsdSimple* def,
                          const char* v, int depth) {
    if (name && strncmp(name, "xs:", 3) == 0)
        return xsd_builtin_valid(name, v);
    if (def) return xsd_valid_chain(s, def, v, depth + 1);
    if (name) {
        const XsdSimple* next = xsd_find_simple(s, name);
        if (next) return xsd_valid_chain(s, next, v, depth + 1);
    }
    return 1;
}

/* #1882: does the base chain resolve to a LIST type? A restriction
 * OF a list keeps deriv RESTRICT, but its length facets still count
 * items — the value space is the list's. */
static int xsd_chain_is_list(struct leptris_xsd_schema* s,
                             const XsdSimple* t) {
    int depth = 0;
    while (t && depth < 32) {
        if (t->deriv == XSD_SIMPLE_LIST) return 1;
        if (!t->base) {
            /* #1936: an anonymous inline base continues the chain —
             * a restriction of an inline list keeps list semantics
             * (the length family counts items). */
            if (t->base_def) { t = t->base_def; depth++; continue; }
            return 0;
        }
        if (strncmp(t->base, "xs:", 3) == 0)
            return strcmp(t->base, "xs:IDREFS") == 0 ||
                   strcmp(t->base, "xs:NMTOKENS") == 0 ||
                   strcmp(t->base, "xs:ENTITIES") == 0;
        t = xsd_find_simple(s, t->base);
        depth++;
    }
    return 0;
}

/* The chain's terminal builtin (NULL = none resolved): the length
 * family needs it to count octets for the binary bases
 * (leptris-ruby#434). */
static const char* xsd_chain_terminal(struct leptris_xsd_schema* s,
                                      const XsdSimple* t) {
    int depth = 0;
    while (t && depth < 32) {
        if (!t->base) return NULL;
        if (strncmp(t->base, "xs:", 3) == 0) return t->base;
        t = xsd_find_simple(s, t->base);
        depth++;
    }
    return NULL;
}

static int xsd_deriv_valid(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v,
                           int depth) {
    if (t->deriv == XSD_SIMPLE_LIST) {
        char* dup = xsd_strdup(v);
        if (!dup) return 0;
        size_t n = 0;
        int ok = 1;
        for (char* p = dup; *p;) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char* end = p;
            while (*end && !isspace((unsigned char)*end)) end++;
            char was = *end;
            *end = 0;
            if (ok && !xsd_item_valid(s, t->item_type, t->item_def, p,
                                      depth))
                ok = 0;
            *end = was;
            p = end;
            n++;
        }
        free(dup);
        /* length family counts ITEMS for lists (XSD semantics) */
        if (ok && t->length >= 0 && (long)n != t->length) ok = 0;
        if (ok && t->min_length >= 0 && (long)n < t->min_length) ok = 0;
        if (ok && t->max_length >= 0 && (long)n > t->max_length) ok = 0;
        /* pattern applies to the whole lexical; several patterns are
         * a disjunction (p16: same as [abc]+|[123]+) */
        if (ok && t->patterns) {
            int matched = 0;
            for (XsdFacetValue* p = t->patterns; p; p = p->next)
                if (rng_regex_matches(p->text, v)) {
                    matched = 1;
                    break;
                }
            if (!matched) ok = 0;
        }
        return ok;
    }
    if (t->deriv == XSD_SIMPLE_UNION) {
        for (size_t i = 0; i < t->member_count; i++)
            if (xsd_item_valid(s, t->members[i], NULL, v, depth))
                return 1;
        for (size_t i = 0; i < t->member_def_count; i++)
            if (xsd_item_valid(s, NULL, t->member_defs[i], v, depth))
                return 1;
        return 0;
    }
    /* #1936: the value satisfies the inline base (its own
     * derivation — list item walks, union members — first), then
     * this level's facets. */
    if (t->base_def && !xsd_deriv_valid(s, t->base_def, v, depth + 1))
        return 0;
    return xsd_simple_valid(t, v, xsd_chain_terminal(s, t), 0);
}

static int xsd_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v, int depth) {
    if (!t || depth > 32) return 0;
    if (t->base && strncmp(t->base, "xs:", 3) != 0) {
        const XsdSimple* next = xsd_find_simple(s, t->base);
        if (next && !xsd_valid_chain(s, next, v, depth + 1)) return 0;
    }
    if (t->deriv != XSD_SIMPLE_RESTRICT)
        return xsd_deriv_valid(s, t, v, depth);
    /* #1936: the inline anonymous base drives the value first
     * (its own derivation — list item walks, union members),
     * then this restriction's facets. */
    if (t->base_def && !xsd_deriv_valid(s, t->base_def, v, depth + 1))
        return 0;
    return xsd_simple_valid(t, v, xsd_chain_terminal(s, t),
                            xsd_chain_is_list(s, t));
}

static void xsd_set_error(struct leptris_xsd_schema* s, const char* msg) {
    free(s->error);
    s->error = NULL;
    if (msg) {
        size_t n = strlen(msg) + 1;
        s->error = (char*)malloc(n);
        if (s->error) memcpy(s->error, msg, n);
    }
}

/* ---- #2155: the 1.1 facet-applicability sweep ---------------------
 *
 * The captured restriction facets are checked against the
 * datatypes.c applicability table (which facets are legal on which
 * terminal builtin), the facet VALUES' lexical validity, the
 * cross-facet order clauses, and — for the 1.1-only builtins — the
 * narrowing clauses against the base chain (a restriction may not
 * widen, and a FIXED base facet cannot be re-specified with a
 * different value). Runs post-capture, like the pattern-regex
 * sweep; first violation names the rule. */

/* The narrowing chain hop: inline anonymous bases ride base_def,
 * named local bases resolve name-only, builtins end the walk. */
static const XsdSimple* facet_base_next(struct leptris_xsd_schema* s,
                                        const XsdSimple* t) {
    if (!t) return NULL;
    if (t->base_def) return t->base_def;
    if (t->base && strncmp(t->base, "xs:", 3) != 0)
        return xsd_find_simple(s, t->base);
    return NULL;
}

static int xsd_restriction_facets_ok(struct leptris_xsd_schema* s,
                                     const XsdSimple* t, char* msg,
                                     size_t cap) {
    int chain_is_list = xsd_chain_is_list(s, t);
    const char* term = xsd_chain_terminal(s, t);
    int tclass = term ? xsd_terminal_class(term) : 7 /* OTHER */;
    int checkable = chain_is_list || tclass != 7;
    if (!checkable) return 1; /* unions / anyAtomic chains: posture */
    int is11 = term && xsd_builtin_is_11(term);

    /* ---- applicability + facet-value validity ------------------- */
    const char* number_bufs[7];
    char bufs[7][24];
    snprintf(bufs[0], sizeof(bufs[0]), "%ld", t->length);
    number_bufs[0] = bufs[0];
    snprintf(bufs[1], sizeof(bufs[0]), "%ld", t->min_length);
    number_bufs[1] = bufs[1];
    snprintf(bufs[2], sizeof(bufs[0]), "%ld", t->max_length);
    number_bufs[2] = bufs[2];
    snprintf(bufs[3], sizeof(bufs[0]), "%ld", t->total_digits);
    number_bufs[3] = bufs[3];
    snprintf(bufs[4], sizeof(bufs[0]), "%ld", t->fraction_digits);
    number_bufs[4] = bufs[4];
    snprintf(bufs[5], sizeof(bufs[0]), "%ld", t->min_scale);
    number_bufs[5] = bufs[5];
    snprintf(bufs[6], sizeof(bufs[0]), "%ld", t->max_scale);
    number_bufs[6] = bufs[6];
    struct {
        const char* facet;
        const char* value;
        int present;
    } slots[13];
    int nslots = 0;
    slots[nslots].facet = "length";
    slots[nslots].value = number_bufs[0];
    slots[nslots++].present = (t->length != -1);
    slots[nslots].facet = "minLength";
    slots[nslots].value = number_bufs[1];
    slots[nslots++].present = (t->min_length != -1);
    slots[nslots].facet = "maxLength";
    slots[nslots].value = number_bufs[2];
    slots[nslots++].present = (t->max_length != -1);
    slots[nslots].facet = "totalDigits";
    slots[nslots].value = number_bufs[3];
    slots[nslots++].present = (t->total_digits != -1);
    slots[nslots].facet = "fractionDigits";
    slots[nslots].value = number_bufs[4];
    slots[nslots++].present = (t->fraction_digits != -1);
    slots[nslots].facet = "minScale";
    slots[nslots].value = number_bufs[5];
    slots[nslots++].present = (t->min_scale != -1);
    slots[nslots].facet = "maxScale";
    slots[nslots].value = number_bufs[6];
    slots[nslots++].present = (t->max_scale != -1);
    slots[nslots].facet = "minInclusive";
    slots[nslots].value = t->min_inclusive;
    slots[nslots++].present = (t->min_inclusive != NULL);
    slots[nslots].facet = "minExclusive";
    slots[nslots].value = t->min_exclusive;
    slots[nslots++].present = (t->min_exclusive != NULL);
    slots[nslots].facet = "maxInclusive";
    slots[nslots].value = t->max_inclusive;
    slots[nslots++].present = (t->max_inclusive != NULL);
    slots[nslots].facet = "maxExclusive";
    slots[nslots].value = t->max_exclusive;
    slots[nslots++].present = (t->max_exclusive != NULL);
    slots[nslots].facet = "whiteSpace";
    slots[nslots].value = t->whitespace;
    slots[nslots++].present = (t->whitespace != NULL);
    slots[nslots].facet = "explicitTimezone";
    slots[nslots].value = t->explicit_timezone;
    slots[nslots++].present = (t->explicit_timezone != NULL);
    for (int i = 0; i < nslots; i++) {
        if (!slots[i].present) continue;
        if (!xsd_facet_applicable(term, slots[i].facet, chain_is_list)) {
            snprintf(msg, cap,
                     "facet-applicability: the %s facet is not "
                     "applicable to its %s base",
                     slots[i].facet, term ? term : "list");
            return 0;
        }
        if (!xsd_facet_value_valid(term, slots[i].facet, slots[i].value,
                                   chain_is_list)) {
            snprintf(msg, cap,
                     "facet-value: '%s' is not a valid lexical of %s "
                     "for the %s facet",
                     slots[i].value, term, slots[i].facet);
            return 0;
        }
    }
    /* enumeration entries are typed too — each is a lexical of the
     * terminal builtin (d3_3_4si21 / d3_4_26si09 / d3_4_28si13) */
    for (const XsdFacetValue* e = t->enum_values; e; e = e->next) {
        if (!xsd_facet_value_valid(term, "enumeration", e->text,
                                   chain_is_list)) {
            snprintf(msg, cap,
                     "facet-value: enumeration entry '%s' is not a "
                     "valid lexical of %s",
                     e->text, term);
            return 0;
        }
    }

    /* ---- the fixed whiteSpace + the collapse-fixed posture ------ */
    if (is11 && t->whitespace &&
        strcmp(t->whitespace, "collapse") != 0) {
        snprintf(msg, cap,
                 "facet-value: whiteSpace on %s is fixed at collapse",
                 term);
        return 0;
    }
    if (is11 && t->explicit_timezone && term &&
        strcmp(term, "xs:dateTimeStamp") == 0 &&
        strcmp(t->explicit_timezone, "required") != 0) {
        snprintf(msg, cap,
                 "facet-value: explicitTimezone on dateTimeStamp can "
                 "only be required");
        return 0;
    }

    /* ---- cross-facet order clauses ------------------------------- */
    int c;
    if (t->min_inclusive && t->min_exclusive) {
        snprintf(msg, cap,
                 "cross-facet: minInclusive and minExclusive cannot "
                 "both be specified");
        return 0;
    }
    if (t->max_inclusive && t->max_exclusive) {
        snprintf(msg, cap,
                 "cross-facet: maxInclusive and maxExclusive cannot "
                 "both be specified");
        return 0;
    }
    if (t->min_inclusive && t->max_inclusive &&
        (c = xsd_facet_value_compare(term, t->min_inclusive,
                                     t->max_inclusive)) > 0) {
        snprintf(msg, cap,
                 "cross-facet: minInclusive exceeds maxInclusive");
        return 0;
    }
    if (t->min_exclusive && t->max_exclusive &&
        xsd_facet_value_compare(term, t->min_exclusive,
                                t->max_exclusive) >= 0) {
        snprintf(msg, cap,
                 "cross-facet: minExclusive is not below maxExclusive");
        return 0;
    }
    if (t->min_inclusive && t->max_exclusive &&
        xsd_facet_value_compare(term, t->min_inclusive,
                                t->max_exclusive) >= 0) {
        snprintf(msg, cap,
                 "cross-facet: minInclusive is not below maxExclusive");
        return 0;
    }
    if (t->min_exclusive && t->max_inclusive &&
        xsd_facet_value_compare(term, t->min_exclusive,
                                t->max_inclusive) >= 0) {
        snprintf(msg, cap,
                 "cross-facet: minExclusive is not below maxInclusive");
        return 0;
    }
    if (t->min_scale != -1 && t->max_scale != -1 &&
        t->min_scale > t->max_scale) {
        snprintf(msg, cap, "cross-facet: minScale exceeds maxScale");
        return 0;
    }

    /* ---- narrowing + fixed re-specification (1.1 builtins) ------ */
    if (!is11) return 1;
    long b_min_scale = -1, b_max_scale = -1;
    long b_total = -1, b_frac = -1;
    long b_min_len = -1, b_max_len = -1, b_len = -1;
    const char* b_min_incl = NULL;
    const char* b_min_excl = NULL;
    const char* b_max_incl = NULL;
    const char* b_max_excl = NULL;
    for (const XsdSimple* b = facet_base_next(s, t); b;
         b = facet_base_next(s, b)) {
        if (b->min_inclusive) {
            if (!b_min_incl ||
                xsd_facet_value_compare(term, b->min_inclusive,
                                        b_min_incl) > 0)
                b_min_incl = b->min_inclusive;
        }
        if (b->min_exclusive) {
            if (!b_min_excl ||
                xsd_facet_value_compare(term, b->min_exclusive,
                                        b_min_excl) > 0)
                b_min_excl = b->min_exclusive;
        }
        if (b->max_inclusive) {
            if (!b_max_incl ||
                xsd_facet_value_compare(term, b->max_inclusive,
                                        b_max_incl) < 0)
                b_max_incl = b->max_inclusive;
        }
        if (b->max_exclusive) {
            if (!b_max_excl ||
                xsd_facet_value_compare(term, b->max_exclusive,
                                        b_max_excl) < 0)
                b_max_excl = b->max_exclusive;
        }
        if (b->total_digits >= 0 &&
            (b_total < 0 || b->total_digits < b_total))
            b_total = b->total_digits;
        if (b->fraction_digits >= 0 &&
            (b_frac < 0 || b->fraction_digits < b_frac))
            b_frac = b->fraction_digits;
        /* scales are INTEGER (negative legal) — the -1 sentinel is
         * the only "absent" marker */
        if (b->min_scale != -1 &&
            (b_min_scale == -1 || b->min_scale > b_min_scale))
            b_min_scale = b->min_scale;
        if (b->max_scale != -1 &&
            (b_max_scale == -1 || b->max_scale < b_max_scale))
            b_max_scale = b->max_scale;
        if (b->min_length >= 0 && b->min_length > b_min_len)
            b_min_len = b->min_length;
        if (b->max_length >= 0 &&
            (b_max_len < 0 || b->max_length < b_max_len))
            b_max_len = b->max_length;
        if (b->length >= 0) b_len = b->length;
        /* the fixed law: the derived re-specification must carry the
         * same value (§4.3 — checked against EVERY fixed level; an
         * incomparable pair (-2, NaN) is no violation) */
        if (b->fixed_min_inclusive && t->min_inclusive &&
            xsd_facet_value_compare(term, t->min_inclusive,
                                    b->min_inclusive) == 1) {
            snprintf(msg, cap,
                     "fixed: minInclusive is fixed at '%s' in the base",
                     b->min_inclusive);
            return 0;
        }
        if (b->fixed_min_exclusive && t->min_exclusive &&
            xsd_facet_value_compare(term, t->min_exclusive,
                                    b->min_exclusive) == 1) {
            snprintf(msg, cap,
                     "fixed: minExclusive is fixed at '%s' in the base",
                     b->min_exclusive);
            return 0;
        }
        if (b->fixed_max_inclusive && t->max_inclusive &&
            xsd_facet_value_compare(term, t->max_inclusive,
                                    b->max_inclusive) == -1) {
            snprintf(msg, cap,
                     "fixed: maxInclusive is fixed at '%s' in the base",
                     b->max_inclusive);
            return 0;
        }
        if (b->fixed_max_exclusive && t->max_exclusive &&
            xsd_facet_value_compare(term, t->max_exclusive,
                                    b->max_exclusive) == -1) {
            snprintf(msg, cap,
                     "fixed: maxExclusive is fixed at '%s' in the base",
                     b->max_exclusive);
            return 0;
        }
        if (b->fixed_total_digits && t->total_digits >= 0 &&
            t->total_digits != b->total_digits) {
            snprintf(msg, cap,
                     "fixed: totalDigits is fixed at %ld in the base",
                     b->total_digits);
            return 0;
        }
        if (b->fixed_fraction_digits && t->fraction_digits >= 0 &&
            t->fraction_digits != b->fraction_digits) {
            snprintf(msg, cap,
                     "fixed: fractionDigits is fixed at %ld in the "
                     "base",
                     b->fraction_digits);
            return 0;
        }
        if (b->fixed_min_scale && t->min_scale != -1 &&
            t->min_scale != b->min_scale) {
            snprintf(msg, cap,
                     "fixed: minScale is fixed at %ld in the base",
                     b->min_scale);
            return 0;
        }
        if (b->fixed_max_scale && t->max_scale != -1 &&
            t->max_scale != b->max_scale) {
            snprintf(msg, cap,
                     "fixed: maxScale is fixed at %ld in the base",
                     b->max_scale);
            return 0;
        }
        if (b->fixed_length && t->length >= 0 &&
            t->length != b->length) {
            snprintf(msg, cap,
                     "fixed: length is fixed at %ld in the base",
                     b->length);
            return 0;
        }
    }
    if (t->total_digits >= 0 && b_total >= 0 &&
        t->total_digits > b_total) {
        snprintf(msg, cap,
                 "narrowing: the derived totalDigits exceeds the "
                 "base's");
        return 0;
    }
    if (t->fraction_digits >= 0 && b_frac >= 0 &&
        t->fraction_digits > b_frac) {
        snprintf(msg, cap,
                 "narrowing: the derived fractionDigits exceeds the "
                 "base's");
        return 0;
    }
    if (t->min_scale != -1 && b_min_scale != -1 &&
        t->min_scale < b_min_scale) {
        snprintf(msg, cap,
                 "narrowing: the derived minScale undercuts the "
                 "base's");
        return 0;
    }
    if (t->max_scale != -1 && b_max_scale != -1 &&
        t->max_scale > b_max_scale) {
        snprintf(msg, cap,
                 "narrowing: the derived maxScale exceeds the base's");
        return 0;
    }
    /* the EFFECTIVE scale window must stay non-empty (pdecimal014:
     * a derived maxScale under an inherited minScale) — the -1
     * sentinel is "absent", negative scales are real values */
    long t_min_s = t->min_scale, t_max_s = t->max_scale;
    long eff_min = (t_min_s != -1 && b_min_scale != -1)
                       ? (t_min_s > b_min_scale ? t_min_s : b_min_scale)
                   : (t_min_s != -1 ? t_min_s : b_min_scale);
    long eff_max = (t_max_s != -1 && b_max_scale != -1)
                       ? (t_max_s < b_max_scale ? t_max_s : b_max_scale)
                   : (t_max_s != -1 ? t_max_s : b_max_scale);
    if (eff_min != -1 && eff_max != -1 && eff_min > eff_max) {
        snprintf(msg, cap,
                 "cross-facet: the effective scale window is empty");
        return 0;
    }
    /* the narrowing clauses: -2 (incomparable) is never a violation */
    if (t->min_inclusive && b_min_incl &&
        xsd_facet_value_compare(term, t->min_inclusive, b_min_incl) ==
            -1) {
        snprintf(msg, cap,
                 "narrowing: the derived minInclusive undercuts the "
                 "base's");
        return 0;
    }
    if (t->min_inclusive && b_min_excl) {
        c = xsd_facet_value_compare(term, t->min_inclusive, b_min_excl);
        if (c == -1 || c == 0) {
            snprintf(msg, cap,
                     "narrowing: the derived minInclusive leaves an "
                     "empty value space against the base's "
                     "minExclusive");
            return 0;
        }
    }
    if (t->min_exclusive && b_min_excl &&
        xsd_facet_value_compare(term, t->min_exclusive, b_min_excl) ==
            -1) {
        snprintf(msg, cap,
                 "narrowing: the derived minExclusive undercuts the "
                 "base's");
        return 0;
    }
    if (t->min_exclusive && b_min_incl) {
        c = xsd_facet_value_compare(term, t->min_exclusive, b_min_incl);
        if (c == -1 || c == 0) {
            snprintf(msg, cap,
                     "narrowing: the derived minExclusive leaves an "
                     "empty value space against the base's "
                     "minInclusive");
            return 0;
        }
    }
    if (t->max_inclusive && b_max_incl &&
        xsd_facet_value_compare(term, t->max_inclusive, b_max_incl) >
            0) {
        snprintf(msg, cap,
                 "narrowing: the derived maxInclusive exceeds the "
                 "base's");
        return 0;
    }
    if (t->max_inclusive && b_max_excl &&
        xsd_facet_value_compare(term, t->max_inclusive, b_max_excl) >=
            0) {
        snprintf(msg, cap,
                 "narrowing: the derived maxInclusive leaves an empty "
                 "value space against the base's maxExclusive");
        return 0;
    }
    if (t->max_exclusive && b_max_excl &&
        xsd_facet_value_compare(term, t->max_exclusive, b_max_excl) >
            0) {
        snprintf(msg, cap,
                 "narrowing: the derived maxExclusive exceeds the "
                 "base's");
        return 0;
    }
    if (t->max_exclusive && b_max_incl &&
        xsd_facet_value_compare(term, t->max_exclusive, b_max_incl) >=
            0) {
        snprintf(msg, cap,
                 "narrowing: the derived maxExclusive leaves an empty "
                 "value space against the base's maxInclusive");
        return 0;
    }
    if (t->min_length >= 0 && b_min_len >= 0 &&
        t->min_length < b_min_len) {
        snprintf(msg, cap,
                 "narrowing: the derived minLength undercuts the "
                 "base's");
        return 0;
    }
    if (t->max_length >= 0 && b_max_len >= 0 &&
        t->max_length > b_max_len) {
        snprintf(msg, cap,
                 "narrowing: the derived maxLength exceeds the base's");
        return 0;
    }
    if (t->length >= 0 && b_len >= 0 && t->length != b_len) {
        snprintf(msg, cap,
                 "narrowing: the derived length differs from the "
                 "base's");
        return 0;
    }
    return 1;
}

static int xsd_simple_facets_ok(struct leptris_xsd_schema* s,
                                const XsdSimple* t, int depth, char* msg,
                                size_t cap) {
    if (!t || depth > 32) return 1;
    if (t->deriv == XSD_SIMPLE_RESTRICT &&
        !xsd_restriction_facets_ok(s, t, msg, cap))
        return 0;
    if (!xsd_simple_facets_ok(s, t->base_def, depth + 1, msg, cap))
        return 0;
    if (!xsd_simple_facets_ok(s, t->item_def, depth + 1, msg, cap))
        return 0;
    for (size_t i = 0; i < t->member_def_count; i++)
        if (!xsd_simple_facets_ok(s, t->member_defs[i], depth + 1, msg,
                                  cap))
            return 0;
    return 1;
}

static int xsd_datatype_facets_supported(struct leptris_xsd_schema* s) {
    char msg[256];
    for (const XsdSimple* t = s->simple_types; t; t = t->next)
        if (!xsd_simple_facets_ok(s, t, 0, msg, sizeof(msg))) {
            xsd_set_error(s, msg);
            return 0;
        }
    return 1;
}

/* #2155: unprefixed builtin references canonize to the "xs:"-spelled
 * builtin when NO local declaration carries the name — the same
 * fallback the validate.c element path takes. The suite's ibm
 * duration/stamp/precisionDecimal fixtures restrict the unprefixed
 * spellings (base="yearMonthDuration"), and both the applicability
 * walk and the value chain need the terminal to resolve. */
static void xsd_canon_builtin_ref(struct leptris_xsd_schema* s,
                                  char** name) {
    if (!*name || **name == ':' || strncmp(*name, "xs:", 3) == 0)
        return;
    if (strchr(*name, ':')) return; /* prefixed local ref */
    if (xsd_find_simple(s, *name)) return;
    char buf[256];
    if (snprintf(buf, sizeof(buf), "xs:%s", *name) >= (int)sizeof(buf))
        return;
    if (!xsd_builtin_known(buf)) return;
    free(*name);
    *name = xsd_strdup(buf);
}

static void xsd_canon_builtin_refs_simple(struct leptris_xsd_schema* s,
                                          XsdSimple* t, int depth) {
    if (!t || depth > 32) return;
    xsd_canon_builtin_ref(s, &t->base);
    xsd_canon_builtin_ref(s, &t->item_type);
    for (size_t i = 0; i < t->member_count; i++)
        xsd_canon_builtin_ref(s, &t->members[i]);
    xsd_canon_builtin_refs_simple(s, t->base_def, depth + 1);
    xsd_canon_builtin_refs_simple(s, t->item_def, depth + 1);
    for (size_t i = 0; i < t->member_def_count; i++)
        xsd_canon_builtin_refs_simple(s, t->member_defs[i], depth + 1);
}

static void xsd_canon_builtin_refs(struct leptris_xsd_schema* s) {
    for (XsdSimple* t = s->simple_types; t; t = t->next)
        xsd_canon_builtin_refs_simple(s, t, 0);
}


/* XSD Part 2 §4.3.4: every pattern facet value is a regular
 * expression — an unsupported one is a schema error AT COMPILE
 * time (reG26-33/reH19: the suite's schemaTest=invalid rows, e.g.
 * the mid-dash [a-c-1-4x-z-7-9]). Walks the def chains so inline
 * anonymous members are covered too. */
extern int rng_regex_supported(const char* pat);
static int xsd_patterns_supported(const XsdSimple* t, int depth) {
    if (!t || depth > 32) return 1;
    for (const XsdFacetValue* p = t->patterns; p; p = p->next)
        if (!rng_regex_supported(p->text)) return 0;
    if (!xsd_patterns_supported(t->base_def, depth + 1)) return 0;
    if (!xsd_patterns_supported(t->item_def, depth + 1)) return 0;
    for (size_t i = 0; i < t->member_def_count; i++)
        if (!xsd_patterns_supported(t->member_defs[i], depth + 1))
            return 0;
    return 1;
}

/* A top-level schema-language declaration component. */
static int xsd_is_declaration(const char* local, const char* uri) {
    if (uri && strcmp(uri, XSD_NS) != 0) return 0;
    if (!local) return 0;
    static const char* names[] = {
        "element",    "attribute",    "simpleType", "complexType",
        "group",      "attributeGroup", "notation", "include",
        "defaultOpenContent",
        "import",     "redefine",     "override", NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(local, names[i]) == 0) return 1;
    return 0;
}

static char* xsd_read_file(const char* path, size_t* out_len) {
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    *out_len = got;
    return buf;
}

static LeptrisXsdSchema xsd_compile_impl(const char* xsd_text,
                                         size_t len,
                                         const char* base_dir,
                                         const char* self_path,
                                         LeptrisStatus* status);

LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile(const char* xsd_text,
                                                 size_t len,
                                                 LeptrisStatus* status) {
    return xsd_compile_impl(xsd_text, len, NULL, NULL, status);
}

LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile_file(
    const char* path, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!path || !*path) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    size_t len = 0;
    char* text = xsd_read_file(path, &len);
    if (!text) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    /* relative schemaLocations resolve against the schema's own
     * directory (both separators: Windows paths carry '\') */
    const char* slash = strrchr(path, '/');
    const char* bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    char* base_dir = NULL;
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n == 0) n = 1; /* "/f.xsd" roots at "/" */
        base_dir = (char*)malloc(n + 1);
        if (base_dir) {
            memcpy(base_dir, path, n);
            base_dir[n] = 0;
        }
    } else {
        base_dir = xsd_strdup(".");
    }
    LeptrisXsdSchema s = xsd_compile_impl(text, len, base_dir,
                                          path, status);
    free(base_dir);
    free(text);
    return s;
}

/* Numeric attribute value (atoi semantics); 1 when it parsed. */
static int xsd_uint_attr(LeptrisElement e, const char* name, int* out) {
    const char* v = leptris_element_attribute(e, name);
    if (!v || !v[0]) return 0;
    char* end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v || n < 0) return 0;
    *out = (int)n;
    return 1;
}

/* src-include.1 (conformance lane schema-error class, #1691):
 * the included/redefined schema's targetNamespace must equal the
 * includer's — or be absent. Slice 4 (#1691): RECURSIVE — the
 * violation can live one hop deeper than the root (582906-2: the
 * root IMPORTS prog1/prog2 legally; prog1/prog2 INCLUDE common
 * whose tns differs from theirs), so every loaded document's own
 * includes are checked against that document's tns. */
static int xsd_check_include_tns_pass(struct leptris_xsd_schema* s,
                                      LeptrisElement root,
                                      const char* doc_tns,
                                      const char* base_dir,
                                      int depth) {
    if (!root || depth > 8) return 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* local = NULL;
        const char* prefix = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name(c, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0)
            continue;
        int is_include = (strcmp(local, "include") == 0 ||
                          strcmp(local, "redefine") == 0);
        int is_import = (strcmp(local, "import") == 0);
        /* The tns rule only binds include/redefine; imports may
         * differ freely — but an imported document's OWN includes
         * still must satisfy it (582906-2: the root imports
         * prog1/prog2 legally, and their INCLUDE of common is the
         * violation), so imports recurse without the verdict. */
        if (!is_include && !is_import) continue;
        const char* loc =
            leptris_element_attribute(c, "schemaLocation");
        if (!loc || !base_dir)
            continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", base_dir, loc);
        size_t flen = 0;
        char* text = xsd_read_file(path, &flen);
        if (!text) continue;
        LeptrisDocument sub = leptris_parse_string(text, flen, NULL);
        free(text);
        if (!sub) continue;
        LeptrisElement subroot = leptris_document_root(sub);
        const char* sub_tns =
            subroot ? leptris_element_attribute(subroot,
                                                "targetNamespace")
                    : NULL;
        int err = 0;
        char msg[512];
        if (is_include) {
            if (doc_tns && sub_tns && doc_tns[0] && sub_tns[0] &&
                strcmp(sub_tns, doc_tns) != 0) {
                snprintf(msg, sizeof(msg),
                         "src-include.1: the target namespace '%s' of the "
                         "included/redefined schema '%s' differs from '%s' "
                         "of the including schema",
                         sub_tns, loc, doc_tns);
                err = 1;
            } else if ((!doc_tns || !doc_tns[0]) && sub_tns && sub_tns[0]) {
                snprintf(msg, sizeof(msg),
                         "src-include.1: the target namespace '%s' of the "
                         "included/redefined schema '%s' has to be absent, "
                         "since the including schema has no target namespace",
                         sub_tns, loc);
                err = 1;
            }
        }
        if (!err && subroot) {
            /* Recurse with the sub-document's own tns and the
             * sub-file's directory as the include base. */
            char sub_base[4096];
            const char* slash = strrchr(path, '/');
            const char* bslash = strrchr(path, '\\');
            if (bslash && (!slash || bslash > slash)) slash = bslash;
            if (slash) {
                size_t bl = (size_t)(slash - path);
                if (bl >= sizeof(sub_base)) bl = sizeof(sub_base) - 1;
                memcpy(sub_base, path, bl);
                sub_base[bl] = 0;
                if (xsd_check_include_tns_pass(s, subroot, sub_tns,
                                               sub_base, depth + 1)) {
                    err = 2;
                }
            }
        }
        leptris_document_free(sub);
        if (err == 1) {
            xsd_set_error(s, msg);
            return -1;
        }
        if (err == 2) return -1;
    }
    return 0;
}

static int xsd_check_include_tns(struct leptris_xsd_schema* s,
                                 LeptrisElement root) {
    return xsd_check_include_tns_pass(s, root, s->target_ns, s->base_dir,
                                      0);
}

/* Schema-structure constraints (the conformance lane's schema-error
 * class, #1691): the xs:-vocabulary rules the oracle enforces at
 * load. First violation wins; the message names the rule. */
/* Attribute allowlists for the xs: vocabulary (conformance lane
 * schema-error class, #1691 slice 3): the src-attribute.* /
 * src-element.* clauses the oracle enforces at load. */
static int xsd_attr_allowed(const char* local, int toplevel,
                            const char* attr) {
    if (strcmp(attr, "id") == 0) return 1;
    if (strcmp(local, "attribute") == 0) {
        if (strcmp(attr, "use") == 0 || strcmp(attr, "form") == 0)
            return !toplevel;
        /* ref form: ref + use only. name form: the rest. */
        return 1;  /* name/type/default/fixed allowed in name form */
    }
    if (strcmp(local, "element") == 0) {
        if (strcmp(attr, "minOccurs") == 0 ||
            strcmp(attr, "maxOccurs") == 0)
            return !toplevel;
        if (strcmp(attr, "form") == 0) return !toplevel;
        return 1;  /* name/ref/type/default/fixed/nillable/block/... */
    }
    if (strcmp(local, "annotation") == 0)
        return strcmp(attr, "id") == 0;
    return 1;
}

static int xsd_check_structure(struct leptris_xsd_schema* s,
                               LeptrisElement e, int depth) {
    if (!e) return 0;
    for (LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* local = NULL;
        const char* prefix = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name(c, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) {
            /* foreign-namespace content: recurse only */
            if (xsd_check_structure(s, c, depth + 1) != 0) return -1;
            continue;
        }
        int toplevel = (depth == 0);
        /* Unknown attributes on the xs: vocabulary (annot-err: a
         * 'death' attribute on xs:annotation). */
        for (LeptrisAttribute a = leptris_element_get_first_attribute(c);
             a; a = leptris_attr_next(a)) {
            const char* an = leptris_attribute_get_name(
                (LeptrisAttribute)a);
            if (strncmp(an, "xmlns", 5) == 0) continue;
            if (!xsd_attr_allowed(local, toplevel, an)) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "src-%s: The attribute '%s' is not allowed on "
                         "xs:%s",
                         local, an, local);
                xsd_set_error(s, msg);
                return -1;
            }
        }
        if (strcmp(local, "attribute") == 0 ||
            strcmp(local, "element") == 0) {
            const char* nm = leptris_element_attribute(c, "name");
            const char* ref = leptris_element_attribute(c, "ref");
            if ((nm && nm[0]) && (ref && ref[0])) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "src-attribute.1: xs:%s cannot carry both "
                         "'name' and 'ref'",
                         local);
                xsd_set_error(s, msg);
                return -1;
            }
            if ((!nm || !nm[0]) && (!ref || !ref[0])) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "src-attribute.1: xs:%s requires 'name' or "
                         "'ref'",
                         local);
                xsd_set_error(s, msg);
                return -1;
            }
            /* name == "xmlns" reserves the namespace declaration
             * name (scc-no-xmlns). */
            if (strcmp(local, "attribute") == 0 && nm &&
                strcmp(nm, "xmlns") == 0) {
                xsd_set_error(
                    s,
                    "scc-no-xmlns: an attribute declaration must not "
                    "be named 'xmlns'");
                return -1;
            }
            const char* dv = leptris_element_attribute(c, "default");
            const char* fx = leptris_element_attribute(c, "fixed");
            if (dv && dv[0] && fx && fx[0]) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "src-element.1: xs:%s cannot carry both "
                         "'default' and 'fixed'",
                         local);
                xsd_set_error(s, msg);
                return -1;
            }
        }
        if (strcmp(local, "attribute") == 0) {
            const char* ref = leptris_element_attribute(c, "ref");
            const char* dv = leptris_element_attribute(c, "default");
            /* 3.2.3:2 — @default requires use=optional (the
             * src-attribute2 family) */
            const char* use = leptris_element_attribute(c, "use");
            if (dv && dv[0] && use && strcmp(use, "required") == 0) {
                xsd_set_error(
                    s,
                    "src-attribute.2: an attribute declaration with "
                    "use='required' must not carry 'default'");
                return -1;
            }
            /* inline type child (complexType/simpleType) */
            int has_type_child = 0;
            int has_non_annotation_child = 0;
            for (LeptrisNodeRef k = leptris_node_first_child(
                     leptris_element_as_node(c));
                 k; k = leptris_node_next_sibling(k)) {
                if (leptris_node_get_type(k) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                const char* kl = NULL;
                const char* ku = NULL;
                leptris_element_expanded_name((LeptrisElement)k, &kl,
                                              NULL, &ku);
                if (!kl || !ku || strcmp(ku, XSD_NS) != 0) continue;
                if (strcmp(kl, "complexType") == 0 ||
                    strcmp(kl, "simpleType") == 0)
                    has_type_child = 1;
                if (strcmp(kl, "annotation") != 0)
                    has_non_annotation_child = 1;
            }
            /* 3.2.3:3.2 — @ref excludes @type, @form and any
             * non-annotation child (the src-attribute3-2 family) */
            if (ref && ref[0]) {
                const char* form =
                    leptris_element_attribute(c, "form");
                const char* ty =
                    leptris_element_attribute(c, "type");
                if ((ty && ty[0]) || (form && form[0]) ||
                    has_non_annotation_child) {
                    xsd_set_error(
                        s,
                        "src-attribute.3: an attribute declaration "
                        "with 'ref' must not carry 'type', 'form' or "
                        "an inline type child");
                    return -1;
                }
            } else if (has_type_child) {
                /* 3.2.3:4 — @type excludes the inline type child
                 * (src-attribute4) */
                const char* ty =
                    leptris_element_attribute(c, "type");
                if (ty && ty[0]) {
                    xsd_set_error(
                        s,
                        "src-attribute.4: xs:attribute cannot carry "
                        "both a 'type' attribute and an inline type "
                        "child");
                    return -1;
                }
            }
        }
        if (strcmp(local, "element") == 0) {
            /* type attribute and an inline type child are mutually
             * exclusive (src-element.3 / src-attribute.4). */
            const char* ty = leptris_element_attribute(c, "type");
            int has_type_child = 0;
            for (LeptrisNodeRef k = leptris_node_first_child(
                     leptris_element_as_node(c));
                 k; k = leptris_node_next_sibling(k)) {
                if (leptris_node_get_type(k) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                const char* kl = NULL;
                const char* kp = NULL;
                const char* ku = NULL;
                leptris_element_expanded_name((LeptrisElement)k, &kl,
                                              &kp, &ku);
                if (kl && ku && strcmp(ku, XSD_NS) == 0 &&
                    (strcmp(kl, "complexType") == 0 ||
                     strcmp(kl, "simpleType") == 0))
                    has_type_child = 1;
            }
            if (ty && ty[0] && has_type_child) {
                xsd_set_error(
                    s,
                    "src-element.3: xs:element cannot carry both a "
                    "'type' attribute and an inline type child");
                return -1;
            }
        }
        /* An xs:-prefixed type reference names a real builtin (the
         * length3 corpus schema's type='xs:non-positive-integer' —
         * a 1999-draft name — must fail, not compile untyped). The
         * ur-types and NOTATION sit outside the tier-1 table on
         * purpose. */
        if (strcmp(local, "element") == 0 ||
            strcmp(local, "attribute") == 0) {
            const char* ty = leptris_element_attribute(c, "type");
            if (ty && strncmp(ty, "xs:", 3) == 0 &&
                xsd_builtin_valid(ty, "x") == -1 &&
                strcmp(ty, "xs:anyType") != 0 &&
                strcmp(ty, "xs:anySimpleType") != 0 &&
                strcmp(ty, "xs:anyAtomicType") != 0 &&
                strcmp(ty, "xs:NOTATION") != 0) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "src-attribute.4: the type reference '%s' "
                         "does not resolve to a builtin definition",
                         ty);
                xsd_set_error(s, msg);
                return -1;
            }
        }
        if (strcmp(local, "element") == 0 ||
            strcmp(local, "sequence") == 0 ||
            strcmp(local, "choice") == 0 ||
            strcmp(local, "all") == 0) {
            int mn = 1, mx = 1;
            int has_mn = xsd_uint_attr(c, "minOccurs", &mn);
            int has_mx = xsd_uint_attr(c, "maxOccurs", &mx);
            const char* mx_raw =
                leptris_element_attribute(c, "maxOccurs");
            int unbounded =
                (mx_raw && strcmp(mx_raw, "unbounded") == 0);
            /* Absent minOccurs defaults to 1: maxOccurs="0" alone is
             * a violation (src-element.2.2's maxOccurs=0 shape). */
            if (has_mn && !has_mx && !unbounded && mn > mx) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "occurrence: xs:%s minOccurs (%d) exceeds "
                         "maxOccurs (%d)",
                         local, mn, mx);
                xsd_set_error(s, msg);
                return -1;
            }
            if (has_mn && has_mx && !unbounded && mn > mx) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "occurrence: xs:%s minOccurs (%d) exceeds "
                         "maxOccurs (%d)",
                         local, mn, mx);
                xsd_set_error(s, msg);
                return -1;
            }
        }
        if (xsd_check_structure(s, c, depth + 1) != 0) return -1;
    }
    return 0;
}

/* ---- #1691 slice 4: derivation constraints -----------------------
 *
 * The oracle (libxml2 2.9.13) enforces two derivation-ok families at
 * load that the capture pass never sees:
 *   - cos-ct-derived-ok content-type match: a complexContent
 *     extension must not change the content type (issue491).
 *   - derivation-ok-restriction-4.1: a restricted complex type with
 *     an attribute wildcard requires the base to carry a wildcard,
 *     and a subset of it.
 * Document-level, before capture: only same-document global complex
 * types resolve here (the corpus shapes); imports ride the capture
 * tables and stay out of this pass. */

/* In-scope prefix -> uri for `e` (namespace declarations live in
 * the element's own decl list — leptris_element_attribute does not
 * answer "xmlns"/"xmlns:p"). Walks the ancestor chain. */
static void xsd_ns_for_prefix(LeptrisElement e, const char* prefix,
                              char* out, size_t outsz) {
    out[0] = 0;
    if (prefix && !prefix[0]) prefix = NULL;
    for (LeptrisElement a = e; a; a = leptris_element_get_parent(a)) {
        size_t n = leptris_element_namespace_count(a);
        for (size_t i = 0; i < n; i++) {
            const char* dp =
                leptris_element_namespace_decl_prefix(a, i);
            if (prefix) {
                if (dp && strcmp(dp, prefix) == 0) {
                    const char* du =
                        leptris_element_namespace_decl_uri(a, i);
                    snprintf(out, outsz, "%s", du ? du : "");
                    return;
                }
            } else if (!dp) {
                const char* du =
                    leptris_element_namespace_decl_uri(a, i);
                snprintf(out, outsz, "%s", du ? du : "");
                return;
            }
        }
    }
}

/* Split `qname` (possibly prefixed) and resolve the prefix in
 * e's scope. Unresolvable prefixes yield empty local (skip). */
static void xsd_resolve_qname(LeptrisElement e, const char* qname,
                              char* uri, size_t urisz, char* local,
                              size_t localsz) {
    uri[0] = 0;
    local[0] = 0;
    if (!qname || !qname[0]) return;
    const char* colon = strchr(qname, ':');
    char prefix[64] = {0};
    if (colon) {
        size_t pl = (size_t)(colon - qname);
        if (pl >= sizeof(prefix)) pl = sizeof(prefix) - 1;
        memcpy(prefix, qname, pl);
        snprintf(local, localsz, "%s", colon + 1);
        char u[256];
        xsd_ns_for_prefix(e, prefix, u, sizeof(u));
        if (!u[0]) {
            /* XML Namespaces: the prefixes xml and xmlns are
             * PREDECLARED for every document — xs:import of
             * http://www.w3.org/XML/1998/namespace (the dc.xsd
             * shape: <xs:attribute ref="xml:lang"/>) resolves
             * without any in-scope declaration. An empty uri here
             * failed the slice-5 ref check and poisoned the whole
             * schema graph (leptris#478 repro 1). */
            if (strcmp(prefix, "xml") == 0)
                snprintf(u, sizeof(u), "%s",
                         "http://www.w3.org/XML/1998/namespace");
            else if (strcmp(prefix, "xmlns") == 0)
                snprintf(u, sizeof(u), "%s",
                         "http://www.w3.org/2000/xmlns/");
            else {
                local[0] = 0;
                return;
            }
        }
        snprintf(uri, urisz, "%s", u);
    } else {
        char u[256];
        xsd_ns_for_prefix(e, NULL, u, sizeof(u));
        snprintf(uri, urisz, "%s", u);
        snprintf(local, localsz, "%s", qname);
    }
}

static LeptrisElement xsd_find_child(LeptrisElement e, const char* local) {
    if (!e) return NULL;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, &p, &u);
        if (l && u && strcmp(u, XSD_NS) == 0 && strcmp(l, local) == 0)
            return (LeptrisElement)n;
    }
    return NULL;
}

static LeptrisElement xsd_find_any_attribute(LeptrisElement e) {
    if (!e) return NULL;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, &p, &u);
        if (l && u && strcmp(u, XSD_NS) == 0) {
            if (strcmp(l, "anyAttribute") == 0) return (LeptrisElement)n;
            LeptrisElement deep = xsd_find_any_attribute((LeptrisElement)n);
            if (deep) return deep;
        }
    }
    return NULL;
}

static int xsd_mixed_flag(LeptrisElement ct) {
    /* #2151: xsd_bool_attr strips whitespace (open013's
     * mixed=" 1 ") and reads both spellings; the complexContent
     * spelling is part of the effective mixed (open021's R declares
     * it there). */
    if (xsd_bool_attr(ct, "mixed", 0)) return 1;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(ct));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* el = NULL;
        const char* eu = NULL;
        leptris_element_expanded_name(e, &el, NULL, &eu);
        if (el && eu && strcmp(eu, XSD_NS) == 0 &&
            strcmp(el, "complexContent") == 0 &&
            leptris_element_attribute(e, "mixed"))
            return xsd_bool_attr(e, "mixed", 0);
    }
    return 0;
}

/* Global complex type by {uri}local among the root's depth-0
 * declarations (globals live in the schema's target namespace). */
static LeptrisElement xsd_find_global_ct(LeptrisElement root,
                                         const char* uri,
                                         const char* local) {
    if (!root || !uri || !local) return NULL;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(c, &l, &p, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0 ||
            strcmp(l, "complexType") != 0)
            continue;
        const char* nm = leptris_element_attribute(c, "name");
        if (!nm || strcmp(nm, local) != 0) continue;
        LeptrisElement schema = leptris_element_get_parent(c);
        const char* tns = schema
            ? leptris_element_attribute(schema, "targetNamespace")
            : NULL;
        if (strcmp(uri, tns ? tns : "") == 0) return c;
    }
    return NULL;
}

static int xsd_check_derivation(struct leptris_xsd_schema* s,
                                LeptrisElement root) {
    if (!root) return 0;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ct = (LeptrisElement)n;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(ct, &l, &p, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0 ||
            strcmp(l, "complexType") != 0)
            continue;
        LeptrisElement cc = xsd_find_child(ct, "complexContent");
        if (!cc) continue;
        LeptrisElement ext = xsd_find_child(cc, "extension");
        LeptrisElement res = xsd_find_child(cc, "restriction");
        LeptrisElement deriv = ext ? ext : res;
        if (!deriv) continue;
        int is_extension = (deriv == ext);
        const char* base_q = leptris_element_attribute(deriv, "base");
        if (!base_q || !base_q[0]) continue;
        char buri[256], blocal[128];
        xsd_resolve_qname(ct, base_q, buri, sizeof(buri), blocal,
                          sizeof(blocal));
        if (!blocal[0]) continue;
        if (strcmp(buri, XSD_NS) == 0) continue; /* builtin base */
        LeptrisElement base = xsd_find_global_ct(root, buri, blocal);
        if (!base) continue; /* cross-document: capture handles it */

        if (is_extension) {
            /* cos-ct-derived-ok: the content types must agree. */
            LeptrisElement cc_b = xsd_find_child(base, "complexContent");
            LeptrisElement sc_b = xsd_find_child(base, "simpleContent");
            int d_mixed = xsd_mixed_flag(ct);
            int b_mixed = cc_b ? xsd_mixed_flag(base) : 0;
            if ((sc_b && !cc_b) || (cc_b && d_mixed != b_mixed) ||
                (!cc_b && !sc_b && d_mixed)) {
                xsd_set_error(
                    s,
                    "derivation-ok: the content type of both, the "
                    "type and its base type, must either 'mixed' or "
                    "'element-only'");
                return -1;
            }
        } else {
            /* derivation-ok-restriction-4.1: attribute wildcards. */
            LeptrisElement r_any = xsd_find_any_attribute(deriv);
            if (!r_any) continue;
            LeptrisElement b_any = xsd_find_any_attribute(base);
            if (!b_any) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "derivation-ok-restriction-4.1: the complex "
                         "type definition has an attribute wildcard, "
                         "but the base complex type definition '%s' "
                         "does not have one",
                         blocal);
                xsd_set_error(s, msg);
                return -1;
            }
            const char* r_ns =
                leptris_element_attribute(r_any, "namespace");
            const char* b_ns =
                leptris_element_attribute(b_any, "namespace");
            if (!r_ns || !r_ns[0]) r_ns = "##any";
            if (!b_ns || !b_ns[0]) b_ns = "##any";
            if (strcmp(b_ns, "##any") == 0) continue;
            if (strcmp(r_ns, "##any") != 0 &&
                strcmp(r_ns, b_ns) != 0) {
                /* token-list subset: every derived token must be in
                 * the base list (##other/##local compared verbatim;
                 * the corpus shapes are plain lists). */
                char bcopy[256];
                snprintf(bcopy, sizeof(bcopy), "%s", b_ns);
                int subset = 1;
                char rcopy[256];
                snprintf(rcopy, sizeof(rcopy), "%s", r_ns);
                for (char* tok = strtok(rcopy, " "); tok;
                     tok = strtok(NULL, " ")) {
                    int found = 0;
                    char* save = NULL;
                    char* bt = strtok_r(bcopy, " ", &save);
                    while (bt) {
                        if (strcmp(bt, tok) == 0) {
                            found = 1;
                            break;
                        }
                        bt = strtok_r(NULL, " ", &save);
                    }
                    if (!found) {
                        subset = 0;
                        break;
                    }
                    snprintf(bcopy, sizeof(bcopy), "%s", b_ns);
                }
                if (!subset) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                             "derivation-ok-restriction-4.1: the "
                             "attribute wildcard is not a valid "
                             "subset of the wildcard in the base "
                             "complex type definition '%s'",
                             blocal);
                    xsd_set_error(s, msg);
                    return -1;
                }
            }
        }
    }
    return 0;
}

/* ---- #1691 slice 6: UPA (Unique Particle Attribution) -------------
 *
 * The oracle rejects non-deterministic content models at load
 * (deter0_0: (odd,even)* followed by odd?; any4_0: a wildcard
 * choice branch overlapping an explicit element). The check that
 * catches the real-world shapes without building a full derivative
 * machine:
 *   - choice/all branches: pairwise FIRST-set overlap;
 *   - sequences: a child that can repeat or be skipped may not have
 *     a FIRST overlap with the union of its siblings' FIRSTs
 *     ((a,a) stays deterministic; (a?,a) and (a*,a) do not).
 * FIRST entries are (name, wildcard-ns-class) pairs; a wildcard
 * overlaps a name when its namespace set covers the name's
 * namespace (the schema's targetNamespace for bare names). */

#define XSD_UPA_MAX 64

struct xsd_upa_first {
    const char* name;   /* local name; NULL for a pure wildcard */
    char ns[256];       /* the name's namespace ("" unqualified) */
    char wc[256];       /* wildcard namespace spec ("" if not wc) */
};

static int xsd_upa_ns_covers(const char* wc, const char* ns,
                             const char* tns) {
    if (!wc || !wc[0]) return 0;
    if (strcmp(wc, "##any") == 0) return 1;
    if (strcmp(wc, "##other") == 0)
        /* any namespace OTHER than the schema's targetNamespace and
         * absent from no-namespace */
        return ns && ns[0] && (!tns || !tns[0] || strcmp(ns, tns) != 0);
    if (strcmp(wc, "##targetNamespace") == 0)
        return ns && ns[0] && tns && strcmp(ns, tns) == 0;
    if (strcmp(wc, "##local") == 0) return !ns || !ns[0];
    return ns && ns[0] && strstr(wc, ns) != NULL;
}

static int xsd_upa_first_overlap(const struct xsd_upa_first* a,
                                 const struct xsd_upa_first* b,
                                 const char* tns, int relax_wildcard) {
    if (a->wc[0] && b->wc[0]) {
        /* two wildcards overlap unless their sets are disjoint —
         * conservatively report any pair carrying ##any, or equal
         * specs */
        if (strcmp(a->wc, "##any") == 0 || strcmp(b->wc, "##any") == 0 ||
            strcmp(a->wc, b->wc) == 0)
            return 1;
        return 0;
    }
    if (a->wc[0])
        /* XSD 1.1 (§3.8.6.4) relaxes UPA: an element particle and a
         * wildcard particle may COMPETE (the element wins the
         * attribution) — open034's wildcard after a+ is legal 1.1.
         * The 1.0 posture keeps the strict overlap. */
        return b->name
                   ? (relax_wildcard
                          ? 0
                          : xsd_upa_ns_covers(a->wc, b->ns, tns))
                   : 0;
    if (b->wc[0])
        return a->name
                   ? (relax_wildcard
                          ? 0
                          : xsd_upa_ns_covers(b->wc, a->ns, tns))
                   : 0;
    if (!a->name || !b->name) return 0;
    if (strcmp(a->name, b->name) != 0) return 0;
    const char* ans = a->ns[0] ? a->ns : (tns ? tns : "");
    const char* bns = b->ns[0] ? b->ns : (tns ? tns : "");
    return strcmp(ans, bns) == 0;
}

static int xsd_upa_nullable(LeptrisElement e, int depth);
static int xsd_upa_first(LeptrisElement e, struct xsd_upa_first* out,
                         int max, int* n, const char* tns, int depth);

static void xsd_upa_name_of(LeptrisElement e, const char* tns,
                            struct xsd_upa_first* f) {
    memset(f, 0, sizeof(*f));
    const char* ref = leptris_element_attribute(e, "ref");
    const char* nm = leptris_element_attribute(e, "name");
    f->name = (ref && ref[0]) ? strrchr(ref, ':') ? strrchr(ref, ':') + 1
                                                  : ref
              : (nm ? nm : NULL);
    (void)tns;
    if (f->name && strchr(f->name, ':')) {
        /* prefixed literal name — strip for the local compare */
        f->name = strrchr(f->name, ':') + 1;
    }
}

static int xsd_upa_first(LeptrisElement e, struct xsd_upa_first* out,
                         int max, int* n, const char* tns, int depth) {
    if (!e || depth > 24 || *n >= max) return 0;
    const char* l = NULL;
    const char* p2 = NULL;
    const char* u = NULL;
    leptris_element_expanded_name(e, &l, &p2, &u);
    if (!l || !u || strcmp(u, XSD_NS) != 0) return 0;

    if (strcmp(l, "element") == 0) {
        struct xsd_upa_first f;
        xsd_upa_name_of(e, tns, &f);
        /* the element's namespace: the schema's targetNamespace for
         * qualified globals/locals (form=qualified is the corpus
         * shape); unqualified names carry "" */
        const char* form = leptris_element_attribute(e, "form");
        snprintf(f.ns, sizeof(f.ns), "%s",
                 (form && strcmp(form, "unqualified") == 0)
                     ? ""
                     : (tns ? tns : ""));
        if (f.name && *n < max) out[(*n)++] = f;
        return 0;
    }
    if (strcmp(l, "any") == 0) {
        struct xsd_upa_first f;
        memset(&f, 0, sizeof(f));
        const char* ns = leptris_element_attribute(e, "namespace");
        snprintf(f.wc, sizeof(f.wc), "%s",
                 (ns && ns[0]) ? ns : "##any");
        if (*n < max) out[(*n)++] = f;
        return 0;
    }
    if (strcmp(l, "sequence") == 0 || strcmp(l, "group") == 0) {
        for (LeptrisNodeRef c = leptris_node_first_child(
                 leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            xsd_upa_first((LeptrisElement)c, out, max, n, tns, depth + 1);
            if (!xsd_upa_nullable((LeptrisElement)c, depth + 1)) break;
        }
        return 0;
    }
    if (strcmp(l, "choice") == 0 || strcmp(l, "all") == 0) {
        for (LeptrisNodeRef c = leptris_node_first_child(
                 leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            xsd_upa_first((LeptrisElement)c, out, max, n, tns, depth + 1);
        }
        return 0;
    }
    /* optional wrappers (xs already models minOccurs on the particle
     * itself; xs:element with minOccurs=0 is handled by callers) */
    return 0;
}

static int xsd_upa_nullable(LeptrisElement e, int depth) {
    if (!e || depth > 24) return 0;
    const char* l = NULL;
    const char* p2 = NULL;
    const char* u = NULL;
    leptris_element_expanded_name(e, &l, &p2, &u);
    if (!l || !u || strcmp(u, XSD_NS) != 0) return 0;
    int mn = 1;
    const char* mn_s = leptris_element_attribute(e, "minOccurs");
    if (mn_s && strcmp(mn_s, "0") == 0) return 1;
    if (strcmp(l, "choice") == 0) {
        for (LeptrisNodeRef c = leptris_node_first_child(
                 leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c))
            if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT &&
                xsd_upa_nullable((LeptrisElement)c, depth + 1))
                return 1;
        return 0;
    }
    if (strcmp(l, "sequence") == 0) {
        for (LeptrisNodeRef c = leptris_node_first_child(
                 leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            if (!xsd_upa_nullable((LeptrisElement)c, depth + 1))
                return 0;
        }
        return 1;
    }
    return 0;
}

static int xsd_upa_check_node(struct leptris_xsd_schema* s,
                              LeptrisElement e, const char* tns,
                              int depth, int relax_wildcard);

static int xsd_upa_collect_children(LeptrisElement e, LeptrisElement* kids,
                                    int max) {
    int n = 0;
    for (LeptrisNodeRef c = leptris_node_first_child(
             leptris_element_as_node(e));
         c && n < max; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* kl = NULL;
        const char* kp = NULL;
        const char* ku = NULL;
        leptris_element_expanded_name((LeptrisElement)c, &kl, &kp, &ku);
        if (!kl || !ku || strcmp(ku, XSD_NS) != 0) continue;
        if (strcmp(kl, "annotation") == 0) continue;
        kids[n++] = (LeptrisElement)c;
    }
    return n;
}

static int xsd_upa_particle_can_repeat_or_skip(LeptrisElement c) {
    const char* mn = leptris_element_attribute(c, "minOccurs");
    const char* mx = leptris_element_attribute(c, "maxOccurs");
    if (mn && strcmp(mn, "0") == 0) return 1;
    if (mx && strcmp(mx, "unbounded") == 0) return 1;
    if (mx && mx[0] && strcmp(mx, "1") != 0) {
        long v = strtol(mx, NULL, 10);
        if (v != 1) return 1;
    }
    /* an optional GROUP: its restart can collide with the sibling */
    const char* l = NULL;
    const char* p2 = NULL;
    const char* u = NULL;
    leptris_element_expanded_name(c, &l, &p2, &u);
    return 0;
}

static int xsd_upa_check_node(struct leptris_xsd_schema* s,
                              LeptrisElement e, const char* tns,
                              int depth, int relax_wildcard) {
    if (!e || depth > 24) return 0;
    const char* l = NULL;
    const char* p2 = NULL;
    const char* u = NULL;
    leptris_element_expanded_name(e, &l, &p2, &u);
    if (!l || !u || strcmp(u, XSD_NS) != 0) return 0;
    int is_choice = (strcmp(l, "choice") == 0);
    int is_all = (strcmp(l, "all") == 0);
    int is_seq = (strcmp(l, "sequence") == 0);

    LeptrisElement kids[64];
    int nk = xsd_upa_collect_children(e, kids, 64);

    if ((is_choice || is_all) && nk >= 2) {
        for (int i = 0; i < nk; i++) {
            struct xsd_upa_first fi[XSD_UPA_MAX];
            int ni = 0;
            xsd_upa_first(kids[i], fi, XSD_UPA_MAX, &ni, tns, depth + 1);
            for (int j = i + 1; j < nk; j++) {
                struct xsd_upa_first fj[XSD_UPA_MAX];
                int nj = 0;
                xsd_upa_first(kids[j], fj, XSD_UPA_MAX, &nj, tns,
                              depth + 1);
                for (int a = 0; a < ni; a++)
                    for (int b = 0; b < nj; b++) {
                        if (xsd_upa_first_overlap(&fi[a], &fj[b], tns, relax_wildcard)) {
                            xsd_set_error(
                                s,
                                "upa: the content model is not "
                                "deterministic: ambiguous choice "
                                "branches");
                            return -1;
                        }
                    }
            }
        }
    }
    if (is_seq) {
        for (int i = 0; i + 1 < nk; i++) {
            if (!xsd_upa_particle_can_repeat_or_skip(kids[i])) continue;
            struct xsd_upa_first fi[XSD_UPA_MAX];
            int ni = 0;
            xsd_upa_first(kids[i], fi, XSD_UPA_MAX, &ni, tns, depth + 1);
            /* the union of every LATER sibling's FIRST — after the
             * repeating/skippable child, any of them may come next */
            struct xsd_upa_first rest[XSD_UPA_MAX];
            int nr = 0;
            for (int j = i + 1; j < nk; j++)
                xsd_upa_first(kids[j], rest, XSD_UPA_MAX, &nr, tns,
                              depth + 1);
            for (int a = 0; a < ni; a++)
                for (int b = 0; b < nr; b++) {
                    if (xsd_upa_first_overlap(&fi[a], &rest[b], tns, relax_wildcard)) {
                        xsd_set_error(
                            s,
                            "upa: the content model is not "
                            "deterministic: an optional or repeating "
                            "particle overlaps a following particle");
                        return -1;
                    }
                }
        }
    }
    for (int i = 0; i < nk; i++)
        if (xsd_upa_check_node(s, kids[i], tns, depth + 1, relax_wildcard) != 0)
            return -1;
    return 0;
}

/* #2184: a wildcard spelling ##definedSibling structurally cannot
 * overlap its parent's element particles — the §3.8.6.4 relaxation
 * MUST arm for such models (the defined-sibling expansion runs
 * post-capture, after this document-level gate). General
 * notQName/notNamespace spellings do NOT arm it: all305's and
 * s3_3_6si03's overlap invalidity depends on the strict posture. */
static int xsd_doc_has_defined_sibling(LeptrisElement root) {
    if (!root) return 0;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(e, &l, NULL, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0) continue;
        if ((strcmp(l, "any") == 0 || strcmp(l, "anyAttribute") == 0)) {
            const char* nq = leptris_element_attribute(e, "notQName");
            if (nq && strstr(nq, "##definedSibling")) return 1;
        }
        /* nested models (complexType bodies, groups) */
        for (LeptrisNodeRef c = leptris_node_first_child(
                 leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            if (xsd_doc_has_defined_sibling((LeptrisElement)c))
                return 1;
        }
    }
    return 0;
}

static int xsd_check_upa(struct leptris_xsd_schema* s,
                         LeptrisElement root) {
    if (!root) return 0;
    const char* tns = s->target_ns;
    /* XSD 1.1 §3.8.6.4 relaxes the wildcard-vs-element overlap;
     * arm the relaxation on any 1.1 evidence (open034 is unmarked).
     * #2151; #2184 adds the ##definedSibling spellings. */
    int relax_wildcard = (s->version_11 || s->saw_open_content ||
                          xsd_doc_has_defined_sibling(root));
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        if (xsd_upa_check_node(s, (LeptrisElement)n, tns, 0,
                               relax_wildcard) != 0)
            return -1;
    }
    return 0;
}

/* ---- #1691 slice 5: element/attribute/group ref resolution -------
 *
 * The oracle (libxml2 2.9.13) rejects a schema whose xs:element/@ref
 * (or attribute, group, attributeGroup) QName does not resolve to a
 * declaration (import1_0: the import's schemaLocation file is
 * missing, so {http://BAR}bar.B resolves to nothing). Resolution is
 * by DECLARATION, not by namespace: the available set records every
 * loaded document's root-level declarations keyed by that
 * document's effective target namespace (chameleon includes adopt
 * the includer's). A namespace-only import or a missing file
 * provides nothing. type=/base= QNames stay out of scope: the
 * oracle fails to resolve spec-valid xs: builtins (length3_0's
 * xs:non-positive-integer), so its type verdicts are not
 * byte-matchable without codifying that gap. */

struct xsd_decl {
    char uri[256];
    char name[128];
};



static void xsd_decls_add(struct xsd_decl* decls, int* n, int max,
                          const char* uri, const char* name) {
    if (!name || !name[0] || *n >= max) return;
    for (int i = 0; i < *n; i++) {
        if (strcmp(decls[i].name, name) == 0 &&
            strcmp(decls[i].uri, uri ? uri : "") == 0)
            return;
    }
    snprintf(decls[*n].uri, sizeof(decls[*n].uri), "%s", uri ? uri : "");
    snprintf(decls[*n].name, sizeof(decls[*n].name), "%s", name);
    (*n)++;
}

static int xsd_decls_contain(struct xsd_decl* decls, int n,
                             const char* uri, const char* name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(decls[i].name, name) == 0 &&
            strcmp(decls[i].uri, uri ? uri : "") == 0)
            return 1;
    }
    return 0;
}

static void xsd_dirname_into(const char* path, char* out, size_t outsz) {
    const char* slash = strrchr(path, '/');
    const char* bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (!slash) {
        out[0] = 0;
        return;
    }
    size_t bl = (size_t)(slash - path);
    if (bl >= outsz) bl = outsz - 1;
    memcpy(out, path, bl);
    out[bl] = 0;
}

/* claimed[] tracks the namespaces an import already served — the
 * oracle serves the FIRST import whose schemaLocation loads per
 * namespace and skips the rest ("already imported with the schema
 * located at ..."), so later docs' declarations must not
 * resolve. */
static void xsd_collect_decls_doc(LeptrisElement root, const char* eff_tns,
                                  const char* base_dir,
                                  struct xsd_decl* decls, int* n, int max,
                                  char (*claimed)[256], int* nclaimed,
                                  int cmax, int depth) {
    if (!root || depth > 8 || *n >= max) return;
    for (LeptrisNodeRef k = leptris_node_first_child(
             leptris_element_as_node(root));
         k; k = leptris_node_next_sibling(k)) {
        if (leptris_node_get_type(k) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* kl = NULL;
        const char* kp = NULL;
        const char* ku = NULL;
        leptris_element_expanded_name((LeptrisElement)k, &kl, &kp, &ku);
        if (!kl || !ku || strcmp(ku, XSD_NS) != 0) continue;
        if (strcmp(kl, "element") == 0 || strcmp(kl, "attribute") == 0 ||
            strcmp(kl, "group") == 0 ||
            strcmp(kl, "attributeGroup") == 0) {
            const char* nm = leptris_element_attribute(
                (LeptrisElement)k, "name");
            xsd_decls_add(decls, n, max, eff_tns, nm);
        }
    }
    /* cn, not n: the count pointer `n` is live across this loop —
     * shadowing it passed the loop node as the decls count and the
     * decls_add increment wrote node memory (#1717). */
    for (LeptrisNodeRef cn = leptris_node_first_child(
             leptris_element_as_node(root));
         cn; cn = leptris_node_next_sibling(cn)) {
        if (leptris_node_get_type(cn) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)cn;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(c, &l, &p, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0)
            continue;
        int is_inc = (strcmp(l, "include") == 0 ||
                      strcmp(l, "redefine") == 0 ||
                      strcmp(l, "override") == 0);
        int is_imp = (strcmp(l, "import") == 0);
        if (!is_inc && !is_imp) continue;
        const char* loc = leptris_element_attribute(c, "schemaLocation");
        if (!loc || !base_dir) continue; /* provides nothing */
        /* The import-namespace claim: the first LOADABLE import
         * per namespace wins; later ones are skipped. */
        char imp_ns[256];
        imp_ns[0] = 0;
        if (is_imp) {
            const char* ns = leptris_element_attribute(c, "namespace");
            snprintf(imp_ns, sizeof(imp_ns), "%s", ns ? ns : "");
            int seen = 0;
            for (int ci = 0; ci < *nclaimed; ci++)
                if (strcmp(claimed[ci], imp_ns) == 0) seen = 1;
            if (seen) continue; /* skipped import: provides nothing */
        }
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", base_dir, loc);
        size_t flen = 0;
        char* text = xsd_read_file(path, &flen);
        if (!text) continue; /* missing file: provides nothing */
        LeptrisDocument sub = leptris_parse_string(text, flen, NULL);
        free(text);
        if (!sub) continue;
        LeptrisElement subroot = leptris_document_root(sub);
        if (is_imp && *nclaimed < cmax) {
            int seen = 0;
            for (int ci = 0; ci < *nclaimed; ci++)
                if (strcmp(claimed[ci], imp_ns) == 0) seen = 1;
            if (!seen)
                snprintf(claimed[(*nclaimed)++], 256, "%s", imp_ns);
        }
        if (subroot) {
            const char* sub_tns = leptris_element_attribute(
                subroot, "targetNamespace");
            const char* eff = is_inc ? (sub_tns ? sub_tns : eff_tns)
                                     : sub_tns;
            char sub_base[4096];
            xsd_dirname_into(path, sub_base, sizeof(sub_base));
            xsd_collect_decls_doc(subroot, eff, sub_base, decls, n, max,
                                  claimed, nclaimed, cmax, depth + 1);
        }
        /* leptris#1913/#1926: override and redefine bodies'
         * declarations belong to the symbol space too — walk the
         * derivation element as a pseudo-root (it has no
         * include/import children of its own). */
        if (strcmp(l, "override") == 0 || strcmp(l, "redefine") == 0)
            xsd_collect_decls_doc(c, eff_tns, base_dir, decls, n, max,
                                  claimed, nclaimed, cmax, depth + 1);
        leptris_document_free(sub);
    }
}

static int xsd_check_refs_walk(LeptrisElement root,
                               struct leptris_xsd_schema* s,
                               struct xsd_decl* decls, int ndecls) {
    if (!root) return 0;
    for (LeptrisNodeRef n = leptris_node_first_child(
             leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* l = NULL;
        const char* p = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(c, &l, &p, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0)
            continue;
        int is_ref_holder =
            (strcmp(l, "element") == 0 || strcmp(l, "attribute") == 0 ||
             strcmp(l, "group") == 0 || strcmp(l, "attributeGroup") == 0);
        if (!is_ref_holder) {
            if (xsd_check_refs_walk(c, s, decls, ndecls) != 0) return -1;
            continue;
        }
        const char* ref = leptris_element_attribute(c, "ref");
        if (!ref || !ref[0]) {
            if (xsd_check_refs_walk(c, s, decls, ndecls) != 0) return -1;
            continue;
        }
        char ruri[256], rlocal[128];
        xsd_resolve_qname(c, ref, ruri, sizeof(ruri), rlocal,
                          sizeof(rlocal));
        /* The XML namespace's attribute declarations are BUILT IN
         * (XSD 1.0 §3.15 / the xml.xsd the spec normatively
         * provides): xml:lang/space/base/id resolve without any
         * loaded declaration — a locationless
         * <xs:import namespace="...XML/1998/namespace"/> provides
         * nothing by design (leptris-ruby#478). */
        if (!(strcmp(ruri, "http://www.w3.org/XML/1998/namespace") == 0 &&
              (strcmp(rlocal, "lang") == 0 || strcmp(rlocal, "space") == 0 ||
               strcmp(rlocal, "base") == 0 || strcmp(rlocal, "id") == 0)) &&
            !xsd_decls_contain(decls, ndecls, ruri, rlocal)) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "src-element.2: The QName value '{%s}%s' does not "
                     "resolve to a(n) %s declaration",
                     ruri, rlocal,
                     strcmp(l, "attribute") == 0 ? "attribute"
                                                 : "element");
            xsd_set_error(s, msg);
            return -1;
        }
        if (xsd_check_refs_walk(c, s, decls, ndecls) != 0) return -1;
    }
    return 0;
}

static int xsd_check_refs(struct leptris_xsd_schema* s,
                          LeptrisElement root) {
    if (!root) return 0;
    struct xsd_decl decls[512];
    int ndecls = 0;
    char claimed[32][256];
    int nclaimed = 0;
    xsd_collect_decls_doc(root, s->target_ns, s->base_dir, decls,
                          &ndecls, 512, claimed, &nclaimed, 32, 0);
    return xsd_check_refs_walk(root, s, decls, ndecls);
}

/* ---- #2150 §4.3: xs:override -------------------------------------
 *
 * The document-level override checks, run pre-capture like the
 * include-tns and ref walks so failures propagate as schema errors:
 *
 * - src-override.1 (over016/over017): the overridden document's
 *   target namespace must EQUAL the overriding document's — the
 *   target-namespace-ABSENT overridden document is the only legal
 *   exception (the chameleon form; over019).
 * - src-override.2 (over021/over022): the overriding component set
 *   is not unique — one (kind, name) pair twice within a body, or
 *   across two bodies targeting the same document.
 * - src-override.3 (over024): mutual override is an error exactly
 *   when the inner override would REPLACE a component of the
 *   overriding document; the empty-body mutual form (over023) is
 *   legal. Requires the compiled file's own normalized path. */

static int xsd_ovr_comp_has(const struct xsd_ovr_name* set, size_t n,
                            const char* kind, const char* name) {
    if (!name || !name[0]) return 0;
    for (size_t i = 0; i < n; i++)
        if (strcmp(set[i].kind, kind) == 0 &&
            strcmp(set[i].name, name) == 0)
            return 1;
    return 0;
}

static void xsd_ovr_comp_add(struct xsd_ovr_name* set, size_t* n,
                             size_t max, const char* kind,
                             const char* name) {
    if (!name || !name[0] || *n >= max) return;
    if (xsd_ovr_comp_has(set, *n, kind, name)) return;
    snprintf(set[*n].kind, sizeof(set[*n].kind), "%s", kind);
    snprintf(set[*n].name, sizeof(set[*n].name), "%s", name);
    (*n)++;
}

/* A top-level component-kind declaration (the kinds override
 * replaces by name). */
static int xsd_ovr_is_component(const char* local) {
    return local && (strcmp(local, "element") == 0 ||
                     strcmp(local, "attribute") == 0 ||
                     strcmp(local, "simpleType") == 0 ||
                     strcmp(local, "complexType") == 0 ||
                     strcmp(local, "group") == 0 ||
                     strcmp(local, "attributeGroup") == 0 ||
                     strcmp(local, "notation") == 0);
}

/* The (kind, name) pairs one xs:override BODY declares. */
static void xsd_ovr_collect_body(LeptrisElement ovr,
                                 struct xsd_ovr_name* set, size_t* n,
                                 size_t max) {
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(ovr));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)c, &l, NULL, &u);
        if (!l || (u && strcmp(u, XSD_NS) != 0)) continue;
        if (!xsd_ovr_is_component(l)) continue;
        const char* nm =
            leptris_element_attribute((LeptrisElement)c, "name");
        xsd_ovr_comp_add(set, n, max, l, nm);
    }
}

/* The inner override targets THIS compiled file: an error when its
 * body would replace any of the overriding document's top-level
 * components (the empty body replaces nothing and stays legal). */
static int xsd_check_override_mutual(struct leptris_xsd_schema* s,
                                     LeptrisElement subroot,
                                     const char* sub_base_dir,
                                     const struct xsd_ovr_name* top,
                                     size_t ntop) {
    if (!s->self_path) return 0;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(subroot));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)c, &l, NULL, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0 ||
            strcmp(l, "override") != 0)
            continue;
        const char* loc = leptris_element_attribute((LeptrisElement)c,
                                                    "schemaLocation");
        if (!loc || !sub_base_dir) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", sub_base_dir, loc);
        char canon[4096];
        xsd_path_normalize(path, canon, sizeof(canon));
        if (strcmp(canon, s->self_path) != 0) continue;
        struct xsd_ovr_name inner[64];
        size_t ninner = 0;
        xsd_ovr_collect_body((LeptrisElement)c, inner, &ninner, 64);
        for (size_t i = 0; i < ninner; i++) {
            if (!xsd_ovr_comp_has(top, ntop, inner[i].kind,
                                  inner[i].name))
                continue;
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "src-override.3: mutual override — the overridden "
                     "schema's override would replace the overriding "
                     "schema's %s '%s'",
                     inner[i].kind, inner[i].name);
            xsd_set_error(s, msg);
            return -1;
        }
    }
    return 0;
}

static int xsd_check_override_docs(struct leptris_xsd_schema* s,
                                   LeptrisElement root) {
    if (!root) return 0;
    struct xsd_ovr_name top[128];
    size_t ntop = 0;
    struct xsd_ovr_name seen[128];
    size_t nseen = 0;
    /* the overriding document's own top-level components (the
     * mutual-conflict intersection set) */
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, NULL, &u);
        if (!l || (u && strcmp(u, XSD_NS) != 0)) continue;
        if (!xsd_ovr_is_component(l)) continue;
        xsd_ovr_comp_add(
            top, &ntop, 128, l,
            leptris_element_attribute((LeptrisElement)n, "name"));
    }
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, NULL, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0 ||
            strcmp(l, "override") != 0)
            continue;
        /* src-override.2: the overriding set must be unique across
         * every root-level override body of this document. The walk
         * is RAW (no dedup): two same-named children in ONE body
         * are already the violation (over021). */
        for (LeptrisNodeRef c =
                 leptris_node_first_child(
                     leptris_element_as_node((LeptrisElement)n));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            const char* cl = NULL;
            const char* cu = NULL;
            leptris_element_expanded_name((LeptrisElement)c, &cl, NULL,
                                          &cu);
            if (!cl || (cu && strcmp(cu, XSD_NS) != 0)) continue;
            if (!xsd_ovr_is_component(cl)) continue;
            const char* cnm = leptris_element_attribute(
                (LeptrisElement)c, "name");
            if (!cnm || !cnm[0]) continue;
            if (xsd_ovr_comp_has(seen, nseen, cl, cnm)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "src-override.2: the component %s '%s' is "
                         "overridden more than once",
                         cl, cnm);
                xsd_set_error(s, msg);
                return -1;
            }
            xsd_ovr_comp_add(seen, &nseen, 128, cl, cnm);
        }
        const char* loc = leptris_element_attribute((LeptrisElement)n,
                                                    "schemaLocation");
        if (!loc || !s->base_dir) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", s->base_dir, loc);
        char canon[4096];
        xsd_path_normalize(path, canon, sizeof(canon));
        size_t flen = 0;
        char* text = xsd_read_file(path, &flen);
        if (!text) continue;
        LeptrisDocument sub = leptris_parse_string(text, flen, NULL);
        free(text);
        if (!sub) continue;
        LeptrisElement subroot = leptris_document_root(sub);
        if (!subroot) {
            leptris_document_free(sub);
            continue;
        }
        /* src-override.1: tns alignment */
        const char* sub_tns =
            leptris_element_attribute(subroot, "targetNamespace");
        if (sub_tns && (!s->target_ns ||
                        strcmp(sub_tns, s->target_ns) != 0)) {
            char msg[512];
            if (s->target_ns)
                snprintf(msg, sizeof(msg),
                         "src-override.1: the overridden schema declares "
                         "target namespace '%s' but the overriding "
                         "schema's target namespace is '%s'",
                         sub_tns, s->target_ns);
            else
                snprintf(msg, sizeof(msg),
                         "src-override.1: the overridden schema declares "
                         "target namespace '%s' but the overriding "
                         "schema declares none",
                         sub_tns);
            xsd_set_error(s, msg);
            leptris_document_free(sub);
            return -1;
        }
        /* src-override.3: the mutual-override conflict */
        char sub_base[4096];
        xsd_dirname_into(canon, sub_base, sizeof(sub_base));
        int rc = xsd_check_override_mutual(s, subroot, sub_base, top,
                                           ntop);
        leptris_document_free(sub);
        if (rc != 0) return -1;
    }
    return 0;
}

/* ---- #2150 §4.3.2: post-capture override semantics ---------------
 *
 * Overriding references re-resolve against the POST-merge set: a
 * kept component's type reference to a component that is absent
 * from the merged schema — including one the merge IGNORED because
 * it overrides nothing (over026) — is a schema error. Only the
 * overriding document's root-level bodies walk: the direct type
 * references of the replaced declarations (element/attribute @type,
 * the derivation @base of the body's own types). */

static int xsd_ovr_type_resolves(struct leptris_xsd_schema* s,
                                 LeptrisElement e, const char* qname) {
    if (!qname || !qname[0]) return 1; /* absent = anySimpleType */
    const char* ln = xsd_local_name(qname);
    /* builtin EXISTENCE under any spelling (xsd: canonized,
     * unprefixed canonized at capture since #2155) */
    if (strncmp(ln, "xs:", 3) == 0) {
        if (xsd_builtin_exists(ln)) return 1;
        /* xs:-prefixed but unknown: fall through to the local
         * ladders (an unprefixed local type named like the builtin
         * keeps its resolution) */
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "xs:%s", ln);
        if (xsd_builtin_exists(buf)) return 1;
    }
    char ruri[256], rlocal[128];
    xsd_resolve_qname(e, qname, ruri, sizeof(ruri), rlocal,
                      sizeof(rlocal));
    if (xsd_find_complex_ns(s, ln, ruri)) return 1;
    if (xsd_find_complex(s, ln)) return 1;
    if (xsd_find_simple_ns_pub(s, ln, ruri)) return 1;
    if (xsd_find_simple(s, ln)) return 1;
    return 0;
}

static int xsd_check_override_refs_body(struct leptris_xsd_schema* s,
                                        LeptrisElement ovr) {
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(ovr));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement d = (LeptrisElement)c;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name(d, &l, NULL, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0) continue;
        const char* ty = NULL;
        if (strcmp(l, "element") == 0 || strcmp(l, "attribute") == 0) {
            ty = leptris_element_attribute(d, "type");
        } else if (strcmp(l, "simpleType") == 0 ||
                   strcmp(l, "complexType") == 0) {
            /* the body's own derivation base (one hop) */
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(d));
                 q && !ty; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (!ql) continue;
                if (strcmp(ql, "restriction") == 0 ||
                    strcmp(ql, "extension") == 0 ||
                    strcmp(ql, "list") == 0) {
                    ty = leptris_element_attribute(
                        qe, strcmp(ql, "list") == 0 ? "itemType"
                                                    : "base");
                } else if (strcmp(ql, "complexContent") == 0 ||
                           strcmp(ql, "simpleContent") == 0) {
                    for (LeptrisNodeRef r = leptris_node_first_child(
                             leptris_element_as_node(qe));
                         r && !ty; r = leptris_node_next_sibling(r)) {
                        if (leptris_node_get_type(r) !=
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            continue;
                        LeptrisElement re = (LeptrisElement)r;
                        const char* rl = NULL;
                        leptris_element_expanded_name(
                            re, &rl, NULL, NULL);
                        if (rl && (strcmp(rl, "restriction") == 0 ||
                                   strcmp(rl, "extension") == 0))
                            ty = leptris_element_attribute(re, "base");
                    }
                }
            }
        }
        if (ty && ty[0] && !xsd_ovr_type_resolves(s, d, ty)) {
            char ruri[256], rlocal[128];
            xsd_resolve_qname(d, ty, ruri, sizeof(ruri), rlocal,
                              sizeof(rlocal));
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "src-override.4: the QName value '{%s}%s' does "
                     "not resolve in the merged schema (a component "
                     "that overrides nothing is ignored)",
                     ruri, rlocal);
            xsd_set_error(s, msg);
            return -1;
        }
    }
    return 0;
}

static int xsd_check_override_refs(struct leptris_xsd_schema* s,
                                   LeptrisElement root) {
    if (!root) return 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, NULL, &u);
        if (!l || !u || strcmp(u, XSD_NS) != 0 ||
            strcmp(l, "override") != 0)
            continue;
        if (xsd_check_override_refs_body(s, (LeptrisElement)n) != 0)
            return -1;
    }
    return 0;
}

/* §4.3.2 (over011/over014): an override's references resolve
 * against the POST-merge set, so a replacement whose derivation
 * base names its own (replaced) name restricts ITSELF — circular.
 * OVERRIDE-SCOPED: the 1.0 redefine hook legitimately re-derives a
 * type from its own original (the open042/044 shapes) and plain
 * self-named restrictions are the derivation lanes' pre-existing
 * classes — only schemas that captured an override body check. */
static int xsd_check_derivation_cycles(struct leptris_xsd_schema* s) {
    if (!s->override_bodies) return 0;
    for (XsdSimple* t = s->simple_types; t; t = t->next) {
        if (!t->name || !t->base) continue;
        if (strncmp(t->base, "xs:", 3) == 0) continue;
        if (strcmp(t->base, t->name) != 0) continue;
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "circular derivation: the restriction base of "
                 "'%s' names its own definition",
                 t->name);
        xsd_set_error(s, msg);
        return -1;
    }
    for (XsdCm* c = s->complex_types; c; c = c->next) {
        if (!c->name) continue;
        const char* rb = c->ext_base ? c->ext_base : c->res_base;
        if (!rb || strcmp(rb, c->name) != 0) continue;
        const char* bns =
            c->ext_base_ns ? c->ext_base_ns : c->res_base_ns;
        /* a same-LOCAL-name base in another namespace is another
         * component, not a self-reference */
        if (bns && c->ns && strcmp(bns, c->ns) != 0) continue;
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "circular derivation: the %s base of '%s' names its "
                 "own definition",
                 c->ext_base ? "extension" : "restriction", c->name);
        xsd_set_error(s, msg);
        return -1;
    }
    return 0;
}

/* ---- #2185: the derivation-ok attribute-use clauses ----------------
 *
 * The oracle (libxml2 2.9.13) enforces two attribute families over
 * the derived types that the capture pass never sees. Both run over
 * the POST-capture tables (extension folds in, pending refs
 * resolved, default groups applied), so cross-document and
 * deferred-base shapes ride the same comparison:
 *
 *   - duplicate attribute use (derivation-ok-extension-err): an
 *     extension's OWN row re-declaring a FOLDED base row by expanded
 *     name fails the schema — unless the own row is use="prohibited",
 *     which removes the base use instead of duplicating it
 *     (derivation-ok-extension_0's legal twin shape).
 *
 *   - restriction attribute-use consistency (derivation-ok-
 *     restriction-2.1.1/2.2, restriction-attr1): every derived row
 *     must match a NON-prohibited base row by expanded name. A base
 *     REQUIRED use cannot weaken — a derived optional OR prohibited
 *     row against it fails 2.1.1. A derived optional/required row
 *     with no matching base use fails 2.2 (a prohibited base row is
 *     not in the base {attribute uses}, so it never matches); a
 *     derived prohibited row with no match is the redundant
 *     prohibition and stays legal. */
static const XsdTypeAttrs* xsd_derivation_type_attrs(
    struct leptris_xsd_schema* s, const XsdCm* c) {
    if (!c || !c->name) return NULL;
    const XsdTypeAttrs* ta = xsd_find_type_attrs_ns(s, c->name, c->ns);
    if (!ta) ta = xsd_find_type_attrs(s, c->name);
    return ta;
}

static int xsd_check_derivation_ok_attrs(struct leptris_xsd_schema* s) {
    for (XsdCm* c = s->complex_types; c; c = c->next) {
        const XsdTypeAttrs* ta = xsd_derivation_type_attrs(s, c);
        if (!ta) continue;
        /* duplicate attribute use on the POST-fold table */
        for (const XsdAttrDecl* own = ta->attrs; own; own = own->next) {
            if (own->from_base || own->prohibited || !own->name)
                continue;
            for (const XsdAttrDecl* b = ta->attrs; b; b = b->next) {
                if (!b->from_base || b == own || !b->name) continue;
                if (!xsd_attr_rows_same_name(own, b)) continue;
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "duplicate attribute use: the extension of "
                         "'%s' re-declares attribute '%s'",
                         c->name, own->name);
                xsd_set_error(s, msg);
                return -1;
            }
        }
        /* restriction attribute-use consistency */
        if (!c->res_base) continue;
        XsdCm* base = xsd_find_complex_ns(s, c->res_base,
                                          c->res_base_ns ? c->res_base_ns
                                                         : c->ns);
        if (!base) base = xsd_find_complex_ns(s, c->res_base, c->ns);
        if (!base) continue; /* unresolved: the ref check owns it */
        const XsdTypeAttrs* ba = xsd_derivation_type_attrs(s, base);
        if (!ba) continue;
        for (const XsdAttrDecl* d = ta->attrs; d; d = d->next) {
            if (!d->name) continue;
            const XsdAttrDecl* m = NULL;
            for (const XsdAttrDecl* b = ba->attrs; b; b = b->next) {
                /* a prohibited base row is not in {attribute uses} */
                if (b->prohibited || !b->name) continue;
                if (xsd_attr_rows_same_name(d, b)) {
                    m = b;
                    break;
                }
            }
            if (!m) {
                /* 2.2's wildcard arm: a derived use with no matching
                 * base use is legal when the base's attribute
                 * wildcard admits the row's namespace (ns5/tns5's
                 * bare <xs:anyAttribute/> base, target003's URI-list
                 * wildcard). */
                if (d->prohibited) continue; /* redundant removal */
                const char* bunit = ba->any_attr_unit_ns
                                        ? ba->any_attr_unit_ns
                                        : xsd_target_ns(s);
                if (xsd_any_attr_matches(ba->any_attr_ns, d->ns,
                                         bunit))
                    continue;
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "derivation-ok-restriction-2.2: attribute "
                         "'%s' of '%s' has no matching use in the "
                         "base '%s'",
                         d->name, c->name, c->res_base);
                xsd_set_error(s, msg);
                return -1;
            }
            if (m->required && !d->required) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "derivation-ok-restriction-2.1.1: attribute "
                         "'%s' of '%s' weakens a required use in the "
                         "base '%s'",
                         d->name, c->name, c->res_base);
                xsd_set_error(s, msg);
                return -1;
            }
            /* #2184 §3.2.7.1 / cos-st-restricts 1.1: the derived
             * use's {inheritable} must EQUAL the base use's — a
             * restriction may not flip it (cta9004: true→false,
             * cta9005: unset→true). An unset flag reads as false
             * (the property's default). 1.1-only: the property does
             * not exist in 1.0, so the check rides the 1.1-evidence
             * posture. */
            if ((s->version_11 || s->saw_alternative || s->saw_assert) &&
                (d->inheritable >= 0 ? d->inheritable : 0) !=
                    (m->inheritable >= 0 ? m->inheritable : 0)) {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "cos-st-restricts: attribute '%s' of '%s' "
                         "changes the base use's inheritable in '%s'",
                         d->name, c->name, c->res_base);
                xsd_set_error(s, msg);
                return -1;
            }
        }
    }
    return 0;
}

/* ---- #2184: the alternative-table compile constraints -------------
 *
 * §3.12.4's dispatch assumes a table the compile side never checked.
 * The sweep runs post-capture (the type-table captures and the
 * derivation folds have landed) and enforces:
 *
 *   - §3.12.2 XPath valid: an alternative test that does not parse
 *     fails the SCHEMA. Evaluation errors stay the §3.12.4 "false"
 *     channel (Xsd11Cta.TestEvaluationErrorIsFalse) — the s3_12si04/
 *     si05/si06 shapes.
 *
 *   - §3.3.6.1 cl.7 / cos-ct-type-substitutable: every alternative's
 *     type derives from the element's declared type (by extension or
 *     restriction — through the simpleContent text type for simple
 *     bases), or is xs:error, or the declared type is the ur-type.
 *     The s3_12si02/si03 + cta9008err shapes.
 *
 *   - cos-element-consistent (§3.8.6): two same-name particles in
 *     one content model must carry EQUAL type tables — cta9009err's
 *     third row and cta9010err's table-vs-no-table pair. The
 *     comparison skips chains with inline alternative types (their
 *     synthesized slot names are node addresses; a structural
 *     comparison is out of scope here and the legal corpus does not
 *     restate inline tables).
 *
 *   - particle restriction: a complexContent restriction's same-name
 *     particle must preserve the base particle's type table —
 *     cta0043's narrowed stamp table.
 *
 * The src-element.5 mid-table test-less form is flagged at capture
 * (the chain truncates there) and surfaced here via ct_failed. */

/* Builtin derivation edges (derived ← base). The atomic root and
 * anySimpleType are handled analytically below; the list builtins
 * derive through their item types. */
static const struct { const char* d; const char* b; } k_builtin_base[] = {
    {"xs:anyAtomicType", "xs:anySimpleType"},
    {"xs:normalizedString", "xs:string"},
    {"xs:token", "xs:normalizedString"},
    {"xs:language", "xs:token"},
    {"xs:Name", "xs:token"},
    {"xs:NCName", "xs:Name"},
    {"xs:ID", "xs:NCName"},
    {"xs:IDREF", "xs:NCName"},
    {"xs:ENTITY", "xs:NCName"},
    {"xs:NMTOKEN", "xs:token"},
    {"xs:IDREFS", "xs:IDREF"},
    {"xs:NMTOKENS", "xs:NMTOKEN"},
    {"xs:ENTITIES", "xs:ENTITY"},
    {"xs:integer", "xs:decimal"},
    {"xs:nonPositiveInteger", "xs:integer"},
    {"xs:negativeInteger", "xs:nonPositiveInteger"},
    {"xs:long", "xs:integer"},
    {"xs:int", "xs:long"},
    {"xs:short", "xs:int"},
    {"xs:byte", "xs:short"},
    {"xs:nonNegativeInteger", "xs:integer"},
    {"xs:positiveInteger", "xs:nonNegativeInteger"},
    {"xs:unsignedLong", "xs:nonNegativeInteger"},
    {"xs:unsignedInt", "xs:unsignedLong"},
    {"xs:unsignedShort", "xs:unsignedInt"},
    {"xs:unsignedByte", "xs:unsignedShort"},
    {"xs:dateTimeStamp", "xs:dateTime"},
    {"xs:yearMonthDuration", "xs:duration"},
    {"xs:dayTimeDuration", "xs:duration"},
};

/* The three list builtins do not derive from anyAtomicType. */
static int xsd_builtin_is_list_form(const char* t) {
    return strcmp(t, "xs:IDREFS") == 0 || strcmp(t, "xs:NMTOKENS") == 0 ||
           strcmp(t, "xs:ENTITIES") == 0;
}

static int xsd_builtin_derives_from(const char* d, const char* b) {
    if (strcmp(d, b) == 0) return 1;
    if (strcmp(b, "xs:anyType") == 0) return 1;
    /* every builtin (but the root and the list forms) is simple and
     * derives from xs:anySimpleType; atomics additionally derive
     * from xs:anyAtomicType */
    if (strcmp(b, "xs:anySimpleType") == 0 && !xsd_builtin_is_list_form(d))
        return 1;
    if (strcmp(b, "xs:anyAtomicType") == 0 && !xsd_builtin_is_list_form(d) &&
        strcmp(d, "xs:anySimpleType") != 0)
        return 1;
    for (int hop = 0; hop < 24; hop++) {
        const char* next = NULL;
        for (size_t i = 0; i < sizeof(k_builtin_base) / sizeof(k_builtin_base[0]);
             i++)
            if (strcmp(k_builtin_base[i].d, d) == 0) {
                next = k_builtin_base[i].b;
                break;
            }
        if (!next) return 0;
        if (strcmp(next, b) == 0) return 1;
        d = next;
    }
    return 0;
}

/* A simple type name (builtin "xs:-" or local) derives from target. */
static int xsd_simple_name_derives_from(struct leptris_xsd_schema* s,
                                        const char* name, const char* target) {
    if (!name || !target) return 0;
    if (strcmp(name, target) == 0) return 1;
    /* every simple type derives from the simple/ur roots */
    if (strcmp(target, "xs:anyType") == 0 ||
        strcmp(target, "xs:anySimpleType") == 0)
        return 1;
    if (strncmp(name, "xs:", 3) == 0)
        return xsd_builtin_derives_from(name, target);
    if (strncmp(target, "xs:", 3) == 0 &&
        strcmp(target, "xs:anyAtomicType") == 0)
        return 0; /* a local type of unknown variety: posture */
    const XsdSimple* t = xsd_find_simple(s, name);
    for (int hop = 0; t && hop < 16; hop++) {
        if (t->base) {
            if (strcmp(t->base, target) == 0) return 1;
            if (strncmp(t->base, "xs:", 3) == 0)
                return xsd_builtin_derives_from(t->base, target);
        }
        const XsdSimple* next =
            t->base_def ? t->base_def
                        : (t->base ? xsd_find_simple(s, t->base) : NULL);
        if (!next) return 0;
        t = next;
    }
    return 0;
}

/* The complex derivation chain: restriction (res_base) and extension
 * (assert_base — set eagerly at capture or stolen by the deferred
 * fold; ext_base kept as the belt), with the simpleContent text type
 * carrying the chain through a simple base. */
static int xsd_cm_derives_from(struct leptris_xsd_schema* s, XsdCm* cm,
                               const char* target, int hops) {
    if (!cm || hops > 16) return 0;
    if (cm->name && strcmp(cm->name, target) == 0) return 1;
    if (cm->text_type &&
        xsd_simple_name_derives_from(s, cm->text_type, target))
        return 1;
    const char* tns = xsd_target_ns(s);
    XsdCm* next = NULL;
    if (cm->res_base) {
        next = xsd_find_complex_ns(s, cm->res_base,
                                   cm->res_base_ns ? cm->res_base_ns : tns);
        if (!next) next = xsd_find_complex(s, cm->res_base);
    }
    if (!next && cm->assert_base) {
        next = xsd_find_complex_ns(
            s, cm->assert_base, cm->assert_base_ns ? cm->assert_base_ns : tns);
        if (!next) next = xsd_find_complex(s, cm->assert_base);
    }
    if (!next && cm->ext_base) {
        next = xsd_find_complex_ns(s, cm->ext_base,
                                   cm->ext_base_ns ? cm->ext_base_ns : tns);
        if (!next) next = xsd_find_complex(s, cm->ext_base);
    }
    return xsd_cm_derives_from(s, next, target, hops + 1);
}

/* One resolved type reference. kind: 0 unresolved, 1 ur-type
 * (anyType / anySimpleType / anyAtomicType), 2 builtin, 3 complex,
 * 4 local simple. */
typedef struct {
    int kind;
    XsdCm* cm;
    const XsdSimple* sm;
} XsdTypeRef;

static XsdTypeRef xsd_resolve_type_ref(struct leptris_xsd_schema* s,
                                       const char* name) {
    XsdTypeRef r = {0, NULL, NULL};
    if (!name || !name[0]) {
        r.kind = 1; /* no declared type = xs:anyType (§3.3.2) */
        return r;
    }
    if (strncmp(name, "xs:", 3) == 0) {
        if (strcmp(name, "xs:anyType") == 0 ||
            strcmp(name, "xs:anySimpleType") == 0 ||
            strcmp(name, "xs:anyAtomicType") == 0)
            r.kind = 1;
        else
            r.kind = 2;
        return r;
    }
    const char* tns = xsd_target_ns(s);
    r.cm = xsd_find_complex_ns(s, name, tns);
    if (!r.cm) r.cm = xsd_find_complex(s, name);
    if (r.cm) {
        r.kind = 3;
        return r;
    }
    r.sm = xsd_find_simple(s, name);
    if (r.sm) r.kind = 4;
    return r;
}

/* §3.3.6.1 cl.7 / cos-ct-type-substitutable: the alternative's type
 * derives from the declared type. Unresolved names skip (the ref
 * checks own them); xs:error always substitutes (no valid instances,
 * so the alternative can never admit anything). */
static int xsd_alt_substitutable(struct leptris_xsd_schema* s,
                                 const XsdAlternative* a,
                                 const char* decl_type) {
    if (a->xs_error) return 1;
    XsdTypeRef d = xsd_resolve_type_ref(s, decl_type);
    XsdTypeRef alt = xsd_resolve_type_ref(s, a->type);
    if (d.kind == 0 || alt.kind == 0) return 1;
    if (d.kind == 1) return 1; /* declared ur-type: everything derives */
    if (decl_type && a->type && strcmp(decl_type, a->type) == 0) return 1;
    if (alt.kind == 1)
        return d.kind == 2 || d.kind == 4; /* a ur simple for a simple */
    if (alt.kind == 3)
        return (d.kind == 3 || d.kind == 2 || d.kind == 4) &&
               xsd_cm_derives_from(s, alt.cm, decl_type, 0);
    if (alt.kind == 2 || alt.kind == 4) {
        if (d.kind != 2 && d.kind != 4) return 0;
        /* cos-ct-type-substitutable's union and list arms: D
         * substitutes a union B when D derives from ANY of B's
         * member types (cta0014/15: xs:date substitutes the
         * date/time/dateTime/gYearMonth union), and a list B when D
         * derives from B's item type. */
        if (d.kind == 4 && d.sm) {
            if (d.sm->deriv == XSD_SIMPLE_UNION && d.sm->members) {
                for (size_t i = 0; i < d.sm->member_count; i++)
                    if (d.sm->members[i] &&
                        xsd_simple_name_derives_from(s, a->type,
                                                     d.sm->members[i]))
                        return 1;
                return 0;
            }
            if (d.sm->deriv == XSD_SIMPLE_LIST && d.sm->item_type)
                return xsd_simple_name_derives_from(s, a->type,
                                                    d.sm->item_type);
        }
        return xsd_simple_name_derives_from(s, a->type, decl_type);
    }
    return 0;
}

/* Does the chain carry an inline (synthesized-slot) alternative
 * type? Those compare by node-address names — equality between
 * separately captured chains is undecidable here, so table
 * comparisons skip them. */
static int xsd_table_has_synth(const XsdAlternative* a) {
    for (; a; a = a->next)
        if (a->type && strncmp(a->type, "alternative:", 12) == 0) return 1;
    return 0;
}

static int xsd_type_tables_equal(const XsdAlternative* a,
                                 const XsdAlternative* b) {
    if (xsd_table_has_synth(a) || xsd_table_has_synth(b)) return 1;
    while (a && b) {
        int same_test = (a->test && b->test) ? strcmp(a->test, b->test) == 0
                                             : (!a->test && !b->test);
        int same_type = (a->type && b->type) ? strcmp(a->type, b->type) == 0
                                             : (!a->type && !b->type);
        if (!same_test || !same_type || a->xs_error != b->xs_error)
            return 0;
        a = a->next;
        b = b->next;
    }
    return a == NULL && b == NULL;
}

/* Per-particle constraint checks (§3.12.2 + §3.3.6.1 cl.7) on one
 * alternative chain. Returns 0 ok, -1 failed (error set). */
static int xsd_check_particle_table(struct leptris_xsd_schema* s,
                                    const char* decl_type,
                                    const XsdAlternative* chain,
                                    const char* owner) {
    for (const XsdAlternative* a = chain; a; a = a->next) {
        if (a->test && !a->compiled) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "src-element.5 (XPath valid): the alternative "
                     "test of '%s' does not parse: \"%s\"",
                     owner ? owner : "?", a->test);
            xsd_set_error(s, msg);
            return -1;
        }
        if (!xsd_alt_substitutable(s, a, decl_type)) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "cos-ct-type-substitutable: the alternative type "
                     "'%s' of '%s' does not derive from the declared "
                     "type '%s'",
                     a->type, owner ? owner : "?",
                     decl_type ? decl_type : "xs:anyType");
            xsd_set_error(s, msg);
            return -1;
        }
    }
    return 0;
}

/* Particle table collector for the content-model walks. */
typedef struct {
    char ns[192];
    char name[192];
    char type[192];
    const XsdAlternative* table;
} XsdParticleRow;

static void xsd_collect_particles(const XsdCm* n, XsdParticleRow* arr,
                                  int cap, int* count, int depth) {
    if (!n || depth > 32) return;
    if (n->kind == XSD_CM_ELEMENT) {
        if (!n->name || *count >= cap) return;
        XsdParticleRow* r = &arr[(*count)++];
        snprintf(r->ns, sizeof(r->ns), "%s", n->ns ? n->ns : "");
        snprintf(r->name, sizeof(r->name), "%s", n->name);
        snprintf(r->type, sizeof(r->type), "%s", n->type ? n->type : "");
        r->table = n->alternatives;
        return;
    }
    for (const XsdCm* c = n->first_child; c; c = c->next)
        xsd_collect_particles(c, arr, cap, count, depth + 1);
}

static int xsd_particle_same_name(const XsdParticleRow* a,
                                  const XsdParticleRow* b) {
    return strcmp(a->name, b->name) == 0 && strcmp(a->ns, b->ns) == 0;
}

/* The sweep: per-element-decl constraints (globals + every complex
 * type's particles), the content-model table-equality check, and the
 * restriction particle-table preservation check. */
static int xsd_check_type_tables(struct leptris_xsd_schema* s) {
    if (s->ct_failed) {
        xsd_set_error(s, "src-element.5: a test-less xs:alternative "
                         "must be the last alternative of the element "
                         "declaration");
        return -1;
    }
    /* global element declarations */
    for (const XsdElementDecl* d = s->elements; d; d = d->next) {
        if (d->alternatives &&
            xsd_check_particle_table(s, d->type, d->alternatives, d->name) != 0)
            return -1;
    }
    /* complex types: particles + same-model table equality */
    for (XsdCm* c = s->complex_types; c; c = c->next) {
        XsdParticleRow rows[256];
        int n = 0;
        xsd_collect_particles(c, rows, 256, &n, 0);
        for (int i = 0; i < n; i++) {
            if (rows[i].table &&
                xsd_check_particle_table(s, rows[i].type[0] ? rows[i].type
                                                            : NULL,
                                         rows[i].table,
                                         rows[i].name) != 0)
                return -1;
            /* cos-element-consistent: same (ns, name) twice in one
             * model — the tables must be equal */
            for (int j = i + 1; j < n; j++) {
                if (!xsd_particle_same_name(&rows[i], &rows[j])) continue;
                if (rows[i].table == rows[j].table) continue;
                if ((rows[i].table == NULL) != (rows[j].table == NULL)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                             "cos-element-consistent: the type table "
                             "of '%s' in '%s' differs from its "
                             "same-name particle",
                             rows[i].name, c->name ? c->name : "?");
                    xsd_set_error(s, msg);
                    return -1;
                }
                if (!xsd_type_tables_equal(rows[i].table, rows[j].table)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                             "cos-element-consistent: the same-name "
                             "particles '%s' in '%s' carry different "
                             "type tables",
                             rows[i].name, c->name ? c->name : "?");
                    xsd_set_error(s, msg);
                    return -1;
                }
            }
        }
        /* particle restriction: the derived same-name particle must
         * preserve the base particle's type table */
        if (!c->res_base) continue;
        const char* tns = xsd_target_ns(s);
        XsdCm* base = xsd_find_complex_ns(
            s, c->res_base, c->res_base_ns ? c->res_base_ns : tns);
        if (!base) base = xsd_find_complex(s, c->res_base);
        if (!base) continue;
        XsdParticleRow brows[256];
        int bn = 0;
        xsd_collect_particles(base, brows, 256, &bn, 0);
        for (int i = 0; i < n; i++) {
            if (!rows[i].table) continue;
            for (int j = 0; j < bn; j++) {
                if (!xsd_particle_same_name(&rows[i], &brows[j])) continue;
                if (!brows[j].table) break; /* base has none: posture */
                if (!xsd_type_tables_equal(rows[i].table, brows[j].table)) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                             "cos-particle-restrict: the restriction "
                             "'%s' changes the type table of '%s'",
                             c->name ? c->name : "?", rows[i].name);
                    xsd_set_error(s, msg);
                    return -1;
                }
                break;
            }
        }
    }
    return 0;
}

/* #2184 a-props-correct (§3.2.2.1): an attribute typed xs:NOTATION
 * — or a type whose terminal is NOTATION — must be ENUMERATED (the
 * notation symbols exist only as enumeration values; the over027
 * shape). The builtin carries no facets of its own, so a bare
 * xs:NOTATION type reference always fails; an enumerated local
 * restriction is the legal form. Unresolved names stay posture (the
 * ref checks own them). */
static int xsd_attr_row_notation_ok(struct leptris_xsd_schema* s,
                                    const XsdAttrDecl* a) {
    if (!a->type) return 1;
    if (strcmp(a->type, "xs:NOTATION") == 0 ||
        strcmp(a->type, "xsd:NOTATION") == 0)
        return 0;
    if (strncmp(a->type, "xs:", 3) == 0) return 1;
    const XsdSimple* t = xsd_find_simple(s, a->type);
    if (!t) return 1;
    if (t->enum_values) return 1;
    const char* term = xsd_chain_terminal(s, t);
    if (term && (strcmp(term, "xs:NOTATION") == 0 ||
                 strcmp(term, "xsd:NOTATION") == 0))
        return 0;
    return 1;
}

static int xsd_check_notation_attrs(struct leptris_xsd_schema* s) {
    for (const XsdAttrDecl* a = s->top_attrs; a; a = a->next) {
        if (xsd_attr_row_notation_ok(s, a)) continue;
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "a-props-correct: the attribute '%s' is typed "
                 "NOTATION without an enumeration",
                 a->name ? a->name : "?");
        xsd_set_error(s, msg);
        return -1;
    }
    for (const XsdTypeAttrs* ta = s->type_attrs; ta; ta = ta->next)
        for (const XsdAttrDecl* a = ta->attrs; a; a = a->next) {
            if (xsd_attr_row_notation_ok(s, a)) continue;
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "a-props-correct: the attribute '%s' of type "
                     "'%s' is typed NOTATION without an enumeration",
                     a->name ? a->name : "?",
                     ta->type_name ? ta->type_name : "?");
            xsd_set_error(s, msg);
            return -1;
        }
    return 0;
}

/* #2184 src-ct.1 + §3.2.7.1, walked over the SOURCE elements (the
 * captured models no longer carry the wrapper shape):
 *
 *   - a complexType whose XML representation has a simpleContent /
 *     complexContent child may not also declare xs:attribute /
 *     xs:attributeGroup / xs:anyAttribute at the complexType level —
 *     the s3_12si01 shape;
 *
 *   - @inheritable must be a boolean lexical (§3.2.7.1 —
 *     cta9006err's "" and cta9007err's "2"); gated on the 1.1
 *     posture, the attribute does not exist in 1.0. */
static int xsd_check_schema_source(struct leptris_xsd_schema* s,
                                   LeptrisElement e, int depth) {
    if (depth > 32) return 0;
    int bad = 0;
    for (LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT) continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* cl = NULL;
        const char* cu = NULL;
        leptris_element_expanded_name(c, &cl, NULL, &cu);
        if (cl && cu && strcmp(cu, XSD_NS) == 0) {
            if (strcmp(cl, "complexType") == 0) {
                int saw_wrapper = 0, saw_direct_attr = 0;
                for (LeptrisNodeRef m =
                         leptris_node_first_child(leptris_element_as_node(c));
                     m; m = leptris_node_next_sibling(m)) {
                    if (leptris_node_get_type(m) != LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    const char* ml = NULL;
                    const char* mu = NULL;
                    leptris_element_expanded_name((LeptrisElement)m, &ml,
                                                  NULL, &mu);
                    if (!ml || !mu || strcmp(mu, XSD_NS) != 0) continue;
                    if (strcmp(ml, "simpleContent") == 0 ||
                        strcmp(ml, "complexContent") == 0)
                        saw_wrapper = 1;
                    else if (strcmp(ml, "attribute") == 0 ||
                             strcmp(ml, "attributeGroup") == 0 ||
                             strcmp(ml, "anyAttribute") == 0)
                        saw_direct_attr = 1;
                }
                if (saw_wrapper && saw_direct_attr) {
                    xsd_set_error(s, "src-ct.1: a complexType with a "
                                     "simpleContent/complexContent "
                                     "wrapper may not declare "
                                     "attributes at the complexType "
                                     "level");
                    bad = 1;
                }
            } else if (strcmp(cl, "attribute") == 0) {
                const char* v = leptris_element_attribute(c, "inheritable");
                if (v && (s->version_11 || s->saw_alternative ||
                          s->saw_assert)) {
                    while (*v == ' ' || *v == '\t' || *v == '\n' ||
                           *v == '\r')
                        v++;
                    const char* end = v + strlen(v);
                    while (end > v && (end[-1] == ' ' || end[-1] == '\t' ||
                                       end[-1] == '\n' || end[-1] == '\r'))
                        end--;
                    size_t len = (size_t)(end - v);
                    if (!((len == 4 && strncmp(v, "true", 4) == 0) ||
                          (len == 5 && strncmp(v, "false", 5) == 0) ||
                          (len == 1 && (v[0] == '0' || v[0] == '1')))) {
                        xsd_set_error(s, "src-attribute.1 (§3.2.7.1): "
                                         "inheritable must be a boolean");
                        bad = 1;
                    }
                }
            }
        }
        if (!bad && xsd_check_schema_source(s, c, depth + 1) != 0) bad = 1;
        if (bad) return 1;
    }
    return bad;
}

static LeptrisXsdSchema xsd_compile_impl(const char* xsd_text,
                                         size_t len,
                                         const char* base_dir,
                                         const char* self_path,
                                         LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!xsd_text || len == 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }

    struct leptris_xsd_schema* s =
        (struct leptris_xsd_schema*)calloc(1, sizeof(*s));
    if (!s) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    s->base_dir = xsd_strdup(base_dir);
    /* #2150 §4.3.2: the compiled file's own normalized path — the
     * mutual-override conflict check matches inner override targets
     * against it (lexical normalization only; no filesystem id). */
    if (self_path) {
        char canon[4096];
        xsd_path_normalize(self_path, canon, sizeof(canon));
        s->self_path = xsd_strdup(canon);
        /* #2184: the entry document is IN the visited set — the
         * redefine/override chase must not re-enter and re-capture
         * it (the mutual-override's closing edge targets it; without
         * the mark the second round saw a bogus ancestor chain). The
         * cycle check still sees the edge before the dedup. */
        xsd_visited_add(s, canon);
    }
    /* #2151: unit 0 is the compiled document itself; includes and
     * imports push their own unit id while their declarations are
     * captured (the defaultOpenContent application is per-unit). */
    {
        const char* unit0 = base_dir ? base_dir : "";
        s->units = (char**)malloc(sizeof(char*));
        if (s->units) {
            s->units[0] = xsd_strdup(unit0);
            if (s->units[0]) s->unit_count = 1;
        }
        s->current_unit = 0;
    }

    /* target namespace (xs:any ##other/##targetNamespace filters) */
    s->target_ns = NULL;

    LeptrisDocument doc = leptris_parse_string(xsd_text, len, status);
    if (!doc) {
        xsd_set_error(s, "schema document is not well-formed XML");
        goto fail;
    }

    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        xsd_set_error(s, "schema document has no root element");
        goto fail;
    }
    s->target_ns = xsd_strdup(
        leptris_element_attribute(root, "targetNamespace"));
    {
        /* XSD 1.1 §4.3 schema versioning: the W3C suite and the
         * binding ecosystem mark 1.1 with vc:minVersion (the
         * versioning namespace); the plain version attribute is
         * the other legal spelling. vc:maxVersion below 1.1 pins
         * a 1.0 processor target regardless. leptris#1910/#1911:
         * reading only @version silently compiled asserts and
         * alternatives away for every vc-marked schema. */
        const char* ver = leptris_element_attribute(root, "version");
        const char* vmin = leptris_element_attribute_ns(
            root, "http://www.w3.org/2007/XMLSchema-versioning",
            "minVersion");
        const char* vmax = leptris_element_attribute_ns(
            root, "http://www.w3.org/2007/XMLSchema-versioning",
            "maxVersion");
        int is11 = (ver && strcmp(ver, "1.1") == 0) ||
                   (vmin && strcmp(vmin, "1.1") == 0);
        if (vmax && strcmp(vmax, "1.0") == 0) is11 = 0;
        s->version_11 = is11;
        /* #2149: order-independent 1.1-syntax evidence — one walk
         * for xs:alternative elements before any component capture.
         * Real-world cta schemas carry alternatives while marked
         * version="1.0" (or unmarked); their selected types' asserts
         * must evaluate, so the assert capture keys on this flag. */
        s->saw_alternative = xsd_doc_has_alternative(root, 0);
        /* #2154: same order-independent evidence walk for assertion
         * syntax — xs:assert and xs:assertion elements anywhere in
         * the unit arm the assert capture and the facet evaluator. */
        s->saw_assert = xsd_doc_has_assert(root, 0);
        /* #2151: open-content syntax evidence (same posture — the
         * Open corpus is unmarked). Includes re-arm it per unit so
         * an xs:any occurrence rule inside an included document
         * (open048) still applies. */
        s->saw_open_content = xsd_doc_has_open_content(root, 0);
    }

    const char* local = NULL;
    const char* prefix = NULL;
    const char* uri = NULL;
    leptris_element_expanded_name(root, &local, &prefix, &uri);
    /* Accept the XML Schema namespace under any prefix, and the
     * unprefixed spelling libxml2 fixtures use without a default
     * xmlns (compilation is namespace-driven from here on). */
    if (!local || strcmp(local, "schema") != 0 ||
        (uri && strcmp(uri, XSD_NS) != 0)) {
        xsd_set_error(s, "root element is not xs:schema");
        goto fail;
    }

    /* Schema-structure constraints (#1691 schema-error class):
     * required/allowed attributes on the xs: vocabulary and
     * occurrence ordering, checked at the document level exactly
     * where the oracle raises them — before capture. */
    if (xsd_check_structure(s, root, 0) != 0)
        goto fail;

    /* Derivation constraints (#1691 slice 4): content-type match on
     * extension (cos-ct-derived-ok) and the attribute-wildcard
     * subset rules on restriction (derivation-ok-restriction-4-1). */
    if (xsd_check_derivation(s, root) != 0)
        goto fail;

    /* Ref resolution (#1691 slice 5): element/attribute/group
     * ref QNames must resolve to a declaration in an available
     * document. */
    if (xsd_check_refs(s, root) != 0)
        goto fail;

    /* UPA (#1691 slice 6): choice-branch FIRST overlap and the
     * optional/repeating-sequence adjacency rule. */
    if (xsd_check_upa(s, root) != 0)
        goto fail;

    /* Schema Component Constraint: xsi: Not Allowed — the schema's
     * own targetNamespace must never be the xsi namespace
     * (scc-no-xsi). */
    if (s->target_ns &&
        strcmp(s->target_ns,
               "http://www.w3.org/2001/XMLSchema-instance") == 0) {
        xsd_set_error(
            s,
            "scc-no-xsi: the target namespace must not be "
            "'http://www.w3.org/2001/XMLSchema-instance'");
        goto fail;
    }

    /* src-include.1: the included/redefined schema's
     * targetNamespace must equal the includer's — or be absent
     * (the chameleon form adopts the includer's at capture). */
    if (xsd_check_include_tns(s, root) != 0)
        goto fail;

    /* #2150 §4.3: the override document checks — targetNamespace
     * alignment (src-override.1), overriding-set uniqueness
     * (src-override.2), and the mutual-override conflict
     * (src-override.3). */
    if (xsd_check_override_docs(s, root) != 0)
        goto fail;

    xsd_capture_root(s, root);
    /* #2184 §4.2.2/§4.3: the redefine/override location graph must
     * not cycle — flagged during the capture chase, where the
     * visited dedup would otherwise swallow the closing edge. */
    if (s->ro_cyclic) {
        xsd_set_error(s, "src-redefine: the redefine/override "
                         "schemaLocation graph is cyclic");
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }
    xsd_resolve_extension_bases(s);
    /* #2184 §3.10.2: expand ##definedSibling tokens into the
     * concrete sibling names — after the fold, when the spliced
     * base particles are in place. */
    xsd_wc_expand_defined_sibling_pass(s);
    /* #2151: the open-content derivation pass (inheritance, wildcard
     * union, the cos-ct-extends ladder, the restriction subset
     * checks) — after the fold, when every base has landed. The
     * capture may already have flagged an error (the src-ct class):
     * surfacing it here is what turns the check into a rejection. */
    if (s->oc_failed || xsd_oc_finalize(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }
    xsd_resolve_pending_attr_refs(s);
    xsd_canon_builtin_refs(s);

    /* #2182 §3.4.2.5: default attribute groups — every unit's
     * @defaultAttributes list resolves (§3.17.6.2), then the
     * groups' uses append to each applying type's table (§3.4.2.4)
     * with the c-props-correct.4 duplicate check. A broken ref or
     * a duplicate use fails the SCHEMA. */
    if (xsd_apply_default_attrs(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    /* #2185: the derivation-ok attribute-use clauses — duplicate
     * attribute use on the extension-folded table and the
     * restriction derived-vs-base consistency (plus the #2184
     * {inheritable} match), over the POST-capture tables. A
     * violation fails the SCHEMA. */
    if (xsd_check_derivation_ok_attrs(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    /* #2184: the wildcard negative-set sweep — the parked
     * src-wildcard capture errors (co-occurrence, malformed or
     * undeclared-prefix notQName tokens, vacuous exclusions) plus
     * the §3.10.6.3 attribute-wildcard RESTRICTION subset law over
     * the captured canon rows. */
    if (xsd_check_attr_wildcards(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    /* #2184: the alternative-table compile constraints — §3.12.2
     * XPath-valid tests, §3.3.6.1 cl.7 type substitutability,
     * cos-element-consistent table equality, the restriction
     * particle-table preservation, and the src-element.5 mid-table
     * default flagged at capture. */
    if (xsd_check_type_tables(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }
    if (root && xsd_check_schema_source(s, root, 0) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    /* #2184 a-props-correct: NOTATION-typed attributes must be
     * enumerated. */
    if (xsd_check_notation_attrs(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    /* #2150 §4.3.2: overriding references resolve against the
     * POST-merge set (src-override.4), and a self-named derivation
     * base is circular. */
    if (xsd_check_override_refs(s, root) != 0)
        goto fail;
    if (xsd_check_derivation_cycles(s) != 0)
        goto fail;

    /* #2156: identity-constraint @ref forms resolve and the
     * §3.11.6 subset checks run once the whole capture has landed
     * (forward refs). A broken constraint fails the SCHEMA. */
    if (xsd_resolve_ic_refs(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    for (const XsdSimple* t = s->simple_types; t; t = t->next)
        if (!xsd_patterns_supported(t, 0)) {
            xsd_set_error(s, "pattern facet is not a supported "
                             "regular expression");
            if (status) *status = LEPTRIS_ERROR_PARSE;
            leptris_document_free(doc);
            return (LeptrisXsdSchema)s;
        }

    /* #2155: the 1.1 facet-applicability sweep — the schema-error
     * class where inapplicable facets (length on the duration
     * family, bounds on lists), invalid facet values, cross-facet
     * order violations and base-widening restrictions must REJECT
     * the schema at compile time. */
    if (!xsd_datatype_facets_supported(s)) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        leptris_document_free(doc);
        return (LeptrisXsdSchema)s;
    }

    leptris_document_free(doc);
    return s;

fail:
    if (doc) leptris_document_free(doc);
    if (status) *status = LEPTRIS_ERROR_PARSE;
    /* Failure returns an error-carrying handle (the documented
     * contract): leptris_xsd_error says why, free it the same way. */
    return (LeptrisXsdSchema)s;
}

/* Every top-level declaration under an xs:schema root. xs:include
 * / xs:import / xs:redefine resolve their schemaLocation against
 * the owning schema's base_dir (string-compiled schemas have none
 * and skip them); the referenced root's declarations merge into
 * the same tables. */
/* leptris#1644: lexical path normalization — collapse "." and
 * ".." segments (and duplicate separators) so the same file reached
 * through different relative spellings yields one dedup key.
 * Purely lexical: no filesystem access, portable. */
/* leptris-ruby#440 shape A: extensions whose base was not yet
 * captured fold here, after every unit has merged into the tables
 * — the correct place in XSD terms, since type references resolve
 * against the WHOLE schema, not the preceding slice of it.
 * Iterates to a fixpoint so extension chains resolve regardless of
 * file order; unresolved leftovers keep their deferral marker. */
static void xsd_resolve_extension_bases(struct leptris_xsd_schema* s) {
    int progressed = 1;
    while (progressed) {
        progressed = 0;
        for (XsdCm* c = s->complex_types; c; c = c->next) {
            if (!c->ext_base) continue;
            /* #1881: the fold resolves in the BASE's namespace —
             * recorded at capture from the base QName's prefix —
             * falling back to the deriver's ns (same-unit refs,
             * where the old behavior lives). */
            const char* bns = c->ext_base_ns ? c->ext_base_ns : c->ns;
            XsdCm* base = xsd_find_complex_ns(s, c->ext_base, bns);
            if (!base) base = xsd_find_complex_ns(s, c->ext_base, c->ns);
            if (!base || base->ext_base) continue;
            XsdCm* copied = xsd_cm_copy_list(base->first_child);
            if (copied) {
                XsdCm* tail = copied;
                while (tail->next) tail = tail->next;
                tail->next = c->first_child;
                c->first_child = copied;
                /* capture wraps a multi-particle splice under one
                 * sequence (the NFA consumes ONE root particle) —
                 * the deferred fold owes the same normalization */
                if (c->first_child->next) {
                    XsdCm* seq = xsd_cm_new(XSD_CM_SEQ);
                    if (seq) {
                        seq->first_child = c->first_child;
                        c->first_child = seq;
                    }
                }
            }
            const XsdTypeAttrs* bt =
                xsd_find_type_attrs_ns(s, c->ext_base, bns);
            if (!bt)
                bt = xsd_find_type_attrs_ns(s, c->ext_base, c->ns);
            if (bt) {
                XsdTypeAttrs* mine = NULL;
                for (XsdTypeAttrs* t = s->type_attrs; t; t = t->next)
                    if (strcmp(t->type_name, c->name) == 0 &&
                        ((!c->ns && !t->ns) ||
                         (c->ns && t->ns &&
                          strcmp(t->ns, c->ns) == 0))) {
                        mine = t;
                        break;
                    }
                XsdAttrDecl* prepend =
                    xsd_copy_attr_rows(bt->attrs, 1);
                if (mine) {
                    XsdAttrDecl* tail_a = prepend;
                    while (tail_a && tail_a->next)
                        tail_a = tail_a->next;
                    if (tail_a) {
                        tail_a->next = mine->attrs;
                        mine->attrs = prepend;
                    }
                    /* #2184 §3.10.6.3: the base's effective wildcard
                     * UNIONS into the derived's (no separate base
                     * arm — one canon wildcard per row) */
                    xsd_wc_fold_union(mine, bt);
                } else if (prepend) {
                    XsdTypeAttrs* row =
                        (XsdTypeAttrs*)calloc(1, sizeof(*row));
                    if (row) {
                        row->type_name = xsd_strdup(c->name);
                        if (c->ns) row->ns = xsd_strdup(c->ns);
                        row->attrs = prepend;
                        row->next = s->type_attrs;
                        s->type_attrs = row;
                    }
                }
            }
            /* #2154 §3.13.6: the assert chain follows the derivation
             * base even after the fold — steal the linkage before
             * the clear (the fold itself already ran; multi-hop
             * chains keep one link per type) */
            if (!c->assert_base) {
                c->assert_base = c->ext_base;
                c->assert_base_ns = c->ext_base_ns;
            } else {
                free(c->ext_base);
                free(c->ext_base_ns);
            }
            c->ext_base = NULL;
            c->ext_base_ns = NULL;
            progressed = 1;
        }
    }
}

/* ---- #2151: the open-content derivation pass ---------------------- */

/* Is `token` a member of the whitespace-separated grammar list? */
static int oc_ns_list_has(const char* list, const char* token) {
    if (!list || !token) return 0;
    const char* p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (strlen(token) == len && strncmp(start, token, len) == 0)
            return 1;
    }
    return 0;
}

/* The positive filter of a wildcard grammar: 1 when a namespace
 * `uri` (NULL = absent) is admitted before any notNamespace
 * exclusion. NULL grammar = ##any. */
static int oc_ns_admits(const XsdCm* w, const char* uri) {
    const char* f = w->any_ns;
    if (!f || strcmp(f, "##any") == 0) return 1;
    if (strcmp(f, "##local") == 0) return uri == NULL;
    if (strcmp(f, "##targetNamespace") == 0) return uri != NULL;
    /* ##other / list forms: token walk (##other = NOT {tns, absent},
     * but the not-variety is only ever a SUBSET relation's super
     * side here — enumerating its admission needs the tns, which
     * rides any_unit_tns on the wildcard itself) */
    int saw_other = 0;
    const char* p = f;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (len == 7 && strncmp(start, "##other", 7) == 0) {
            saw_other = 1;
            continue;
        }
        if (len == 7 && strncmp(start, "##local", 7) == 0) {
            if (uri == NULL) return 1;
            continue;
        }
        if (len == 17 &&
            strncmp(start, "##targetNamespace", 17) == 0) {
            if (uri) return 1;
            continue;
        }
        if (uri && strlen(uri) == len &&
            strncmp(start, uri, len) == 0)
            return 1;
    }
    if (saw_other) return uri != NULL; /* not: only the absent ns is
                                          excluded from admission */
    return 0;
}

/* Wildcard Subset (§3.10.6.2) over the engine's grammar strings:
 * the ns-level variety ladder plus the {disallowed names} clauses
 * (s3_4_1si06). Approximation: a wildcard's @notNamespace exclusions
 * participate by literal token containment. */
static int oc_wildcard_subset(const XsdCm* sub, const XsdCm* super) {
    const char* sf = sub->any_ns;
    const char* uf = super->any_ns;
    int s_any = !sf || strcmp(sf, "##any") == 0;
    int u_any = !uf || strcmp(uf, "##any") == 0;
    if (u_any && !super->not_ns) {
        /* clause 1 */
    } else if (s_any) {
        return 0; /* an unrestricted sub is only a subset of any */
    } else if (!s_any && !u_any &&
               sf[0] != '#' && uf[0] != '#') {
        /* enumeration ⊆ enumeration: clause 2 */
        if (!oc_ns_list_has(uf, "")) { /* non-empty super list */
            const char* p = sf;
            int all = 1;
            while (*p && all) {
                while (*p == ' ' || *p == '\t' || *p == '\n' ||
                       *p == '\r')
                    p++;
                const char* start = p;
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                       *p != '\r')
                    p++;
                size_t len = (size_t)(p - start);
                if (len == 0) break;
                char tok[512];
                if (len >= sizeof(tok)) len = sizeof(tok) - 1;
                memcpy(tok, start, len);
                tok[len] = 0;
                if (!oc_ns_list_has(uf, tok)) all = 0;
            }
            if (!all) return 0;
        }
    } else if (!s_any && sf[0] != '#' && u_any) {
        /* enumeration ⊆ any: clause 1 */
    } else {
        /* not-forms (##other) and mixed pairs: conservatively not a
         * subset unless both sides carry the same spelling */
        if (!((sf && uf) && strcmp(sf, uf) == 0)) return 0;
    }
    /* @notNamespace: the super's exclusions must be excluded by the
     * sub as well (token containment) */
    if (super->not_ns) {
        const char* p = super->not_ns;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
                p++;
            const char* start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                   *p != '\r')
                p++;
            size_t len = (size_t)(p - start);
            if (len == 0) break;
            char tok[512];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len);
            tok[len] = 0;
            int blocked = oc_ns_list_has(sub->not_ns, tok);
            if (!blocked && sub->any_ns && sub->any_ns[0] != '#')
                blocked = !oc_ns_list_has(sub->any_ns, tok);
            if (!blocked) return 0;
        }
    }
    /* {disallowed names}: each super notQName must be blocked by
     * the sub (clause 1 of §3.10.6.2 — s3_4_1si06) */
    if (super->not_qname) {
        const char* p = super->not_qname;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
                p++;
            const char* start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                   *p != '\r')
                p++;
            size_t len = (size_t)(p - start);
            if (len == 0) break;
            char tok[512];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len);
            tok[len] = 0;
            int blocked = sub->not_qname &&
                          oc_ns_list_has(sub->not_qname, tok);
            if (!blocked) return 0;
        }
    }
    return 1;
}

/* Attribute Wildcard Union (§3.10.6.3) at the engine's grammar
 * level: folds the BASE wildcard into the DERIVED wildcard in place
 * (pc/annotations stay the derived's, §3.4.2.3.3 cl. 6.2). The
 * derived @notNamespace exclusions lose every token the base
 * admits (the union is wider); an any-variety base widens the
 * derived grammar to ##any, keeping only the exclusions the base
 * itself carries. */
static void oc_union_filter_not_ns(XsdCm* derived, const XsdCm* base) {
    if (!derived->not_ns) return;
    const char* p = derived->not_ns;
    char kept[1024];
    size_t o = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        char tok[512];
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, start, len);
        tok[len] = 0;
        /* does the BASE admit the excluded namespace? */
        int base_admits;
        if (strcmp(tok, "##local") == 0) {
            base_admits = oc_ns_admits(base, NULL) &&
                          !oc_ns_list_has(base->not_ns, "##local");
        } else if (strcmp(tok, "##targetNamespace") == 0) {
            base_admits = base->any_unit_tns
                              ? oc_ns_admits(base, base->any_unit_tns)
                              : 0;
        } else {
            base_admits = oc_ns_admits(base, tok) &&
                          !oc_ns_list_has(base->not_ns, tok);
        }
        if (!base_admits && o + len + 2 < sizeof(kept)) {
            if (o) kept[o++] = ' ';
            memcpy(kept + o, tok, len);
            o += len;
        }
    }
    kept[o] = 0;
    free(derived->not_ns);
    derived->not_ns = o ? xsd_strdup(kept) : NULL;
}

static void oc_wildcard_union(XsdCm* derived, const XsdCm* base) {
    if (!derived || !base) return;
    const char* d = derived->any_ns;
    const char* b = base->any_ns;
    int d_any = !d || strcmp(d, "##any") == 0;
    int b_any = !b || strcmp(b, "##any") == 0;
    if (!d_any && !b_any && d[0] != '#' && b[0] != '#') {
        /* enumeration ∪ enumeration: concat the lists */
        size_t dl = strlen(d), bl = strlen(b);
        char* joined = (char*)malloc(dl + bl + 2);
        if (joined) {
            memcpy(joined, d, dl);
            joined[dl] = ' ';
            memcpy(joined + dl + 1, b, bl + 1);
            free(derived->any_ns);
            derived->any_ns = joined;
        }
    } else if (!d_any && b_any) {
        /* the base admits everything (minus its own exclusions):
         * the union is ##any, keeping the base's exclusions */
        free(derived->any_ns);
        derived->any_ns = xsd_strdup("##any");
        free(derived->not_ns);
        derived->not_ns = base->not_ns ? xsd_strdup(base->not_ns)
                                       : NULL;
        return;
    }
    /* d_any (##any or a notNamespace-only wildcard) and enum /
     * not-form bases: the union keeps the exclusions only for
     * namespaces the base does not admit (open047: the base's
     * enumerated ns re-enters). */
    oc_union_filter_not_ns(derived, base);
    /* {disallowed names}: the union keeps what either side excludes
     * (§3.10.6.3 simplification — concat) */
    if (base->not_qname) {
        if (derived->not_qname) {
            size_t dl = strlen(derived->not_qname);
            size_t bl = strlen(base->not_qname);
            char* joined = (char*)malloc(dl + bl + 2);
            if (joined) {
                memcpy(joined, derived->not_qname, dl);
                joined[dl] = ' ';
                memcpy(joined + dl + 1, base->not_qname, bl + 1);
                free(derived->not_qname);
                derived->not_qname = joined;
            }
        } else {
            derived->not_qname = xsd_strdup(base->not_qname);
        }
    }
}

/* The post-capture open-content derivation pass (§3.4.2.3.3 cl. 6.2
 * + cos-ct-extends.1.4.3.2.2.3/4 + cos-content-act-restrict). Runs
 * to a fixpoint so extension chains resolve regardless of order.
 * Returns nonzero when the schema must be rejected. */
static int xsd_oc_finalize(struct leptris_xsd_schema* s) {
    /* Runs for every schema: the OC branches self-gate on open
     * content presence; the derivation-mixed check
     * (cos-ct-extends.1.4.3.2.2.1, s3_4_1si08) is a plain
     * extension rule with no 1.1-only syntax to arm on. */
    int progressed = 1;
    while (progressed && !s->oc_failed) {        progressed = 0;
        for (XsdCm* t = s->complex_types; t && !s->oc_failed;
             t = t->next) {
            if (t->oc_checked || !t->first_child) {
                if (!t->oc_checked) t->oc_checked = 1;
                continue;
            }
            XsdCm* model = t->first_child;
            const char* base_name = t->oc_ext_base
                                        ? t->oc_ext_base
                                        : t->res_base;
            if (!base_name) {
                t->oc_checked = 1;
                continue;
            }
            int is_ext = t->oc_ext_base != NULL;
            const char* bns = is_ext
                                  ? (t->oc_ext_base_ns
                                         ? t->oc_ext_base_ns
                                         : t->ns)
                                  : (t->res_base_ns ? t->res_base_ns
                                                    : t->ns);
            XsdCm* base = xsd_find_complex_ns(s, base_name, bns);
            if (!base) base = xsd_find_complex_ns(s, base_name, t->ns);
            if (!base || (!base->oc_checked && base->oc_ext_base)) {
                /* deferred (or chain-pending): retry next round */
                if (base && !base->oc_checked && base->oc_ext_base)
                    continue;
                if (!base) {
                    t->oc_checked = 1;
                    progressed = 1;
                }
                continue;
            }
            t->oc_checked = 1;
            progressed = 1;
            XsdCm* bot = base->first_child;
            XsdCm* bot_any = bot ? bot->open_any : NULL;
            int bot_mode = bot ? bot->oc_mode : 0;
            if (is_ext) {
                if (t->oc_declared == 2) {
                    /* mode="none": blocks the default and keeps the
                     * base's open content (open031) */
                    if (bot_any && !model->open_any) {
                        model->open_any = xsd_cm_copy_list(bot_any);
                        if (model->open_any) {
                            model->oc_owned = 1;
                            model->oc_mode = bot_mode;
                        }
                    }
                } else if (model->open_any) {
                    /* declared OR default-applied: the wildcard
                     * element (§3.4.2.3.3 cl. 5/6) unions over the
                     * base's and rides the ladder (open030/si04:
                     * the default takes precedence over the base) */
                    if (bot_any && !t->oc_unioned) {
                        oc_wildcard_union(model->open_any, bot_any);
                        t->oc_unioned = 1;
                    }
                    /* the ladder (cos-ct-extends.1.4.3.2.2.3/4) */
                    if (bot_any && !s->oc_failed) {
                        if (bot_mode == 2 && model->oc_mode == 1) {
                            xsd_set_error(s,
                                          "extension may not narrow "
                                          "the open content mode "
                                          "from interleave to "
                                          "suffix");
                            s->oc_failed = 1;
                        } else if (!oc_wildcard_subset(bot_any,
                                                       model->open_any)) {
                            xsd_set_error(s,
                                          "the base open content "
                                          "wildcard is not a subset "
                                          "of the derived wildcard");
                            s->oc_failed = 1;
                        }
                    }
                } else {
                    /* nothing declared, no default applied:
                     * inherit (open027) */
                    if (bot_any && !model->open_any) {
                        model->open_any = xsd_cm_copy_list(bot_any);
                        if (model->open_any) {
                            model->oc_owned = 1;
                            model->oc_mode = bot_mode;
                        }
                    }
                }
                /* cos-ct-extends.1.4.3.2.2.1 (s3_4_1si08): the
                 * content types must both be mixed or both
                 * element-only */
                if (!s->oc_failed && bot && bot->first_child &&
                    bot->mixed != t->mixed) {
                    xsd_set_error(s,
                                  "the extension content type and "
                                  "its base must both be mixed or "
                                  "both element-only");
                    s->oc_failed = 1;
                }
            } else {
                /* restriction: the subset checks (suite posture:
                 * an EMPTY derived model exempts them — open020/021/
                 * 022; the declared mode="none" spelling means no
                 * open content at all). Effective OC (declared or
                 * default-applied) over a NON-empty model. */
                if (model->open_any && model->oc_mode &&
                    model->first_child && !s->oc_failed) {
                    if (!bot_any) {
                        xsd_set_error(s,
                                      "a restriction may not add "
                                      "open content to a base "
                                      "without any");
                        s->oc_failed = 1;
                    } else {
                        if (bot_mode == 1 && model->oc_mode == 2) {
                            xsd_set_error(s,
                                          "restriction may not widen "
                                          "the open content mode to "
                                          "interleave");
                            s->oc_failed = 1;
                        } else if (!oc_wildcard_subset(
                                       model->open_any, bot_any)) {
                            xsd_set_error(s,
                                          "the restriction open "
                                          "content wildcard is not "
                                          "a subset of the base's");
                            s->oc_failed = 1;
                        } else if (model->open_any->pc_level >
                                   bot_any->pc_level) {
                            xsd_set_error(s,
                                          "the restriction open "
                                          "content processContents "
                                          "is weaker than the "
                                          "base's");
                            s->oc_failed = 1;
                        }
                    }
                }
            }
        }
    }
    return s->oc_failed ? -1 : 0;
}

static void xsd_path_normalize(const char* in, char* out, size_t outsz) {
    struct {
        const char* p;
        size_t len;
    } comps[512];
    size_t n = 0;
    int is_abs = in[0] == '/';
    const char* q = in;
    while (*q) {
        while (*q == '/') q++;
        if (!*q) break;
        const char* start = q;
        while (*q && *q != '/') q++;
        size_t len = (size_t)(q - start);
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (n > 0)
                n--; /* ".." pops; a leading pop on an absolute
                        path would escape the root — drop it */
            continue;
        }
        if (n < sizeof(comps) / sizeof(comps[0])) {
            comps[n].p = start;
            comps[n].len = len;
            n++;
        }
    }
    size_t o = 0;
    if (is_abs && o < outsz) out[o++] = '/';
    for (size_t i = 0; i < n; i++) {
        if (o && o < outsz && out[o - 1] != '/') out[o++] = '/';
        if (o + comps[i].len >= outsz) break;
        memcpy(out + o, comps[i].p, comps[i].len);
        o += comps[i].len;
    }
    if (o == 0 && outsz > 1) out[o++] = '.';
    out[o] = 0;
}

static int xsd_visited_has(struct leptris_xsd_schema* s,
                           const char* path) {
    for (size_t i = 0; i < s->visited_len; i++)
        if (strcmp(s->visited[i], path) == 0) return 1;
    return 0;
}

struct xsd_sub_doc {
    LeptrisDocument doc;
    struct xsd_sub_doc* next;
};

static void xsd_sub_doc_retain(struct leptris_xsd_schema* s,
                               LeptrisDocument doc) {
    struct xsd_sub_doc* node =
        (struct xsd_sub_doc*)calloc(1, sizeof(*node));
    if (!node) return; /* retention is best-effort; the freed-elem
                          lazy path is never taken for eagerly
                          captured defs */
    node->doc = doc;
    node->next = (struct xsd_sub_doc*)s->sub_docs;
    s->sub_docs = node;
}

static void xsd_visited_add(struct leptris_xsd_schema* s,
                            const char* path) {
    if (s->visited_len == s->visited_cap) {
        size_t nc = s->visited_cap ? s->visited_cap * 2 : 8;
        char** nv = (char**)realloc(s->visited, nc * sizeof(*nv));
        if (!nv) return; /* dedup degrades to the old behavior */
        s->visited = nv;
        s->visited_cap = nc;
    }
    char* dup = xsd_strdup(path);
    if (!dup) return;
    s->visited[s->visited_len++] = dup;
}

/* #2184: the redefine/override chase ancestor set. The stack holds
 * every target document currently being chased; the cycle check
 * matches a new target against it (and the entry document's
 * self_path) BEFORE the visited dedup swallows the closing edge. */
static int xsd_ro_stack_has(struct leptris_xsd_schema* s,
                            const char* path) {
    for (size_t i = 0; i < s->ro_len; i++)
        if (strcmp(s->ro_stack[i], path) == 0) return 1;
    return 0;
}

static void xsd_ro_stack_push(struct leptris_xsd_schema* s,
                              const char* path) {
    if (s->ro_len == s->ro_cap) {
        size_t nc = s->ro_cap ? s->ro_cap * 2 : 8;
        char** ns = (char**)realloc(s->ro_stack, nc * sizeof(*ns));
        if (!ns) return;
        s->ro_stack = ns;
        s->ro_cap = nc;
    }
    char* dup = xsd_strdup(path);
    if (!dup) return;
    s->ro_stack[s->ro_len++] = dup;
}

static void xsd_ro_stack_pop(struct leptris_xsd_schema* s) {
    if (!s->ro_len) return;
    free(s->ro_stack[--s->ro_len]);
}

/* #2184: an override/redefine body with NO declaration children
 * contributes nothing — src-override.3 keeps the empty-body mutual
 * override legal (over023's I_base shape), and an empty redefine is
 * the include equivalent. Such a directive never closes a cycle. */
static int xsd_override_body_empty(LeptrisElement e) {
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(e));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* cl = NULL;
        const char* cu = NULL;
        leptris_element_expanded_name((LeptrisElement)c, &cl, NULL, &cu);
        if (cl && cu && strcmp(cu, XSD_NS) == 0 &&
            xsd_is_declaration(cl, cu))
            return 0;
    }
    return 1;
}

static XsdAttrDecl* xsd_capture_attr_rows(
    struct leptris_xsd_schema* s, LeptrisElement container, int depth);
/* #1880: top-level attributeGroup definitions are captured in a
 * PRE-PASS — the symbol space is order-free, but the ref expansion
 * in xsd_capture_attr_rows looks up s->attr_groups at capture time,
 * so a group declared AFTER the referencing type used to vanish. */
/* Phase 1 registers the def (name + element) only; phase 2
 * captures the rows once EVERY top-level group is registered —
 * leptris#1957: an inner attributeGroup ref naming a group that
 * appears LATER in the document used to expand empty. */
static void xsd_register_attr_group_def(struct leptris_xsd_schema* s,
                                        LeptrisElement n,
                                        const char* nm) {
    XsdAttrGroupDef* g = (XsdAttrGroupDef*)calloc(1, sizeof(*g));
    if (!g) return;
    g->name = xsd_strdup(nm);
    g->elem = n;
    g->next = s->attr_groups;
    s->attr_groups = g;
}

/* ---- #2150 §4.3.2: the overridden document's merged component set
 *
 * The set the override BODY is checked against: the overridden
 * document's own named top-level components, plus — recursively —
 * the sets of the documents it includes, plus the named components
 * of its own override/redefine bodies and those bodies' targets'
 * sets (a schema document that is itself an overriding document
 * contributes its overrides' results to its component set — the
 * over009 shape: its para IS a component even though it was
 * captured through its own override element). Namespace-blind: the
 * capture stamps both sides with the same effective target
 * namespace, and the misaligned forms were rejected pre-capture
 * (src-override.1). */

static int xsd_override_set_add(struct leptris_xsd_schema* s,
                                const char* kind, const char* name) {
    if (!name || !name[0]) return 0;
    if (s->override_set_len == s->override_set_cap) {
        size_t nc = s->override_set_cap ? s->override_set_cap * 2
                                        : 32;
        struct xsd_ovr_name* nv = (struct xsd_ovr_name*)realloc(
            s->override_set, nc * sizeof(*nv));
        if (!nv) return 0;
        s->override_set = nv;
        s->override_set_cap = nc;
    }
    struct xsd_ovr_name* slot =
        &s->override_set[s->override_set_len++];
    snprintf(slot->kind, sizeof(slot->kind), "%s", kind);
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    return 1;
}

static int xsd_override_corresponds(struct leptris_xsd_schema* s,
                                    const char* kind,
                                    const char* name) {
    return xsd_ovr_comp_has(s->override_set, s->override_set_len,
                            kind, name);
}

/* The (kind, name) pairs an override/redefine BODY declares ride the
 * set too (see above). */
static void xsd_override_set_add_body(struct leptris_xsd_schema* s,
                                      LeptrisElement ovr) {
    struct xsd_ovr_name body[64];
    size_t nbody = 0;
    xsd_ovr_collect_body(ovr, body, &nbody, 64);
    for (size_t i = 0; i < nbody; i++)
        xsd_override_set_add(s, body[i].kind, body[i].name);
}

static void xsd_override_set_collect(struct leptris_xsd_schema* s,
                                     LeptrisElement root,
                                     const char* base_dir, int depth,
                                     char (*visited)[4096],
                                     int* nvisited) {
    if (!root || depth > 6) return;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* l = NULL;
        const char* u = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &l, NULL, &u);
        if (!l || (u && strcmp(u, XSD_NS) != 0)) continue;
        if (xsd_ovr_is_component(l)) {
            xsd_override_set_add(
                s, l,
                leptris_element_attribute((LeptrisElement)n, "name"));
            continue;
        }
        int is_ref = (strcmp(l, "include") == 0 ||
                      strcmp(l, "redefine") == 0 ||
                      strcmp(l, "override") == 0);
        if (!is_ref) continue;
        /* the body's declared components ride the set */
        xsd_override_set_add_body(s, (LeptrisElement)n);
        const char* loc = leptris_element_attribute(
            (LeptrisElement)n, "schemaLocation");
        if (!loc || !base_dir) continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", base_dir, loc);
        char canon[4096];
        xsd_path_normalize(path, canon, sizeof(canon));
        int seen = 0;
        for (int i = 0; i < *nvisited; i++)
            if (strcmp(visited[i], canon) == 0) seen = 1;
        if (seen) continue;
        if (*nvisited < 8)
            snprintf(visited[(*nvisited)++], 4096, "%s", canon);
        size_t flen = 0;
        char* text = xsd_read_file(path, &flen);
        if (!text) continue;
        LeptrisDocument sub = leptris_parse_string(text, flen, NULL);
        free(text);
        if (!sub) continue;
        LeptrisElement subroot = leptris_document_root(sub);
        if (subroot) {
            char sub_base[4096];
            xsd_dirname_into(canon, sub_base, sizeof(sub_base));
            xsd_override_set_collect(s, subroot, sub_base, depth + 1,
                                     visited, nvisited);
        }
        leptris_document_free(sub);
    }
}

/* The capture gate: an overriding body component that overrides
 * nothing is IGNORED (§4.3.2) — dropped from the merged schema. */
static int xsd_override_gate(struct leptris_xsd_schema* s,
                             const char* local, LeptrisElement n) {
    if (!s->override_active) return 0;
    const char* nm = leptris_element_attribute(n, "name");
    if (!nm) return 0;
    return !xsd_override_corresponds(s, local, nm);
}

static void xsd_capture_root(struct leptris_xsd_schema* s,
                             LeptrisElement root) {
    /* pre-pass phase 1: register every top-level attributeGroup def
     * (name + element), document order — the symbol space is
     * order-free. Phase 2 captures the rows once ALL defs are
     * registered, so an inner attributeGroup ref naming a LATER
     * group resolves (leptris#1957: level-2 members used to
     * vanish). */
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* local = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &local,
                                      NULL, &uri);
        if (!local || strcmp(local, "attributeGroup") != 0 ||
            !uri || strcmp(uri, XSD_NS) != 0)
            continue;
        const char* nm = leptris_element_attribute((LeptrisElement)n,
                                                   "name");
        if (!nm) continue;
        /* #2150 §4.3.2: an overriding body group that overrides
         * nothing is ignored — not registered, not captured. */
        if (xsd_override_gate(s, local, (LeptrisElement)n)) continue;
        xsd_register_attr_group_def(s, (LeptrisElement)n, nm);
    }
    for (XsdAttrGroupDef* g = s->attr_groups; g; g = g->next)
        if (!g->attrs && g->elem)
            g->attrs = xsd_capture_attr_rows(s, g->elem, 0);
    /* #2182 §3.4.2.5: capture THIS document's @defaultAttributes
     * list (a whitespace-separated QName list, document order).
     * Each QName resolves against the xs:schema element's in-scope
     * namespace declarations (§3.17.6.2 — an unprefixed QName rides
     * the default namespace; open035's bare name meets the schema's
     * default xmlns). The unit's list applies to that unit's
     * complexTypes in the post-capture pass; resolution is checked
     * for the WHOLE list regardless of any type's opt-out. */
    {
        const char* da =
            leptris_element_attribute(root, "defaultAttributes");
        XsdDefAttrRef* refs = NULL;
        XsdDefAttrRef* refs_tail = NULL;
        const char* p = da ? da : NULL;
        while (p && *p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' ||
                   *p == '\r')
                p++;
            if (!*p) break;
            const char* tok = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                   *p != '\r')
                p++;
            size_t tl = (size_t)(p - tok);
            char qname[512];
            if (tl >= sizeof(qname)) continue;
            memcpy(qname, tok, tl);
            qname[tl] = '\0';
            char uri[256], local[256];
            xsd_resolve_qname(root, qname, uri, sizeof(uri), local,
                              sizeof(local));
            XsdDefAttrRef* r =
                (XsdDefAttrRef*)calloc(1, sizeof(*r));
            if (!r) continue;
            r->local = xsd_strdup(local);
            if (strchr(qname, ':') && !local[0]) {
                /* prefixed QName with no in-scope binding —
                 * resolution fails (§3.17.6.2) */
                ;
            } else if (uri[0]) {
                r->ns = xsd_strdup(uri);
            } else {
                r->ns = xsd_strdup("");
            }
            if (!r->local) {
                free(r);
                continue;
            }
            if (refs_tail)
                refs_tail->next = r;
            else
                refs = r;
            refs_tail = r;
        }
        if (refs) {
            XsdUnitDefAttrs* ud =
                (XsdUnitDefAttrs*)calloc(1, sizeof(*ud));
            if (ud) {
                ud->unit = s->current_unit;
                ud->refs = refs;
                ud->next = s->unit_defattrs;
                s->unit_defattrs = ud;
            } else {
                while (refs) {
                    XsdDefAttrRef* rn = refs->next;
                    free(refs->local);
                    free(refs->ns);
                    free(refs);
                    refs = rn;
                }
            }
        }
    }
    /* #2151: capture THIS document's defaultOpenContent up front so
     * declarations that precede it in document order (a redefine
     * body — open042) still see it; the in-order walk below keeps
     * the PLACEMENT rule (s3_4_1si02: defaultOpenContent may follow
     * only include/import/redefine/override/annotation). */
    int oc_order_idx = -1;
    int walk_idx = 0;
    if ((s->version_11 || s->saw_open_content) && !s->default_open) {
        walk_idx = 0;
        for (LeptrisNodeRef n =
                 leptris_node_first_child(
                     leptris_element_as_node(root));
             n && !s->default_open;
             n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement e = (LeptrisElement)n;
            const char* el = NULL;
            const char* eu = NULL;
            leptris_element_expanded_name(e, &el, NULL, &eu);
            if (!el || !eu || strcmp(eu, XSD_NS) != 0 ||
                strcmp(el, "defaultOpenContent") != 0) {
                walk_idx++;
                continue;
            }
            oc_order_idx = walk_idx;
            s->default_oc_mode = 0;
            const char* dmode =
                leptris_element_attribute(e, "mode");
            if (dmode && strcmp(dmode, "suffix") == 0)
                s->default_oc_mode = 1;
            else if (dmode && strcmp(dmode, "interleave") == 0)
                s->default_oc_mode = 2;
            else
                s->default_oc_mode = 2; /* S4SD default */
            for (LeptrisNodeRef q =
                     leptris_node_first_child(
                         leptris_element_as_node(e));
                 q && !s->default_open;
                 q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (ql && strcmp(ql, "any") == 0)
                    s->default_open = xsd_capture_any(s, qe);
            }
            if (s->default_open) {
                const char* ape = leptris_element_attribute(
                    e, "appliesToEmpty");
                s->default_oc_empty =
                    (ape && strcmp(ape, "true") == 0);
                s->default_oc_unit_id = s->current_unit;
            } else {
                s->default_oc_mode = 0;
            }
        }
    }
    int oc_misplaced = 0;
    int oc_walk_pos = 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* local = NULL;
        const char* prefix = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &local, &prefix,
                                      &uri);
        /* #2151: the defaultOpenContent placement rule — only
         * include/import/redefine/override/annotation may precede it
         * (s3_4_1si02). */
        if (oc_order_idx >= 0 && local) {
            if (oc_walk_pos == oc_order_idx)
                ;
            else if (oc_walk_pos < oc_order_idx &&
                     strcmp(local, "include") != 0 &&
                     strcmp(local, "import") != 0 &&
                     strcmp(local, "redefine") != 0 &&
                     strcmp(local, "override") != 0 &&
                     strcmp(local, "annotation") != 0)
                oc_misplaced = 1;
        }
        oc_walk_pos++;
        if (!xsd_is_declaration(local, uri)) continue;
        s->declaration_count++;

        if (strcmp(local, "include") == 0 ||
            strcmp(local, "import") == 0 ||
            strcmp(local, "redefine") == 0 ||
            strcmp(local, "override") == 0) {
            const char* loc = leptris_element_attribute(
                (LeptrisElement)n, "schemaLocation");
            if (loc && s->base_dir && s->include_depth < 16) {
                char path[4096];
                snprintf(path, sizeof(path), "%s/%s", s->base_dir,
                         loc);
                /* leptris#1644: capture each schema unit once per
                 * compile — mark before recursing so cycles close
                 * (w.xsd includes a1.xsd includes w.xsd). */
                char canon[4096];
                xsd_path_normalize(path, canon, sizeof(canon));
                /* #2184 §4.2.2/§4.3: a redefine/override location
                 * graph must not close back on itself (the IBM
                 * cyclic-redefine family — si02c redefines si02
                 * redefines si02b redefines si02c). A target equal
                 * to the CONTAINING document is the legal
                 * self-reference hook; a target closing the ancestor
                 * chain (or the entry document) is the error. The
                 * check runs BEFORE the visited dedup, which is
                 * exactly where the closing edge would be silently
                 * swallowed. Include/import cycles stay legal. */
                if (strcmp(local, "redefine") == 0 ||
                    strcmp(local, "override") == 0) {
                    int closes =
                        xsd_ro_stack_has(s, canon) ||
                        (s->self_path && strcmp(canon, s->self_path) == 0);
                    int self_ref =
                        s->ro_len > 0 &&
                        strcmp(s->ro_stack[s->ro_len - 1], canon) == 0;
                    /* an empty body contributes nothing — the mutual
                     * override whose closing form replaces nothing
                     * stays legal (src-override.3, over023) */
                    int empty_body = xsd_override_body_empty(
                        (LeptrisElement)n);
                    if (closes && !self_ref && !empty_body)
                        s->ro_cyclic = 1;
                }
                /* #2208 §4.3/§4.2.2: an override/redefine TARGET
                 * already in the visited set — the base landed as an
                 * earlier set member, or an earlier import chased it
                 * — must still trigger the directive: the BODY's
                 * components replace the target's same-named ones
                 * (§4.3) or fold the hook (§4.2.2) even though the
                 * target itself is not re-captured. Skipping only the
                 * target's capture keeps include/import of an
                 * already-captured unit contributing nothing. */
                int ovr_seen =
                    (strcmp(local, "override") == 0 ||
                     strcmp(local, "redefine") == 0) &&
                    xsd_visited_has(s, canon);
                if (xsd_visited_has(s, canon) && !ovr_seen) continue;
                if (!ovr_seen) xsd_visited_add(s, canon);
                if (strcmp(local, "redefine") == 0 ||
                    strcmp(local, "override") == 0)
                    xsd_ro_stack_push(s, canon);
                /* The SUBFILE's own schemaLocations resolve
                 * against ITS directory, not the compiled
                 * file's (#440 shape A): wml-2010 -> word12 ->
                 * ../iso/wml.xsd, and wml.xsd's same-dir
                 * shared-*.xsd siblings would resolve against
                 * microsoft/ and silently vanish — truncating
                 * every content model referencing them. */
                char* saved_dir = s->base_dir;
                char* sub_dir = NULL;
                const char* lslash = strrchr(canon, '/');
                if (lslash) {
                    sub_dir = strndup(canon, lslash - canon);
                    if (sub_dir) s->base_dir = sub_dir;
                }
                size_t flen = 0;
                char* text = xsd_read_file(path, &flen);
                if (text) {
                    LeptrisDocument sub =
                        leptris_parse_string(text, flen, NULL);
                    if (sub) {
                        LeptrisElement subroot =
                            leptris_document_root(sub);
                        const char* tns =
                            subroot
                                ? leptris_element_attribute(
                                      subroot, "targetNamespace")
                                : NULL;
                        /* same-namespace merges and no-namespace
                         * chameleon includes; foreign-namespace
                         * imports stay lax at this tier */
                        /* xs:include: same-namespace (or
                         * chameleon — the no-tns form adopts ours).
                         * xs:import: foreign namespaces are the
                         * point — merge regardless (#1626: wml-2010
                         * shape imports were skipped when the
                         * compiler had its own targetNamespace). */
                        int same_ns = subroot && (!tns || !s->target_ns ||
                                                  strcmp(tns, s->target_ns) == 0);
                        /* #2208: an already-captured override/redefine
                         * target re-parses only to feed the directive
                         * arm below — its components are in the tables
                         * from the first capture, and re-capturing
                         * them would duplicate every declaration. */
                        if (subroot && !ovr_seen &&
                            (strcmp(local, "import") == 0 || same_ns)) {
                            /* leptris-ruby#440/#439: each captured
                             * unit stamps ITS OWN targetNamespace on
                             * its declarations — chameleon INCLUDES
                             * (no tns) adopt the includer's.
                             * leptris#1958: an IMPORT of a
                             * no-targetNamespace document keeps its
                             * declarations namespace-FREE (XSD 1.0
                             * §4.2.3 — chameleon semantics are
                             * include-only); stamping the importer's
                             * tns made its same-named components
                             * shadow the importing namespace's own
                             * (schF1: the imported ct-A won the
                             * (ns-a, ct-A) exact ladder slot and
                             * swapped a1/a2's types). */
                            char* saved_ns = s->target_ns;
                            char* sub_ns = tns ? xsd_strdup(tns) : NULL;
                            if (sub_ns) s->target_ns = sub_ns;
                            else if (strcmp(local, "import") == 0)
                                s->target_ns = NULL;
                            /* #2151: the included unit is its own
                             * schema document — the includer's
                             * defaultOpenContent does not apply to
                             * its types (open040/open041), its own
                             * does. An override/redefine body (below)
                             * stays in the INCLUDING document, so it
                             * keeps the includer's default (open042).
                             * The unit's evidence re-arms the
                             * 1.1-syntax gate for open048's included
                             * openContent rule. */
                            if (!s->saw_open_content)
                                s->saw_open_content =
                                    xsd_doc_has_open_content(subroot,
                                                             0);
                            int saved_unit = s->current_unit;
                            if (s->unit_count < s->unit_cap ||
                                (s->units = (char**)realloc(
                                     s->units,
                                     (s->unit_cap
                                          ? s->unit_cap * 2
                                          : 8) *
                                     sizeof(char*)))) {
                                if (!s->unit_cap) s->unit_cap = 8;
                                char* un = xsd_strdup(canon);
                                if (un) {
                                    s->units[s->unit_count++] = un;
                                    s->current_unit =
                                        (int)s->unit_count - 1;
                                }
                            }
                            XsdCm* saved_def = s->default_open;
                            int saved_empty = s->default_oc_empty;
                            int saved_mode = s->default_oc_mode;
                            int saved_def_unit = s->default_oc_unit_id;
                            s->default_open = NULL;
                            s->default_oc_empty = 0;
                            s->default_oc_mode = 0;
                            s->default_oc_unit_id = 0;
                            s->include_depth++;
                            xsd_capture_root(s, subroot);
                            s->include_depth--;
                            /* the sub-document's own default stays
                             * alive (types borrowed it) — pool it */
                            XsdCm* sub_default = s->default_open;
                            s->default_open = saved_def;
                            if (sub_default) {
                                /* retain: captured types borrow the
                                 * wildcard — free with the schema */
                                if (!s->oc_default_pool)
                                    s->oc_default_pool = sub_default;
                                else {
                                    XsdCm* tail = s->oc_default_pool;
                                    while (tail->next)
                                        tail = tail->next;
                                    tail->next = sub_default;
                                }
                            }
                            s->default_oc_empty = saved_empty;
                            s->default_oc_mode = saved_mode;
                            s->default_oc_unit_id = saved_def_unit;
                            s->current_unit = saved_unit;
                            if (sub_ns) free(sub_ns);
                            s->target_ns = saved_ns;
                        }
                        /* leptris#1913 (XSD 1.1 §I.4) + #1926 (XSD
                         * 1.0 §4.2.2): the override/redefine
                         * element's OWN declaration children replace
                         * same-named components of the base just
                         * captured — capture them AFTER the base so
                         * the lookup ladder (head-first) sees the
                         * replacements. A redefine body that EXTENDS
                         * the redefined name folds its extension
                         * against the base at this moment (base
                         * captured, ladder sees the new head), which
                         * is exactly the redefine hook. */
                        if (subroot && same_ns &&
                            (strcmp(local, "override") == 0 ||
                             strcmp(local, "redefine") == 0)) {
                            /* #2150 §4.3.2: build the overridden
                             * document's merged component set — the
                             * body's components outside it are
                             * ignored. The sub-documents' locations
                             * resolve against the sub directory
                             * (s->base_dir is already swapped). */
                            if (strcmp(local, "override") == 0) {
                                s->override_active = 1;
                                s->override_bodies++;
                                s->override_set_len = 0;
                                char ovisited[8][4096];
                                int nvisited = 0;
                                xsd_override_set_collect(
                                    s, subroot, s->base_dir, 0,
                                    ovisited, &nvisited);
                            }
                            s->include_depth++;
                            s->capture_force = 1;
                            /* #2151: the suite posture (open042 vs
                             * open043) — a REDEFINE body's types ride
                             * the including document's
                             * defaultOpenContent; an OVERRIDE body's
                             * types do not (the override replaces
                             * whole components of the OTHER
                             * document). */
                            XsdCm* saved_ovr = NULL;
                            int saved_ovr_e = 0, saved_ovr_m = 0,
                                saved_ovr_u = 0;
                            int saved_da_supp =
                                s->defattr_suppressed;
                            if (strcmp(local, "override") == 0) {
                                saved_ovr = s->default_open;
                                saved_ovr_e = s->default_oc_empty;
                                saved_ovr_m = s->default_oc_mode;
                                saved_ovr_u = s->default_oc_unit_id;
                                s->default_open = NULL;
                                s->default_oc_empty = 0;
                                s->default_oc_mode = 0;
                                s->default_oc_unit_id = 0;
                                /* #2182: the override body's types
                                 * ride the OVERRIDDEN document's
                                 * defaultAttributes (§4.2.5) —
                                 * capture suppressed (open045). */
                                s->defattr_suppressed = 1;
                            }
                            xsd_capture_root(s, (LeptrisElement)n);
                            if (strcmp(local, "override") == 0) {
                                /* an override body may declare its
                                 * own defaultOpenContent — pool it
                                 * (types borrowed it) */
                                XsdCm* ovr_default = s->default_open;
                                s->default_open = saved_ovr;
                                s->default_oc_empty = saved_ovr_e;
                                s->default_oc_mode = saved_ovr_m;
                                s->default_oc_unit_id = saved_ovr_u;
                                s->defattr_suppressed =
                                    saved_da_supp;
                                if (ovr_default &&
                                    ovr_default != saved_ovr) {
                                    if (!s->oc_default_pool)
                                        s->oc_default_pool = ovr_default;
                                    else {
                                        XsdCm* tail =
                                            s->oc_default_pool;
                                        while (tail->next)
                                            tail = tail->next;
                                        tail->next = ovr_default;
                                    }
                                }
                            }
                            s->capture_force = 0;
                            s->include_depth--;
                            s->override_active = 0;
                            /* leptris#1958 (schP2): a REDEFINED
                             * simpleType's restriction base names the
                             * ORIGINAL definition — both share the
                             * name, and the name-only chain lookup
                             * (head-first = the replacement) would
                             * self-loop. Rename the original to a
                             * hidden alias and repoint the
                             * replacement's base at it; every OTHER
                             * reference keeps resolving to the
                             * replacement (head-first). OVERRIDE does
                             * NOT get this: its references re-resolve
                             * against the POST-merge set, so a
                             * self-named base is a circular
                             * derivation (#2150) — caught by
                             * xsd_check_derivation_cycles. */
                            if (strcmp(local, "redefine") == 0) {
                            for (XsdSimple* t = s->simple_types; t;
                                 t = t->next) {
                                if (!t->base || strcmp(t->base, t->name) != 0)
                                    continue;
                                XsdSimple* other = NULL;
                                for (XsdSimple* o = s->simple_types; o;
                                     o = o->next) {
                                    if (o == t) continue;
                                    if (o->name &&
                                        strcmp(o->name, t->name) == 0) {
                                        other = o;
                                        break;
                                    }
                                }
                                if (!other) continue;
                                size_t alen = strlen(other->name) + 24;
                                char* alias =
                                    (char*)malloc(alen);
                                if (!alias) continue;
                                snprintf(alias, alen, "%s#rb%p",
                                         other->name, (void*)other);
                                free(other->name);
                                other->name = alias;
                                free(t->base);
                                t->base = xsd_strdup(alias);
                            }
                            }
                        }
                        /* leptris#1957: retained — attributeGroup
                         * defs hold elements from this unit for the
                         * lazy ref expansion; freed at teardown. */
                        xsd_sub_doc_retain(s, sub);
                    }
                    free(text);
                }
                /* base_dir is restored on EVERY exit path — an
                 * unreadable subfile must not leave the parent's
                 * base_dir lost (and its strdup leaked) while the
                 * later imports resolve against the subfile's
                 * directory. */
                if (sub_dir) {
                    s->base_dir = saved_dir;
                    free(sub_dir);
                }
                /* #2184: the chased redefine/override target leaves
                 * the ancestor chain (mirrors the push after the
                 * visited add). */
                if (strcmp(local, "redefine") == 0 ||
                    strcmp(local, "override") == 0)
                    xsd_ro_stack_pop(s);
            }
            continue;
        }

        if (strcmp(local, "element") == 0 && uri && !strcmp(uri, XSD_NS)) {
            /* #2150 §4.3.2: an overriding body component that
             * overrides nothing is ignored (dropped) */
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            const char* nm = leptris_element_attribute((LeptrisElement)n,
                                                       "name");
            const char* ty = leptris_element_attribute((LeptrisElement)n,
                                                       "type");
            if (nm && ty) {
                XsdElementDecl* d =
                    (XsdElementDecl*)calloc(1, sizeof(*d));
                if (d) {
                    d->name = xsd_strdup(nm);
                    if (s->target_ns) d->ns = xsd_strdup(s->target_ns);
                    d->type = xsd_strdup(xsd_local_name(ty));
                    d->alternatives =
                        xsd_capture_alternatives(s, (LeptrisElement)n);
                    d->fixed = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "fixed"));
                    d->default_value = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "default"));
                    const char* sg = leptris_element_attribute(
                        (LeptrisElement)n, "substitutionGroup");
                    if (sg) {
                        const char* colon = strchr(sg, ':');
                        d->sub_head = xsd_strdup(
                            colon ? colon + 1 : sg);
                    }
                    d->next = s->elements;
                    s->elements = d;
                }
                xsd_capture_ics(s, (LeptrisElement)n, nm);
            } else if (nm) {
                /* slice 6: identity constraints ride the element. */
                xsd_capture_ics(s, (LeptrisElement)n, nm);
                /* #1592: inline anonymous type — capture under the
                 * synthesized slot and point the declaration at it. */
                char synth[512];
                snprintf(synth, sizeof(synth), "element:%s", nm);
                /* moxml#466: the declaration is captured even when
                 * the element carries no inline type (a bare
                 * <xs:element name=... substitutionGroup=.../> member
                 * has none) — the strict root walk and the head
                 * chain both need the row to exist. */
                int has_inline =
                    xsd_capture_inline(s, (LeptrisElement)n, synth);
                XsdElementDecl* d =
                    (XsdElementDecl*)calloc(1, sizeof(*d));
                if (d) {
                    d->name = xsd_strdup(nm);
                    if (s->target_ns) d->ns = xsd_strdup(s->target_ns);
                    if (has_inline)
                        d->type = xsd_strdup(synth);
                    d->alternatives =
                        xsd_capture_alternatives(s, (LeptrisElement)n);
                    /* #1936 (stE): the fixed VALUE constraint rides
                     * the decl whatever the type shape — inline
                     * anonymous types used to lose it entirely. */
                    d->fixed = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "fixed"));
                    d->default_value = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "default"));
                    const char* sg = leptris_element_attribute(
                        (LeptrisElement)n, "substitutionGroup");
                    if (sg) {
                        const char* colon = strchr(sg, ':');
                        d->sub_head = xsd_strdup(
                            colon ? colon + 1 : sg);
                    }
                    d->next = s->elements;
                    s->elements = d;
                }
            }
        } else if (strcmp(local, "complexType") == 0 && uri &&
                   !strcmp(uri, XSD_NS)) {
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm)
                xsd_capture_complex_model(s, (LeptrisElement)n, nm);
        } else if (strcmp(local, "defaultOpenContent") == 0 && uri &&
                   !strcmp(uri, XSD_NS)) {
            /* #2151: captured in the pre-pass above (order-independence
             * for redefine bodies); this in-order visit only confirms
             * the placement rule (checked after the loop). */
            continue;
        } else if (strcmp(local, "attribute") == 0 && uri && !strcmp(uri, XSD_NS)) {
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
                if (d) {
                    d->name = xsd_strdup(nm);
                    d->type = xsd_strdup(xsd_local_name(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "type")));
                    d->fixed = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "fixed"));
                    const char* use = leptris_element_attribute(
                        (LeptrisElement)n, "use");
                    d->required = (use && strcmp(use, "required") == 0);
                    d->prohibited =
                        (use && strcmp(use, "prohibited") == 0);
                    d->inheritable =
                        xsd_bool_attr((LeptrisElement)n, "inheritable",
                                      -1);
                    /* #679: a TOP-LEVEL attribute declaration is
                     * always qualified with the unit's
                     * targetNamespace (§3.2.3) — mirror the global
                     * element stamp. */
                    if (s->target_ns) d->ns = xsd_strdup(s->target_ns);
                    d->next = s->top_attrs;
                    s->top_attrs = d;
                }
            }
        } else if (strcmp(local, "attributeGroup") == 0 && uri && !strcmp(uri, XSD_NS)) {
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            /* the #1880 pre-pass already captured it — a second
             * registration would shadow the first */
            if (nm) {
                XsdAttrGroupDef* seen = s->attr_groups;
                for (; seen; seen = seen->next)
                    if (strcmp(seen->name, nm) == 0) break;
                if (!seen || s->capture_force) {
                    /* Sub-document defs register + capture inline —
                     * their own inner refs ride the same lazy
                     * expansion (leptris#1957). */
                    xsd_register_attr_group_def(s, (LeptrisElement)n, nm);
                    XsdAttrGroupDef* rg = s->attr_groups;
                    if (rg && !rg->attrs)
                        rg->attrs =
                            xsd_capture_attr_rows(s, rg->elem, 0);
                }
            }
        } else if (strcmp(local, "group") == 0 && uri && !strcmp(uri, XSD_NS)) {
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdGroupDef* g = (XsdGroupDef*)calloc(1, sizeof(*g));
                if (g) {
                    g->name = xsd_strdup(nm);
                    g->model = xsd_cm_parse(s, (LeptrisElement)n, NULL);
                    g->next = s->groups;
                    s->groups = g;
                }
            }
        } else if (strcmp(local, "simpleType") == 0 && uri && !strcmp(uri, XSD_NS)) {
            if (xsd_override_gate(s, local, (LeptrisElement)n))
                continue;
            XsdSimple* captured = xsd_capture_simple((LeptrisElement)n);
            if (captured) {
                /* leptris-ruby#440: stamp the declaring unit's
                 * targetNamespace — same-name simpleTypes across
                 * imported namespaces resolve by (ns, name)
                 * (shared-math ST_Jc vs wml ST_Jc). */
                if (s->target_ns) captured->ns = xsd_strdup(s->target_ns);
                captured->next = s->simple_types;
                s->simple_types = captured;
            }
        }
    }
    if (oc_misplaced) {
        xsd_set_error(s,
                      "defaultOpenContent must precede all "
                      "declarations other than include, import, "
                      "redefine, override and annotation");
        s->oc_failed = 1;
    }
}

static void xsd_free_simple_one(XsdSimple* t) {
    if (!t) return;
    free(t->name);
    free(t->ns);
    free(t->base);
    XsdFacetValue* pv = t->patterns;
    while (pv) {
        XsdFacetValue* pn = pv->next;
        free(pv->text);
        free(pv);
        pv = pn;
    }
    XsdFacetValue* fv = t->enum_values;
    while (fv) {
        XsdFacetValue* fn = fv->next;
        free(fv->text);
        free(fv);
        fv = fn;
    }
    free(t->min_inclusive);
    free(t->min_exclusive);
    free(t->max_inclusive);
    free(t->max_exclusive);
    free(t->explicit_timezone);
    free(t->whitespace);
    /* #2154: the accumulated assertion facet list */
    XsdAssertionFacet* av = t->assertions;
    while (av) {
        XsdAssertionFacet* an = av->next;
        if (av->q) leptris_xquery_free(av->q);
        free(av->test);
        free(av->ns_prolog);
        free(av);
        av = an;
    }
    free(t->item_type);
    xsd_free_simple_one(t->item_def);
    xsd_free_simple_one(t->base_def);
    for (size_t i = 0; i < t->member_count; i++) free(t->members[i]);
    free(t->members);
    for (size_t i = 0; i < t->member_def_count; i++)
        xsd_free_simple_one(t->member_defs[i]);
    free(t->member_defs);
    free(t);
}

LEPTRIS_API void leptris_xsd_free(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return;
    /* leptris#1957: retained sub-documents last — attributeGroup
     * defs' LeptrisElement pointers live inside them. */
    struct xsd_sub_doc* sd = (struct xsd_sub_doc*)s->sub_docs;
    while (sd) {
        struct xsd_sub_doc* sn = sd->next;
        leptris_document_free(sd->doc);
        free(sd);
        sd = sn;
    }
    for (size_t i = 0; i < s->ro_len; i++) free(s->ro_stack[i]);
    free(s->ro_stack);
    free(s->report);
    xsd_cm_free(s->complex_types);
    XsdElementIcs* slot = s->element_ics;
    while (slot) {
        XsdElementIcs* sn = slot->next;
        XsdIc* ic = slot->constraints;
        while (ic) {
            XsdIc* in = ic->next;
            free(ic->name);
            free(ic->selector);
            for (int b = 0; b < ic->sel_count; b++)
                for (int i = 0; i < ic->sel[b].step_count; i++) {
                    free(ic->sel[b].steps[i].ns);
                    free(ic->sel[b].steps[i].local);
                }
            for (size_t f = 0; f < ic->field_count; f++) {
                free(ic->fields[f]);
                for (int i = 0; i < ic->fpaths[f].step_count; i++) {
                    free(ic->fpaths[f].steps[i].ns);
                    free(ic->fpaths[f].steps[i].local);
                }
            }
            free(ic->refer);
            free(ic->ref_local);
            free(ic);
            ic = in;
        }
        free(slot->element_name);
        free(slot);
        slot = sn;
    }
    XsdTypeAttrs* ta = s->type_attrs;
    while (ta) {
        XsdTypeAttrs* tn = ta->next;
        free(ta->ns);
        XsdAttrDecl* a = ta->attrs;
        while (a) {
            XsdAttrDecl* an = a->next;
            free(a->name);
            free(a->type);
            free(a->fixed);
            free(a->ns);
            free(a);
            a = an;
        }
        free(ta->type_name);
        free(ta->any_attr_ns);
        free(ta->any_attr_not_qname);
        free(ta->any_attr_not_ns);
        free(ta->any_attr_unit_ns);
        free(ta);
        ta = tn;
    }
    /* #2182: the per-unit defaultAttributes lists */
    XsdUnitDefAttrs* ud = s->unit_defattrs;
    while (ud) {
        XsdUnitDefAttrs* un = ud->next;
        XsdDefAttrRef* r = ud->refs;
        while (r) {
            XsdDefAttrRef* rn = r->next;
            free(r->local);
            free(r->ns);
            free(r);
            r = rn;
        }
        free(ud);
        ud = un;
    }
    XsdElementDecl* d = s->elements;
    while (d) {
        XsdElementDecl* dn = d->next;
        free(d->ns);
        XsdAlternative* a = d->alternatives;
        while (a) {
            XsdAlternative* an = a->next;
            if (a->compiled) leptris_xquery_free(a->compiled);
            free(a->test);
            free(a->type);
            for (int i = 0; i < a->test_attr_n; i++)
                free(a->test_attrs[i]);
            free(a->test_attrs);
            free(a);
            a = an;
        }
        free(d->name);
        free(d->type);
        free(d->fixed);
        free(d->default_value);
        free(d->sub_head);
        free(d);
        d = dn;
    }
    XsdAttrGroupDef* ag = s->attr_groups;
    while (ag) {
        XsdAttrGroupDef* agn = ag->next;
        free(ag->name);
        XsdAttrDecl* a = ag->attrs;
        while (a) {
            XsdAttrDecl* an2 = a->next;
            free(a->name);
            free(a->type);
            free(a->fixed);
            free(a->ns);
            free(a);
            a = an2;
        }
        free(ag);
        ag = agn;
    }
    XsdAttrDecl* ta_row = s->top_attrs;
    while (ta_row) {
        XsdAttrDecl* ta_next = ta_row->next;
        free(ta_row->name);
        free(ta_row->type);
        free(ta_row->fixed);
        free(ta_row->ns);
        free(ta_row);
        ta_row = ta_next;
    }
    XsdGroupDef* g = s->groups;
    while (g) {
        XsdGroupDef* gn = g->next;
        free(g->name);
        xsd_cm_free(g->model);
        free(g);
        g = gn;
    }
    if (s->default_open) xsd_cm_free(s->default_open);
    if (s->oc_default_pool) xsd_cm_free(s->oc_default_pool);
    for (size_t i = 0; i < s->visited_len; i++) free(s->visited[i]);
    free(s->visited);
    /* #2150: the override correspondence set (fixed-buffer slots)
     * and the compiled file's own normalized path */
    free(s->override_set);
    free(s->self_path);
    /* #2151: schema-document units + openContent id registry */
    for (size_t i = 0; i < s->unit_count; i++) free(s->units[i]);
    free(s->units);
    for (size_t i = 0; i < s->oc_id_count; i++) free(s->oc_ids[i]);
    free(s->oc_ids);
    free(s->target_ns);
    free(s->base_dir);
    if (s->validator)
        xsd_validator_free((struct xsd_validator*)s->validator);
    XsdSimple* t = s->simple_types;
    while (t) {
        XsdSimple* next = t->next;
        xsd_free_simple_one(t);
        t = next;
    }
    free(s->error);
    free(s);
}

/* ---- slice 2: lexical validation --------------------------------- */

static XsdCm* xsd_cm_copy_list(const XsdCm* head) {
    XsdCm* out = NULL;
    XsdCm* out_tail = NULL;
    for (const XsdCm* src = head; src; src = src->next) {
        XsdCm* d = (XsdCm*)calloc(1, sizeof(*d));
        if (!d) break;
        d->kind = src->kind;
        d->name = xsd_strdup(src->name);
        d->type = xsd_strdup(src->type);
        d->ns = xsd_strdup(src->ns);
        d->any_ns = xsd_strdup(src->any_ns);
        d->not_ns = xsd_strdup(src->not_ns);
        d->not_qname = xsd_strdup(src->not_qname);
        d->any_unit_tns = xsd_strdup(src->any_unit_tns);
        d->process_skip = src->process_skip;
        d->pc_level = src->pc_level;
        d->min = src->min;
        d->max = src->max;
        d->first_child = xsd_cm_copy_list(src->first_child);
        if (out_tail)
            out_tail->next = d;
        else
            out = d;
        out_tail = d;
    }
    return out;
}

static void xsd_register_complex(struct leptris_xsd_schema* s,
                                 XsdCm* root, XsdAttrDecl* attrs,
                                 const char* name, char* any_attr_ns,
                                 char* any_attr_not_qname,
                                 char* any_attr_not_ns,
                                 char* any_attr_unit_ns) {
    if (!root) return;
    root->name = xsd_strdup(name);
    /* leptris-ruby#440: the DECLARING file's targetNamespace — the
     * merged-import schema is flat, so same-name types from
     * different namespaces (wml CT_TcPr vs dml CT_TcPr) resolve by
     * (ns, name). */
    if (s->target_ns) root->ns = xsd_strdup(s->target_ns);
    root->next = s->complex_types;
    s->complex_types = root;
    /* #2182: EVERY complexType root registers its row slot — an
     * attribute-less type's EMPTY entry keeps the entry keyed to
     * THIS root, so a same-named replacement (redefine/override
     * re-capture) never applies its defaults or wildcards onto the
     * shadowed component's entry, and the strict attribute walk
     * runs on attribute-less types (zero declared rows). */
    {
        XsdTypeAttrs* ta = (XsdTypeAttrs*)calloc(1, sizeof(*ta));
        if (ta) {
            ta->type_name = xsd_strdup(name);
            if (root->ns) ta->ns = xsd_strdup(root->ns);
            ta->attrs = attrs;
            ta->any_attr_ns = any_attr_ns;
            ta->any_attr_not_qname = any_attr_not_qname;
            ta->any_attr_not_ns = any_attr_not_ns;
            ta->any_attr_unit_ns = any_attr_unit_ns;
            ta->next = s->type_attrs;
            s->type_attrs = ta;
        } else {
            free(any_attr_ns);
            free(any_attr_not_qname);
            free(any_attr_not_ns);
            free(any_attr_unit_ns);
        }
    }
}

/* moxml#579: the attribute names a CTA test references. §C.1 keeps
 * the test to a restricted XPath — attribute references spell as
 * @name / @pfx:name or attribute::name. String literals are skipped
 * (a '@' inside quotes is data, not a reference); the collected
 * names dedupe and keep their prefix form so the strict check can
 * strcmp them against the instance's attribute qnames. */
static void xsd_cta_test_attrs(const char* test, char*** out,
                               int* out_n) {
    *out = NULL;
    *out_n = 0;
    if (!test) return;
    int cap = 4;
    int n = 0;
    char** arr = (char**)calloc((size_t)cap, sizeof(char*));
    if (!arr) return;
    const char* p = test;
    while (*p) {
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (*p && *p != q) p++;
            if (*p) p++;
            continue;
        }
        int is_attr = 0;
        if (*p == '@') {
            p++;
            is_attr = 1;
        } else if (strncmp(p, "attribute", 9) == 0 && p[9] == ':' &&
                   p[10] == ':' &&
                   (p == test || !isalnum((unsigned char)p[-1]))) {
            p += 11;
            is_attr = 1;
        }
        if (is_attr) {
            const char* s0 = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '_' ||
                          *p == '-' || *p == '.' || *p == ':' ||
                          (unsigned char)*p >= 0x80))
                p++;
            if (p > s0) {
                int len = (int)(p - s0);
                int dup = 0;
                for (int i = 0; i < n && !dup; i++)
                    if ((int)strlen(arr[i]) == len &&
                        strncmp(arr[i], s0, (size_t)len) == 0)
                        dup = 1;
                if (!dup) {
                    if (n == cap) {
                        int ncap = cap * 2;
                        char** grown = (char**)realloc(
                            arr, (size_t)ncap * sizeof(char*));
                        if (!grown) break;
                        arr = grown;
                        cap = ncap;
                    }
                    char* name = (char*)malloc((size_t)len + 1);
                    if (!name) break;
                    memcpy(name, s0, (size_t)len);
                    name[len] = '\0';
                    arr[n++] = name;
                }
            }
            continue;
        }
        p++;
    }
    if (n == 0) {
        free(arr);
        return;
    }
    *out = arr;
    *out_n = n;
}

/* XSD 1.1: the xs:alternative children of an element declaration
 * (§3.12.2), captured as the ordered type table. Capture is keyed on
 * the alternative ELEMENTS: their presence is 1.1 syntax, so the
 * schema-wide @version gate must not hide them — the cta corpus
 * carries version="1.0" (or none) on 23 of its 24 schemas. Each
 * alternative names its type with exactly one of the two forms
 * (§3.12.3): a QName attribute (stored as the local name, resolved
 * through the same ladders as a declared type) or an inline
 * complexType/simpleType child captured into a synthesized slot.
 * A test-less alternative is the DEFAULT: it closes the table
 * (§3.3.2.1 — every alternative but the last carries a test; a
 * test-less alternative mid-table is src-element.5, flagged
 * ct_failed). A test that fails to COMPILE (§3.12.2 XPath valid)
 * fails the schema (#2184); only evaluation errors take the §3.12.4
 * "false" channel. */
static XsdAlternative* xsd_capture_alternatives(
    struct leptris_xsd_schema* s, LeptrisElement decl_elem) {
    XsdAlternative* head = NULL;
    XsdAlternative* tail = NULL;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(decl_elem));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL;
        leptris_element_expanded_name(e, &local, NULL, NULL);
        if (!local || strcmp(local, "alternative") != 0) continue;
        const char* test = leptris_element_attribute(e, "test");
        const char* type = leptris_element_attribute(e, "type");
        XsdAlternative* a = (XsdAlternative*)calloc(1, sizeof(*a));
        if (!a) break;
        if (test && test[0]) {
            a->test = xsd_strdup(test);
            a->compiled = leptris_xquery_parse(test, strlen(test));
            xsd_cta_test_attrs(a->test, &a->test_attrs,
                               &a->test_attr_n);
        }
        if (type && type[0]) {
            char turi[512], tlocal[256];
            xsd_resolve_qname(e, type, turi, sizeof(turi), tlocal,
                              sizeof(tlocal));
            a->type = xsd_strdup(xsd_local_name(type));
            if (turi[0] && strcmp(turi, XSD_NS) == 0 &&
                strcmp(tlocal, "error") == 0)
                a->xs_error = 1;
        } else {
            /* inline type child → synthesized slot ("alternative:"
             * + the node address: alternatives carry no name, and
             * NCName type names cannot contain ':' — collision-free) */
            char synth[64];
            snprintf(synth, sizeof(synth), "alternative:%p", (void*)e);
            if (xsd_capture_inline(s, e, synth))
                a->type = xsd_strdup(synth);
        }
        if (!a->type) {
            /* §3.12.3 requires one of the two forms; neither present
             * — nothing to select, drop the row */
            free(a->test);
            if (a->compiled) leptris_xquery_free(a->compiled);
            for (int i = 0; i < a->test_attr_n; i++)
                free(a->test_attrs[i]);
            free(a->test_attrs);
            free(a);
            continue;
        }
        if (tail)
            tail->next = a;
        else
            head = a;
        tail = a;
        s->saw_alternative = 1;
        if (!a->test) {
            /* #2184 src-element.5 / §3.3.9.2: the test-less
             * alternative is the DEFAULT — it may only CLOSE the
             * table. A further xs:alternative sibling makes the
             * schema itself invalid (the capture truncates the chain
             * here, so the violation rides the ct_failed flag to the
             * post-capture gate — cta9001err). */
            for (LeptrisNodeRef m = leptris_node_next_sibling(n); m;
                 m = leptris_node_next_sibling(m)) {
                if (leptris_node_get_type(m) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                const char* ml = NULL;
                leptris_element_expanded_name((LeptrisElement)m, &ml,
                                              NULL, NULL);
                if (ml && strcmp(ml, "alternative") == 0) {
                    s->ct_failed = 1;
                    break;
                }
            }
            break; /* the default alternative ends the table */
        }
    }
    return head;
}

/* xs:boolean attribute reader with the suite's whitespace-tolerant
 * spellings (" 1 "); dflt when absent or malformed. */
static int xsd_bool_attr(LeptrisElement e, const char* name, int dflt) {
    const char* v = leptris_element_attribute(e, name);
    if (!v) return dflt;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    /* trim the trailing whitespace too (" 1 " is legal — the attr
     * value is CDATA-normalized, not trimmed) */
    const char* end = v + strlen(v);
    while (end > v && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\n' || end[-1] == '\r'))
        end--;
    size_t len = (size_t)(end - v);
    if ((len == 4 && strncmp(v, "true", 4) == 0) ||
        (len == 1 && v[0] == '1'))
        return 1;
    if ((len == 5 && strncmp(v, "false", 5) == 0) ||
        (len == 1 && v[0] == '0'))
        return 0;
    return dflt;
}

/* #2149: does the document carry an xs:alternative element at any
 * depth (1.1 syntax evidence for schemas unmarked or marked 1.0)? */
static int xsd_doc_has_alternative(LeptrisElement e, int depth) {
    if (depth > 32) return 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* cl = NULL;
        const char* cu = NULL;
        leptris_element_expanded_name(c, &cl, NULL, &cu);
        if (cl && cu && strcmp(cu, XSD_NS) == 0 &&
            strcmp(cl, "alternative") == 0)
            return 1;
        if (xsd_doc_has_alternative(c, depth + 1)) return 1;
    }
    return 0;
}

/* #2154: does the document carry an xs:assert / xs:assertion element
 * at any depth (assertion-syntax evidence for schemas unmarked or
 * marked 1.0)? */
static int xsd_doc_has_assert(LeptrisElement e, int depth) {
    if (depth > 32) return 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* cl = NULL;
        const char* cu = NULL;
        leptris_element_expanded_name(c, &cl, NULL, &cu);
        if (cl && cu && strcmp(cu, XSD_NS) == 0 &&
            (strcmp(cl, "assert") == 0 ||
             strcmp(cl, "assertion") == 0))
            return 1;
        if (xsd_doc_has_assert(c, depth + 1)) return 1;
    }
    return 0;
}

/* #2151: does the document carry an xs:openContent or
 * xs:defaultOpenContent element at any depth (open-content-syntax
 * evidence for schemas unmarked or marked 1.0 — the whole
 * saxonData/Open corpus carries no version marker)? */
static int xsd_doc_has_open_content(LeptrisElement e, int depth) {
    if (depth > 32) return 0;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(e));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement c = (LeptrisElement)n;
        const char* cl = NULL;
        const char* cu = NULL;
        leptris_element_expanded_name(c, &cl, NULL, &cu);
        if (cl && cu && strcmp(cu, XSD_NS) == 0 &&
            (strcmp(cl, "openContent") == 0 ||
             strcmp(cl, "defaultOpenContent") == 0))
            return 1;
        if (xsd_doc_has_open_content(c, depth + 1)) return 1;
    }
    return 0;
}

/* #2151: resolve the prefixed QName tokens of an open content
 * wildcard's notQName against the DECLARING element's in-scope
 * xmlns bindings, rewriting them "{uri}local" — the instance-side
 * match compares (uri, local) directly (s3_4_1ii04). Unresolvable
 * prefixes keep the raw token (the deferred posture). */
static void xsd_oc_resolve_not_qname(XsdCm* w, LeptrisElement any_el) {
    if (!w || !w->not_qname || !strchr(w->not_qname, ':')) return;
    char out[1024];
    size_t o = 0;
    const char* p = w->not_qname;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        char tok[512];
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, start, len);
        tok[len] = 0;
        const char* colon = strchr(tok, ':');
        if (colon && colon != tok && o + 600 < sizeof(out)) {
            /* resolve through the engine's in-scope binding walk */
            char buri[256], blocal[256];
            xsd_resolve_qname(any_el, tok, buri, sizeof(buri), blocal,
                              sizeof(blocal));
            if (buri[0] && blocal[0]) {
                int wn =
                    snprintf(out + o, sizeof(out) - o, "{%s}%s ", buri,
                             blocal);
                if (wn > 0) o += (size_t)wn;
                continue;
            }
        }
        int wn = snprintf(out + o, sizeof(out) - o, "%s ", tok);
        if (wn > 0) o += (size_t)wn;
    }
    if (o) {
        if (out[o - 1] == ' ') out[o - 1] = 0;
        free(w->not_qname);
        w->not_qname = xsd_strdup(out);
    }
}

/* #2151: NCName form check for xs:ID values (open039: "open001/2").
 * ASCII core grammar; non-ASCII bytes pass as name characters (the
 * parser's posture elsewhere), ASCII punctuation beyond the NCName
 * set fails. */
static int xsd_is_ncname_compile(const char* s) {
    if (!s || !*s) return 0;
    for (const char* p = s; *p; p++) {
        char c = *p;
        int ok = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  c == '_' || c == ':' || (unsigned char)c >= 0x80 ||
                  (p > s && ((c >= '0' && c <= '9') || c == '-' ||
                             c == '.')));
        if (!ok) return 0;
    }
    return 1;
}

/* #2151: the whitespace-collapsed xs:ID registry (open038: id=" x1 "
 * collides with id="x1"). Returns 1 when the value was already
 * seen. */
static int xsd_oc_id_seen(struct leptris_xsd_schema* s,
                          const char* raw) {
    char norm[256];
    size_t o = 0;
    for (const char* p = raw; *p && o + 1 < sizeof(norm); p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            continue;
        norm[o++] = *p;
    }
    norm[o] = 0;
    for (size_t i = 0; i < s->oc_id_count; i++)
        if (strcmp(s->oc_ids[i], norm) == 0) return 1;
    if (s->oc_id_count == s->oc_id_cap) {
        size_t cap = s->oc_id_cap ? s->oc_id_cap * 2 : 8;
        char** grown = (char**)realloc(s->oc_ids, cap * sizeof(char*));
        if (!grown) return 0;
        s->oc_ids = grown;
        s->oc_id_cap = cap;
    }
    char* dup = xsd_strdup(norm);
    if (dup) s->oc_ids[s->oc_id_count++] = dup;
    return 0;
}

static XsdCm* xsd_capture_any(struct leptris_xsd_schema* s,
                              LeptrisElement any_child) {
    XsdCm* w = xsd_cm_new(XSD_CM_ANY);
    if (!w) return NULL;
    w->any_ns = xsd_strdup(
        leptris_element_attribute(any_child, "namespace"));
    /* #2151: XSD 1.1 @notNamespace — the exclusion list rides the
     * wildcard (open047's union side). */
    w->not_ns = xsd_strdup(
        leptris_element_attribute(any_child, "notNamespace"));
    /* #2151: the open-content wildcard's @notQName (s3_4_1ii04) —
     * #2184: prefixed tokens canonize to {uri}local at capture so
     * the matcher's canon arm bites (wild027/028, wild036-039) */
    w->not_qname = xsd_wc_canon_not_qname(
        s, any_child, leptris_element_attribute(any_child, "notQName"));
    w->any_unit_tns = xsd_capture_unit_tns(any_child);
    /* #2184 src-wildcard sweep: co-occurrence + vacuous exclusions.
     * The cm keeps the RAW grammar/not_ns (the content-model matcher
     * resolves ##targetNamespace/##other per-unit at match time);
     * the check runs on canon views. */
    {
        char* cns = NULL;
        char* cother = NULL;
        xsd_wc_canon_ns(w->any_ns, w->any_unit_tns, &cns, &cother);
        char* cnns = xsd_wc_canon_not_ns(w->not_ns, w->any_unit_tns);
        if (cother) {
            xsd_wc_append(&cnns, cother, strlen(cother));
            free(cother);
        }
        xsd_wc_check(s, w->any_ns, w->not_ns, cns, cnns,
                     w->not_qname);
        free(cns);
        free(cnns);
    }
    const char* pc = leptris_element_attribute(any_child,
                                               "processContents");
    w->process_skip =
        (pc && (strcmp(pc, "skip") == 0 || strcmp(pc, "lax") == 0))
            ? 1
            : 0;
    w->pc_level = (pc && strcmp(pc, "skip") == 0) ? 2
                  : (pc && strcmp(pc, "lax") == 0) ? 1 : 0;
    w->min = 0;
    w->max = -1;
    return w;
}

static XsdAssert* xsd_capture_assert(struct leptris_xsd_schema* s,
                                     LeptrisElement e) {
    const char* test = leptris_element_attribute(e, "test");
    if (!test || !*test) return NULL;
    XsdAssert* a = (XsdAssert*)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->test = xsd_strdup(test);
    /* #2154 (§3.13.2): the bindings the test's prefixes and
     * unprefixed names resolve under, captured at the assert
     * element */
    a->ns_prolog = xsd_capture_ns_prolog(e, s->base_dir);
    /* #2149: compiled when the schema claims 1.1 OR carries
     * xs:alternative chains — the alternative evidence marks schemas
     * whose selected types' asserts belong to the CTA semantics
     * (test4.xsd-style cta corpora are marked version="1.0").
     * #2154: OR carries assert/assertion SYNTAX — a schema whose
     * authors wrote asserts must have them evaluate regardless of
     * the @version spelling. Schemas claiming none of the three keep
     * the 1.0-lax posture: the assertions family's PSVI-typed assert
     * expressions (data(.) instance of xs:date*) cannot be evaluated
     * for them yet, and evaluating what we cannot support would
     * reject suite-valid documents. The test compiles under a $value
     * declaration (§3.13.4.1 2.2/2.3 — the evaluator binds it; the
     * same wrapper the xs:assertion facet uses). NULL = syntax
     * error: evaluation reports the assertion as failed (XSD 1.1:
     * an error is a failure). */
    if (s->version_11 || s->saw_alternative || s->saw_assert) {
        /* prolog first (§3.13.2 bindings), then the $value
         * declaration, then the test — sized for the whole text */
        size_t pl = a->ns_prolog ? strlen(a->ns_prolog) : 0;
        size_t tl = strlen(test);
        char* wrapped = (char*)malloc(pl + tl + 64);
        if (wrapped) {
            size_t o = 0;
            if (pl) {
                memcpy(wrapped + o, a->ns_prolog, pl);
                o += pl;
                wrapped[o++] = ' ';
            }
            {
                static const char head[] =
                    "declare variable $value external; ";
                memcpy(wrapped + o, head, sizeof(head) - 1);
                o += sizeof(head) - 1;
            }
            memcpy(wrapped + o, test, tl + 1);
            a->compiled = leptris_xquery_parse(wrapped, o + tl);
            free(wrapped);
        }
    }
    return a;
}

/* Append every xs:assert child under `container` to the type. */
static void xsd_capture_assert_children(
    struct leptris_xsd_schema* s, LeptrisElement container,
    XsdCm* root) {
    XsdAssert* tail = root->asserts;
    while (tail && tail->next) tail = tail->next;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(container));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL;
        leptris_element_expanded_name(e, &local, NULL, NULL);
        if (!local || strcmp(local, "assert") != 0) continue;
        XsdAssert* a = xsd_capture_assert(s, e);
        if (!a) continue;
        /* LINK the append: with root->asserts already populated,
         * tail = a alone dropped every assert past the first
         * (leaked, and the type silently lost its assert list) —
         * surfaced by #2149's widened capture call sites. */
        if (!root->asserts)
            root->asserts = a;
        else if (tail)
            tail->next = a;
        tail = a;
    }
}

/* One complexType capture, shared by top-level declarations and
 * inline anonymous types: content model + derivation
 * (complexContent/simpleContent extension|restriction) + the
 * attribute rows the derivation carries. */
static XsdCm* xsd_capture_complex_model(struct leptris_xsd_schema* s,
                                        LeptrisElement ct,
                                        const char* name) {
    XsdCm* root = xsd_cm_new(XSD_CM_SEQ); /* wrapper keeps the name */
    if (!root) return NULL;
    /* leptris-rs#35 (ctA033, idF025's inline c): mixed="true" admits
     * character data even with no particle model — the flag must
     * survive capture or the validator treats the type as empty. */
    root->mixed = xsd_mixed_flag(ct);
    /* #2182 §3.4.2.5: the type's opt-out flag (tri-state: absent =
     * apply) and the unit whose @defaultAttributes list governs it.
     * Override bodies capture suppressed — the body rides the
     * OVERRIDDEN document's defaults, not the overriding one's. */
    root->defattr_apply = xsd_bool_attr(ct, "defaultAttributesApply",
                                        -1);
    root->defattr_unit =
        s->defattr_suppressed ? -1 : s->current_unit;
    XsdAttrDecl* attrs = NULL;
    char* any_attr_ns = NULL;
    char* any_attr_not_qname = NULL;
    char* any_attr_not_ns = NULL;
    char* any_attr_unit_ns = NULL;
    /* #2184: the eager extension's already-captured base row — its
     * effective wildcard unions into the derived's at the fold */
    const XsdTypeAttrs* eager_bt = NULL;

    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(ct));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;

        if (strcmp(local, "complexContent") == 0) {
            /* extension|restriction child carries base + model */
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement dq = (LeptrisElement)q;
                const char* dl = NULL;
                leptris_element_expanded_name(dq, &dl, NULL, NULL);
                if (!dl || (strcmp(dl, "extension") != 0 &&
                            strcmp(dl, "restriction") != 0))
                    continue;
                const char* base_raw =
                    leptris_element_attribute(dq, "base");
                /* #1881: resolve the base QName against the
                 * extension element's in-scope declarations — the
                 * resolved namespace rides the deferral so the
                 * post-capture fold can cross units (g:foo1 extends
                 * f:foo). */
                char buri[256], blocal[256];
                buri[0] = 0;
                blocal[0] = 0;
                if (base_raw)
                    xsd_resolve_qname(dq, base_raw, buri, sizeof(buri),
                                      blocal, sizeof(blocal));
                const char* base =
                    base_raw ? (blocal[0] ? blocal
                                          : xsd_local_name(base_raw))
                             : NULL;
                XsdCm* base_root =
                    (base && strncmp(base, "xs:", 3) != 0)
                        ? xsd_find_complex_ns(s, base, s->target_ns)
                        : NULL;
                /* leptris-ruby#440 shape A: a base that is not
                 * captured YET (forward reference — CT_TcPr extends
                 * CT_TcPrInner defined later in the same file) or
                 * that is itself still deferred folds in the
                 * post-capture resolver, not here: folding a
                 * half-captured base silently truncates the model.
                 * The visited memo means each file merges once, so
                 * the double capture that used to mask this is
                 * gone. EXTENSION ONLY (moxml#466): a restriction's
                 * model REPLACES the base's — folding it in prepends
                 * a duplicate particle sequence and demands the base
                 * content next to the derived content. */
                int defer_base =
                    base_root
                        ? base_root->ext_base != NULL
                        : (base && strncmp(base, "xs:", 3) != 0);
                if (defer_base && base &&
                    strcmp(dl, "extension") == 0) {
                    free(root->ext_base);
                    root->ext_base = xsd_strdup(base);
                    free(root->ext_base_ns);
                    root->ext_base_ns =
                        buri[0] ? xsd_strdup(buri) : NULL;
                }
                /* #2151: the open-content pass resolves the base
                 * regardless of the fold's deferral (inheritance,
                 * wildcard union, the cos-ct-extends ladder). */
                if (base && strcmp(dl, "extension") == 0 &&
                    strncmp(base, "xs:", 3) != 0 && !root->oc_ext_base) {
                    root->oc_ext_base = xsd_strdup(base);
                    root->oc_ext_base_ns =
                        buri[0] ? xsd_strdup(buri) : NULL;
                }
                if (strcmp(dl, "extension") == 0) {
                    /* base content first, derived content after */
                    root->first_child = xsd_cm_copy_list(
                        (base_root && !defer_base)
                            ? base_root->first_child
                            : NULL);
                    XsdCm* own_tail = root->first_child;
                    while (own_tail && own_tail->next)
                        own_tail = own_tail->next;
                    XsdCm* own = xsd_cm_parse(s, dq, NULL);
                    if (own_tail)
                        own_tail->next = own;
                    else if (!root->first_child)
                        root->first_child = own;
                    if (base_root && !defer_base) {
                        const XsdTypeAttrs* bt = xsd_find_type_attrs(
                            s, base);
                        attrs = xsd_copy_attr_rows(bt ? bt->attrs
                                                      : NULL,
                                                   1);
                        /* #2184: the base's effective wildcard
                         * unions into the derived's at the fold
                         * below — no separate base arm. */
                        eager_bt = bt;
                    }
                    XsdAttrDecl* own_attrs =
                        xsd_capture_attr_rows(s, dq, 0);
                    if (own_attrs) {
                        XsdAttrDecl* at = attrs;
                        while (at && at->next) at = at->next;
                        if (at)
                            at->next = own_attrs;
                        else
                            attrs = own_attrs;
                    }
                    /* #2154 §3.13.6: the assert chain follows the
                     * base — recorded EAGERLY here (the deferred fold
                     * steals ext_base into assert_base instead) */
                    if (base && strncmp(base, "xs:", 3) != 0 &&
                        !root->assert_base) {
                        root->assert_base = xsd_strdup(base);
                        root->assert_base_ns =
                            buri[0] ? xsd_strdup(buri) : NULL;
                    }
                } else {
                    /* restriction: the derived model REPLACES.
                     * leptris#1923: the base still rides the type —
                     * libxml2 admits the restriction chain's declared
                     * attributes at validation, so record the base
                     * (local + resolved ns) for the strict-attribute
                     * admission walk. */
                    root->first_child = xsd_cm_parse(s, dq, NULL);
                    attrs = xsd_capture_attr_rows(s, dq, 0);
                    if (base && strncmp(base, "xs:", 3) != 0) {
                        free(root->res_base);
                        root->res_base = xsd_strdup(base);
                        free(root->res_base_ns);
                        root->res_base_ns =
                            buri[0] ? xsd_strdup(buri) : NULL;
                    }
                }
                /* XSD 1.1: asserts nested under the derivation —
                 * captured when the schema claims 1.1, carries
                 * alternatives (#2149) or carries assert syntax
                 * (#2154) */
                if (s->version_11 || s->saw_alternative ||
                    s->saw_assert)
                    xsd_capture_assert_children(s, dq, root);
                if (!any_attr_ns) {
                    /* #2184: gather the derivation's OWN wildcard
                     * (direct xs:anyAttribute + attributeGroup refs)
                     * canonized and intersected; an extension then
                     * unions the base's effective wildcard in (the
                     * eager base is already captured; a deferred one
                     * folds at xsd_resolve_extension_bases). */
                    XsdAttrWc eff;
                    if (xsd_capture_attr_wc(s, dq, &eff)) {
                        any_attr_ns = eff.ns;
                        any_attr_not_qname = eff.not_qname;
                        any_attr_not_ns = eff.not_ns;
                        free(any_attr_unit_ns);
                        any_attr_unit_ns = eff.unit_ns;
                    } else {
                        free(eff.unit_ns);
                    }
                    if (eager_bt) {
                        XsdAttrWc bb;
                        memset(&bb, 0, sizeof(bb));
                        bb.ns = eager_bt->any_attr_ns;
                        bb.not_qname = eager_bt->any_attr_not_qname;
                        bb.not_ns = eager_bt->any_attr_not_ns;
                        bb.unit_ns = eager_bt->any_attr_unit_ns;
                        xsd_wc_fold_union_locals(
                            &any_attr_ns, &any_attr_not_qname,
                            &any_attr_not_ns, &any_attr_unit_ns,
                            &bb);
                    }
                }
            }
        } else if (strcmp(local, "simpleContent") == 0) {
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement dq = (LeptrisElement)q;
                const char* dl = NULL;
                leptris_element_expanded_name(dq, &dl, NULL, NULL);
                if (!dl || (strcmp(dl, "extension") != 0 &&
                            strcmp(dl, "restriction") != 0))
                    continue;
                const char* base = xsd_local_name(
                    leptris_element_attribute(dq, "base"));
                if (base) {
                    if (strncmp(base, "xs:", 3) == 0) {
                        root->text_type = xsd_strdup(base);
                    } else {
                        /* restriction facets under a local base
                         * ride a synthesized "complex:NAME" slot */
                        int has_facets = 0;
                        for (LeptrisNodeRef f =
                                 leptris_node_first_child(
                                     leptris_element_as_node(dq));
                             f; f = leptris_node_next_sibling(f)) {
                            if (leptris_node_get_type(f) !=
                                LEPTRIS_NODE_TYPE_ELEMENT)
                                continue;
                            const char* fl = NULL;
                            leptris_element_expanded_name(
                                (LeptrisElement)f, &fl, NULL, NULL);
                            /* #2183: xs:assert is the complexType
                             * assertion (§3.13), not a simple-type
                             * facet — its presence must not arm the
                             * synthesized-simple path, which cuts
                             * the base's list chain (listunion6's
                             * INTLIST died and $value bound as one
                             * string) */
                            if (fl && strcmp(fl, "facet") != 0 &&
                                strcmp(fl, "simpleType") != 0 &&
                                strcmp(fl, "assert") != 0)
                                has_facets = 1;
                        }
                        XsdSimple* captured =
                            has_facets
                                ? xsd_capture_simple(dq)
                                : NULL;
                        if (captured) {
                            char synth[512];
                            snprintf(synth, sizeof(synth),
                                     "complex:%s", name);
                            free(captured->name);
                            captured->name = xsd_strdup(synth);
                            if (s->target_ns)
                                captured->ns = xsd_strdup(s->target_ns);
                            captured->next = s->simple_types;
                            s->simple_types = captured;
                            root->text_type = xsd_strdup(synth);
                        } else {
                            root->text_type = xsd_strdup(base);
                        }
                        /* leptris#1923: a simpleContent restriction
                         * rides its base for strict-attribute
                         * admission (libxml2 admits the chain's
                         * declared attributes — ctE019). */
                        if (strcmp(dl, "restriction") == 0) {
                            free(root->res_base);
                            root->res_base = xsd_strdup(base);
                            free(root->res_base_ns);
                            root->res_base_ns = NULL;
                        }
                    }
                }
                attrs = xsd_capture_attr_rows(s, dq, 0);
                /* XSD 1.1: asserts nested under the simpleContent
                 * derivation (test4.xsd: the alternative-selected
                 * simpleContent type asserts $value mod 2 = 0) —
                 * same condition as the complexContent branch
                 * (#2154 adds the assert-syntax key). */
                if (s->version_11 || s->saw_alternative ||
                    s->saw_assert)
                    xsd_capture_assert_children(s, dq, root);
            }
        }
    }
    if (!root->first_child && !root->text_type) {
        /* no derivation: the direct-content spelling. A derivation
         * that captured attributes but produced no element model
         * (e.g. an attributes-only restriction) already set them —
         * re-capturing would orphan those rows. */
        root->first_child = xsd_cm_parse(s, ct, NULL);
        if (!attrs) attrs = xsd_capture_attr_rows(s, ct, 0);
        if (!any_attr_ns && !any_attr_not_qname &&
            !any_attr_not_ns) {
            /* #2184: gather + intersect the type's own wildcard
             * (direct xs:anyAttribute + attributeGroup refs); the
             * declaring-unit tns rides each member (wildcards in
             * REFERENCED groups from another unit carry their own) */
            XsdAttrWc eff;
            if (xsd_capture_attr_wc(s, ct, &eff)) {
                any_attr_ns = eff.ns;
                any_attr_not_qname = eff.not_qname;
                any_attr_not_ns = eff.not_ns;
                free(any_attr_unit_ns);
                any_attr_unit_ns = eff.unit_ns;
            } else {
                free(eff.unit_ns);
            }
        }
    }
    /* #2151: XSD 1.1 openContent (§3.4.2.3.3 clauses 5/6). The
     * declared spelling (direct on the complexType or under the
     * derivation element) wins over the schema default; the default
     * applies when no openContent child exists and the content model
     * is non-empty (or mixed-empty, or the default declares
     * appliesToEmpty). mode="none" declares NO open content and
     * blocks the default (open031). Errors are the suite's
     * schema-error class (src-ct.3, open036-039/048, s3_4_1si01/03,
     * ct-props-correct.5, src-ct.5). The block arms on any 1.1
     * evidence: a version marker, open-content syntax, or a
     * complexContent/@mixed attribute (1.1-only syntax). */
    int cc_mixed_seen = 0;
    {
        for (LeptrisNodeRef n =
                 leptris_node_first_child(
                     leptris_element_as_node(ct));
             n && !cc_mixed_seen; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement e = (LeptrisElement)n;
            const char* el = NULL;
            const char* eu = NULL;
            leptris_element_expanded_name(e, &el, NULL, &eu);
            if (el && eu && strcmp(eu, XSD_NS) == 0 &&
                strcmp(el, "complexContent") == 0 &&
                leptris_element_attribute(e, "mixed"))
                cc_mixed_seen = 1;
        }
    }
    if (s->version_11 || s->saw_open_content || cc_mixed_seen) {
        root->oc_unit_id = s->current_unit;
        /* src-ct.5 (s3_4_1si09): complexType/@mixed and
         * complexContent/@mixed must agree when both are spelled. */
        {
            const char* ct_mixed =
                leptris_element_attribute(ct, "mixed");
            for (LeptrisNodeRef n =
                     leptris_node_first_child(
                         leptris_element_as_node(ct));
                 n && ct_mixed; n = leptris_node_next_sibling(n)) {
                if (leptris_node_get_type(n) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement e = (LeptrisElement)n;
                const char* el = NULL;
                const char* eu = NULL;
                leptris_element_expanded_name(e, &el, NULL, &eu);
                if (!el || !eu || strcmp(eu, XSD_NS) != 0 ||
                    strcmp(el, "complexContent") != 0)
                    continue;
                const char* cc_mixed =
                    leptris_element_attribute(e, "mixed");
                if (cc_mixed) {
                    int a = xsd_bool_attr(ct, "mixed", -1);
                    int b = xsd_bool_attr(e, "mixed", -1);
                    if (a != -1 && b != -1 && a != b) {
                        xsd_set_error(s,
                                      "complexType/@mixed and "
                                      "complexContent/@mixed must "
                                      "not conflict");
                        s->oc_failed = 1;
                        break;
                    }
                }
            }
        }
        LeptrisElement oc_elem = NULL;
        int oc_found = 0;
        int oc_mode_decl = 2; /* S4SD default: interleave */
        for (LeptrisNodeRef n =
                 leptris_node_first_child(
                     leptris_element_as_node(ct));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement e = (LeptrisElement)n;
            const char* el = NULL;
            const char* eu = NULL;
            leptris_element_expanded_name(e, &el, NULL, &eu);
            if (!el || !eu || strcmp(eu, XSD_NS) != 0) continue;
            if (strcmp(el, "openContent") == 0) {
                oc_elem = e;
                oc_found = 1;
            } else if (strcmp(el, "complexContent") == 0) {
                for (LeptrisNodeRef q =
                         leptris_node_first_child(
                             leptris_element_as_node(e));
                     q; q = leptris_node_next_sibling(q)) {
                    if (leptris_node_get_type(q) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement dq = (LeptrisElement)q;
                    const char* dl = NULL;
                    leptris_element_expanded_name(dq, &dl, NULL,
                                                  NULL);
                    if (!dl || (strcmp(dl, "extension") != 0 &&
                                strcmp(dl, "restriction") != 0))
                        continue;
                    for (LeptrisNodeRef r =
                             leptris_node_first_child(
                                 leptris_element_as_node(dq));
                         r; r = leptris_node_next_sibling(r)) {
                        if (leptris_node_get_type(r) !=
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            continue;
                        LeptrisElement re = (LeptrisElement)r;
                        const char* rl = NULL;
                        leptris_element_expanded_name(re, &rl, NULL,
                                                      NULL);
                        if (rl && strcmp(rl, "openContent") == 0) {
                            oc_elem = re;
                            oc_found = 1;
                        }
                    }
                }
            } else if (strcmp(el, "simpleContent") == 0) {
                /* an openContent under a simpleContent derivation is
                 * ILLEGAL grammar (the S4SD's simpleContent
                 * restriction/extension carry no openContent slot):
                 * detect it so the ct-props-correct.5 rejection
                 * fires (the type's text_type guard below). */
                for (LeptrisNodeRef q =
                         leptris_node_first_child(
                             leptris_element_as_node(e));
                     q; q = leptris_node_next_sibling(q)) {
                    if (leptris_node_get_type(q) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement dq = (LeptrisElement)q;
                    const char* dl = NULL;
                    leptris_element_expanded_name(dq, &dl, NULL,
                                                  NULL);
                    if (!dl || (strcmp(dl, "extension") != 0 &&
                                strcmp(dl, "restriction") != 0))
                        continue;
                    for (LeptrisNodeRef r =
                             leptris_node_first_child(
                                 leptris_element_as_node(dq));
                         r; r = leptris_node_next_sibling(r)) {
                        if (leptris_node_get_type(r) !=
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            continue;
                        LeptrisElement re = (LeptrisElement)r;
                        const char* rl = NULL;
                        leptris_element_expanded_name(re, &rl, NULL,
                                                      NULL);
                        if (rl && strcmp(rl, "openContent") == 0) {
                            oc_elem = re;
                            oc_found = 1;
                        }
                    }
                }
            }
        }
        if (oc_found) {
            root->oc_declared = 1;
            const char* mode =
                leptris_element_attribute(oc_elem, "mode");
            if (mode && strcmp(mode, "suffix") == 0)
                oc_mode_decl = 1;
            else if (mode && strcmp(mode, "none") == 0)
                oc_mode_decl = 0;
            else
                oc_mode_decl = 2; /* absent = interleave */
            root->oc_declared = oc_mode_decl ? 1 : 2;
            int any_count = 0;
            int anno_count = 0;
            for (LeptrisNodeRef q =
                     leptris_node_first_child(
                         leptris_element_as_node(oc_elem));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (ql && strcmp(ql, "annotation") == 0) {
                    anno_count++;
                    continue;
                }
                if (ql && strcmp(ql, "any") == 0) {
                    any_count++;
                    /* bug 15618 (open048): the open content wildcard
                     * carries no occurrence attributes */
                    if (leptris_element_attribute(qe, "minOccurs") ||
                        leptris_element_attribute(qe, "maxOccurs")) {
                        xsd_set_error(s,
                                      "the openContent wildcard does "
                                      "not allow occurrence "
                                      "attributes");
                        s->oc_failed = 1;
                        break;
                    }
                    if (!root->open_any) {
                        root->open_any = xsd_capture_any(s, qe);
                        if (root->open_any) {
                            root->oc_owned = 1;
                            xsd_oc_resolve_not_qname(root->open_any,
                                                     qe);
                        }
                    }
                    continue;
                }
                /* anything else: s3_4_1si03 */
                xsd_set_error(s,
                              "openContent allows only an "
                              "annotation and one xs:any");
                s->oc_failed = 1;
                break;
            }
            if (anno_count > 1) {
                xsd_set_error(s,
                              "openContent allows at most one "
                              "annotation");
                s->oc_failed = 1;
            } else if (any_count > 1) {
                xsd_set_error(s,
                              "openContent allows only one xs:any");
                s->oc_failed = 1;
            } else if (oc_mode_decl && any_count == 0) {
                xsd_set_error(s,
                              "openContent with a mode other than "
                              "none requires an xs:any child");
                s->oc_failed = 1;
            } else if (!oc_mode_decl && any_count > 0) {
                xsd_set_error(s,
                              "openContent mode none forbids an "
                              "xs:any child");
                s->oc_failed = 1;
            }
            const char* oid =
                leptris_element_attribute(oc_elem, "id");
            if (oid && *oid) {
                if (!xsd_is_ncname_compile(oid)) {
                    xsd_set_error(s,
                                  "openContent/@id is not a valid "
                                  "NCName");
                    s->oc_failed = 1;
                } else if (xsd_oc_id_seen(s, oid)) {
                    xsd_set_error(s,
                                  "duplicate xs:ID on openContent");
                    s->oc_failed = 1;
                }
            }
            /* ct-props-correct.5: open content requires
             * element-only or mixed content — a simpleContent type
             * cannot carry one. */
            if (root->text_type) {
                xsd_set_error(s,
                              "openContent is not allowed on a "
                              "simple content type");
                s->oc_failed = 1;
            }
            root->oc_mode = oc_mode_decl;
        } else if (s->default_open &&
                   s->default_oc_unit_id == s->current_unit) {
            /* §3.4.2.3.3 clause 5.2: the schema default applies when
             * the explicit content is non-empty; mixed-empty keeps
             * variety mixed (non-empty); appliesToEmpty extends the
             * default to plain-empty models (open011). */
            int content_empty = !root->first_child ||
                                !root->first_child->first_child;
            if (!content_empty || root->mixed || s->default_oc_empty) {
                root->oc_mode =
                    s->default_oc_mode ? s->default_oc_mode : 2;
                root->open_any = s->default_open; /* borrowed */
                root->oc_owned = 0;
            }
        }
    }
    /* XSD 1.1: direct xs:assert children of the complexType —
     * captured when the schema claims 1.1, carries alternatives
     * (#2149) or carries assert syntax (#2154) */
    if (s->version_11 || s->saw_alternative || s->saw_assert)
        xsd_capture_assert_children(s, ct, root);
    /* the NFA and model walkers consume ONE root particle —
     * a multi-particle model (derivation splices) normalizes
     * under a sequence */
    if (root->first_child && root->first_child->next) {
        XsdCm* seq = xsd_cm_new(XSD_CM_SEQ);
        if (seq) {
            seq->first_child = root->first_child;
            root->first_child = seq;
        }
    }
    /* open-content semantics ride the model root (the NFA builds
     * from ct->first_child, never the name wrapper); an open
     * content without a particle model still synthesizes an empty
     * sequence carrier (§3.4.2.3.3 cl. 6.2 synthesizes the empty
     * sequence particle when the variety upgrades — open010). */
    if (!root->first_child && root->open_any) {
        XsdCm* seq = xsd_cm_new(XSD_CM_SEQ);
        if (seq) root->first_child = seq;
    }
    if (root->first_child) {
        root->first_child->open_any = root->open_any;
        root->first_child->oc_mode = root->oc_mode;
        root->first_child->oc_owned = root->oc_owned;
        root->first_child->oc_declared = root->oc_declared;
        root->first_child->oc_unit_id = root->oc_unit_id;
        root->open_any = NULL;
        root->oc_owned = 0;
        /* variety EMPTY (§3.4.2.3.3 cl. 4.1.1/3.1.2): explicit
         * content empty, not mixed, and NO open content applied —
         * such a type forbids character content entirely
         * (open012.n3). An applied open content upgrades the
         * variety to element-only (cl. 6.2): the element-only text
         * rule holds (non-whitespace text invalid — open010.n2,
         * whitespace legal — open010.v2). */
        root->first_child->empty_content =
            (s->version_11 || s->saw_open_content) && !root->mixed &&
            !root->first_child->first_child && !root->first_child->oc_mode;
        root->first_child->empty_upgraded =
            (s->version_11 || s->saw_open_content) && !root->mixed &&
            !root->first_child->first_child && root->first_child->oc_mode;
    } else if (root->open_any && root->oc_owned) {
        xsd_cm_free(root->open_any);
        root->open_any = NULL;
    }
    xsd_register_complex(s, root, attrs, name, any_attr_ns,
                         any_attr_not_qname, any_attr_not_ns,
                         any_attr_unit_ns);
    return root;
}

static const XsdElementDecl* xsd_find_element_decl(
    struct leptris_xsd_schema* s, const char* name) {
    for (XsdElementDecl* d = s->elements; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

const XsdElementDecl* xsd_substitute_member(
    struct leptris_xsd_schema* s, const char* name, const char* head) {
    const XsdElementDecl* d = xsd_find_element_decl(s, name);
    for (int hops = 0; d && d->sub_head && hops < 16; hops++) {
        if (strcmp(d->sub_head, head) == 0) return d;
        d = xsd_find_element_decl(s, d->sub_head);
    }
    return NULL;
}

int xsd_is_substitute(struct leptris_xsd_schema* s,
                      const char* name, const char* head) {
    return xsd_substitute_member(s, name, head) != NULL;
}

int xsd_schema_version_11(struct leptris_xsd_schema* s) {
    return s ? s->version_11 : 0;
}

XsdGroupDef* xsd_find_group(struct leptris_xsd_schema* s,
                            const char* name) {
    if (!s || !name) return NULL;
    for (XsdGroupDef* g = s->groups; g; g = g->next)
        if (strcmp(g->name, name) == 0) return g;
    return NULL;
}

/* leptris-rs#73: the global attribute declaration for a local name
 * (the ID-count pass resolves wildcard-admitted names against it). */
const XsdAttrDecl* xsd_find_top_attr(
    struct leptris_xsd_schema* s, const char* name) {
    if (!s || !name) return NULL;
    for (XsdAttrDecl* a = s->top_attrs; a; a = a->next)
        if (strcmp(a->name, name) == 0) return a;
    return NULL;
}

/* Deep-copy a captured row chain (attributeGroup refs splice
 * copies — every referencing type owns its rows). from_base marks
 * copies of a DERIVATION BASE's uses (#2182): the default-attrs
 * application merges against them (cos-st-extends) instead of
 * failing c-props-correct.4. */
static XsdAttrDecl* xsd_copy_attr_rows(const XsdAttrDecl* src,
                                       int from_base) {
    XsdAttrDecl* head = NULL;
    XsdAttrDecl* tail = NULL;
    for (; src; src = src->next) {
        XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
        if (!d) break;
        d->name = xsd_strdup(src->name);
        d->type = xsd_strdup(src->type);
        d->fixed = xsd_strdup(src->fixed);
        d->ns = xsd_strdup(src->ns);
        d->required = src->required;
        d->prohibited = src->prohibited;
        d->inheritable = src->inheritable;
        d->pending_ref = src->pending_ref;
        d->from_base = from_base ? 1 : 0;
        if (tail)
            tail->next = d;
        else
            head = d;
        tail = d;
    }
    return head;
}

/* leptris-ruby#569: re-resolve the rows the #1880 attributeGroup
 * pre-pass captured before the import chase had registered the
 * referenced unit's top-level attributes. Idempotent; pending rows
 * that never resolve keep declaring their name (untyped) — the
 * compile-time ref check already rejects true danglers loudly. */
#define XSD_PENDING_REF 1
#define XSD_PENDING_USE 2
static void xsd_resolve_pending_attr_refs(struct leptris_xsd_schema* s) {
    if (!s) return;
    int progressed = 1;
    while (progressed) {
        progressed = 0;
        for (XsdAttrGroupDef* g = s->attr_groups; g; g = g->next)
            for (XsdAttrDecl* a = g->attrs; a; a = a->next) {
                if (!(a->pending_ref & XSD_PENDING_REF) || !a->name)
                    continue;
                const XsdAttrDecl* src = xsd_find_top_attr(s, a->name);
                if (!src) continue;
                free(a->type);
                free(a->fixed);
                a->type = xsd_strdup(src->type);
                a->fixed = xsd_strdup(src->fixed);
                free(a->ns);
                a->ns = xsd_strdup(src->ns);
                if (!(a->pending_ref & XSD_PENDING_USE)) {
                    a->required = src->required;
                    a->prohibited = src->prohibited;
                }
                if (a->inheritable < 0) a->inheritable = src->inheritable;
                a->pending_ref &= ~XSD_PENDING_REF;
                progressed = 1;
            }
        for (XsdTypeAttrs* t = s->type_attrs; t; t = t->next)
            for (XsdAttrDecl* a = t->attrs; a; a = a->next) {
                if (!(a->pending_ref & XSD_PENDING_REF) || !a->name)
                    continue;
                const XsdAttrDecl* src = xsd_find_top_attr(s, a->name);
                if (!src) continue;
                free(a->type);
                free(a->fixed);
                a->type = xsd_strdup(src->type);
                a->fixed = xsd_strdup(src->fixed);
                free(a->ns);
                a->ns = xsd_strdup(src->ns);
                if (!(a->pending_ref & XSD_PENDING_USE)) {
                    a->required = src->required;
                    a->prohibited = src->prohibited;
                }
                if (a->inheritable < 0) a->inheritable = src->inheritable;
                a->pending_ref &= ~XSD_PENDING_REF;
                progressed = 1;
            }
    }
}

/* ---- #2182 §3.4.2.5: default attribute groups --------------------- */

/* Resolve one @defaultAttributes QName against the merged schema's
 * attributeGroup definitions. The group's namespace is the
 * declaring document's targetNamespace (read through its retained
 * element). ns == NULL never matches (an unresolvable ref — the
 * caller fails the schema), ns == "" matches a no-tns unit. */
static XsdAttrGroupDef* xsd_find_attr_group_qname(
    struct leptris_xsd_schema* s, const char* local, const char* ns) {
    if (!local) return NULL;
    for (XsdAttrGroupDef* g = s->attr_groups; g; g = g->next) {
        if (!g->name || strcmp(g->name, local) != 0) continue;
        char* gns = g->elem ? xsd_capture_unit_tns(g->elem) : NULL;
        int match;
        if (!ns)
            match = 0;
        else if (!ns[0])
            match = !gns || !gns[0];
        else
            match = gns && strcmp(gns, ns) == 0;
        free(gns);
        if (match) return g;
    }
    return NULL;
}

/* Mutable XsdTypeAttrs lookup — the const accessor only serves
 * reads; the application pass appends rows and wildcards. */
static XsdTypeAttrs* xsd_type_attrs_mutable(
    struct leptris_xsd_schema* s, const char* name, const char* ns) {
    for (XsdTypeAttrs* t = s->type_attrs; t; t = t->next)
        if (strcmp(t->type_name, name) == 0 &&
            ((!ns && !t->ns) ||
             (ns && t->ns && strcmp(t->ns, ns) == 0) ||
             (ns && !t->ns && !ns[0]) || (!ns && t->ns && !t->ns[0])))
            return t;
    return NULL;
}

/* Do two attribute rows denote the same expanded name? (ns ==
 * NULL and ns == "" both denote no namespace.) */
static int xsd_attr_rows_same_name(const XsdAttrDecl* a,
                                   const XsdAttrDecl* b) {
    if (!a->name || !b->name || strcmp(a->name, b->name) != 0)
        return 0;
    int an = a->ns && a->ns[0];
    int bn = b->ns && b->ns[0];
    if (an != bn) return 0;
    return !an || strcmp(a->ns, b->ns) == 0;
}

/* Apply ONE default group to ONE type's attribute table: the
 * group's uses append AFTER the type's own (§3.4.2.4 — computed as
 * if an <attributeGroup ref> child appeared after any other
 * attributeGroup children). A collision with the type's OWN use is
 * c-props-correct.4 and fails the schema (si02/si03); a collision
 * with a base-derived use is the cos-st-extends merge — skipped.
 * The group's xs:anyAttribute joins the type's wildcard computation
 * (own slot when free, else the base-union slot). Returns 0 on
 * success, -1 on the schema error. */
static int xsd_defattr_apply_group(struct leptris_xsd_schema* s,
                                   XsdTypeAttrs* ta,
                                   XsdAttrGroupDef* g,
                                   const char* tname) {
    XsdAttrDecl* rows =
        g->attrs ? xsd_copy_attr_rows(g->attrs, 0)
                 : xsd_capture_attr_rows_ex(s, g->elem, 0, g);
    for (XsdAttrDecl* r = rows; r; r = r->next) {
        int skip = 0;
        for (XsdAttrDecl* e = ta->attrs; e && !skip; e = e->next) {
            if (!xsd_attr_rows_same_name(e, r)) continue;
            if (e->from_base) {
                /* derivation union: one use survives */
                skip = 1;
            } else {
                char msg[512];
                snprintf(msg, sizeof(msg),
                         "c-props-correct.4: the defaultAttributes "
                         "group re-declares attribute '%s' on type "
                         "'%s'",
                         r->name ? r->name : "?",
                         tname ? tname : "?");
                xsd_set_error(s, msg);
                while (rows) {
                    XsdAttrDecl* rn = rows->next;
                    free(rows->name);
                    free(rows->type);
                    free(rows->fixed);
                    free(rows->ns);
                    free(rows);
                    rows = rn;
                }
                return -1;
            }
        }
        if (skip) {
            free(r->name);
            free(r->type);
            free(r->fixed);
            free(r->ns);
            /* splice out: the walker continues from r->next, so
             * rebuild the chain by freeing in place — the caller's
             * loop iterates `rows`, which we rewire below */
            r->name = NULL;
            r->type = NULL;
            r->fixed = NULL;
            r->ns = NULL;
            r->from_base = 2; /* tombstone */
            continue;
        }
    }
    /* append the survivors to the tail */
    XsdAttrDecl** tailp = &ta->attrs;
    while (*tailp) tailp = &(*tailp)->next;
    for (XsdAttrDecl* r = rows; r; r = r->next) {
        if (r->from_base == 2) continue;
        XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
        if (!d) break;
        d->name = r->name;
        d->type = r->type;
        d->fixed = r->fixed;
        d->ns = r->ns;
        d->required = r->required;
        d->prohibited = r->prohibited;
        d->inheritable = r->inheritable;
        d->pending_ref = r->pending_ref;
        *tailp = d;
        tailp = &d->next;
        r->name = NULL;
        r->type = NULL;
        r->fixed = NULL;
        r->ns = NULL;
    }
    while (rows) {
        XsdAttrDecl* rn = rows->next;
        free(rows->name);
        free(rows->type);
        free(rows->fixed);
        free(rows->ns);
        free(rows);
        rows = rn;
    }
    /* the group's xs:anyAttribute rides the default (open044):
     * #2184 — UNION the gathered group wildcard into the type's
     * effective wildcard (§3.4.2.4 appends the group's uses) */
    {
        XsdAttrWc gw;
        if (xsd_capture_attr_wc(s, g->elem, &gw)) {
            xsd_wc_fold_union_locals(&ta->any_attr_ns,
                                     &ta->any_attr_not_qname,
                                     &ta->any_attr_not_ns,
                                     &ta->any_attr_unit_ns, &gw);
            free(gw.ns);
            free(gw.unit_ns);
            free(gw.not_ns);
            free(gw.not_qname);
        }
    }
    return 0;
}

/* The post-capture pass: resolve every unit's @defaultAttributes
 * list (§3.17.6.2 — checked regardless of any type's opt-out,
 * open203.bad) and append each applying type's groups' uses
 * (§3.4.2.4). Returns 0, or -1 with s->error set (the caller
 * fails the compile). */
static int xsd_apply_default_attrs(struct leptris_xsd_schema* s) {
    for (XsdUnitDefAttrs* ud = s->unit_defattrs; ud; ud = ud->next)
        for (XsdDefAttrRef* r = ud->refs; r; r = r->next) {
            if (xsd_find_attr_group_qname(s, r->local, r->ns))
                continue;
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "defaultAttributes: attribute group '%s' does "
                     "not resolve%s%s",
                     r->local ? r->local : "?",
                     r->ns && r->ns[0] ? " in namespace " : "",
                     r->ns && r->ns[0] ? r->ns : "");
            xsd_set_error(s, msg);
            return -1;
        }
    /* one application per (type name, ns): a capture_force
     * re-registration can list a type's root twice — the merged
     * table is keyed by the name, not by each capture */
    typedef struct {
        const char* name;
        const char* ns;
    } applied_t;
    size_t applied_n = 0, applied_cap = 0;
    applied_t* applied = NULL;
    int rc = 0;
    for (XsdCm* c = s->complex_types; c && !rc; c = c->next) {
        if (!c->name || c->defattr_apply == 0 || c->defattr_unit < 0)
            continue;
        int seen = 0;
        for (size_t i = 0; i < applied_n && !seen; i++)
            if (strcmp(applied[i].name, c->name) == 0 &&
                ((!applied[i].ns && !c->ns) ||
                 (applied[i].ns && c->ns &&
                  strcmp(applied[i].ns, c->ns) == 0)))
                seen = 1;
        if (seen) continue;
        XsdUnitDefAttrs* ud = NULL;
        for (XsdUnitDefAttrs* u = s->unit_defattrs; u; u = u->next)
            if (u->unit == c->defattr_unit) {
                ud = u;
                break;
            }
        if (!ud || !ud->refs) continue;
        if (applied_n == applied_cap) {
            size_t ncap = applied_cap ? applied_cap * 2 : 8;
            applied_t* grown = (applied_t*)realloc(
                applied, ncap * sizeof(applied_t));
            if (!grown) break;
            applied = grown;
            applied_cap = ncap;
        }
        applied[applied_n].name = c->name;
        applied[applied_n].ns = c->ns;
        applied_n++;
        XsdTypeAttrs* ta =
            xsd_type_attrs_mutable(s, c->name, c->ns);
        if (!ta) {
            ta = (XsdTypeAttrs*)calloc(1, sizeof(*ta));
            if (!ta) break;
            ta->type_name = xsd_strdup(c->name);
            if (c->ns) ta->ns = xsd_strdup(c->ns);
            ta->next = s->type_attrs;
            s->type_attrs = ta;
        }
        for (XsdDefAttrRef* r = ud->refs; r && !rc; r = r->next) {
            XsdAttrGroupDef* g =
                xsd_find_attr_group_qname(s, r->local, r->ns);
            if (!g) continue; /* failed the check above */
            rc = xsd_defattr_apply_group(s, ta, g, c->name);
        }
    }
    free(applied);
    return rc;
}

/* Capture the attribute rows of a complexType or attributeGroup:
 * named rows, inline anonymous simpleTypes (synthesized slots),
 * refs to top-level attributes, and attributeGroup refs. */
/* Named row with an inline anonymous simpleType rides a
 * synthesized "attribute:NAME" slot, the element-slot pattern. */
static const char* xsd_inline_attr_type(
    struct leptris_xsd_schema* s, LeptrisElement ae,
    const char* attr_name) {
    for (LeptrisNodeRef q = leptris_node_first_child(
             leptris_element_as_node(ae));
         q; q = leptris_node_next_sibling(q)) {
        if (leptris_node_get_type(q) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement qe = (LeptrisElement)q;
        const char* ql = NULL;
        leptris_element_expanded_name(qe, &ql, NULL, NULL);
        if (!ql || strcmp(ql, "simpleType") != 0) continue;
        char synth[512];
        snprintf(synth, sizeof(synth), "attribute:%s", attr_name);
        XsdSimple* captured = xsd_capture_simple(qe);
        if (!captured) return NULL;
        free(captured->name);
        captured->name = xsd_strdup(synth);
        captured->next = s->simple_types;
        s->simple_types = captured;
        return captured->name;
    }
    return NULL;
}

static XsdAttrDecl* xsd_capture_attr_rows(struct leptris_xsd_schema* s,
                                          LeptrisElement container,
                                          int depth) {
    return xsd_capture_attr_rows_ex(s, container, depth, NULL);
}

static XsdAttrDecl* xsd_capture_attr_rows_ex(struct leptris_xsd_schema* s,
                                             LeptrisElement container,
                                             int depth,
                                             const XsdAttrGroupDef* owner) {
    XsdAttrDecl* head = NULL;
    XsdAttrDecl* tail = NULL;
    if (depth > 8) return NULL;
    for (LeptrisNodeRef ac = leptris_node_first_child(
             leptris_element_as_node(container));
         ac; ac = leptris_node_next_sibling(ac)) {
        if (leptris_node_get_type(ac) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ae = (LeptrisElement)ac;
        const char* al = NULL, *ap = NULL, *au = NULL;
        leptris_element_expanded_name(ae, &al, &ap, &au);
        if (!al || (au && strcmp(au, XSD_NS) != 0)) continue;

        if (strcmp(al, "attribute") == 0) {
            const char* an = leptris_element_attribute(ae, "name");
            const char* use = leptris_element_attribute(ae, "use");
            /* §3.3.5.6: inheritable rides the USE; a use-attribute
             * value overrides the referenced declaration's own
             * (cta0012: the non-inheritable use masks the
             * inheritable decl). calloc zeroes the field — force
             * the unset tri-state before reading the element. */
            int inh = xsd_bool_attr(ae, "inheritable", -1);
            XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
            if (!d) continue;
            d->inheritable = inh;
            if (an) {
                d->name = xsd_strdup(an);
                d->type = xsd_strdup(xsd_local_name(
                    leptris_element_attribute(ae, "type")));
                if (!d->type)
                    d->type = xsd_strdup(
                        xsd_inline_attr_type(s, ae, an));
                d->fixed = xsd_strdup(
                    leptris_element_attribute(ae, "fixed"));
                d->required = (use && strcmp(use, "required") == 0);
                d->prohibited =
                    (use && strcmp(use, "prohibited") == 0);
                /* leptris-ruby#679: form qualification (§3.2.3) —
                 * @form wins; otherwise the unit's
                 * attributeFormDefault decides (the same unit walk
                 * the elementFormDefault particle capture rides).
                 * Qualified -> the unit's targetNamespace; the row
                 * then binds only qualified instance attributes. */
                {
                    const char* aform =
                        leptris_element_attribute(ae, "form");
                    const char* afd = NULL;
                    if (!aform) {
                        for (LeptrisElement up =
                                 leptris_element_parent(ae);
                             up; up = leptris_element_parent(up)) {
                            const char* ul = NULL;
                            leptris_element_expanded_name(
                                up, &ul, NULL, NULL);
                            if (ul && strcmp(ul, "schema") == 0) {
                                afd = leptris_element_attribute(
                                    up, "attributeFormDefault");
                                break;
                            }
                        }
                    }
                    if (aform ? strcmp(aform, "qualified") == 0
                              : (afd &&
                                 strcmp(afd, "qualified") == 0)) {
                        char* tns = xsd_capture_unit_tns(ae);
                        if (tns && tns[0])
                            d->ns = tns;
                        else
                            free(tns);
                    }
                    /* #2185: an explicit @targetNamespace on the row
                     * (1.1 allows it on local declarations —
                     * target003) is the row's {target namespace};
                     * it overrides the form-derived one. */
                    {
                        const char* tns =
                            leptris_element_attribute(ae,
                                                      "targetNamespace");
                        if (tns && tns[0]) {
                            free(d->ns);
                            d->ns = xsd_strdup(tns);
                        }
                    }
                }
            } else {
                /* ref to a top-level attribute declaration */
                const char* ref = xsd_local_name(
                    leptris_element_attribute(ae, "ref"));
                const XsdAttrDecl* src =
                    ref ? xsd_find_top_attr(s, ref) : NULL;
                if (!src && !ref) {
                    free(d);
                    continue;
                }
                if (!src) {
                    /* leptris-ruby#569: the #1880 pre-pass captures
                     * attributeGroup defs BEFORE the import chase
                     * registers the referenced unit's globals — an
                     * unresolved ref used to be silently DROPPED
                     * here, hole-punching the type's rows (a:blip
                     * @r:embed "not declared (strict)") or leaving
                     * the type attribute-less so the strict walk
                     * never ran. Keep the row pending; the
                     * post-capture fixup resolves it. */
                    d->name = xsd_strdup(xsd_local_name(ref));
                    d->pending_ref = use ? 3 : 1;
                    d->required = (use && strcmp(use, "required") == 0);
                    d->prohibited =
                        (use && strcmp(use, "prohibited") == 0);
                } else {
                    d->name = xsd_strdup(src->name);
                    d->type = xsd_strdup(src->type);
                    d->fixed = xsd_strdup(src->fixed);
                    /* #679: a ref binds in the REFERENCED
                     * declaration's namespace (top-level attribute
                     * declarations are always qualified with their
                     * unit's tns) */
                    d->ns = xsd_strdup(src->ns);
                    d->required = use
                                      ? (strcmp(use, "required") == 0)
                                      : src->required;
                    d->prohibited = use
                                        ? (strcmp(use, "prohibited") ==
                                           0)
                                        : src->prohibited;
                    if (inh < 0) d->inheritable = src->inheritable;
                }
            }
            if (tail)
                tail->next = d;
            else
                head = d;
            tail = d;
        } else if (strcmp(al, "attributeGroup") == 0) {
            const char* ref = xsd_local_name(
                leptris_element_attribute(ae, "ref"));
            if (!ref) continue;
            XsdAttrGroupDef* g = s->attr_groups;
            for (; g; g = g->next)
                if (strcmp(g->name, ref) == 0) break;
            /* XSD 1.0 §4.2.2 redefine hook (moxml#545, attgC035-038):
             * a body redefining group N refs N to mean the ORIGINAL
             * group. The name-keyed slot may hold the redefining
             * body itself — when expanding THAT def and the ref
             * names it again, take the shadowed def instead, or the
             * original's members never splice into the composition. */
            if (g && g == owner) {
                XsdAttrGroupDef* alt = g->next;
                for (; alt; alt = alt->next)
                    if (strcmp(alt->name, g->name) == 0) break;
                g = alt;
            }
            if (!g) continue;
            /* leptris#1957: expand lazily through the def's own
             * element when its rows are not captured yet (a group
             * registered in phase 1 whose phase-2 capture has not
             * run, or runs later in document order). depth+1 keeps
             * the cycle guard. */
            XsdAttrDecl* copied =
                g->attrs ? xsd_copy_attr_rows(g->attrs, 0)
                         : xsd_capture_attr_rows_ex(s, g->elem, depth + 1,
                                                    g);
            if (!copied) continue;
            if (!head)
                head = copied;
            else
                tail->next = copied;
            tail = copied;
            while (tail->next) tail = tail->next;
        }
    }
    return head;
}

/* One container's xs:anyAttribute grammar (NULL when absent). */
/* #1735: the target namespace of the schema unit containing +e+
 * (the enclosing xs:schema's @targetNamespace). ##targetNamespace/
 * ##other in wildcards resolve against THIS, not the merged
 * schema's main tns — the unit boundary is where provenance is
 * otherwise lost. A unit with NO targetNamespace captures "" (the
 * empty string), NOT NULL: NULL still means "not captured" and
 * would fall back to the merged schema's tns, aiming the ##other
 * exclusion at the wrong namespace (the anyAttr-derive1 family —
 * a no-tns base unit's ##other must admit the main tns). */
static char* xsd_capture_unit_tns(LeptrisElement e) {
    for (LeptrisElement up = leptris_element_parent(e); up;
         up = leptris_element_parent(up)) {
        const char* local = NULL;
        leptris_element_expanded_name(up, &local, NULL, NULL);
        if (local && strcmp(local, "schema") == 0) {
            const char* tns =
                leptris_element_attribute(up, "targetNamespace");
            return xsd_strdup(tns ? tns : "");
        }
    }
    return NULL;
}

/* #2154 (§3.13.2): an assert test's {namespace bindings} ride the
 * in-scope declarations of the SCHEMA document at the assert
 * element, and {xpathDefaultNamespace} (§3.16.2/§3.13.2 — the local
 * attribute, else the schema root's) resolves unprefixed names.
 * Both become XQuery prolog declarations so the test compiles
 * against the schema's names: `chess:white` binds chess from the
 * schema (assert007), `y` under xpathDefaultNamespace="#
 * #targetNamespace" (ns1). Only prefixes the assert element has IN
 * SCOPE participate — the schema's unrelated declarations never
 * leak; the xmlns DEFAULT is xpathDefaultNamespace's channel. */
static char* xsd_capture_ns_prolog(LeptrisElement e,
                                   const char* base_dir) {
    struct { const char* pfx; const char* uri; } seen[16];
    int nseen = 0;
    for (LeptrisElement up = e; up; up = leptris_element_parent(up)) {
        size_t n = leptris_element_namespace_count(up);
        for (size_t i = 0; i < n && nseen < 16; i++) {
            const char* dp =
                leptris_element_namespace_decl_prefix(up, i);
            if (!dp || !dp[0]) continue; /* the default decl */
            int dup = 0;
            for (int k = 0; k < nseen && !dup; k++)
                if (strcmp(seen[k].pfx, dp) == 0) dup = 1;
            if (dup) continue;
            const char* du = leptris_element_namespace_decl_uri(up, i);
            if (!du) continue;
            seen[nseen].pfx = dp;
            seen[nseen].uri = du;
            nseen++;
        }
    }
    /* {xpathDefaultNamespace}: the assert element's attribute, else
     * the schema root's; ##defaultNamespace (absent) declares
     * nothing */
    const char* xdn =
        leptris_element_attribute(e, "xpathDefaultNamespace");
    char* unit_tns = NULL;
    if (!xdn || !xdn[0] || strcmp(xdn, "##targetNamespace") == 0) {
        unit_tns = xsd_capture_unit_tns(e);
        if (!xdn || !xdn[0]) {
            for (LeptrisElement up = e; up;
                 up = leptris_element_parent(up)) {
                const char* local = NULL;
                leptris_element_expanded_name(up, &local, NULL, NULL);
                if (local && strcmp(local, "schema") == 0) {
                    xdn = leptris_element_attribute(
                        up, "xpathDefaultNamespace");
                    break;
                }
            }
        }
    }
    const char* defns = NULL;
    if (xdn && xdn[0] && strcmp(xdn, "##defaultNamespace") != 0 &&
        strcmp(xdn, "##local") != 0 && strcmp(xdn, "##any") != 0) {
        /* ##local/##any leave unprefixed names namespace-free (the
         * XPath default) — no declaration (d4_3_15v27) */
        if (strcmp(xdn, "##targetNamespace") == 0)
            defns = (unit_tns && unit_tns[0]) ? unit_tns : NULL;
        else
            defns = xdn;
    }
    /* #2183: the test's static base URI — fn:doc inside an assert
     * test resolves its relative URIs against the SCHEMA
     * document's directory (assert011), delivered as
     * `declare base-uri`. A path carrying a double quote is
     * skipped (no escaping channel in the prolog). */
    int have_base = base_dir && base_dir[0] &&
                    !strchr(base_dir, '"');
    if (nseen == 0 && !defns && !have_base) {
        free(unit_tns);
        return NULL;
    }
    size_t cap = 64;
    for (int k = 0; k < nseen; k++)
        cap += strlen(seen[k].pfx) + strlen(seen[k].uri) + 32;
    if (defns) cap += strlen(defns) + 48;
    if (have_base) cap += strlen(base_dir) + 48;
    char* out = (char*)malloc(cap);
    if (!out) {
        free(unit_tns);
        return NULL;
    }
    size_t o = 0;
    for (int k = 0; k < nseen; k++) {
        /* the schema vocabulary's own prefix never resolves
         * instance names — skip it (only other prefixes bind) */
        if (strcmp(seen[k].uri, XSD_NS) == 0) continue;
        int w = snprintf(out + o, cap - o,
                         "declare namespace %s = \"%s\";",
                         seen[k].pfx, seen[k].uri);
        if (w > 0 && (size_t)w < cap - o) o += (size_t)w;
    }
    if (defns) {
        int w = snprintf(out + o, cap - o,
                         "declare default element namespace \"%s\";",
                         defns);
        if (w > 0 && (size_t)w < cap - o) o += (size_t)w;
    }
    if (have_base) {
        int w = snprintf(out + o, cap - o,
                         "declare base-uri \"%s/\";", base_dir);
        if (w > 0 && (size_t)w < cap - o) o += (size_t)w;
    }
    out[o] = 0;
    free(unit_tns);
    return o ? out : (free(out), NULL);
}

/* ---- #2184: the attribute wildcard negative sets ------------------
 * §3.10.2 XML representation + §3.10.6.3 subset/union/intersection.
 * Every wildcard is CANONIZED at capture:
 *   - grammar: ##targetNamespace resolved to the declaring unit's
 *     tns, ##other folded into ns=##any + not_ns {tns, ##local},
 *     absent @namespace spelled "##any" (leptris-rs#73);
 *   - notNamespace: ##targetNamespace resolved (no unit tns -> the
 *     token denotes the ABSENT namespace -> "##local");
 *   - notQName: prefixed tokens resolved to "{uri}local" against
 *     the declaring element's in-scope bindings.
 * A type's effective wildcard = the §3.10.6.3 INTERSECTION over its
 * direct xs:anyAttribute and every wildcard carried by its
 * attributeGroup refs; across an EXTENSION the base's wildcard
 * unions in (folds at the resolve site). */

/* The parked wildcard error: parks the message and flips wc_failed
 * (surfaced post-capture, the ct_failed pattern). */
static void xsd_wc_fail(struct leptris_xsd_schema* s, const char* msg) {
    xsd_set_error(s, msg);
    s->wc_failed = 1;
}

/* One grammar/exclusion token walk with shared whitespace rules. */
static const char* xsd_wc_next_tok(const char* p, const char** start,
                                   size_t* len) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    *start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        p++;
    *len = (size_t)(p - *start);
    return p;
}

static void xsd_wc_append(char** dst, const char* tok, size_t len) {
    size_t old = *dst ? strlen(*dst) : 0;
    char* grown = (char*)realloc(*dst, old + len + 2);
    if (!grown) return;
    *dst = grown;
    if (old) (*dst)[old++] = ' ';
    memcpy(*dst + old, tok, len);
    (*dst)[old + len] = 0;
}

static int xsd_wc_tok_in(const char* list, const char* tok, size_t len) {
    const char* p = list;
    while (p && *p) {
        const char* start;
        size_t l;
        p = xsd_wc_next_tok(p, &start, &l);
        if (l == 0) break;
        if (l == len && strncmp(start, tok, len) == 0) return 1;
    }
    return 0;
}

/* Canonize the POSITIVE namespace grammar. *out_ns gets the canon
 * grammar (##any / URI list / ##local / ""); *out_other gets the
 * exclusions ##other implies (the declaring unit's tns + ##local).
 * A ##targetNamespace token with no unit tns drops out (admits
 * nothing). */
static void xsd_wc_canon_ns(const char* raw, const char* tns,
                            char** out_ns, char** out_other) {
    if (!raw || !*raw) {
        /* leptris-rs#73: absent @namespace means ##any — a bare
         * <xs:anyAttribute /> is a live wildcard, not an invisible
         * one. */
        *out_ns = xsd_strdup("##any");
        return;
    }
    int saw_any = 0;
    const char* p = raw;
    while (*p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (len == 5 && strncmp(start, "##any", 5) == 0) {
            saw_any = 1;
            continue;
        }
        if (len == 17 &&
            strncmp(start, "##targetNamespace", 17) == 0) {
            if (tns && *tns) xsd_wc_append(out_ns, tns, strlen(tns));
            continue;
        }
        if (len == 7 && strncmp(start, "##other", 7) == 0) {
            saw_any = 1;
            if (tns && *tns) xsd_wc_append(out_other, tns, strlen(tns));
            xsd_wc_append(out_other, "##local", 7);
            continue;
        }
        xsd_wc_append(out_ns, start, len);
    }
    if (saw_any) {
        free(*out_ns);
        *out_ns = xsd_strdup("##any");
    } else if (!*out_ns) {
        *out_ns = xsd_strdup("");
    }
}

/* Canonize an @notNamespace exclusion list: ##targetNamespace
 * resolves to the unit tns, or denotes the ABSENT namespace
 * ("##local") when the declaring unit has none (wild033). */
static char* xsd_wc_canon_not_ns(const char* raw, const char* tns) {
    if (!raw) return NULL;
    char* out = NULL;
    const char* p = raw;
    while (*p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (len == 17 &&
            strncmp(start, "##targetNamespace", 17) == 0) {
            if (tns && *tns)
                xsd_wc_append(&out, tns, strlen(tns));
            else
                xsd_wc_append(&out, "##local", 7);
            continue;
        }
        if (len == 7 && strncmp(start, "##local", 7) == 0) {
            xsd_wc_append(&out, "##local", 7);
            continue;
        }
        xsd_wc_append(&out, start, len);
    }
    return out;
}

/* The @notNamespace hit test over a CANON list (URIs + ##local).
 * target_ns defends against legacy raw captures that still carry
 * ##targetNamespace tokens. */
static int xsd_wc_not_ns_hits(const char* tokens, const char* uri,
                              const char* target_ns) {
    if (!tokens) return 0;
    const char* p = tokens;
    while (*p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (len == 7 && strncmp(start, "##local", 7) == 0) {
            if (uri == NULL) return 1;
            continue;
        }
        if (len == 17 &&
            strncmp(start, "##targetNamespace", 17) == 0) {
            if (target_ns && uri && strcmp(uri, target_ns) == 0)
                return 1;
            continue;
        }
        if (uri && strlen(uri) == len &&
            strncmp(start, uri, len) == 0)
            return 1;
    }
    return 0;
}

/* Canonize the @notQName token list: prefixed QNames resolve to
 * "{uri}local" against the declaring element's in-scope bindings;
 * malformed spellings (empty prefix, empty local, an embedded
 * colon) and undeclared prefixes FAIL the schema (wild036-039).
 * ##defined / ##definedSibling / bare NCName literals pass
 * through. Returns the canon list, or NULL with wc_failed set. */
static char* xsd_wc_canon_not_qname(struct leptris_xsd_schema* s,
                                    LeptrisElement decl,
                                    const char* raw) {
    if (!raw) return NULL;
    char* out = NULL;
    const char* p = raw;
    while (*p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (len == 9 && strncmp(start, "##defined", 9) == 0) {
            xsd_wc_append(&out, "##defined", 9);
            continue;
        }
        if (len == 16 &&
            strncmp(start, "##definedSibling", 16) == 0) {
            xsd_wc_append(&out, "##definedSibling", 16);
            continue;
        }
        const char* colon = memchr(start, ':', len);
        if (!colon) {
            xsd_wc_append(&out, start, len); /* bare no-ns literal */
            continue;
        }
        /* prefixed: exactly one colon, non-empty halves */
        size_t plen = (size_t)(colon - start);
        size_t llen = len - plen - 1;
        if (plen == 0 || llen == 0 ||
            memchr(colon + 1, ':', llen)) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "src-wildcard: malformed notQName token "
                     "'%.*s'", (int)len, start);
            xsd_wc_fail(s, msg);
            free(out);
            return NULL;
        }
        char pfx[64];
        if (plen >= sizeof(pfx)) plen = sizeof(pfx) - 1;
        memcpy(pfx, start, plen);
        pfx[plen] = 0;
        char uri[512];
        xsd_ns_for_prefix(decl, pfx, uri, sizeof(uri));
        if (!uri[0]) {
            /* the xml prefix is predeclared to the XML namespace
             * (in scope without a xmlns:xml declaration — wild027:
             * notQName='xml:space xml:id' with no binding spelled);
             * xmlns defends the same way xsd_resolve_qname does */
            if (strcmp(pfx, "xml") == 0)
                snprintf(uri, sizeof(uri), "%s",
                         "http://www.w3.org/XML/1998/namespace");
            else if (strcmp(pfx, "xmlns") == 0)
                snprintf(uri, sizeof(uri), "%s",
                         "http://www.w3.org/2000/xmlns/");
        }
        if (!uri[0]) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "src-wildcard: the notQName prefix '%s' is "
                     "not declared in the schema's scope", pfx);
            xsd_wc_fail(s, msg);
            free(out);
            return NULL;
        }
        char tok[600];
        snprintf(tok, sizeof(tok), "{%s}%.*s", uri, (int)llen,
                 colon + 1);
        xsd_wc_append(&out, tok, strlen(tok));
    }
    return out;
}

/* The instance namespace a canon notQName token denotes: NULL for
 * the absent namespace, the URI for "{uri}local", NULL for
 * ##defined/#definedSibling (their namespace varies). */
static void xsd_wc_qname_ns(const char* tok, size_t len,
                            const char** uri, size_t* ulen) {
    *uri = NULL;
    *ulen = 0;
    if (len == 0 || tok[0] != '{') return;
    const char* close = memchr(tok, '}', len);
    if (!close) return;
    *uri = tok + 1;
    *ulen = (size_t)(close - tok - 1);
}

/* src-wildcard sweep on ONE wildcard element: the @namespace and
 * @notNamespace varieties are mutually exclusive, and a notQName
 * token whose namespace the wildcard could never admit is vacuous
 * (wild031-035). The grammar/not_ns arguments are the CANON forms;
 * raw_not_ns defends the co-occurrence check. */
static int xsd_wc_check(struct leptris_xsd_schema* s,
                        const char* raw_ns, const char* raw_not_ns,
                        const char* canon_ns, const char* canon_not_ns,
                        const char* canon_nq) {
    if (raw_ns && raw_not_ns) {
        xsd_wc_fail(s,
                    "src-wildcard: the @namespace and @notNamespace "
                    "attributes cannot both be present");
        return 0;
    }
    if (!canon_nq) return 1;
    const char* p = canon_nq;
    while (*p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (start[0] == '#') continue; /* ##defined family */
        const char* turi;
        size_t tulen;
        xsd_wc_qname_ns(start, len, &turi, &tulen);
        char tbuf[512];
        const char* t = NULL;
        if (turi) {
            if (tulen >= sizeof(tbuf)) tulen = sizeof(tbuf) - 1;
            memcpy(tbuf, turi, tulen);
            tbuf[tulen] = 0;
            t = tbuf;
        }
        int admitted = xsd_any_attr_matches(canon_ns, t, NULL) &&
                       !xsd_wc_not_ns_hits(canon_not_ns, t, NULL);
        if (!admitted) {
            xsd_wc_fail(s,
                        "src-wildcard: the notQName exclusion is "
                        "vacuous — the wildcard cannot admit a "
                        "name in that namespace");
            return 0;
        }
    }
    return 1;
}

/* Capture ONE xs:anyAttribute element into a canon member. Returns
 * 0 (with wc_failed set) when the wildcard is illegal. */
static int xsd_wc_capture_member(struct leptris_xsd_schema* s,
                                 LeptrisElement ae, XsdAttrWc* m) {
    memset(m, 0, sizeof(*m));
    m->unit_ns = xsd_capture_unit_tns(ae);
    const char* raw_ns = leptris_element_attribute(ae, "namespace");
    const char* raw_nns =
        leptris_element_attribute(ae, "notNamespace");
    const char* raw_nq = leptris_element_attribute(ae, "notQName");
    char* other = NULL;
    xsd_wc_canon_ns(raw_ns, m->unit_ns, &m->ns, &other);
    m->not_ns = xsd_wc_canon_not_ns(raw_nns, m->unit_ns);
    if (other) {
        xsd_wc_append(&m->not_ns, other, strlen(other));
        free(other);
    }
    m->not_qname = xsd_wc_canon_not_qname(s, ae, raw_nq);
    if (!xsd_wc_check(s, raw_ns, raw_nns, m->ns, m->not_ns,
                      m->not_qname))
        return 0;
    return 1;
}

static void xsd_wc_member_free(XsdAttrWc* m) {
    while (m) {
        XsdAttrWc* n = m->next;
        free(m->ns);
        free(m->unit_ns);
        free(m->not_ns);
        free(m->not_qname);
        free(m);
        m = n;
    }
}

static int xsd_wc_empty(const XsdAttrWc* m) {
    return !m->ns && !m->not_ns && !m->not_qname;
}

/* §3.10.6.3 intersection fold: the admitted set of the fold admits
 * a name only when EVERY member admits it — the positive sets
 * intersect and the exclusion lists concat. */
static void xsd_wc_intersect(XsdAttrWc* acc, const XsdAttrWc* m) {
    if (xsd_wc_empty(acc)) {
        free(acc->ns);
        free(acc->not_ns);
        free(acc->not_qname);
        memset(acc, 0, sizeof(*acc));
        acc->ns = m->ns ? xsd_strdup(m->ns) : NULL;
        acc->not_ns = m->not_ns ? xsd_strdup(m->not_ns) : NULL;
        acc->not_qname =
            m->not_qname ? xsd_strdup(m->not_qname) : NULL;
        if (!acc->unit_ns) acc->unit_ns = m->unit_ns
                                              ? xsd_strdup(m->unit_ns)
                                              : NULL;
        return;
    }
    /* positive: ##any ∩ X = X; list ∩ list = common tokens */
    if (m->ns && strcmp(m->ns, "##any") == 0) {
        /* keep acc's */
    } else if (acc->ns && strcmp(acc->ns, "##any") == 0) {
        free(acc->ns);
        acc->ns = xsd_strdup(m->ns);
    } else if (acc->ns && m->ns) {
        char* kept = NULL;
        const char* p = acc->ns;
        while (*p) {
            const char* start;
            size_t len;
            p = xsd_wc_next_tok(p, &start, &len);
            if (len == 0) break;
            char tok[512];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len);
            tok[len] = 0;
            const char* uri = strcmp(tok, "##local") == 0
                                  ? NULL
                                  : tok;
            if (xsd_any_attr_matches(m->ns, uri, NULL))
                xsd_wc_append(&kept, tok, strlen(tok));
        }
        free(acc->ns);
        acc->ns = kept ? kept : xsd_strdup("");
    }
    if (m->not_ns) xsd_wc_append(&acc->not_ns, m->not_ns,
                                 strlen(m->not_ns));
    if (m->not_qname) xsd_wc_append(&acc->not_qname, m->not_qname,
                                    strlen(m->not_qname));
}

/* §3.10.6.3 union fold (extension + the default attribute group's
 * wildcard): the positive sets union and a name/namespace survives
 * the exclusion fold only when EVERY arm rejects it. */
static void xsd_wc_union(XsdAttrWc* acc, const XsdAttrWc* b) {
    if (xsd_wc_empty(b)) return;
    if (xsd_wc_empty(acc)) {
        free(acc->ns);
        free(acc->not_ns);
        free(acc->not_qname);
        memset(acc, 0, sizeof(*acc));
        acc->ns = b->ns ? xsd_strdup(b->ns) : NULL;
        acc->not_ns = b->not_ns ? xsd_strdup(b->not_ns) : NULL;
        acc->not_qname =
            b->not_qname ? xsd_strdup(b->not_qname) : NULL;
        if (!acc->unit_ns) acc->unit_ns = b->unit_ns
                                              ? xsd_strdup(b->unit_ns)
                                              : NULL;
        return;
    }
    const char* a_ns_orig = acc->ns;
    const char* b_ns_orig = b->ns;
    int a_any = a_ns_orig && strcmp(a_ns_orig, "##any") == 0;
    int b_any = b_ns_orig && strcmp(b_ns_orig, "##any") == 0;
    char* ns = NULL;
    if (a_any || b_any) {
        ns = xsd_strdup("##any");
    } else if (a_ns_orig && b_ns_orig) {
        ns = xsd_strdup(a_ns_orig);
        xsd_wc_append(&ns, b_ns_orig, strlen(b_ns_orig));
    } else if (a_ns_orig) {
        ns = xsd_strdup(a_ns_orig);
    } else {
        ns = xsd_strdup(b_ns_orig ? b_ns_orig : "");
    }
    /* exclusion fold: keep a token only when both arms reject it */
    const char* sources[4];
    int is_a[4];
    int is_q[4];
    int n = 0;
    if (acc->not_ns) {
        sources[n] = acc->not_ns;
        is_a[n] = 1;
        is_q[n] = 0;
        n++;
    }
    if (acc->not_qname) {
        sources[n] = acc->not_qname;
        is_a[n] = 1;
        is_q[n] = 1;
        n++;
    }
    if (b->not_ns) {
        sources[n] = b->not_ns;
        is_a[n] = 0;
        is_q[n] = 0;
        n++;
    }
    if (b->not_qname) {
        sources[n] = b->not_qname;
        is_a[n] = 0;
        is_q[n] = 1;
        n++;
    }
    char* kept_ns = NULL;
    char* kept_nq = NULL;
    for (int si = 0; si < n; si++) {
        const char* p = sources[si];
        int from_a = is_a[si];
        int is_qname_list = is_q[si];
        while (*p) {
            const char* start;
            size_t len;
            p = xsd_wc_next_tok(p, &start, &len);
            if (len == 0) break;
            char tok[600];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len);
            tok[len] = 0;
            int defined_tok =
                strcmp(tok, "##defined") == 0 ||
                strcmp(tok, "##definedSibling") == 0;
            /* arm rejection of this token */
            int rej_a = 1;
            int rej_b = 1;
            if (defined_tok) {
                rej_a = from_a
                            ? xsd_wc_tok_in(acc->not_qname, tok,
                                            strlen(tok))
                            : xsd_wc_tok_in(b->not_qname, tok,
                                            strlen(tok));
                rej_b = from_a
                            ? xsd_wc_tok_in(b->not_qname, tok,
                                            strlen(tok))
                            : xsd_wc_tok_in(acc->not_qname, tok,
                                            strlen(tok));
            } else if (is_qname_list) {
                const char* turi;
                size_t tulen;
                xsd_wc_qname_ns(tok, strlen(tok), &turi, &tulen);
                char tbuf[512];
                const char* t = NULL;
                if (turi) {
                    if (tulen >= sizeof(tbuf))
                        tulen = sizeof(tbuf) - 1;
                    memcpy(tbuf, turi, tulen);
                    tbuf[tulen] = 0;
                    t = tbuf;
                }
                rej_a = xsd_wc_tok_in(acc->not_qname, tok,
                                      strlen(tok)) ||
                        !xsd_any_attr_matches(a_ns_orig, t, NULL);
                rej_b = xsd_wc_tok_in(b->not_qname, tok,
                                      strlen(tok)) ||
                        !xsd_any_attr_matches(b_ns_orig, t, NULL);
            } else {
                const char* t = strcmp(tok, "##local") == 0
                                    ? NULL
                                    : tok;
                rej_a = xsd_wc_tok_in(acc->not_ns, tok,
                                      strlen(tok)) ||
                        !xsd_any_attr_matches(a_ns_orig, t, NULL);
                rej_b = xsd_wc_tok_in(b->not_ns, tok,
                                      strlen(tok)) ||
                        !xsd_any_attr_matches(b_ns_orig, t, NULL);
            }
            if (rej_a && rej_b) {
                if (is_qname_list || defined_tok)
                    xsd_wc_append(&kept_nq, tok, strlen(tok));
                else
                    xsd_wc_append(&kept_ns, tok, strlen(tok));
            }
        }
    }
    free(acc->ns);
    free(acc->not_ns);
    free(acc->not_qname);
    acc->ns = ns;
    acc->not_ns = kept_ns;
    acc->not_qname = kept_nq;
}

/* Gather every wildcard under `container` (its direct
 * xs:anyAttribute plus each referenced attributeGroup's, recursive,
 * depth-bounded) into the linked `members` list. */
static void xsd_wc_gather(struct leptris_xsd_schema* s,
                          LeptrisElement container, int depth,
                          XsdAttrWc** members, XsdAttrWc** tail) {
    if (depth > 8) return;
    for (LeptrisNodeRef ac = leptris_node_first_child(
             leptris_element_as_node(container));
         ac; ac = leptris_node_next_sibling(ac)) {
        if (leptris_node_get_type(ac) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ae = (LeptrisElement)ac;
        const char* al = NULL, *au = NULL;
        leptris_element_expanded_name(ae, &al, NULL, &au);
        if (!al || (au && strcmp(au, XSD_NS) != 0)) continue;
        if (strcmp(al, "anyAttribute") == 0) {
            XsdAttrWc* m = (XsdAttrWc*)calloc(1, sizeof(*m));
            if (!m) continue;
            if (!xsd_wc_capture_member(s, ae, m)) {
                free(m->ns);
                free(m->unit_ns);
                free(m->not_ns);
                free(m->not_qname);
                free(m);
                continue;
            }
            if (*tail)
                (*tail)->next = m;
            else
                *members = m;
            *tail = m;
            continue;
        }
        if (strcmp(al, "attributeGroup") == 0) {
            const char* ref = xsd_local_name(
                leptris_element_attribute(ae, "ref"));
            if (!ref) continue;
            XsdAttrGroupDef* g = s->attr_groups;
            for (; g; g = g->next)
                if (strcmp(g->name, ref) == 0) break;
            if (g && g->elem)
                xsd_wc_gather(s, g->elem, depth + 1, members,
                              tail);
        }
    }
}

/* The effective attribute wildcard for a complexType body: the
 * §3.10.6.3 intersection over the gathered members. Fills `eff`;
 * returns 1 when a wildcard exists. */
static int xsd_capture_attr_wc(struct leptris_xsd_schema* s,
                               LeptrisElement container,
                               XsdAttrWc* eff) {
    memset(eff, 0, sizeof(*eff));
    XsdAttrWc* members = NULL;
    XsdAttrWc* tail = NULL;
    xsd_wc_gather(s, container, 0, &members, &tail);
    for (XsdAttrWc* m = members; m; m = m->next)
        xsd_wc_intersect(eff, m);
    xsd_wc_member_free(members);
    if (xsd_wc_empty(eff)) {
        free(eff->ns);
        free(eff->not_ns);
        free(eff->not_qname);
        free(eff->unit_ns);
        memset(eff, 0, sizeof(*eff));
        return 0;
    }
    return 1;
}

/* Fold the BASE wildcard into the four locals the capture driver
 * carries (the eager extension path and the default-group ride). */
static void xsd_wc_fold_union_locals(char** ns, char** nq, char** nns,
                                     char** unit,
                                     const XsdAttrWc* b) {
    if (xsd_wc_empty(b)) return;
    XsdAttrWc acc;
    memset(&acc, 0, sizeof(acc));
    acc.ns = *ns;
    acc.not_qname = *nq;
    acc.not_ns = *nns;
    acc.unit_ns = *unit;
    xsd_wc_union(&acc, b);
    *ns = acc.ns;
    *nq = acc.not_qname;
    *nns = acc.not_ns;
    *unit = acc.unit_ns;
}

/* The deferred fold (xsd_resolve_extension_bases): union the base
 * row's effective wildcard into the DERIVED row in place. */
static void xsd_wc_fold_union(XsdTypeAttrs* mine,
                              const XsdTypeAttrs* base) {
    XsdAttrWc b;
    memset(&b, 0, sizeof(b));
    b.ns = base->any_attr_ns;
    b.not_qname = base->any_attr_not_qname;
    b.not_ns = base->any_attr_not_ns;
    b.unit_ns = base->any_attr_unit_ns;
    xsd_wc_fold_union_locals(&mine->any_attr_ns,
                             &mine->any_attr_not_qname,
                             &mine->any_attr_not_ns,
                             &mine->any_attr_unit_ns, &b);
}

/* ---- #2184: ##definedSibling + the restriction subset law -------- */

/* §3.10.2: ##definedSibling excludes every name the SIBLING element
 * particles of the wildcard's parent declare. The expansion pass
 * rewrites the token into the concrete canon names (wild070-074,
 * wild084) — after the fold, when the spliced base particles are
 * in place. */
static void xsd_wc_expand_defined_sibling(XsdCm* any, XsdCm* parent,
                                          struct leptris_xsd_schema* s);

static void xsd_wc_expand_defined_sibling(XsdCm* any, XsdCm* parent,
                                          struct leptris_xsd_schema* s) {
    if (!any || !any->not_qname ||
        !strstr(any->not_qname, "##definedSibling"))
        return;
    char names[4096];
    size_t o = 0;
    int saw = 0;
    for (XsdCm* c = parent ? parent->first_child : NULL; c;
         c = c->next) {
        if (c->kind != XSD_CM_ELEMENT || !c->name) continue;
        char tok[600];
        if (c->ns && *c->ns)
            snprintf(tok, sizeof(tok), "{%s}%s", c->ns, c->name);
        else
            snprintf(tok, sizeof(tok), "%s", c->name);
        /* §3.10.2: the sibling names expand by SUBSTITUTION GROUP
         * membership (wild071: the head's member A is a defined
         * sibling too) — every global whose sub_head chains to the
         * sibling's name rides along, transitive to depth 4. */
        char heads[8][256];
        int hn = 0;
        snprintf(heads[hn++], sizeof(heads[0]), "%s", c->name);
        for (int pass = 0; pass < 4; pass++) {
            int added = 0;
            for (const XsdElementDecl* d = s ? s->elements : NULL;
                 d; d = d->next) {
                if (!d->sub_head) continue;
                int in_chain = 0;
                for (int i = 0; i < hn && !in_chain; i++)
                    if (strcmp(d->sub_head, heads[i]) == 0)
                        in_chain = 1;
                if (!in_chain) continue;
                char mtok[600];
                if (d->ns && *d->ns)
                    snprintf(mtok, sizeof(mtok), "{%s}%s", d->ns,
                             d->name);
                else
                    snprintf(mtok, sizeof(mtok), "%s", d->name);
                int dup = strstr(names, mtok) != NULL;
                if (!dup && o + strlen(mtok) + 2 < sizeof(names)) {
                    if (o) names[o++] = ' ';
                    memcpy(names + o, mtok, strlen(mtok) + 1);
                    o += strlen(mtok);
                    saw = 1;
                    added = 1;
                }
                /* walk the member chain */
                int known = 0;
                for (int i = 0; i < hn && !known; i++)
                    if (strcmp(d->name, heads[i]) == 0) known = 1;
                if (!known && hn < 8)
                    snprintf(heads[hn++], sizeof(heads[0]), "%s",
                             d->name);
                (void)added;
            }
            if (!added) break;
        }
        size_t tl = strlen(tok);
        if (strstr(names, tok)) continue;
        if (o + tl + 2 >= sizeof(names)) break;
        if (o) names[o++] = ' ';
        memcpy(names + o, tok, tl + 1);
        o += tl;
        saw = 1;
    }
    if (!saw) return;
    /* rewrite the token list with the sibling names in place of
     * ##definedSibling */
    char out[2048];
    size_t wo = 0;
    const char* p = any->not_qname;
    while (*p && wo + 1 < sizeof(out)) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        if (len == 16 && strncmp(start, "##definedSibling", 16) == 0) {
            size_t nl = o;
            if (wo + nl + 2 >= sizeof(out)) break;
            if (wo) out[wo++] = ' ';
            memcpy(out + wo, names, nl);
            wo += nl;
            continue;
        }
        if (wo + len + 2 >= sizeof(out)) break;
        if (wo) out[wo++] = ' ';
        memcpy(out + wo, start, len);
        wo += len;
    }
    out[wo] = 0;
    free(any->not_qname);
    any->not_qname = xsd_strdup(out);
}

static void xsd_wc_expand_group(XsdCm* node,
                                struct leptris_xsd_schema* s) {
    if (!node) return;
    switch (node->kind) {
        case XSD_CM_SEQ:
        case XSD_CM_CHOICE:
        case XSD_CM_ALL:
            for (XsdCm* c = node->first_child; c; c = c->next) {
                if (c->kind == XSD_CM_ANY)
                    xsd_wc_expand_defined_sibling(c, node, s);
                else
                    xsd_wc_expand_group(c, s);
            }
            break;
        default:
            break;
    }
}

/* The post-capture expansion: every captured complexType's model
 * tree (plus the type's open-content wildcard, whose "siblings"
 * are the model's element particles). The schema-default wildcard
 * is BORROWED (shared across the unit's types) — a type whose
 * instance carries ##definedSibling privatizes it first, so the
 * sibling expansion stays per-type. Runs after
 * xsd_resolve_extension_bases so the fold-spliced copies carry
 * their base's (already expanded) names. */
void xsd_wc_expand_defined_sibling_pass(struct leptris_xsd_schema* s) {
    for (XsdCm* t = s->complex_types; t; t = t->next) {
        xsd_wc_expand_group(t, s);
        XsdCm* model = t->first_child;
        XsdCm* oc = model ? model->open_any : NULL;
        if (oc && oc->not_qname &&
            strstr(oc->not_qname, "##definedSibling") &&
            !(model->oc_owned)) {
            XsdCm* copy = xsd_cm_copy_list(oc);
            if (copy) {
                copy->next = NULL;
                model->open_any = copy;
                model->oc_owned = 1;
                oc = copy;
            }
        }
        if (oc)
            xsd_wc_expand_defined_sibling(oc, model, s);
    }
}

/* Is a namespace `uri` (NULL = absent) inside the admitted set of a
 * canon wildcard row (grammar + not_ns)? */
static int xsd_wc_row_admits_ns(const XsdTypeAttrs* ta,
                                const char* uri) {
    if (!xsd_any_attr_matches(ta->any_attr_ns, uri, NULL)) return 0;
    if (xsd_wc_not_ns_hits(ta->any_attr_not_ns, uri, NULL)) return 0;
    return 1;
}

/* The name a canon notQName token denotes, for the subset walk:
 * 0 = concrete {uri}local (uri filled), 1 = ##defined family. */
static int xsd_wc_nq_token(const char* tok, size_t len, char* uri,
                           size_t cap) {
    if (len > 0 && tok[0] == '#') return 1;
    if (len > 0 && tok[0] == '{') {
        const char* close = memchr(tok, '}', len);
        if (close) {
            size_t ul = (size_t)(close - tok - 1);
            if (ul >= cap) ul = cap - 1;
            memcpy(uri, tok + 1, ul);
            uri[ul] = 0;
            return 0;
        }
    }
    uri[0] = 0; /* bare literal: the absent namespace */
    return 0;
}

/* §3.10.6.3 + derivation-ok-restriction.4.1: the RESTRICTION's
 * attribute wildcard must be a subset of the base's. With the canon
 * forms: every namespace the derived admits, the base admits too;
 * and every name the base disallows, the derived disallows as well
 * (within the derived's admitted namespaces). */
static int xsd_wc_attr_subset(const XsdTypeAttrs* sub,
                              const XsdTypeAttrs* sup,
                              const char* sup_name, char* msg,
                              size_t cap) {
    /* namespaces: witness set = both rows' grammar tokens +
     * exclusion tokens */
    const char* lists[4];
    lists[0] = sub->any_attr_ns;
    lists[1] = sub->any_attr_not_ns;
    lists[2] = sup->any_attr_ns;
    lists[3] = sup->any_attr_not_ns;
    for (int li = 0; li < 4; li++) {
        const char* p = lists[li];
        while (p && *p) {
            const char* start;
            size_t len;
            p = xsd_wc_next_tok(p, &start, &len);
            if (len == 0) break;
            char tok[512];
            if (len >= sizeof(tok)) len = sizeof(tok) - 1;
            memcpy(tok, start, len);
            tok[len] = 0;
            const char* uri = strcmp(tok, "##local") == 0
                                  ? NULL
                                  : tok;
            if (xsd_wc_row_admits_ns(sub, uri) &&
                !xsd_wc_row_admits_ns(sup, uri)) {
                snprintf(msg, cap,
                         "derivation-ok-restriction-4.1: the "
                         "attribute wildcard is not a valid subset "
                         "of the base complex type definition "
                         "'%s' (the namespace '%s' is admitted by "
                         "the derivation but not the base)",
                         sup_name, uri ? uri : "(no namespace)");
                return 0;
            }
        }
    }
    /* names: every name the base disallows must be disallowed by
     * the derivation too (or fall outside the derivation's
     * admitted namespaces) */
    const char* p = sup->any_attr_not_qname;
    while (p && *p) {
        const char* start;
        size_t len;
        p = xsd_wc_next_tok(p, &start, &len);
        if (len == 0) break;
        char tok[600];
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, start, len);
        tok[len] = 0;
        if (xsd_wc_tok_in(sub->any_attr_not_qname, tok, strlen(tok)))
            continue;
        char turi[512];
        int defined = xsd_wc_nq_token(tok, strlen(tok), turi,
                                      sizeof(turi));
        if (defined) {
            /* the base excludes every globally declared name; the
             * derivation must exclude them too */
            snprintf(msg, cap,
                     "derivation-ok-restriction-4.1: the attribute "
                     "wildcard is not a valid subset of the base "
                     "complex type definition '%s' (the base "
                     "excludes the schema's declared names and the "
                     "derivation does not)",
                     sup_name);
            return 0;
        }
        if (xsd_wc_row_admits_ns(sub, turi[0] ? turi : NULL)) {
            snprintf(msg, cap,
                     "derivation-ok-restriction-4.1: the attribute "
                     "wildcard is not a valid subset of the base "
                     "complex type definition '%s' (a name the "
                     "base excludes stays admitted by the "
                     "derivation)",
                     sup_name);
            return 0;
        }
    }
    return 1;
}

/* The post-capture sweep: surface wc_failed (the parked src-wildcard
 * errors) and run the restriction subset law over the captured
 * rows. Returns 0, or -1 with s->error set. */
static int xsd_check_attr_wildcards(struct leptris_xsd_schema* s) {
    if (s->wc_failed) {
        if (!s->error)
            xsd_set_error(s,
                          "src-wildcard: the wildcard namespace set "
                          "is invalid");
        return -1;
    }
    for (XsdCm* c = s->complex_types; c; c = c->next) {
        if (!c->res_base) continue;
        const char* bns =
            c->res_base_ns ? c->res_base_ns : c->ns;
        XsdCm* base = xsd_find_complex_ns(s, c->res_base, bns);
        if (!base) base = xsd_find_complex_ns(s, c->res_base, c->ns);
        if (!base) continue;
        const XsdTypeAttrs* bt =
            xsd_find_type_attrs_ns(s, base->name,
                                   base->ns ? base->ns : bns);
        if (!bt) bt = xsd_find_type_attrs(s, base->name);
        const XsdTypeAttrs* dt =
            xsd_find_type_attrs_ns(s, c->name, c->ns);
        if (!dt) dt = xsd_find_type_attrs(s, c->name);
        if (!bt || !dt) continue;
        int sub_wc = dt->any_attr_ns || dt->any_attr_not_qname ||
                     dt->any_attr_not_ns;
        int sup_wc = bt->any_attr_ns || bt->any_attr_not_qname ||
                     bt->any_attr_not_ns;
        if (!sub_wc || !sup_wc) continue;
        char msg[512];
        if (!xsd_wc_attr_subset(dt, bt, base->name, msg,
                                sizeof(msg))) {
            xsd_set_error(s, msg);
            return -1;
        }
    }
    return 0;
}


LEPTRIS_API int leptris_xsd_builtin_valid(const char* builtin,
                                          const char* lexical) {
    return xsd_builtin_valid(builtin, lexical);
}

/* The instance validator routes every user simpleType through the
 * chain walker so list/union derivations dispatch with the schema
 * in hand (the bare xsd_simple_valid cannot resolve item/member
 * names). */
int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v) {
    return xsd_valid_chain(s, t, v, 0);
}

LEPTRIS_API int leptris_xsd_simple_valid(LeptrisXsdSchema schema,
                                         const char* type_name,
                                         const char* lexical) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !type_name || !lexical) return 0;
    const XsdSimple* t = xsd_find_simple(s, type_name);
    if (!t) return -1; /* unknown type */
    return xsd_valid_chain(s, t, lexical, 0);
}

/* ---- slice 6: identity-constraint capture ------------------------ */

/* #2156: §3.13.2 {default namespace} — the effective
 * xpathDefaultNamespace for a selector/field host: the host's own
 * attribute, else the nearest ancestor's (the schema level in
 * practice), else the 1.1 default ##local. Returns a malloc'd URI,
 * or NULL for no namespace. */
static char* xsd_ic_default_ns(LeptrisElement host) {
    const char* v = NULL;
    for (LeptrisElement a = host; a && !v;
         a = leptris_element_parent(a))
        v = leptris_element_attribute(a, "xpathDefaultNamespace");
    if (!v) v = "##local";
    if (strcmp(v, "##local") == 0) return NULL;
    if (strcmp(v, "##defaultNamespace") == 0) {
        char u[256];
        xsd_ns_for_prefix(host, NULL, u, sizeof(u));
        return u[0] ? xsd_strdup(u) : NULL;
    }
    if (strcmp(v, "##targetNamespace") == 0) {
        LeptrisElement r = host;
        while (leptris_element_parent(r)) r = leptris_element_parent(r);
        const char* tns =
            leptris_element_attribute(r, "targetNamespace");
        return tns ? xsd_strdup(tns) : NULL;
    }
    return xsd_strdup(v);
}

/* One prefix in the host's scope; xml is predeclared. 0 ok, -1
 * unbound (a selector/field prefix must be bound in the SCHEMA
 * document — XPath Valid, §3.13.6.2). */
static int xsd_ic_prefix_ns(LeptrisElement host, const char* prefix,
                            char* out, size_t cap) {
    if (strcmp(prefix, "xml") == 0) {
        snprintf(out, cap, "%s",
                 "http://www.w3.org/XML/1998/namespace");
        return 0;
    }
    xsd_ns_for_prefix(host, prefix, out, cap);
    return out[0] ? 0 : -1;
}

static int xsd_ic_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int xsd_ic_nc_name_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c >= 0x80;
}

static int xsd_ic_nc_name_char(unsigned char c) {
    return xsd_ic_nc_name_start(c) || (c >= '0' && c <= '9') ||
           c == '.' || c == '-';
}

/* §3.11.6.2 [2] / §3.11.6.3 [7]: Path ::= ('.' '//')? (Step '/')*
 * (Step | '@' NameTest); Step ::= '.' | NameTest; NameTest ::=
 * QName | '*' | NCName ':*'; whitespace allowed between tokens. One
 * '|'-branch per call (the caller splits unions — the subset has no
 * string literals, so a plain split is exact). Returns 0, or -1 for
 * any out-of-subset form (c-selector-xpath / c-fields-xpaths:
 * explicit axes, predicates, absolute paths, mid-path '//', '@' in
 * a non-final position, unions in fields) and for unbound prefixes. */
static int xsd_ic_parse_path(const char* xp, LeptrisElement host,
                             int allow_attr, XsdIcPath* out) {
    memset(out, 0, sizeof(*out));
    if (!xp) return -1;
    const char* p = xp;
    while (xsd_ic_ws(*p)) p++;
    if (p[0] == '.' && p[1] == '/' && p[2] == '/') {
        out->desc = 1;
        p += 3;
    }
    for (;;) {
        while (xsd_ic_ws(*p)) p++;
        if (!*p || *p == '|') break;
        if (out->step_count >= (int)(sizeof(out->steps) /
                                     sizeof(out->steps[0])))
            return -1;
        XsdIcStep* st = &out->steps[out->step_count];
        memset(st, 0, sizeof(*st));
        if (*p == '@') {
            if (!allow_attr) return -1; /* '@' is field-final only */
            st->is_attr = 1;
            p++;
            while (xsd_ic_ws(*p)) p++;
        }
        if (*p == '*') {
            /* NameTest '*' — any element (any namespace), or '@*' */
            st->any_ns = 1;
            p++;
        } else if (*p == '.') {
            char q = p[1];
            if (q && q != ' ' && q != '\t' && q != '\r' && q != '\n' &&
                q != '/' && q != '|')
                return -1;
            st->is_self = 1;
            p++;
        } else if (xsd_ic_nc_name_start((unsigned char)*p)) {
            char name1[256];
            size_t nl = 0;
            while (*p && xsd_ic_nc_name_char((unsigned char)*p) &&
                   nl + 1 < sizeof(name1))
                name1[nl++] = *p++;
            name1[nl] = 0;
            char nsbuf[256];
            if (*p == ':') {
                p++;
                if (*p == '*') {
                    /* NCName ':*' — any local in the prefix's ns */
                    if (xsd_ic_prefix_ns(host, name1, nsbuf,
                                         sizeof(nsbuf)) != 0)
                        return -1;
                    st->ns = xsd_strdup(nsbuf);
                    p++;
                } else {
                    if (!xsd_ic_nc_name_start((unsigned char)*p))
                        return -1;
                    char name2[256];
                    size_t n2 = 0;
                    while (*p && xsd_ic_nc_name_char((unsigned char)*p) &&
                           n2 + 1 < sizeof(name2))
                        name2[n2++] = *p++;
                    name2[n2] = 0;
                    if (xsd_ic_prefix_ns(host, name1, nsbuf,
                                         sizeof(nsbuf)) != 0)
                        return -1;
                    st->ns = xsd_strdup(nsbuf);
                    st->local = xsd_strdup(name2);
                }
            } else if (st->is_attr) {
                /* XPath: an unprefixed attribute test matches an
                 * attribute with NO namespace — the default
                 * namespace never applies to attribute tests. */
                st->local = xsd_strdup(name1);
            } else {
                st->ns = xsd_ic_default_ns(host); /* NULL = none */
                st->local = xsd_strdup(name1);
            }
        } else {
            return -1;
        }
        out->step_count++;
        if (st->is_attr) {
            while (xsd_ic_ws(*p)) p++;
            if (*p) return -1; /* the '@' step is final by grammar */
            break;
        }
        while (xsd_ic_ws(*p)) p++;
        if (*p == '/') {
            p++;
            if (*p == '/') return -1; /* mid-path '//' */
            continue;
        }
        if (!*p || *p == '|') break;
        return -1;
    }
    return out->step_count > 0 ? 0 : -1;
}

static void xsd_ic_path_clear(XsdIcPath* p);

/* Capture one constraint's shape: name/@ref, refer, and the
 * selector/field paths parsed into the §3.11.6 subset with their
 * namespaces resolved against the SCHEMA document (each selector /
 * field element is its own §3.13.2 host for in-scope bindings and
 * xpathDefaultNamespace). */
static void xsd_ic_capture_shape(LeptrisElement e, XsdIc* ic) {
    const char* nm = leptris_element_attribute(e, "name");
    const char* ref = leptris_element_attribute(e, "ref");
    if (nm) {
        ic->name = xsd_strdup(nm);
    } else if (ref) {
        const char* colon = strchr(ref, ':');
        ic->ref_local = xsd_strdup(colon ? colon + 1 : ref);
        ic->ref_pending = 1;
    }
    const char* refer = leptris_element_attribute(e, "refer");
    if (refer) {
        /* node tables key by constraint LOCAL name; the corpus never
         * refers across namespaces */
        const char* colon = strchr(refer, ':');
        ic->refer = xsd_strdup(colon ? colon + 1 : refer);
    }
    for (LeptrisNodeRef fc =
             leptris_node_first_child(leptris_element_as_node(e));
         fc; fc = leptris_node_next_sibling(fc)) {
        if (leptris_node_get_type(fc) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement fe = (LeptrisElement)fc;
        const char* fl = NULL, *fu = NULL;
        leptris_element_expanded_name(fe, &fl, NULL, &fu);
        if (!fl || !fu || strcmp(fu, XSD_NS) != 0) continue;
        const int is_sel = strcmp(fl, "selector") == 0;
        if (!is_sel && strcmp(fl, "field") != 0) continue;
        const char* xp = leptris_element_attribute(fe, "xpath");
        if (!xp) {
            ic->parse_error = 1;
            continue;
        }
        if (is_sel) {
            free(ic->selector);
            ic->selector = xsd_strdup(xp);
            /* Selector ::= Path ( '|' Path )* — split top-level. */
            const char* b = xp;
            while (b && ic->sel_count <
                            (int)(sizeof(ic->sel) / sizeof(ic->sel[0]))) {
                const char* bar = strchr(b, '|');
                size_t bl = bar ? (size_t)(bar - b) : strlen(b);
                char* branch = (char*)malloc(bl + 1);
                if (!branch) break;
                memcpy(branch, b, bl);
                branch[bl] = 0;
                if (xsd_ic_parse_path(branch, fe, 0,
                                      &ic->sel[ic->sel_count]) != 0) {
                    /* a failed branch stays uncounted — its step
                     * strings must be released here or the schema
                     * free walk never sees them */
                    xsd_ic_path_clear(&ic->sel[ic->sel_count]);
                    ic->parse_error = 1;
                } else
                    ic->sel_count++;
                free(branch);
                b = bar ? bar + 1 : NULL;
            }
            if (b) ic->parse_error = 1; /* beyond the branch cap */
        } else if (ic->field_count < 8) {
            ic->fields[ic->field_count] = xsd_strdup(xp);
            if (xsd_ic_parse_path(xp, fe, 1,
                                  &ic->fpaths[ic->field_count]) != 0)
                ic->parse_error = 1;
            ic->field_count++;
        } else {
            ic->parse_error = 1; /* beyond the field cap */
        }
    }
}

/* Release one branch's step strings — a parse FAILURE leaves the
 * branch uncounted (sel_count is not advanced), so the schema free
 * walk would never see its allocations (the leaks(1) catch). */
static void xsd_ic_path_clear(XsdIcPath* p) {
    for (int i = 0; i < p->step_count; i++) {
        free(p->steps[i].ns);
        free(p->steps[i].local);
    }
    memset(p, 0, sizeof(*p));
}

static void xsd_ic_append(XsdIc** head, XsdIc** tail, XsdIc* ic) {
    if (!ic) return;
    if (*tail)
        (*tail)->next = ic;
    else
        *head = ic;
    *tail = ic;
}

/* #2156: local element particles carry their own constraints
 * (id040's chap/appx shapes) — named xs:element particles under this
 * declaration are captured under their own names; the descent stops
 * at each particle (its own recursion covers its subtree). Particles
 * of NAMED types are not reached here. */
static void xsd_capture_ics_descend(struct leptris_xsd_schema* s,
                                    LeptrisElement node) {
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(node));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, NULL, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
        if (strcmp(local, "element") == 0) {
            const char* nm = leptris_element_attribute(e, "name");
            if (nm) xsd_capture_ics(s, e, nm);
            /* ref-form particles carry no constraints and no name */
        } else {
            xsd_capture_ics_descend(s, e);
        }
    }
}

void xsd_capture_ics(struct leptris_xsd_schema* s,
                     LeptrisElement elem, const char* element_name) {
    XsdIc* head = NULL;
    XsdIc* tail = NULL;
    /* ICs are direct children of xs:element, or children of its
     * inline complexType — one hop covers the second spelling. */
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(elem));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, NULL, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
        if (strcmp(local, "key") == 0 || strcmp(local, "unique") == 0 ||
            strcmp(local, "keyref") == 0) {
            XsdIc* ic = (XsdIc*)calloc(1, sizeof(*ic));
            if (!ic) continue;
            ic->kind = strcmp(local, "key") == 0     ? 0
                       : strcmp(local, "unique") == 0 ? 1
                                                      : 2;
            xsd_ic_capture_shape(e, ic);
            xsd_ic_append(&head, &tail, ic);
        } else if (strcmp(local, "complexType") == 0) {
            /* descend into the inline type's constraint children */
            for (LeptrisNodeRef ic_n =
                     leptris_node_first_child(leptris_element_as_node(e));
                 ic_n; ic_n = leptris_node_next_sibling(ic_n)) {
                if (leptris_node_get_type(ic_n) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement ie = (LeptrisElement)ic_n;
                const char* il = NULL, *iu = NULL;
                leptris_element_expanded_name(ie, &il, NULL, &iu);
                if (!il || !iu || strcmp(iu, XSD_NS) != 0) continue;
                if (strcmp(il, "key") != 0 && strcmp(il, "unique") != 0 &&
                    strcmp(il, "keyref") != 0)
                    continue;
                XsdIc* ic = (XsdIc*)calloc(1, sizeof(*ic));
                if (!ic) continue;
                ic->kind = strcmp(il, "key") == 0     ? 0
                           : strcmp(il, "unique") == 0 ? 1
                                                       : 2;
                xsd_ic_capture_shape(ie, ic);
                xsd_ic_append(&head, &tail, ic);
            }
        }
    }
    if (head) {
        XsdElementIcs* slot = (XsdElementIcs*)calloc(1, sizeof(*slot));
        if (slot) {
            slot->element_name = xsd_strdup(element_name);
            slot->constraints = head;
            slot->next = s->element_ics;
            s->element_ics = slot;
        }
    }
    xsd_capture_ics_descend(s, elem);
}

const XsdElementIcs* xsd_find_element_ics(struct leptris_xsd_schema* s,
                                          const char* element_name) {
    if (!s || !element_name) return NULL;
    for (XsdElementIcs* t = s->element_ics; t; t = t->next)
        if (strcmp(t->element_name, element_name) == 0) return t;
    return NULL;
}

/* Deep-copy one normalized path branch (the @ref form inherits its
 * target's shape). */
static void xsd_ic_path_copy(XsdIcPath* d, const XsdIcPath* src) {
    *d = *src;
    for (int i = 0; i < src->step_count; i++) {
        d->steps[i].ns =
            src->steps[i].ns ? xsd_strdup(src->steps[i].ns) : NULL;
        d->steps[i].local = src->steps[i].local
                                ? xsd_strdup(src->steps[i].local)
                                : NULL;
    }
}

static XsdIc* xsd_ic_find_named(struct leptris_xsd_schema* s,
                                const char* name) {
    for (XsdElementIcs* t = s->element_ics; t; t = t->next)
        for (XsdIc* ic = t->constraints; ic; ic = ic->next)
            if (!ic->ref_pending && ic->name &&
                strcmp(ic->name, name) == 0)
                return ic;
    return NULL;
}

/* #2156: post-capture pass — every constraint is either in shape or
 * the SCHEMA is in error:
 *   - a selector/field outside the §3.11.6 subset or with an unbound
 *     schema prefix (c-selector-xpath / c-fields-xpaths via XPath
 *     Valid);
 *   - an @ref naming no constraint (src-identity-constraint);
 *   - an @ref whose target carries a different category
 *     (src-identity-constraint cl. 5 — id041.bad).
 * The @ref form copies its target's resolved shape (§3.11.2: the
 * component IS the referenced constraint). Returns -1 on schema
 * error. */
static int xsd_resolve_ic_refs(struct leptris_xsd_schema* s) {
    for (XsdElementIcs* t = s->element_ics; t; t = t->next) {
        for (XsdIc* ic = t->constraints; ic; ic = ic->next) {
            if (ic->parse_error) {
                xsd_set_error(s,
                              "identity-constraint selector/field is "
                              "outside the XSD 1.1 XPath subset "
                              "(c-selector-xpath/c-fields-xpaths)");
                return -1;
            }
            if (!ic->ref_pending) continue;
            XsdIc* target =
                ic->ref_local ? xsd_ic_find_named(s, ic->ref_local)
                              : NULL;
            if (!target) {
                xsd_set_error(s,
                              "identity-constraint @ref does not "
                              "resolve to a named constraint "
                              "(src-identity-constraint)");
                return -1;
            }
            if (target->kind != ic->kind) {
                xsd_set_error(s,
                              "identity-constraint @ref resolves to "
                              "a constraint of a different category "
                              "(src-identity-constraint)");
                return -1;
            }
            free(ic->name);
            ic->name = xsd_strdup(target->name);
            free(ic->selector);
            ic->selector = xsd_strdup(target->selector);
            for (size_t f = 0; f < target->field_count && f < 8; f++) {
                free(ic->fields[f]);
                ic->fields[f] = xsd_strdup(target->fields[f]);
                xsd_ic_path_copy(&ic->fpaths[f], &target->fpaths[f]);
            }
            ic->field_count = target->field_count;
            ic->sel_count = target->sel_count;
            for (int b = 0; b < target->sel_count; b++)
                xsd_ic_path_copy(&ic->sel[b], &target->sel[b]);
            free(ic->refer);
            ic->refer = target->refer ? xsd_strdup(target->refer) : NULL;
            ic->ref_pending = 0;
        }
    }
    return 0;
}

/* ---- validator accessors (validate.c) --------------------------- */

const XsdElementDecl* xsd_find_element(struct leptris_xsd_schema* s,
                                       const char* name) {
    if (!s || !name) return NULL;
    for (XsdElementDecl* d = s->elements; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

/* leptris-ruby#679: the INSTANCE-driven declaration lookup (the root
 * walk and the inheritable-attribute owner lookup). The binding is
 * EXACT on (namespaceURI, localName): a stamped declaration
 * ({target namespace} = the declaring unit's targetNamespace) names
 * an element IN that namespace; an unstamped declaration names a
 * NO-namespace element (XSD 1.0 §3.3.4 — global declarations take
 * the schema's targetNamespace, a no-targetNamespace unit's stay
 * namespace-free). The old #440 ladder's unstamped and name-only
 * sweeps bound foreign-namespace roots — ns0_2 accepted a no-ns
 * root with the tns global, po0/ns1_0 accepted a foreign-ns root —
 * no fallback past a namespace mismatch. */
const XsdElementDecl* xsd_find_element_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri) {
    if (!s || !name) return NULL;
    int child_ns = uri && uri[0];
    for (XsdElementDecl* t = s->elements; t; t = t->next)
        if (strcmp(t->name, name) == 0 &&
            (t->ns ? (child_ns && strcmp(t->ns, uri) == 0)
                   : !child_ns))
            return t;
    return NULL;
}

/* leptris#1958: the CONTENT-MODEL child loop's lookup — a
 * no-namespace instance child binds its own no-targetNamespace
 * declaration FIRST (the importing namespace's same-name global
 * sits ahead in the merged list and must not shadow it). Root and
 * global lookups keep the stock ladder. */
const XsdElementDecl* xsd_find_element_child_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri) {
    if (!s || !name) return NULL;
    if (uri && uri[0])
        return xsd_ladder_element(s, name, uri, 0);
    for (XsdElementDecl* t = s->elements; t; t = t->next)
        if (!t->ns && strcmp(t->name, name) == 0) return t;
    /* leptris#1976: NO ns-blind fallback for a no-ns child. A
     * no-namespace element resolves only against true no-ns
     * declarations — matching a NAMESPACED global of the same name
     * typed whole subtrees with a foreign type and manufactured
     * false lexical rows (the schG2 roundtrip shape: a lone ns-a
     * unit answering the instance's no-ns <e1> with ns-a's ct-A). */
    return NULL;
}

XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,
                        const char* type_name) {
    if (!s || !type_name) return NULL;
    for (XsdCm* c = s->complex_types; c; c = c->next)
        if (strcmp(c->name, type_name) == 0) return c;
    return NULL;
}

/* leptris-ruby#440: prefer the (ns, name) exact match — the flat
 * merged list carries same-name types from every imported
 * namespace. Falls back to name-only (single-namespace schemas
 * and unresolvable contexts keep working). */
XsdCm* xsd_find_complex_ns(struct leptris_xsd_schema* s,
                           const char* type_name, const char* uri) {
    /* STRICT: derivation is same-namespace by XSD rules. */
    return xsd_ladder_complex(s, type_name, uri, 0);
}

/* leptris#1958: the ns-FREE reference — a no-targetNamespace
 * declaration's unprefixed type binds ITS OWN unit's (unstamped)
 * component first; the importing namespace's same-name type (which
 * sits ahead in the merged list) must not shadow it. Used only by
 * the validator's decl-driven fallback (hint == NULL, instance
 * namespace absent) — every other lookup keeps the stock ladder. */
XsdCm* xsd_find_complex_ns_free_first(struct leptris_xsd_schema* s,
                                      const char* type_name) {
    if (!s || !type_name) return NULL;
    for (XsdCm* t = s->complex_types; t; t = t->next)
        if (!t->ns && strcmp(t->name, type_name) == 0) return t;
    for (XsdCm* t = s->complex_types; t; t = t->next)
        if (strcmp(t->name, type_name) == 0) return t;
    return NULL;
}

/* leptris-ruby#440 (follow-up): a TYPE REFERENCE on an instance
 * element resolves against the namespace the REFERENCE declares —
 * OOXML's dml family types every wp:/pic: particle with
 * a:-prefixed main-namespace types (wp:extent -> a:CT_PositiveSize2D).
 * The capture strips prefixes (xsd_local_name), so the element's
 * own namespace is only the FIRST guess: try (element ns, name),
 * then unstamped entries, then the name-only spelling across the
 * merged table. The priority order keeps the #1665 guarantee where
 * it matters — when the element's own namespace declares the type,
 * a foreign same-name decl cannot win. Compile-side lookups
 * (extension bases, group refs) stay STRICT: derivation is
 * same-namespace by XSD rules. */
XsdCm* xsd_find_complex_ref(struct leptris_xsd_schema* s,
                            const char* type_name, const char* uri) {
    /* LADDER: instance-side type references cross namespaces
     * (wp:extent -> a:CT_PositiveSize2D — #1687). */
    return xsd_ladder_complex(s, type_name, uri, 1);
}

/* validate.c: instance-side QName resolution (xsi:type, XSD 1.0
 * §2.6.2/§3.3.4) — the same in-scope prefix ladder the capture
 * helpers use, exported so instance validation and schema capture
 * cannot drift. */
void xsd_resolve_qname_inst(LeptrisElement e, const char* qname,
                            char* uri, size_t urisz, char* local,
                            size_t localsz) {
    xsd_resolve_qname(e, qname, uri, urisz, local, localsz);
}

const XsdSimple* xsd_find_simple_pub(struct leptris_xsd_schema* s,
                                     const char* name) {
    return xsd_find_simple(s, name);
}

const char* xsd_target_ns(struct leptris_xsd_schema* s) {
    return s ? s->target_ns : NULL;
}

/* #2183: the schema document's directory (file compile) — the
 * assertion lane anchors fn:doc's relative-URI resolution here. */
const char* xsd_schema_base_dir(struct leptris_xsd_schema* s) {
    return s ? s->base_dir : NULL;
}

const XsdTypeAttrs* xsd_find_type_attrs(struct leptris_xsd_schema* s,
                                        const char* type_name) {
    if (!s || !type_name) return NULL;
    for (XsdTypeAttrs* t = s->type_attrs; t; t = t->next)
        if (strcmp(t->type_name, type_name) == 0) return t;
    return NULL;
}

const XsdTypeAttrs* xsd_find_type_attrs_ns(
    struct leptris_xsd_schema* s, const char* type_name,
    const char* uri) {
    return xsd_ladder_type_attrs(s, type_name, uri, 0);
}

/* ---- slice 4: instance validation (validate.c engine) ------------ */

LEPTRIS_API int leptris_xsd_validate(LeptrisXsdSchema schema,
                                     LeptrisDocument doc) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !doc) return -1;
    struct xsd_validator* v = xsd_validator_new(s);
    if (!v) return -1;
    int r = xsd_validator_run(v, doc);
    /* park the run's errors on the schema for the accessors; a new
     * validate run replaces them. */
    struct xsd_validator* prev = (struct xsd_validator*)s->validator;
    if (prev) xsd_validator_free(prev);
    s->validator = v;
    return r;
}

LEPTRIS_API const char* leptris_xsd_version(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return NULL;
    return s->version_11 ? "1.1" : "1.0";
}

int xsd_element_name_defined(struct leptris_xsd_schema* s,
                             const char* local, const char* uri) {
    if (!s || !local) return 0;
    for (const XsdElementDecl* d = s->elements; d; d = d->next) {
        if (strcmp(d->name, local) == 0 &&
            (uri ? (d->ns && strcmp(uri, d->ns) == 0)
                 : (d->ns == NULL || !d->ns[0])))
            return 1;
    }
    return 0;
}

int xsd_attr_name_defined(struct leptris_xsd_schema* s,
                          const char* local) {
    if (!s || !local) return 0;
    for (const XsdAttrDecl* d = s->top_attrs; d; d = d->next) {
        if (strcmp(d->name, local) == 0) return 1;
    }
    return 0;
}

LEPTRIS_API size_t leptris_xsd_error_count(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return 0;
    return xsd_validator_error_count(
        (struct xsd_validator*)s->validator);
}

LEPTRIS_API const char* leptris_xsd_error_at(LeptrisXsdSchema schema,
                                             size_t i) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return NULL;
    return xsd_validator_error_at((struct xsd_validator*)s->validator,
                                  i);
}

LEPTRIS_API size_t leptris_xsd_error_report(
    LeptrisXsdSchema schema, const LeptrisXsdErrorRecord** out) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !out) return 0;
    free(s->report);
    s->report = NULL;
    s->report_count = 0;
    struct xsd_validator* v = (struct xsd_validator*)s->validator;
    size_t n = xsd_validator_error_count(v);
    if (n == 0) {
        *out = NULL;
        return 0;
    }
    LeptrisXsdErrorRecord* rows =
        (LeptrisXsdErrorRecord*)calloc(n, sizeof(*rows));
    if (!rows) {
        *out = NULL;
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        /* walk the list in order — the accessors index the same way */
        const struct xsd_error* e =
            xsd_validator_error_entry(v, i);
        if (!e) break;
        rows[i].kind = e->kind;
        rows[i].component = e->component;
        rows[i].line = (unsigned)(e->line > 0 ? e->line : 0);
        rows[i].column = (unsigned)(e->column > 0 ? e->column : 0);
        rows[i].message = e->text;
    }
    s->report = rows;
    s->report_count = n;
    *out = rows;
    return n;
}

LEPTRIS_API int leptris_xsd_content_valid(
    LeptrisXsdSchema schema, const char* element_name,
    const char* const* child_names, const char* const* child_ns,
    size_t child_count) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !element_name) return -1;

    const XsdElementDecl* d = s->elements;
    while (d && strcmp(d->name, element_name) != 0) d = d->next;
    if (!d) return -1; /* unknown element */

    if (!d->type) return 1; /* no type attr: anyType accepts all */

    /* built-in or simpleType ref: element content is text-only —
     * any element child fails */
    if (strncmp(d->type, "xs:", 3) == 0 ||
        xsd_find_simple(s, d->type))
        return child_count == 0;

    XsdCm* ct = s->complex_types;
    while (ct && strcmp(ct->name, d->type) != 0) ct = ct->next;
    if (!ct) return child_count == 0; /* unknown type: accept-empty */

    XsdCm* model = ct->first_child;
    if (!model) return child_count == 0;

    int ok = xsd_content_valid(s, model, s->target_ns, child_names,
                               child_ns, child_count, NULL);
    /* #1894: -2 = the model crosses the automaton size cap — surface
     * the loud structured row and keep the public contract's -1 for
     * "not evaluable". */
    if (ok == -2) {
        xsd_set_error(s, "content model exceeds the automaton size "
                         "cap (pathological occurrence product)");
        return -1;
    }
    return ok;
}

LEPTRIS_API size_t leptris_xsd_declaration_count(LeptrisXsdSchema schema) {
    if (!schema) return 0;
    return ((struct leptris_xsd_schema*)schema)->declaration_count;
}

LEPTRIS_API const char* leptris_xsd_error(LeptrisXsdSchema schema) {
    if (!schema) return NULL;
    return ((struct leptris_xsd_schema*)schema)->error;
}

/* leptris#1881: schema-set compilation — every listed document's
 * declarations merge into ONE schema. The per-document pipeline is
 * the single-compile one (checks, per-unit tns, capture); the
 * visited-set is seeded with every member path first so an import
 * chain back to a set member does not recapture it, and extension
 * bases resolve ONCE after all units are in (type references
 * resolve against the whole schema, the leptris-ruby#440 law). */
LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile_set(
    const char* const* paths, size_t count, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!paths || count == 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    struct leptris_xsd_schema* s =
        (struct leptris_xsd_schema*)calloc(1, sizeof(*s));
    if (!s) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    const char* main_tns = NULL;
    for (size_t i = 0; i < count; i++) {
        const char* path = paths[i];
        if (!path || !*path) {
            xsd_set_error(s, "schema set member path is empty");
            if (status) *status = LEPTRIS_ERROR_PARSE;
            return s;
        }
        size_t len = 0;
        char* text = xsd_read_file(path, &len);
        if (!text) {
            xsd_set_error(s, "schema set member cannot be read");
            if (status) *status = LEPTRIS_ERROR_PARSE;
            return s;
        }
        const char* slash = strrchr(path, '/');
        const char* bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        char* base_dir = NULL;
        if (slash) {
            size_t n = (size_t)(slash - path);
            if (n == 0) n = 1;
            base_dir = (char*)malloc(n + 1);
            if (base_dir) {
                memcpy(base_dir, path, n);
                base_dir[n] = 0;
            }
        } else {
            base_dir = xsd_strdup(".");
        }

        LeptrisDocument doc =
            leptris_parse_string(text, len, status);
        free(text);
        if (!doc) {
            xsd_set_error(s,
                          "schema set member is not well-formed XML");
            free(base_dir);
            if (status) *status = LEPTRIS_ERROR_PARSE;
            return s;
        }
        LeptrisElement root = leptris_document_root(doc);
        if (!root) {
            xsd_set_error(s, "schema set member has no root element");
            leptris_document_free(doc);
            free(base_dir);
            if (status) *status = LEPTRIS_ERROR_PARSE;
            return s;
        }
        const char* local = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name(root, &local, NULL, &uri);
        if (!local || strcmp(local, "schema") != 0 ||
            (uri && strcmp(uri, XSD_NS) != 0)) {
            xsd_set_error(s, "schema set member root is not xs:schema");
            leptris_document_free(doc);
            free(base_dir);
            if (status) *status = LEPTRIS_ERROR_PARSE;
            return s;
        }
        const char* doc_tns =
            leptris_element_attribute(root, "targetNamespace");
        char* saved = s->base_dir;
        s->base_dir = base_dir;
        if (i == 0) {
            main_tns = doc_tns;
            s->target_ns = xsd_strdup(doc_tns);
        }
        /* Per-unit tns during checks + capture; the visited-set
         * seeds each member so import chains close (the import in
         * g.xsd would otherwise re-capture f.xsd mid-set). */
        {
            char* saved_ns = s->target_ns;
            s->target_ns = xsd_strdup(doc_tns);
            char canon[4096];
            xsd_path_normalize(path, canon, sizeof(canon));
            /* An earlier member's import may have captured this
             * document already (order [importer, imported]) — the
             * visited check keeps each unit to one capture. */
            if (xsd_visited_has(s, canon)) {
                free(s->target_ns);
                s->target_ns = saved_ns;
                leptris_document_free(doc);
                free(base_dir);
                s->base_dir = saved;
                continue;
            }
            xsd_visited_add(s, canon);
            int bad = xsd_check_structure(s, root, 0) != 0 ||
                      xsd_check_derivation(s, root) != 0 ||
                      xsd_check_refs(s, root) != 0 ||
                      xsd_check_upa(s, root) != 0 ||
                      xsd_check_include_tns(s, root) != 0;
            if (!bad) xsd_capture_root(s, root);
            free(s->target_ns);
            s->target_ns = saved_ns;
            if (bad) {
                leptris_document_free(doc);
                free(base_dir);
                if (status) *status = LEPTRIS_ERROR_PARSE;
                return s;
            }
        }
        leptris_document_free(doc);
        free(base_dir);
        s->base_dir = saved;
    }
    xsd_resolve_extension_bases(s);
    xsd_resolve_pending_attr_refs(s);
    /* #2156: same post-capture constraint resolution for the
     * include/import chase path. */
    if (xsd_resolve_ic_refs(s) != 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return s;
    }
    return s;
}
