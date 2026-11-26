/* xsd/validate.c — #1075 slice 4: instance-document validation.
 *
 * Walks an instance tree against the compiled schema: element
 * declarations resolve by local name (global declarations), their
 * types drive attribute checking (declared rows, required use),
 * text-content lexical checks (builtins + user simpleTypes), and
 * child-sequence content-model checks (slice 3's NFA). Failures
 * accumulate in the schema-owned error list — the RNG accessor
 * pattern: validate returns 1/0, errors enumerate every miss.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "xsd_internal.h"
#include "../dom/element.h"

extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count,
                             int* oc_from);
extern int xsd_builtin_valid(const char* type, const char* v);
extern int xsd_builtin_known(const char* type);
extern int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                                  const XsdSimple* t, const char* v);



struct leptris_xsd_schema; /* opaque here; compile.c owns the struct */

/* The validator reaches the compiled model through accessors the
 * compiler exports (compile.c defines the struct). */
extern const XsdElementDecl* xsd_find_element_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri);
extern const XsdElementDecl* xsd_find_element(
    struct leptris_xsd_schema* s, const char* name);
extern const XsdElementDecl* xsd_find_element_child_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri);
extern XsdCm* xsd_find_complex_ns(struct leptris_xsd_schema* s,
                              const char* type_name, const char* uri);
extern XsdCm* xsd_find_complex_ref(struct leptris_xsd_schema* s,
                              const char* type_name, const char* uri);
extern XsdCm* xsd_find_complex_ns_free_first(struct leptris_xsd_schema* s,
                                             const char* type_name);
XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,                               const char* type_name);
extern const XsdSimple* xsd_find_simple_ns_pub(
    struct leptris_xsd_schema* s, const char* name, const char* uri);
extern const XsdSimple* xsd_find_simple_pub(
    struct leptris_xsd_schema* s, const char* name);
extern const char* xsd_target_ns(struct leptris_xsd_schema* s);
extern const XsdTypeAttrs* xsd_find_type_attrs_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri);
extern const XsdTypeAttrs* xsd_find_type_attrs(
    struct leptris_xsd_schema* s, const char* type_name);
extern const XsdElementIcs* xsd_find_element_ics(
    struct leptris_xsd_schema* s, const char* element_name);
static int any_attr_matches(const char* grammar, const char* uri,
                            const char* target_ns);
extern void xsd_resolve_qname_inst(LeptrisElement e, const char* qname,
                                   char* uri, size_t urisz, char* local,
                                   size_t localsz);

/* ---- error list ---------------------------------------------------- */

struct xsd_validator {
    struct leptris_xsd_schema* s;
    LeptrisDocument doc; /* the instance (assertion evaluation) */
    struct xsd_error* errors;
    struct xsd_error* tail;
    size_t count;
    /* #2156: elements attributed to a processContents="skip"
     * wildcard (§3.10.4.1) — their subtrees are invisible to
     * identity constraints (§3.11.4 cl. 1 omits skipped nodes from
     * the target node set). Borrowed DOM pointers, freed with the
     * document. */
    struct ic_skipped* skipped;
    /* moxml#579: the attribute names referenced by the test of the
     * CTA alternative in force for the element being assessed
     * (§3.12.4) — those names pass the strict attribute-declaration
     * check undeclared. Borrowed pointers into the schema's
     * alternatives; saved/restored around each dispatch. The row
     * type is char** (the alternative owns the names) — the const
     * view triggers -Wincompatible-pointer-types as an error on
     * GCC. */
    char** cta_exempt;
    int cta_exempt_n;
};

struct ic_skipped {
    LeptrisElement e;
    struct ic_skipped* next;
};

/* §3.10.4.1: an element is skipped if it is attributed to a skip
 * wildcard OR any of its ancestors is. */
static int ic_is_skipped(const struct xsd_validator* v, LeptrisElement e) {
    for (const struct ic_skipped* sk = v ? v->skipped : NULL; sk;
         sk = sk->next)
        for (LeptrisElement a = e; a; a = leptris_element_parent(a))
            if (a == sk->e) return 1;
    return 0;
}

static void verr_push_full(struct xsd_validator* v, const char* kind,
                           const char* component, int line, int column,
                           const char* msg) {
    struct xsd_error* e = (struct xsd_error*)malloc(sizeof(*e));
    if (!e) return;
    size_t n = strlen(msg) + 1;
    e->text = (char*)malloc(n);
    if (!e->text) {
        free(e);
        return;
    }
    memcpy(e->text, msg, n);
    e->kind = kind;
    e->component = component;
    e->line = line;
    e->column = column;
    e->next = NULL;
    if (v->tail)
        v->tail->next = e;
    else
        v->errors = e;
    v->tail = e;
    v->count++;
}

static void verrfk(struct xsd_validator* v, const char* kind,
                   const char* what, const char* name,
                   const char* detail) {
    /* "element 'x': detail" — bounded composition */
    char buf[256];
    int w = snprintf(buf, sizeof(buf), "%s '%s': %s", what,
                     name ? name : "?", detail ? detail : "invalid");
    (void)w;
    verr_push_full(v, kind, name, 0, 0, buf);
}

/* leptris-ruby#419: instance-side errors carry the owning element
 * and its source position (libxml2/Nokogiri shape). The element's
 * own name rides the prefix; the position comes from the recorded
 * source stamp (0 = unlocated — a skipped-positions parse). */
static void verrf_atk(struct xsd_validator* v, LeptrisElement elem,
                      const char* kind, const char* component,
                      const char* what, const char* name,
                      const char* detail) {
    char buf[320];
    const char* el = NULL;
    leptris_element_expanded_name(elem, &el, NULL, NULL);
    if (!el) el = "?";
    int line = 0, col = 0;
    {
        LeptrisSourcePosition pos;
        leptris_node_source_position((LeptrisNodeRef)elem, &pos);
        line = pos.line;
        col = pos.col_start;
    }
    int w;
    if (line > 0)
        w = snprintf(buf, sizeof(buf),
                     "element '%s' @%d:%d: %s '%s': %s", el, line,
                     col, what, name ? name : "?",
                     detail ? detail : "invalid");
    else
        w = snprintf(buf, sizeof(buf), "element '%s': %s '%s': %s",
                     el, what, name ? name : "?",
                     detail ? detail : "invalid");
    (void)w;
    verr_push_full(v, kind, component, line, col, buf);
}

/* ---- lexical helpers ---------------------------------------------- */

/* #2154: the xs:assertion facet pass — defined below the datatype
 * helpers (it rides numeric_builtin). */
static int assertions_hold(struct xsd_validator* v,
                           struct leptris_xsd_schema* s, const XsdSimple* st,
                           const char* lexical, LeptrisElement elem);
static int assertion_chain_is_list(struct leptris_xsd_schema* s,
                                   const XsdSimple* t);
static int assertion_bind_list(struct leptris_xsd_schema* s,
                               const XsdSimple* st, const char* lexical,
                               char* out, size_t cap);

/* The token family — every type derived from xs:token (Datatypes
 * §4.3.6 facet whitespace=collapse): validation compares the
 * COLLAPSED value, so element content " xy " reaches the lexical
 * and facet gates as "xy" (the import0_0 corpus row). */
static int token_family_builtin(const char* t) {
    return strcmp(t, "xs:token") == 0 || strcmp(t, "xs:NMTOKEN") == 0 ||
           strcmp(t, "xs:language") == 0 || strcmp(t, "xs:Name") == 0 ||
           strcmp(t, "xs:NCName") == 0 || strcmp(t, "xs:ID") == 0 ||
           strcmp(t, "xs:IDREF") == 0 || strcmp(t, "xs:ENTITY") == 0 ||
           strcmp(t, "xs:IDREFS") == 0 ||
           strcmp(t, "xs:NMTOKENS") == 0 ||
           strcmp(t, "xs:ENTITIES") == 0;
}

/* XSD §4.3.6: whiteSpace=collapse is FIXED on every builtin
 * EXCEPT string (preserve) and normalizedString (replace) — the
 * numerics, the temporal family, boolean, the binaries, QName all
 * collapse (leptris-rs#29, elemZ002). */
static int builtin_collapses(const char* t) {
    return strcmp(t, "xs:string") != 0 &&
           strcmp(t, "xs:normalizedString") != 0;
}
static int type_collapses_ws(struct leptris_xsd_schema* s,
                             const char* type, const char* ns) {
    if (!type) return 0;
    if (strncmp(type, "xs:", 3) == 0) return builtin_collapses(type);
    const XsdSimple* st = xsd_find_simple_ns_pub(s, type, ns);
    int depth = 0;
    while (st && depth++ < 32) {
        if (!st->base) return 0;
        if (strncmp(st->base, "xs:", 3) == 0)
            return builtin_collapses(st->base);
        st = xsd_find_simple_pub(s, st->base);
    }
    return 0;
}

static int text_valid(struct xsd_validator* v,
                      struct leptris_xsd_schema* s, const char* type,
                      const char* lexical, const char* ns,
                      const char* what, const char* name,
                      LeptrisElement elem) {
    if (!type) return 1;
    int r;
    const XsdSimple* st = NULL;
    /* token-family collapse: validate the collapsed copy */
    char* collapsed = NULL;
    const char* value = lexical;
    if (type_collapses_ws(s, type, ns) && lexical) {
        collapsed = strdup(lexical);
        if (collapsed) {
            char* out = collapsed;
            const char* p = lexical;
            while (*p && isspace((unsigned char)*p)) p++;
            int spaced = 0;
            while (*p) {
                if (isspace((unsigned char)*p)) {
                    spaced = 1;
                    p++;
                    continue;
                }
                if (spaced) { *out++ = ' '; spaced = 0; }
                *out++ = *p++;
            }
            *out = 0;
            value = collapsed;
        }
    }
    if (strncmp(type, "xs:", 3) == 0)
        r = xsd_builtin_valid(type, value);
    else {
        st = xsd_find_simple_ns_pub(s, type, ns);
        if (!st) { free(collapsed); return 1; /* unresolved: accept */ }
        r = xsd_simple_valid_chain(s, st, value);
    }
    if (r == 0) {
        /* collapsed stays alive: value may alias it */
        if (elem)
            verrf_atk(v, elem,
                      strcmp(what, "attribute") == 0
                          ? "attribute-use"
                          : "simpleType-facet",
                      name, what, name,
                      "lexical value does not match its type");
        else
            verrfk(v,
                   strcmp(what, "attribute") == 0
                       ? "attribute-use"
                       : "simpleType-facet",
                   what, name,
                   "lexical value does not match its type");
        free(collapsed);
        return 0;
    }
    /* XSD 1.1 xs:assertion facets (#2154): every facet list across
     * the restriction ladder must hold (§3.16.6 accumulation), with
     * the list-item and union-member chains layered on (§3.16.4).
     * $value binds the typed value — the engine cannot model the
     * facet's ABSENT focus (§3.16.4: context item, position and last
     * undefined), so the context is the owning element; the rows
     * hinging on that gap are dispositioned in the README. */
    if (st && elem && v->doc &&
        !assertions_hold(v, s, st, value, elem)) {
        verrf_atk(v, elem, "assertion", name, what, name,
                  "assertion facet failed");
        free(collapsed);
        return 0;
    }
    free(collapsed);
    return 1;
}

/* One element: attributes, text, children. */
/* One type's xs:assert list — context is the element; §3.13.4.1
 * 2.3: $value binds the content's value for simple-content types
 * (the lexical, numerics as bare literals), the empty sequence
 * otherwise — test4.xsd's selected simpleContent type asserts
 * $value mod 2 = 0 (#2149). #2154 posture: an expression the engine
 * cannot COMPILE or EVALUATE is not enforced — §3.13.4 turns
 * dynamic errors into false verdicts, but the engine's unsupported
 * corners (PSVI-typed instance-of, absent-focus position()) would
 * then reject suite-VALID documents; the evaluation-error rows are
 * dispositioned in the README. A compiled query that evaluates
 * FALSE still fails the element. Returns the number of FAILED
 * asserts. */
static int xsd_eval_assert_list(struct xsd_validator* v,
                                struct leptris_xsd_schema* s,
                                const XsdCm* ct, LeptrisElement elem,
                                const char* local, const char* fill) {
    int failed = 0;
    for (XsdAssert* a = ct->asserts; a; a = a->next) {
        if (!a->compiled || !v->doc) continue;
        int evaluated = 0;
        int ebv = 0;
        if (ct->text_type) {
            const char* text = leptris_element_text(elem);
            if (!text) text = "";
            if (fill && !*text) text = fill;
            char sel[1024];
            /* §3.13.4.1 2.3: a LIST-typed content binds $value as
             * the itemized typed sequence (listunion6's
             * count($value) le 5) */
            const XsdSimple* tst = xsd_find_simple_ns_pub(
                s, ct->text_type, ct->ns);
            if (tst && assertion_chain_is_list(s, tst) &&
                assertion_bind_list(s, tst, text, sel, sizeof(sel))) {
                /* sel holds the itemized binding */
            } else
            /* numeric ONLY when the whole lexical parses as a
             * number — the strspn("+-.eE") shape check spelled
             * every ISO date a numeric (2008-07-01 evaluated as
             * 2008 minus 07 minus 01) */
            {
            int numeric = 0;
            if (*text) {
                char* end = NULL;
                strtod(text, &end);
                numeric = (end && end != text && *end == 0);
            }
            if (numeric) {
                snprintf(sel, sizeof(sel), "%s", text);
            } else {
                size_t w = 0;
                sel[w++] = '\'';
                for (const char* p = text;
                     *p && w < sizeof(sel) - 3; p++) {
                    if (*p == '\'') sel[w++] = '\'';
                    sel[w++] = *p;
                }
                sel[w++] = '\'';
                sel[w] = 0;
            }
            }
            const char* names[1] = {"value"};
            const char* sels[1] = {sel};
            LeptrisXPathResult r = leptris_xquery_eval_params_ex(
                a->compiled, v->doc, elem, names, sels, 1,
                (unsigned)LEPTRIS_XQUERY_ASSERT_DATA_MODEL);
            if (r) {
                ebv = leptris_xpath_result_boolean(r);
                evaluated = 1;
                leptris_xpath_result_free(r);
            }
        } else {
            const char* names[1] = {"value"};
            const char* sels[1] = {"()"};
            LeptrisXPathResult r = leptris_xquery_eval_params_ex(
                a->compiled, v->doc, elem, names, sels, 1,
                (unsigned)LEPTRIS_XQUERY_ASSERT_DATA_MODEL);
            if (r) {
                ebv = leptris_xpath_result_boolean(r);
                evaluated = 1;
                leptris_xpath_result_free(r);
            }
        }
        if (evaluated && !ebv) {
            failed++;
            verrfk(v, "assertion", "assertion", local,
                   a->test ? a->test : "(invalid)");
        }
    }
    return failed;
}

static void validate_element(struct xsd_validator* v,
                             struct leptris_xsd_schema* s,
                             LeptrisElement elem, int depth);
static void xsd_member_canon(const char* mname, char* out, size_t cap) {
    const char* local = mname ? strchr(mname, ':') : NULL;
    local = local ? local + 1 : mname;
    snprintf(out, cap, "xs:%s", local ? local : "");
}

/* Value-space equality within one union member: numerics compare
 * numerically ("1.0" == "1.000"), booleans identify across the
 * {true,1}/{false,0} spellings, everything else is exact text. */
static int xsd_value_equal(const char* canon_type, const char* a,
                           const char* b);
static int xsd_collapse_str(const char* src, char* out, size_t cap);

static int xsd_value_equal(const char* canon_type, const char* a,
                           const char* b) {
    if (strcmp(a, b) == 0) return 1;
    if (strcmp(canon_type, "xs:boolean") == 0) {
        int av = strcmp(a, "true") == 0 || strcmp(a, "1") == 0;
        int bv = strcmp(b, "true") == 0 || strcmp(b, "1") == 0;
        return av == bv;
    }
    if (strcmp(canon_type, "xs:float") == 0 ||
        strcmp(canon_type, "xs:double") == 0)
        return strtod(a, NULL) == strtod(b, NULL);
    if (strstr(canon_type, "int") || strstr(canon_type, "decimal") ||
        strstr(canon_type, "long") || strstr(canon_type, "short") ||
        strstr(canon_type, "byte") ||
        strstr(canon_type, "Negative") || strstr(canon_type, "positive"))
        return strtod(a, NULL) == strtod(b, NULL);
    /* leptris-rs#74 (stE065/066): whitespace-variant families — the
     * token family collapses and normalizedString replaces — equate
     * trailing-blank spellings in the union member's value space
     * (the suite's oracle accepts the collapse reading for the
     * union fixed-comparison class). */
    if (token_family_builtin(canon_type) ||
        strcmp(canon_type, "xs:normalizedString") == 0) {
        char na[512], nb[512];
        if (strlen(a) < sizeof(na) && strlen(b) < sizeof(nb) &&
            xsd_collapse_str(a, na, sizeof(na)) &&
            xsd_collapse_str(b, nb, sizeof(nb)) &&
            strcmp(na, nb) == 0)
            return 1;
    }
    return 0;
}

/* leptris-rs#45 (stE050/stE055): union-typed fixed constraints
 * compare in the VALUE SPACE of the first union member that
 * validates each lexical form — fixed "1" and instance "true"
 * both validate as xs:boolean (its lexical space accepts "1");
 * "1.0" and "1.000" both as xs:float. Raw strcmp wrongly
 * rejected value-equal forms. Returns 1 when a single member
 * type validates both the fixed value and the instance text. */
static int xsd_union_fixed_match(struct leptris_xsd_schema* s,
                                 const XsdSimple* ut, const char* ns,
                                 const char* fixed, const char* text) {
    if (!ut || ut->deriv != XSD_SIMPLE_UNION) return 0;
    size_t n = ut->member_count > ut->member_def_count
                   ? ut->member_count
                   : ut->member_def_count;
    int fixed_member = -1;
    for (size_t i = 0; i < n && fixed_member < 0; i++) {
        const XsdSimple* def =
            i < ut->member_def_count ? ut->member_defs[i] : NULL;
        const char* mname = i < ut->member_count ? ut->members[i] : NULL;
        int ok;
        char canon[128];
        if (mname) xsd_member_canon(mname, canon, sizeof(canon));
        if (def) {
            ok = xsd_simple_valid_chain(s, def, fixed);
        } else if (mname && strncmp(canon, "xs:", 3) == 0) {
            ok = xsd_builtin_valid(canon, fixed);
        } else if (mname) {
            const XsdSimple* m = xsd_find_simple_ns_pub(s, canon, ns);
            ok = m && xsd_simple_valid_chain(s, m, fixed);
        } else {
            continue;
        }
        if (ok) fixed_member = (int)i;
    }
    if (fixed_member < 0) return 0;
    const XsdSimple* def =
        (size_t)fixed_member < ut->member_def_count
            ? ut->member_defs[fixed_member]
            : NULL;
    if (def) return xsd_simple_valid_chain(s, def, text) &&
                    xsd_value_equal("xs:string", fixed, text);
    const char* mname = (size_t)fixed_member < ut->member_count
                            ? ut->members[fixed_member]
                            : NULL;
    if (!mname) return 0;
    char canon[128];
    xsd_member_canon(mname, canon, sizeof(canon));
    if (strncmp(canon, "xs:", 3) == 0)
        return xsd_builtin_valid(canon, text) &&
               xsd_value_equal(canon, fixed, text);
    const XsdSimple* m = xsd_find_simple_ns_pub(s, canon, ns);
    return m && xsd_simple_valid_chain(s, m, text);
}

/* leptris-rs#74 (stE096): a LIST-typed fixed constraint compares in
 * the list VALUE SPACE — whitespace runs separate items, so the
 * itemization of "2004 ---04 --05" and "2004  ---04  --05" is the
 * same list. Each item pair then compares through the item type
 * (a union rides its first-accepting-member mapping; a name rides
 * the value-equality rules). */
static int xsd_list_fixed_match(struct leptris_xsd_schema* s,
                                const XsdSimple* lt, const char* ns,
                                const char* fixed, const char* text);

static int xsd_item_fixed_match(struct leptris_xsd_schema* s,
                                const XsdSimple* item_def,
                                const char* item_name, const char* ns,
                                const char* fixed, const char* text) {
    if (item_def && item_def->deriv == XSD_SIMPLE_UNION)
        return xsd_union_fixed_match(s, item_def, ns, fixed, text);
    if (item_def) return xsd_simple_valid_chain(s, item_def, text) &&
                         strcmp(fixed, text) == 0;
    if (!item_name) return strcmp(fixed, text) == 0;
    char canon[128];
    xsd_member_canon(item_name, canon, sizeof(canon));
    const XsdSimple* m =
        strncmp(canon, "xs:", 3) == 0
            ? NULL
            : xsd_find_simple_ns_pub(s, canon, ns);
    if (m && m->deriv == XSD_SIMPLE_UNION)
        return xsd_union_fixed_match(s, m, ns, fixed, text);
    if (strncmp(canon, "xs:", 3) != 0)
        return m && xsd_simple_valid_chain(s, m, text) &&
               strcmp(fixed, text) == 0;
    return xsd_builtin_valid(canon, text) &&
           xsd_value_equal(canon, fixed, text);
}

static int xsd_list_fixed_match(struct leptris_xsd_schema* s,
                                const XsdSimple* lt, const char* ns,
                                const char* fixed, const char* text) {
    if (!lt || lt->deriv != XSD_SIMPLE_LIST) return 0;
    /* itemize both lexicals on whitespace runs */
    const char* fi[128];
    size_t fl[128], fn = 0;
    const char* p = fixed;
    while (*p && fn < 128) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        fi[fn] = p;
        size_t n = 0;
        while (*p && !isspace((unsigned char)*p)) { p++; n++; }
        fl[fn++] = n;
    }
    const char* ti[128];
    size_t tl[128], tn = 0;
    p = text;
    while (*p && tn < 128) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        ti[tn] = p;
        size_t n = 0;
        while (*p && !isspace((unsigned char)*p)) { p++; n++; }
        tl[tn++] = n;
    }
    if (fn != tn) return 0;
    char fb[128], tb[128];
    for (size_t i = 0; i < fn; i++) {
        if (fl[i] >= sizeof(fb) || tl[i] >= sizeof(tb)) return 0;
        memcpy(fb, fi[i], fl[i]); fb[fl[i]] = 0;
        memcpy(tb, ti[i], tl[i]); tb[tl[i]] = 0;
        if (!xsd_item_fixed_match(s, lt->item_def, lt->item_type, ns,
                                  fb, tb))
            return 0;
    }
    return 1;
}

static void validate_element_decl(struct xsd_validator* v,
                                  struct leptris_xsd_schema* s,
                                  LeptrisElement elem, int depth,
                                  const XsdElementDecl* decl_override);
static void validate_local(struct xsd_validator* v,
                           struct leptris_xsd_schema* s,
                           LeptrisElement elem, const char* local,
                           const char* type, const char* fill,
                           const char* tns_hint);
static void validate_local_uri(struct xsd_validator* v,
                               struct leptris_xsd_schema* s,
                               LeptrisElement elem, const char* local,
                               const char* type, const char* euri,
                               const char* xsi_q, const char* fill,
                               const char* tns_hint);

/* #1879: the strongest processContents among the model's wildcards
 * whose namespace grammar admits (uri) — 2 skip, 1 lax, 0 strict,
 * -1 when no wildcard admits the name. XSD 1.0 section 3.9.4.2:
 * skip imposes NO constraint on the matched element (even a
 * declared one); strict requires a declaration; lax validates
 * declared children only. #2151: the open-content wildcard rides
 * the same gate, EXCEPT when an element particle in the model
 * matches the child's name — the validation-path rule (§3.8.4.1)
 * attributes such a child to the PARTICLE, never to the open
 * content (open025.n1: a declared <i> must face its type even
 * though the skip wildcard could absorb it). */
static int model_wildcard_pc(const XsdCm* model, const char* uri,
                             const char* tns, const char* cname) {
    if (!model) return -1;
    int particle_match = 0;
    int best = -1;
    const XsdCm* stack[64];
    int sp = 0;
    stack[sp++] = model;
    while (sp > 0 && best < 2) {
        const XsdCm* p = stack[--sp];
        for (; p; p = p->next) {
            if (cname && p->kind == XSD_CM_ELEMENT && p->name &&
                strcmp(p->name, cname) == 0)
                particle_match = 1;
            if (p->kind == XSD_CM_ANY) {
                const char* grammar =
                    p->any_ns ? p->any_ns : "##any";
                const char* unit =
                    p->any_unit_tns ? p->any_unit_tns : tns;
                if (any_attr_matches(grammar, uri, unit)) {
                    int pc = p->pc_level;
                    if (pc > best) best = pc;
                }
            }
            if (p->first_child && sp < 64)
                stack[sp++] = p->first_child;
        }
    }
    if (!particle_match && model->open_any) {
        /* #2151: the open-content wildcard gates its absorbed
         * children (skip imposes no constraint at all) */
        const XsdCm* w = model->open_any;
        const char* grammar = w->any_ns ? w->any_ns : "##any";
        const char* unit = w->any_unit_tns ? w->any_unit_tns : tns;
        if (any_attr_matches(grammar, uri, unit)) {
            int pc = w->pc_level;
            if (pc > best) best = pc;
        }
    }
    return best;
}

/* #2149 — inherited attributes in the CTA test context. §3.12.4
 * builds the test's data model from E, E's own attributes, and E's
 * INHERITED attributes (§3.3.5.6: attributes of ancestors carried by
 * inheritable attribute uses, excluding names E carries itself) —
 * the ancestors, siblings and children of E are NOT represented. The
 * engine evaluates tests in place against the instance; when an
 * inheritable ancestor attribute exists, the context is rebuilt as a
 * detached root-only document carrying E's own attributes plus the
 * inherited ones, and the chain evaluates against that. */

static size_t xsd_xml_escape(const char* s, char* out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 6 < cap; s++) {
        char c = *s;
        if (c == '&') {
            memcpy(out + o, "&amp;", 5);
            o += 5;
        } else if (c == '<') {
            memcpy(out + o, "&lt;", 4);
            o += 4;
        } else if (c == '>') {
            memcpy(out + o, "&gt;", 4);
            o += 4;
        } else if (c == '"') {
            memcpy(out + o, "&quot;", 6);
            o += 6;
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
    return o;
}

static int xsd_buf_put(char* buf, size_t cap, size_t* off,
                       const char* fmt, ...) {
    if (*off >= cap) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, cap - *off, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *off) {
        *off = cap;
        return 0;
    }
    *off += (size_t)n;
    return 1;
}

/* The namespace URI a prefixed attribute qname binds to at `from`
 * (climbing in-scope declarations); NULL when unprefixed or unbound. */
static const char* xsd_ns_binding_for(LeptrisElement from,
                                      const char* qname, char* decl,
                                      size_t cap) {
    const char* colon = strchr(qname, ':');
    if (!colon) return NULL;
    snprintf(decl, cap, "xmlns:%.*s", (int)(colon - qname), qname);
    for (LeptrisElement up = from; up;
         up = leptris_element_parent(up)) {
        if (leptris_node_get_type(leptris_element_as_node(up)) !=
            LEPTRIS_NODE_TYPE_ELEMENT)
            break;
        const char* v = leptris_element_attribute(up, decl);
        if (v) return v;
    }
    return NULL;
}

/* Effective {inheritable} (§3.3.5.6) of the attribute `qname` as
 * declared for `owner`'s governing declaration: the governing type's
 * attribute rows first (a use-attribute value wins — cta0012), then
 * the global declaration the row references. Unset rows fall back to
 * the global declaration's flag; 0 when nothing declares it. */
static int xsd_attr_effective_inheritable(struct leptris_xsd_schema* s,
                                          LeptrisElement owner,
                                          const char* qname) {
    const char* colon = strchr(qname, ':');
    const char* local = colon ? colon + 1 : qname;
    const char* ol = NULL, *our = NULL;
    leptris_element_expanded_name(owner, &ol, NULL, &our);
    if (ol) {
        const XsdElementDecl* d = xsd_find_element_ns(s, ol, our);
        if (d && d->type) {
            const XsdTypeAttrs* rows =
                xsd_find_type_attrs_ns(s, d->type, our);
            if (!rows) rows = xsd_find_type_attrs(s, d->type);
            for (XsdAttrDecl* a = rows ? rows->attrs : NULL; a;
                 a = a->next) {
                if (strcmp(a->name, local) == 0 ||
                    strcmp(a->name, qname) == 0) {
                    if (a->inheritable >= 0) return a->inheritable;
                    break; /* declared, flag unset → global fallback */
                }
            }
        }
    }
    const XsdAttrDecl* top = xsd_find_top_attr(s, local);
    return top ? (top->inheritable >= 0 ? top->inheritable : 0) : 0;
}

static int xsd_cta_ebv(XsdAlternative* alt, LeptrisDocument doc,
                       LeptrisElement ctx) {
    int ebv = 0;
    if (alt->compiled && doc) {
        LeptrisXPathResult r =
            leptris_xquery_eval(alt->compiled, doc, ctx);
        if (r) {
            ebv = leptris_xpath_result_boolean(r);
            leptris_xpath_result_free(r);
        }
    }
    return ebv;
}

/* #2149 — XSD 1.1 §3.12.4 conditional type assignment: evaluate the
 * alternative chain in order against the instance element; the first
 * alternative whose test is true selects its type. A test that fails
 * to compile (compiled == NULL) evaluates as false — §3.12.4 turns a
 * dynamic error into a false verdict. A test-less alternative is the
 * type table's DEFAULT: it ends the chain and its type replaces the
 * declared type as the no-match fallback (§3.3.2.1). Returns the
 * selected type name (borrowed), NULL when no alternative applied —
 * *err is set when the selection is xs:error, which has no valid
 * instances (§2.2.1.1) and invalidates the element outright. */
static const char* xsd_cta_select(struct xsd_validator* v,
                                  struct leptris_xsd_schema* s,
                                  LeptrisElement elem,
                                  XsdAlternative* chain, int* err,
                                  XsdAlternative** sel_out) {
    *err = 0;
    if (sel_out) *sel_out = NULL;
    if (!chain) return NULL;

    /* Collect the inherited attributes: nearest ancestor first; a
     * name E carries itself is excluded (§3.12.4 1.1.3), a name
     * already collected from a nearer ancestor is shadowed, and only
     * attributes whose effective {inheritable} is true participate. */
    LeptrisElement ih_src[8];
    const char* ih_qn[8];
    const char* ih_val[8];
    int ih_n = 0;
    {
        const char* own[32];
        int own_n = 0;
        for (LeptrisAttribute at = leptris_element_first_attribute(elem);
             at && own_n < 32; at = leptris_attribute_next(at)) {
            const char* qn = leptris_attribute_get_name(at);
            if (qn && strncmp(qn, "xmlns", 5) != 0)
                own[own_n++] = qn;
        }
        for (LeptrisElement up = leptris_element_parent(elem);
             up && ih_n < 8; up = leptris_element_parent(up)) {
            if (leptris_node_get_type(leptris_element_as_node(up)) !=
                LEPTRIS_NODE_TYPE_ELEMENT)
                break;
            for (LeptrisAttribute at =
                     leptris_element_first_attribute(up);
                 at && ih_n < 8; at = leptris_attribute_next(at)) {
                const char* qn = leptris_attribute_get_name(at);
                if (!qn || strncmp(qn, "xmlns", 5) == 0) continue;
                int seen = 0;
                for (int i = 0; i < own_n && !seen; i++)
                    if (strcmp(own[i], qn) == 0) seen = 1;
                for (int i = 0; i < ih_n && !seen; i++)
                    if (strcmp(ih_qn[i], qn) == 0) seen = 1;
                if (seen) continue;
                if (xsd_attr_effective_inheritable(s, up, qn) == 1) {
                    ih_src[ih_n] = up;
                    ih_qn[ih_n] = qn;
                    ih_val[ih_n] =
                        leptris_attribute_get_value(up, at);
                    ih_n++;
                }
            }
        }
    }

    LeptrisElement ctx = elem;
    LeptrisDocument tdoc = (LeptrisDocument)0;
    if (ih_n > 0) {
        /* Rebuild the §3.12.4 context as a detached root-only
         * element (children emptied, parent severed): E's own
         * attributes, the inherited ones, and the prefix bindings
         * each emitted name needs. */
        char buf[4096];
        size_t off = 0;
        const char* el = NULL;
        leptris_element_expanded_name(elem, &el, NULL, NULL);
        int ok = xsd_buf_put(buf, sizeof(buf), &off, "<%s",
                             el ? el : "leptris-cta-context");
        const char* bound[16];
        int bound_n = 0;
        char decl[300];
        /* E's own prefix, so name-shaped tests keep their binding */
        if (ok) {
            const char* ep = NULL;
            leptris_element_expanded_name(elem, NULL, &ep, NULL);
            if (ep && ep[0]) {
                char pd[300];
                snprintf(pd, sizeof(pd), "xmlns:%s", ep);
                const char* b = NULL;
                for (LeptrisElement u = elem; u && !b;
                     u = leptris_element_parent(u)) {
                    if (leptris_node_get_type(
                            leptris_element_as_node(u)) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        break;
                    b = leptris_element_attribute(u, pd);
                }
                if (b && bound_n < 16) {
                    ok = xsd_buf_put(buf, sizeof(buf), &off,
                                     " xmlns:%s=\"%s\"", ep, b);
                    bound[bound_n++] = ep;
                }
            }
        }
        for (LeptrisAttribute at = leptris_element_first_attribute(elem);
             at && ok; at = leptris_attribute_next(at)) {
            const char* qn = leptris_attribute_get_name(at);
            if (!qn || strncmp(qn, "xmlns", 5) == 0) continue;
            char ev[512];
            xsd_xml_escape(leptris_attribute_get_value(elem, at), ev,
                           sizeof(ev));
            ok = xsd_buf_put(buf, sizeof(buf), &off, " %s=\"%s\"", qn,
                             ev);
            if (ok && strchr(qn, ':')) {
                const char* b = xsd_ns_binding_for(elem, qn, decl,
                                                   sizeof(decl));
                if (b) {
                    int dup = 0;
                    const char* pfx = decl + 6;
                    for (int i = 0; i < bound_n && !dup; i++)
                        if (strcmp(bound[i], pfx) == 0) dup = 1;
                    if (!dup && bound_n < 16) {
                        ok = xsd_buf_put(buf, sizeof(buf), &off,
                                         " %s=\"%s\"", decl, b);
                        bound[bound_n++] = pfx;
                    }
                }
            }
        }
        for (int i = 0; i < ih_n && ok; i++) {
            char ev[512];
            xsd_xml_escape(ih_val[i] ? ih_val[i] : "", ev, sizeof(ev));
            ok = xsd_buf_put(buf, sizeof(buf), &off, " %s=\"%s\"",
                             ih_qn[i], ev);
            if (ok && strchr(ih_qn[i], ':')) {
                const char* b = xsd_ns_binding_for(ih_src[i], ih_qn[i],
                                                   decl, sizeof(decl));
                if (b) {
                    int dup = 0;
                    const char* pfx = decl + 6;
                    for (int j = 0; j < bound_n && !dup; j++)
                        if (strcmp(bound[j], pfx) == 0) dup = 1;
                    if (!dup && bound_n < 16) {
                        ok = xsd_buf_put(buf, sizeof(buf), &off,
                                         " %s=\"%s\"", decl, b);
                        bound[bound_n++] = pfx;
                    }
                }
            }
        }
        if (ok) ok = xsd_buf_put(buf, sizeof(buf), &off, "/>");
        if (ok) {
            LeptrisStatus pst = (LeptrisStatus)0;
            tdoc = leptris_parse_string(buf, strlen(buf), &pst);
            if (tdoc) {
                LeptrisElement root = leptris_document_root(tdoc);
                if (root) ctx = root;
            }
        }
        /* overflow or an unparseable context: evaluate without the
         * inherited set (the lax direction — the test then simply
         * does not see the ancestor value) */
    }

    const char* sel = NULL;
    for (XsdAlternative* alt = chain; alt; alt = alt->next) {
        if (!alt->test) {
            *err = alt->xs_error;
            sel = alt->type;
            break;
        }
        if (xsd_cta_ebv(alt, tdoc ? tdoc : v->doc, ctx)) {
            sel = alt->type;
            /* moxml#579: the SELECTED test's attribute references
             * ride the exemption (§3.12.4); a test-less default
             * references nothing. An xs:error selection invalidates
             * regardless of which arm named it (§2.2.1.1) — the
             * propagation used to cover only the test-less default,
             * which let the strict attribute check against the
             * attribute-less selection carry the verdict by
             * accident (s3_12ii05). */
            if (alt->xs_error) *err = 1;
            if (sel_out) *sel_out = alt;
            break;
        }
    }
    if (tdoc) leptris_document_free(tdoc);
    return sel;
}

static void validate_children(struct xsd_validator* v,
                              struct leptris_xsd_schema* s,
                              LeptrisElement elem, XsdCm* ct) {
    XsdCm* model = ct->first_child;
    /* empty content model: no element children are allowed (the
     * anyAttr-derive2 family accepted whole foreign subtrees here)
     * and NO character content either — an attributes-only
     * complexType is empty (p2: <elem>abc</elem> [invalid]; the
     * oracle rejects whitespace-only content too, #1691 law) */
    if (!model) {        /* simpleContent types ride text_type with no particle —
         * their text is validated at the call site. leptris-rs#35
         * (ctA033, idF025's inline c): mixed="true" admits character
         * content even with no particle model. */
        if (!ct->text_type && !ct->mixed) {
            const char* et = leptris_element_text(elem);
            if (et && *et) {
                const char* pn = NULL;
                leptris_element_expanded_name(elem, &pn, NULL, NULL);
                verrf_atk(v, elem, "content-model", pn ? pn : "",
                          "element", "#text",
                          "character content is not allowed: the "
                          "content model is empty");
            }
        }
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(elem));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement ce = (LeptrisElement)c;
            const char* cl = NULL;
            leptris_element_expanded_name(ce, &cl, NULL, NULL);
            const char* pn = NULL;
            leptris_element_expanded_name(elem, &pn, NULL, NULL);
            verrf_atk(v, elem, "content-model", pn ? pn : "",
                      "element", cl ? cl : "",
                      "not allowed: the content model is empty");
        }
        return;
    }
    /* #2151: variety EMPTY (§3.4.2.3.3 cl. 4.1.1/3.1.2) — an explicit
     * empty content model with no open content applied forbids ALL
     * character content, whitespace included (open012.n3). An
     * applied open content upgrades the variety to element-only:
     * non-whitespace character content is invalid (open010.n2),
     * whitespace-only text stays legal (open010.v2). The walk reads
     * the element's DIRECT text children — the deep
     * leptris_element_text would drag the absorbed elements' own
     * text in. */
    if (model->empty_content || model->empty_upgraded) {
        int text_bad = 0;
        for (LeptrisNodeRef c =
                 leptris_node_first_child(
                     leptris_element_as_node(elem));
             c && !text_bad; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_TEXT &&
                leptris_node_get_type(c) !=
                    LEPTRIS_NODE_TYPE_CDATA)
                continue;
            const char* t = leptris_text_node_get_content(c);
            if (!t || !*t) continue;
            if (model->empty_content) {
                text_bad = 1;
                break;
            }
            int ws_only = 1;
            for (const char* q = t; *q; q++)
                if (*q != ' ' && *q != '\t' && *q != '\n' &&
                    *q != '\r')
                    ws_only = 0;
            if (!ws_only) text_bad = 1;
        }
        if (text_bad) {
            const char* pn = NULL;
            leptris_element_expanded_name(elem, &pn, NULL, NULL);
            verrf_atk(v, elem, "content-model", pn ? pn : "",
                      "element", "#text",
                      "character content is not allowed: the "
                      "content model is empty");
        }
    }
    /* collect child names + uris */
    size_t n = 0;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c))
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) n++;
    const char** names = NULL;
    const char** uris = NULL;
    size_t i = 0;
    if (n > 0) {
        names = (const char**)malloc(n * sizeof(char*));
        uris = (const char**)calloc(n, sizeof(char*));
        if (!names || !uris) {
            free(names);
            free(uris);
            return;
        }
    }
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(ce, &local, &prefix, &uri);
        names[i] = local ? local : "";
        uris[i] = uri;
        i++;
    }
    /* moxml#464-adjacent false-valid: the early return on !n let an
     * empty instance pass ANY content model — required particles
     * never failed. Zero children still walks: the NFA accepts the
     * empty string only when every particle reaches accept with
     * min == 0. names/uris stay NULL (never dereferenced at n==0). */
    /* #2151: the greedy-attribution trace — children from `oc_from`
     * on may be attributed to the open content (the model's particle
     * path diverged there), so they type by the wildcard's
     * processContents instead of the name-matched particle
     * (open047.v3: the second <b> rides the skip wildcard and is
     * NOT type-checked; open025.n1: the first <i> still faces its
     * declared type). */
    int oc_from = -1;
    int ok = xsd_content_valid(s, model, xsd_target_ns(s), names, uris,
                               n, &oc_from);
    if (ok == -2) {
        /* #1894/#514: pathological occurrence product — the automaton
         * exceeds the compile-time size cap. The cap is a resource
         * guard, not a validity verdict: the content model is
         * engine-INERT for this element (no instance finding — the
         * consuming validator decides), the same posture as the
         * compile-time guard. The element's content check is skipped
         * and the walk continues; under-report, never false-report. */
        free(names);
        free(uris);
        return;
    }
    if (ok) {
        free(names);
        free(uris);
    } else {
        /* Name the culprit: libxml2 names the offending child once
         * and recovers; the unnamed form cascaded ambiguously
         * (#1626 — 353 errors for one misordered pair). Greedy
         * linear match against the sequence's particle list;
         * non-sequence models keep the generic message. */
        const char* culprit = NULL;
        if (model->kind == XSD_CM_SEQ) {
            XsdCm* parts[256];
            size_t j = 0;
            for (XsdCm* q = model->first_child; q && j < 256;
                 q = q->next)
                parts[j++] = q;
            size_t pj = 0;
            for (size_t k2 = 0; k2 < n && !culprit; k2++) {
                const char* uri = uris ? uris[k2] : NULL;
                int matched = 0;
                while (pj < j) {
                    XsdCm* q = parts[pj];
                    if (q->name && strcmp(q->name, names[k2]) == 0 &&
                        (!q->ns ||
                         (uri && strcmp(uri, q->ns) == 0))) {
                        matched = 1;
                        pj++;
                        break;
                    }
                    if (q->min == 0) {
                        pj++; /* skippable optional: not expected here */
                        continue;
                    }
                    culprit = names[k2]; /* expected q, found this */
                    break;
                }
                if (!matched && !culprit) culprit = names[k2];
            }
        }
        const char* local = NULL;
        leptris_element_expanded_name(elem, &local, NULL, NULL);
        char msg[256];
        snprintf(msg, sizeof(msg),
                 culprit
                     ? "children do not match the content model (at '%s')"
                     : "children do not match the content model",
                 culprit ? culprit : "");
        verrf_atk(v, elem, "content-model", local, "element", local,
                  msg);
        free(names);
        free(uris);
    }
    /* recurse: the parent's matched particle declaration governs its
     * instance element — Element Locally Valid (Complex Type), XSD 1.0
     * §3.4.4 — so a local anonymous (or named) type shadows a global
     * element declaration of the same name (#1871). Only a particle
     * carrying no type (a ref, which always resolves to a global
     * declaration) falls through to the global lookup here.
     * (first-name-match DFS — position-exact particle typing arrives
     * with the NFA trace.) */
    size_t child_i = 0;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        const char* cl = NULL, *cp = NULL, *cu = NULL;
        leptris_element_expanded_name(ce, &cl, &cp, &cu);
        if (!cl) continue;
        /* #2151: children from the trace's divergence point on may be
         * attributed to the open content — their processContents is
         * the OC wildcard's own, and the name-matched particle does
         * NOT govern them (the model's particle path already
         * diverged). */
        int oc_attributed =
            oc_from >= 0 && (int)child_i >= oc_from && model->open_any;
        child_i++;
        /* #1879: wildcard processContents gates the child — skip
         * imposes no constraint at all (even a declared child);
         * strict demands a declaration when no particle/global
         * types it; lax validates declared children only. */
        int wpc = oc_attributed ? model->open_any->pc_level
                                : model_wildcard_pc(model, cu,
                                                    xsd_target_ns(s),
                                                    cl);
        if (wpc == 2) {
            /* #2156: record the skip attribution (§3.10.4.1) —
             * identity constraints omit skipped subtrees from their
             * target node sets (§3.11.4 cl. 1, the wild101 note). */
            struct ic_skipped* sk =
                (struct ic_skipped*)malloc(sizeof(*sk));
            if (sk) {
                sk->e = ce;
                sk->next = v->skipped;
                v->skipped = sk;
            }
            continue;
        }
        const char* ptype = NULL;
        const char* pfill = NULL;
        XsdCm* pmatch = NULL;
        /* elemZ020 lenient fallback: an unqualified particle may
         * still type a namespaced child when NO global declaration
         * in the child's namespace could (the OOXML-style wp.xsd
         * corpus: no elementFormDefault, no globals). The strict
         * pass remembers the candidate; the global lookup below
         * decides whether it is needed. */
        const char* ltype = NULL;
        const char* lfill = NULL;
        if (model && !oc_attributed) {
            XsdCm* stack[64];
            int sp = 0;
            stack[sp++] = model;
            while (sp > 0 && pmatch == NULL) {
                XsdCm* p = stack[--sp];
                for (; p; p = p->next) {
                    int child_ns = cu && cu[0];
                    /* asymmetric ns gate (elemZ020 + the pinned
                     * #1871 lenient posture): qualified particles
                     * match their own ns and leniently admit no-ns
                     * children; unqualified particles are strict —
                     * no-ns children only, with the lenient
                     * fallback when nothing else can type it. */
                    int pns_ok =
                        p->ns ? (!child_ns ||
                                 strcmp(p->ns, cu) == 0)
                              : !child_ns;
                    /* #2149: a particle may be typed ONLY by its
                     * alternative chain (no declared type at all) —
                     * such a particle still governs its instance
                     * element. */
                    if (p->kind == XSD_CM_ELEMENT && p->name &&
                        strcmp(p->name, cl) == 0 &&
                        (p->type || p->alternatives)) {
                        if (pns_ok) {
                            ptype = p->type;
                            pmatch = p;
                            /* leptris-rs#35 (isDefault005): the
                             * particle's fixed/default value
                             * satisfies EMPTY content (§3.12.2). */
                            pfill = p->fixed_value ? p->fixed_value
                                                   : p->default_value;
                            break;
                        }
                        if (!ltype) {
                            ltype = p->type;
                            lfill = p->fixed_value
                                        ? p->fixed_value
                                        : p->default_value;
                        }
                    }
                    if (p->kind == XSD_CM_GROUP_REF) {
                        XsdGroupDef* g = xsd_find_group(s, p->name);
                        if (g && g->model && sp < 64)
                            stack[sp++] = g->model;
                    }
                    if (sp < 64 && p->first_child)
                        stack[sp++] = p->first_child;
                }
            }
        }
        /* leptris-ruby#535 (XSD 1.0 2.6.2): xsi:type applies at every
         * assessed depth -- a child typed by the parent particle is
         * substituted exactly like a globally declared one. */
        if (pmatch) {
            const char* cxsi = leptris_element_attribute_ns(
                ce, "http://www.w3.org/2001/XMLSchema-instance", "type");
            if (cxsi && cxsi[0]) {
                char xuri[512], xlocal[256];
                xsd_resolve_qname_inst(ce, cxsi, xuri, sizeof(xuri),
                                       xlocal, sizeof(xlocal));
                if (!xlocal[0]) {
                    verrf_atk(v, ce, "type-resolution", cl, "element", cl,
                              "xsi:type prefix is not bound in the instance");
                } else {
                    validate_local_uri(v, s, ce, cl, xlocal, xuri, cxsi,
                                       pfill, NULL);
                }
            } else {
                /* #2149: the particle's alternative chain governs the
                 * instance element (§3.12.4); no match falls back to
                 * the particle's declared type. */
                int cta_err = 0;
                XsdAlternative* cta_alt = NULL;
                const char* eff = xsd_cta_select(v, s, ce,
                                                 pmatch ? pmatch->alternatives
                                                        : NULL,
                                                 &cta_err, &cta_alt);
                if (cta_err) {
                    verrf_atk(v, ce, "type-resolution", cl, "element",
                              cl,
                              "conditional type assignment selected "
                              "xs:error");
                } else {
                    /* moxml#579: the selected test's attribute
                     * references are exempt for this element. */
                    char** prev_exempt = v->cta_exempt;
                    int prev_n = v->cta_exempt_n;
                    v->cta_exempt =
                        cta_alt ? cta_alt->test_attrs : NULL;
                    v->cta_exempt_n =
                        cta_alt ? cta_alt->test_attr_n : 0;
                    validate_local(v, s, ce, cl, eff ? eff : ptype,
                                   pfill, ct ? ct->ns : NULL);
                    v->cta_exempt = prev_exempt;
                    v->cta_exempt_n = prev_n;
                }
            }
        } else {
            const XsdElementDecl* cd =
                xsd_find_element_child_ns(s, cl, cu);
            if (cd)
                validate_element_decl(v, s, ce, 0, cd);
            else if (ltype) {
                /* nothing global claims the namespaced child —
                 * the unqualified particle's type governs
                 * (wp.xsd corpus posture) */
                validate_local(v, s, ce, cl, ltype, lfill,
                               ct ? ct->ns : NULL);
            }
        }
        /* wildcard-admitted children without a declaration pass:
         * strict and lax alike admit undeclared names (the pinned
         * moxml#460 notQName semantics — tier-1 lax direction) */
    }
}

/* $value binds typed: a numeric-derived simple type binds the
 * lexical as a number literal, everything else as a string — the
 * TYPE decides, not the lexical's shape ("5" under xs:string is
 * the string '5'). */
static int numeric_builtin(const char* b) {
    static const char* nums[] = {
        "xs:integer",  "xs:decimal",     "xs:double", "xs:float",
        "xs:int",      "xs:long",        "xs:short",  "xs:byte",
        "xs:nonNegativeInteger", "xs:positiveInteger",
        "xs:negativeInteger",    "xs:nonPositiveInteger",
        "xs:unsignedLong",       "xs:unsignedInt",
        "xs:unsignedShort",      "xs:unsignedByte", NULL};
    for (int i = 0; nums[i]; i++)
        if (strcmp(b, nums[i]) == 0) return 1;
    return 0;
}

/* ---- #2154: xs:assertion facet evaluation (XSD 1.1 §3.16) ----------
 *
 * The facet lists accumulated on the restriction ladder all hold
 * (§3.16.6), the list ITEM type's and union MEMBER type's ladders
 * apply per item/member (§3.16.4), and $value binds the TYPED value:
 * numerics as bare literals, list values as the itemized sequence,
 * temporals through their typed constructors, the lexical string
 * otherwise. */

/* The next restriction-ladder hop: an inline anonymous base rides
 * base_def (#1936); a named local base resolves name-only (the
 * compile-side walk's ladder; builtins end the chain). */
static const XsdSimple* assertion_base_next(
    struct leptris_xsd_schema* s, const XsdSimple* t) {
    if (!t) return NULL;
    if (t->base_def) return t->base_def;
    if (t->base && strncmp(t->base, "xs:", 3) != 0)
        return xsd_find_simple_pub(s, t->base);
    return NULL;
}

/* The chain's terminal builtin ("xs:date"...): NULL when no builtin
 * ends the walk — the value binds as the lexical string. */
static const char* assertion_chain_terminal(struct leptris_xsd_schema* s,
                                            const XsdSimple* t) {
    for (int d = 0; t && d < 32; d++) {
        if (t->base && strncmp(t->base, "xs:", 3) == 0) return t->base;
        t = assertion_base_next(s, t);
    }
    return NULL;
}

/* The first LIST definition in the chain (NULL = the chain has none):
 * its item type drives the itemized binding and the per-item pass. */
static const XsdSimple* assertion_chain_list_def(
    struct leptris_xsd_schema* s, const XsdSimple* t) {
    for (int d = 0; t && d < 32; d++) {
        if (t->deriv == XSD_SIMPLE_LIST) return t;
        t = assertion_base_next(s, t);
    }
    return NULL;
}

/* Does the chain reach a LIST definition? (A restriction of a list
 * keeps the list value space — #1882's rule, validate.c-side.) */
static int assertion_chain_is_list(struct leptris_xsd_schema* s,
                                   const XsdSimple* t) {
    return assertion_chain_list_def(s, t) != NULL;
}

/* One scalar's $value fragment (§3.16.4 cl.2): numeric terminals
 * bind bare (numeric-shaped lexicals only); everything else —
 * temporals and durations included — binds the LEXICAL string, the
 * typed comparison rules live in the operator layer (the
 * nodeset-vs-typed-temporal branch compares the bound lexical in
 * the value space). Returns bytes written, -1 on overflow. */
static int assertion_bind_scalar(const char* term, const char* v,
                                 char* out, size_t cap) {
    (void)term;
    if (term && numeric_builtin(term) && *v &&
        strspn(v, "0123456789.+-eE") == strlen(v)) {
        int w = snprintf(out, cap, "%s", v);
        return (w > 0 && (size_t)w < cap) ? w : -1;
    }
    size_t o = 0;
    if (o + 2 >= cap) return -1;
    out[o++] = '\'';
    for (const char* p = v; *p; p++) {
        if (*p == '\'') {
            if (o + 2 >= cap) return -1;
            out[o++] = '\'';
        }
        if (o + 1 >= cap) return -1;
        out[o++] = *p;
    }
    if (o + 1 >= cap) return -1;
    out[o++] = '\'';
    out[o] = 0;
    return (int)o;
}

/* The LIST binding: (i1,i2,...) per whitespace-separated item, each
 * bound through the ITEM chain's terminal (assert-simple005:
 * count($value) sees the items, not the one string). An empty list
 * binds () (§3.16.4: the empty sequence). Returns 1 when bound. */
static int assertion_bind_list(struct leptris_xsd_schema* s,
                               const XsdSimple* st, const char* lexical,
                               char* out, size_t cap) {
    const XsdSimple* ldef = assertion_chain_list_def(s, st);
    const char* term = NULL;
    if (ldef) {
        const XsdSimple* item_def = ldef->item_def;
        if (!item_def && ldef->item_type &&
            strncmp(ldef->item_type, "xs:", 3) != 0)
            item_def = xsd_find_simple_pub(s, ldef->item_type);
        term = item_def ? assertion_chain_terminal(s, item_def)
                        : ldef->item_type;
    }
    size_t o = 0;
    if (o + 1 >= cap) return 0;
    out[o++] = '(';
    out[o] = 0;
    const char* p = lexical;
    int n = 0;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);
        char item[512];
        if (len >= sizeof(item)) return 0;
        memcpy(item, start, len);
        item[len] = 0;
        char frag[600];
        if (assertion_bind_scalar(term, item, frag, sizeof(frag)) < 0)
            return 0;
        if (n && o + 1 < cap) out[o++] = ',';
        if ((size_t)strlen(frag) >= cap - o) return 0;
        o += (size_t)snprintf(out + o, cap - o, "%s", frag);
        out[o] = 0;
        n++;
    }
    if (o + 1 >= cap) return 0;
    out[o++] = ')';
    out[o] = 0;
    return 1;
}

/* One facet: lazily compiled under a $value declaration (§3.16.4's
 * binding; the same wrapper the complexType assert uses), then
 * evaluated per instance. Compile failure = not enforced (the facet
 * posture — never reject what we cannot assess); evaluation failure
 * or a false EBV = the facet does not hold (§3.16.4: errors are
 * failures). */
static int assertion_ebv(struct xsd_validator* v, XsdAssertionFacet* f,
                         LeptrisElement elem, const char* select) {
    if (!f->tried) {
        f->tried = 1;
        size_t pl = f->ns_prolog ? strlen(f->ns_prolog) : 0;
        size_t tl = f->test ? strlen(f->test) : 0;
        char* wrapped = (char*)malloc(pl + tl + 64);
        if (wrapped) {
            size_t o = 0;
            if (pl) {
                memcpy(wrapped + o, f->ns_prolog, pl);
                o += pl;
                wrapped[o++] = ' ';
            }
            {
                static const char head[] =
                    "declare variable $value external; ";
                memcpy(wrapped + o, head, sizeof(head) - 1);
                o += sizeof(head) - 1;
            }
            memcpy(wrapped + o, f->test ? f->test : "", tl + 1);
            f->q = leptris_xquery_parse(wrapped, o + tl);
            free(wrapped);
        }
    }
    if (!f->q) return 1;
    const char* names[1] = {"value"};
    const char* sels[1] = {select};
    LeptrisXPathResult r = leptris_xquery_eval_params_ex(
        f->q, v->doc, elem, names, sels, 1,
        (unsigned)LEPTRIS_XQUERY_ASSERT_DATA_MODEL);
    int ebv = 0;
    if (r) {
        ebv = leptris_xpath_result_boolean(r);
        leptris_xpath_result_free(r);
    }
    return ebv;
}

/* Every assertion list along one def's restriction ladder must hold
 * with `select` bound to $value. */
static int assertion_ladder_holds(struct xsd_validator* v,
                                  struct leptris_xsd_schema* s,
                                  const XsdSimple* t, LeptrisElement elem,
                                  const char* select) {
    for (int d = 0; t && d < 32; d++) {
        for (XsdAssertionFacet* f = t->assertions; f; f = f->next)
            if (!assertion_ebv(v, f, elem, select)) return 0;
        t = assertion_base_next(s, t);
    }
    return 1;
}

/* LIST: every item faces the item type's own facet ladder
 * (listunion1's MYINT). The lexical gates already itemized; the
 * assertion pass re-itemizes and walks each item's ladder. */
static int assertion_list_items_hold(struct xsd_validator* v,
                                     struct leptris_xsd_schema* s,
                                     const XsdSimple* st,
                                     const char* lexical,
                                     LeptrisElement elem) {
    const XsdSimple* ldef = assertion_chain_list_def(s, st);
    if (!ldef) return 1;
    const XsdSimple* item_def = ldef->item_def;
    if (!item_def && ldef->item_type &&
        strncmp(ldef->item_type, "xs:", 3) != 0)
        item_def = xsd_find_simple_pub(s, ldef->item_type);
    if (!item_def) return 1; /* builtin item: carries no facets */
    const char* p = lexical;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        size_t len = (size_t)(p - start);
        char item[512];
        if (len >= sizeof(item)) continue;
        memcpy(item, start, len);
        item[len] = 0;
        char frag[600];
        if (assertion_bind_scalar(assertion_chain_terminal(s, item_def),
                                  item, frag, sizeof(frag)) < 0)
            continue;
        if (!assertion_ladder_holds(v, s, item_def, elem, frag))
            return 0;
    }
    return 1;
}

/* UNION: the value passes when SOME member's lexical gate AND its own
 * facet ladder hold (§3.16.2 — the member's facets participate;
 * assert-simple006, listunion3). The lexical pass already ran; here a
 * member that accepts lexically but fails its ladder is rejected
 * unless another member takes the value. */
static int assertion_union_members_hold(
    struct xsd_validator* v, struct leptris_xsd_schema* s,
    const XsdSimple* udef, const char* lexical, LeptrisElement elem) {
    for (size_t i = 0; i < udef->member_count; i++) {
        const char* mn = udef->members[i];
        if (!mn) continue;
        /* memberTypes tokens may ride a prefix (s3_16_2v01's
         * memberTypes="sv:fltUnionD") — resolution is by LOCAL name
         * (the compile-side walk's convention). */
        const char* colon = strchr(mn, ':');
        const char* local = colon ? colon + 1 : mn;
        if (strncmp(mn, "xs:", 3) == 0) {
            if (xsd_builtin_valid(mn, lexical) == 1) return 1;
            continue;
        }
        const XsdSimple* m = xsd_find_simple_pub(s, local);
        if (!m) {
            /* a foreign-prefix spelling of a BUILTIN ("s:date")
             * resolves through its local name; a local name that is
             * no schema type never canonizes (fltUnionD is a
             * schema type, not xs:fltUnionD) */
            char canon[128];
            xsd_member_canon(local, canon, sizeof(canon));
            if (strncmp(canon, "xs:", 3) == 0 &&
                xsd_builtin_valid(canon, lexical) == 1)
                return 1;
            continue;
        }
        if (!xsd_simple_valid_chain(s, m, lexical)) continue;
        char sel[600];
        if (assertion_bind_scalar(assertion_chain_terminal(s, m),
                                  lexical, sel, sizeof(sel)) < 0)
            continue;
        if (assertion_ladder_holds(v, s, m, elem, sel)) return 1;
    }
    for (size_t i = 0; i < udef->member_def_count; i++) {
        const XsdSimple* m = udef->member_defs[i];
        if (!xsd_simple_valid_chain(s, m, lexical)) continue;
        char sel[600];
        if (assertion_bind_scalar(assertion_chain_terminal(s, m),
                                  lexical, sel, sizeof(sel)) < 0)
            continue;
        if (assertion_ladder_holds(v, s, m, elem, sel)) return 1;
    }
    return 0;
}

/* The full assertion pass for one validated value. */
static int assertions_hold(struct xsd_validator* v,
                           struct leptris_xsd_schema* s, const XsdSimple* st,
                           const char* lexical, LeptrisElement elem) {
    int is_list = assertion_chain_is_list(s, st);
    char sel[1024];
    int bound =
        is_list ? assertion_bind_list(s, st, lexical, sel, sizeof(sel))
                : (assertion_bind_scalar(assertion_chain_terminal(s, st),
                                         lexical, sel, sizeof(sel)) >= 0);
    if (bound && !assertion_ladder_holds(v, s, st, elem, sel)) return 0;
    if (is_list &&
        !assertion_list_items_hold(v, s, st, lexical, elem))
        return 0;
    /* a UNION anywhere in the ladder: the member semantics govern */
    const XsdSimple* udef = st;
    for (int d = 0; udef && udef->deriv != XSD_SIMPLE_UNION && d < 32;
         d++)
        udef = assertion_base_next(s, udef);
    if (udef && udef->deriv == XSD_SIMPLE_UNION &&
        !assertion_union_members_hold(v, s, udef, lexical, elem))
        return 0;
    return 1;
}

/* XSD 1.1 xs:anyAttribute/@notQName hit test (moxml#460):
 * ##defined resolves against the schema's top-level attribute
 * declarations; prefix-less literals match no-namespace attribute
 * names; prefixed QNames are deferred (instance-side prefix
 * resolution). */
static int attr_not_qname_hits(const char* tokens,
                               struct leptris_xsd_schema* s,
                               const char* local) {
    const char* p = tokens;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (len == 9 && strncmp(start, "##defined", 9) == 0) {
            if (xsd_attr_name_defined(s, local)) return 1;
            continue;
        }
        if (memchr(start, ':', len)) continue; /* prefixed: deferred */
        if (strlen(local) == len && strncmp(local, start, len) == 0)
            return 1;
    }
    return 0;
}

/* xs:anyAttribute grammar: ##any (or an absent attribute),
 * ##other, ##targetNamespace, ##local, or a space-separated URI
 * list. target_ns may be NULL or "" — an ABSENT unit tns: ##other
 * then matches every attribute that HAS a namespace (it excludes
 * only ##local), ##targetNamespace matches nothing. */
static int any_attr_matches(const char* grammar, const char* uri,
                            const char* target_ns) {
    int has_tns = target_ns && target_ns[0];
    if (!grammar || strcmp(grammar, "##any") == 0) return 1;
    if (strcmp(grammar, "##other") == 0)
        return uri && uri[0] &&
               (!has_tns || strcmp(uri, target_ns) != 0);
    if (strcmp(grammar, "##targetNamespace") == 0)
        return has_tns && uri && strcmp(uri, target_ns) == 0;
    if (strcmp(grammar, "##local") == 0) return uri == NULL;
    /* URI list */
    const char* p = grammar;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t len = (size_t)(p - start);
        if (len == 7 && strncmp(start, "##local", 7) == 0) {
            if (uri == NULL) return 1;
            continue;
        }
        if (len == 17 &&
            strncmp(start, "##targetNamespace", 17) == 0) {
            if (has_tns && uri && strcmp(uri, target_ns) == 0)
                return 1;
            continue;
        }
        if (len == 7 && strncmp(start, "##other", 7) == 0) {
            if (uri && uri[0] &&
                (!has_tns || strcmp(uri, target_ns) != 0))
                return 1;
            continue;
        }
        if (uri && strlen(uri) == len && strncmp(uri, start, len) == 0)
            return 1;
    }
    return 0;
}

/* Collapse one lexical per whiteSpace=collapse: trim, internal runs
 * to single spaces. Returns 0 when the source overflows the buffer. */
static int xsd_collapse_str(const char* src, char* out, size_t cap) {
    char* o = out;
    const char* p = src;
    while (*p && isspace((unsigned char)*p)) p++;
    int spaced = 0;
    while (*p) {
        if (isspace((unsigned char)*p)) {
            spaced = 1;
            p++;
            continue;
        }
        if (spaced) { *o++ = ' '; spaced = 0; }
        *o++ = *p++;
        if ((size_t)(o - out) >= cap - 1) return 0;
    }
    *o = 0;
    return 1;
}

/* leptris-rs#36 (addB104, attO006): the fixed constraint holds on
 * the TYPE's value, not the raw lexical (XSD 1.0 §3.2.3/§4.3.1.2) —
 * a whitespace-variant lexical of a collapsing type (token family,
 * numeric) is the same value. Normalize both per the type's
 * whiteSpace and compare. */
static int attr_fixed_matches(struct leptris_xsd_schema* s,
                              const char* type, const char* ns,
                              const char* val, const char* fixed) {
    if (!type) return 0;
    if (!type_collapses_ws(s, type, ns)) return 0;
    char norm_v[512];
    char norm_f[512];
    if (strlen(val) >= sizeof(norm_v)) return 0;
    if (strlen(fixed) >= sizeof(norm_f)) return 0;
    if (!xsd_collapse_str(val, norm_v, sizeof(norm_v))) return 0;
    if (!xsd_collapse_str(fixed, norm_f, sizeof(norm_f))) return 0;
    return strcmp(norm_v, norm_f) == 0;
}

/* leptris-rs#73: xs:ID itself or a user simpleType whose restriction
 * chain roots at xs:ID. */
static int attr_type_is_id(struct leptris_xsd_schema* s,
                           const char* type);

/* leptris-ruby#679: the strict admission check per row — the name
 * matches AND the attribute's namespace matches the row's form
 * (XSD 1.0 §3.2.3): a qualified-form row admits only an attribute IN
 * its namespace; an unqualified row keeps the pinned #1586 leniency
 * for the qualified spelling (a prefixed attribute with the row's
 * local name still declares it — the OOXML wire posture). */
static int attr_row_admits(const XsdAttrDecl* a, const char* local,
                           const char* qn, const char* auri) {
    if (strcmp(a->name, local) != 0 && strcmp(a->name, qn) != 0)
        return 0;
    if (a->ns && a->ns[0])
        return auri && strcmp(auri, a->ns) == 0;
    return 1;
}

static void validate_attributes(struct xsd_validator* v,
                                struct leptris_xsd_schema* s,
                                LeptrisElement elem,
                                const XsdTypeAttrs* ta,
                                const XsdCm* chain_ct) {
    /* ta == NULL (no captured rows — attribute-less complexTypes)
     * skips the walk entirely; simple/builtin element validation
     * passes k_no_attrs to run the strict walk with zero declared
     * rows. */
    if (!ta) return;
    /* declared rows: present? value typed? */
    for (XsdAttrDecl* a = ta ? ta->attrs : NULL; a; a = a->next) {
        /* leptris-ruby#679: the row's {target namespace} decides
         * which instance attribute can satisfy it (§3.2.3). A
         * qualified-form row matches only an attribute IN its
         * namespace (any prefix bound to that URI); an unqualified
         * row keeps the pinned #1586 leniency — the qualified
         * spelling may still bind it (the OOXML wire posture:
         * documents qualify attributes their schemas declare
         * unqualified). */
        const char* val = NULL;
        if (a->ns && a->ns[0])
            val = leptris_element_attribute_ns(elem, a->ns, a->name);
        else
            val = leptris_element_attribute(elem, a->name);
        if (!val && !(a->ns && a->ns[0])) {
            /* lenient: the qualified spelling (#1586 semantics) */
            for (LeptrisAttribute at =
                     leptris_element_first_attribute(elem);
                 at; at = leptris_attribute_next(at)) {
                const char* qn = leptris_attribute_get_name(at);
                const char* colon = qn ? strchr(qn, ':') : NULL;
                if (qn && colon && strcmp(colon + 1, a->name) == 0) {
                    val = leptris_attribute_get_value(elem, at);
                    break;
                }
                if (qn && !colon && strcmp(qn, a->name) == 0) {
                    val = leptris_attribute_get_value(elem, at);
                    break;
                }
            }
        }
        if (!val) {
            if (a->required)
                verrf_atk(v, elem, "attribute-use", a->name, "attribute",
                          a->name, "required but absent");
            continue;
        }
        text_valid(v, s, a->type, val, ta ? ta->ns : NULL, "attribute",
                   a->name, elem);
        if (a->fixed && strcmp(val, a->fixed) != 0 &&
            !attr_fixed_matches(s, a->type, ta ? ta->ns : NULL, val,
                                a->fixed))
            verrf_atk(v, elem, "attribute-use", a->name, "attribute",
                      a->name, "value does not match the fixed value");
    }
    /* strict: every instance attribute must be declared (or pass
     * the type's xs:anyAttribute grammar). Namespace declarations
     * are not attributes. */
    for (LeptrisAttribute at = leptris_element_first_attribute(elem);
         at; at = leptris_attribute_next(at)) {
        const char* qn = leptris_attribute_get_name(at);
        if (!qn || strncmp(qn, "xmlns", 5) == 0) continue;
        const char* colon = strchr(qn, ':');
        const char* local = colon ? colon + 1 : qn;
        /* namespace of the attribute: engine-side attributes
         * carry their uri through the expanded form */
        const char* auri = leptris_attribute_namespace_uri(at);
        int declared = 0;
        for (XsdAttrDecl* a = ta ? ta->attrs : NULL; a && !declared;
             a = a->next)
            if (attr_row_admits(a, local, qn, auri)) declared = 1;
        /* leptris#1923 (oracle parity with libxml2): the restriction
         * chain's declared attributes stay visible for strict
         * admission — a RESTRICTION that narrows the base's rows
         * still admits what the base declared (ctE019). */
        if (!declared && chain_ct) {
            const XsdCm* anc = chain_ct;
            for (int hops = 0; anc && anc->res_base && hops < 8;
                 hops++) {
                const XsdTypeAttrs* base_rows = xsd_find_type_attrs_ns(
                    s, anc->res_base,
                    anc->res_base_ns ? anc->res_base_ns
                                     : xsd_target_ns(s));
                if (!base_rows)
                    base_rows = xsd_find_type_attrs(s, anc->res_base);
                for (XsdAttrDecl* a = base_rows ? base_rows->attrs
                                                : NULL;
                     a && !declared; a = a->next)
                    if (attr_row_admits(a, local, qn, auri))
                        declared = 1;
                if (declared) break;
                const XsdCm* base_ct = anc->res_base
                    ? xsd_find_complex_ns(s, anc->res_base,
                          anc->res_base_ns ? anc->res_base_ns
                                           : xsd_target_ns(s))
                    : NULL;
                if (!base_ct)
                    base_ct = xsd_find_complex(s, anc->res_base);
                anc = base_ct;
            }
        }
        if (declared) continue;
        /* XSD 1.0 §3.2.7: xsi:type, xsi:nil, xsi:schemaLocation and
         * xsi:noNamespaceSchemaLocation are processor-honored, not
         * schema-validated — they pass any content model (the
         * bug141333/anyAttr-derive2/nvdcve false-rejection family). */
        if (auri && strcmp(auri, "http://www.w3.org/2001/XMLSchema-instance") == 0)
            continue;
        if (ta && ta->any_attr_not_qname &&
            attr_not_qname_hits(ta->any_attr_not_qname, s, local)) {
            /* XSD 1.1 notQName: the wildcard never admits this
             * name, declared or not (moxml#460). */
            verrf_atk(v, elem, "attribute-use", local, "attribute",
                      local, "excluded by notQName");
            continue;
        }
        /* extension union: an attribute is allowed when it matches the
         * derived type's wildcard OR the base type's (the spec's
         * attribute-wildcard union; #1691 anyAttr-derive family) */
        /* #1735: each wildcard resolves against its DECLARING
         * unit's tns (recorded at capture); the merged schema's tns
         * is the fallback */
        /* moxml#460: a notQName-only wildcard (no @namespace) is
         * still a live wildcard — absent @namespace means ##any. */
        if ((ta && (ta->any_attr_ns || ta->any_attr_not_qname) &&
             any_attr_matches(ta->any_attr_ns, auri,
                              ta->any_attr_unit_ns
                                  ? ta->any_attr_unit_ns
                                  : xsd_target_ns(s))) ||
            (ta && ta->any_attr_base_ns &&
             any_attr_matches(ta->any_attr_base_ns, auri,
                              ta->any_attr_base_unit_ns
                                  ? ta->any_attr_base_unit_ns
                                  : xsd_target_ns(s))))
            continue;
        /* moxml#579 (§3.12.4): the attribute references of the CTA
         * test in force pass undeclared — the alternative's selector
         * attribute is its own witness, not a declared use. */
        if (v->cta_exempt_n > 0) {
            int exempt = 0;
            for (int i = 0; i < v->cta_exempt_n && !exempt; i++)
                if (strcmp(v->cta_exempt[i], qn) == 0) exempt = 1;
            if (exempt) continue;
        }
        verrf_atk(v, elem, "attribute-use", local, "attribute", local,
                  "not declared (strict)");
    }
    /* leptris-rs#73 (W3C attZ014, XSD 1.0): an element assessed with
     * more than one ID-typed attribute is invalid — declared rows
     * AND wildcard-admitted attributes whose GLOBAL declaration is
     * ID-derived both count. 1.1 lifted the constraint. */
    if (!xsd_schema_version_11(s)) {
        int id_count = 0;
        for (XsdAttrDecl* a = ta->attrs; a; a = a->next)
            if (attr_type_is_id(s, a->type)) id_count++;
        for (LeptrisAttribute at = leptris_element_first_attribute(elem);
             at && id_count < 2; at = leptris_attribute_next(at)) {
            const char* qn = leptris_attribute_get_name(at);
            if (!qn || strncmp(qn, "xmlns", 5) == 0) continue;
            const char* colon = strchr(qn, ':');
            const char* local = colon ? colon + 1 : qn;
            const char* auri = leptris_attribute_namespace_uri(at);
            int declared = 0;
            for (XsdAttrDecl* a = ta->attrs; a && !declared; a = a->next)
                if (attr_row_admits(a, local, qn, auri))
                    declared = 1;
            if (declared) continue;
            if (auri && strcmp(auri,
                               "http://www.w3.org/2001/XMLSchema-instance")
                    == 0)
                continue;
            if (ta->any_attr_not_qname &&
                attr_not_qname_hits(ta->any_attr_not_qname, s, local))
                continue;
            int admitted =
                ((ta->any_attr_ns || ta->any_attr_not_qname) &&
                 any_attr_matches(ta->any_attr_ns, auri,
                                  ta->any_attr_unit_ns
                                      ? ta->any_attr_unit_ns
                                      : xsd_target_ns(s))) ||
                (ta->any_attr_base_ns &&
                 any_attr_matches(ta->any_attr_base_ns, auri,
                                  ta->any_attr_base_unit_ns
                                      ? ta->any_attr_base_unit_ns
                                      : xsd_target_ns(s)));
            if (!admitted) continue;
            const XsdAttrDecl* g =
                xsd_find_top_attr(s, local);
            if (g && attr_type_is_id(s, g->type)) id_count++;
        }
        if (id_count > 1)
            verrf_atk(v, elem, "attribute-use", "ID", "attribute",
                      "ID",
                      "more than one ID-typed attribute (XSD 1.0 "
                      "one-ID-per-element)");
    }
}

/* leptris-rs#73: xs:ID itself or a user simpleType whose restriction
 * chain roots at xs:ID. */
static int attr_type_is_id(struct leptris_xsd_schema* s,
                           const char* type) {
    if (!type) return 0;
    if (strcmp(type, "xs:ID") == 0 || strcmp(type, "xsd:ID") == 0)
        return 1;
    const XsdSimple* st = xsd_find_simple_ns_pub(s, type, NULL);
    for (int hops = 0; st && st->base && hops < 8; hops++) {
        if (strcmp(st->base, "xs:ID") == 0 ||
            strcmp(st->base, "xsd:ID") == 0)
            return 1;
        st = xsd_find_simple_pub(s, st->base);
    }
    return 0;
}

static void validate_element(struct xsd_validator* v,
                             struct leptris_xsd_schema* s,
                             LeptrisElement elem, int depth) {
    validate_element_decl(v, s, elem, depth, NULL);
}

/* decl_override: the CALLER's ns-free declaration find (the content
 * model child loop's leptris#1958 semantics) — NULL = resolve here. */
static void validate_element_decl(struct xsd_validator* v,
                                  struct leptris_xsd_schema* s,
                                  LeptrisElement elem, int depth,
                                  const XsdElementDecl* decl_override) {
    if (depth > 64) return;
    const char* local = NULL, *prefix = NULL, *uri = NULL;
    leptris_element_expanded_name(elem, &local, &prefix, &uri);
    if (!local) return;
    const char* guri = NULL;
    leptris_element_expanded_name(elem, NULL, NULL, &guri);
    const XsdElementDecl* d =
        decl_override ? decl_override
                      : xsd_find_element_ns(s, local, guri);
    if (!d) {
        /* strict: the instance uses an element the schema never
         * declared (a local particle would have typed it) */
        verrf_atk(v, elem, "element-decl", local, "element", local,
                  "not declared (strict)");
        return;
    }

    /* XSD 1.1: the first alternative whose test passes types the
     * element for this instance (context = the element); a test-less
     * default alternative replaces the declared type as the
     * no-match fallback. */
    int cta_err = 0;
    XsdAlternative* cta_alt = NULL;
    const char* eff_type =
        xsd_cta_select(v, s, elem, d->alternatives, &cta_err, &cta_alt);
    if (!eff_type) eff_type = d->type;
    /* #1936 (stE): the element's fixed VALUE constraint applies
     * regardless of the type resolution — inline anonymous types
     * leave eff_type NULL and used to skip the check entirely.
     * XSD 1.0 §3.12.2/§4.3.1.2 (leptris-rs#25, isDefault001): an
     * element with EMPTY content is satisfied BY the fixed value —
     * only non-empty content that differs violates (nine suite
     * rows flipped on the 1.9.402 ride; whitespace-only counts as
     * empty per the §3.3.4 initial reading). */
    const char* fill = NULL;
    {
        const char* text = leptris_element_text(elem);
        int has_content = 0;
        if (text)
            for (const char* q = text; *q; q++)
                if (!isspace((unsigned char)*q)) {
                    has_content = 1;
                    break;
                }
        if (d->fixed) {
            int match = !has_content || strcmp(text, d->fixed) == 0;
            if (!match) {
                const XsdSimple* ut =
                    d->type ? xsd_find_simple_ns_pub(s, d->type, d->ns)
                            : NULL;
                if (ut && ut->deriv == XSD_SIMPLE_LIST)
                    match = xsd_list_fixed_match(s, ut, d->ns, d->fixed,
                                                 text);
                else if (ut)
                    match = xsd_union_fixed_match(s, ut, d->ns, d->fixed,
                                                  text);
            }
            if (!match)
                verrfk(v, "content-model", "element", local,
                   "content does not match the fixed value");
            if (!has_content)
                fill = d->fixed;
        } else if (!has_content && d->default_value) {
            /* leptris-rs#35 (stZ072): the default value supplies the
             * content of an empty element (§3.12.2). */
            fill = d->default_value;
        }
    }
    if (!eff_type) return; /* anyType */
    /* XSD 1.0 §2.6.2/§3.3.4 (moxml#433): xsi:type on the instance
     * names the type definition this element is assessed against —
     * it replaces the declared type and wins over XSD 1.1
     * conditional alternatives. The QName resolves through the
     * element's in-scope namespace declarations; derivation-ok from
     * the declared type is not walked (tier-1 lax direction: the
     * named type is validated as given). */
    const char* xsi_q = leptris_element_attribute_ns(
        elem, "http://www.w3.org/2001/XMLSchema-instance", "type");
    if (xsi_q && xsi_q[0]) {
        char xuri[512], xlocal[256];
        xsd_resolve_qname_inst(elem, xsi_q, xuri, sizeof(xuri),
                               xlocal, sizeof(xlocal));
        if (!xlocal[0]) {
            verrf_atk(v, elem, "type-resolution", local, "element",
                      local,
                      "xsi:type prefix is not bound in the instance");
            return;
        }
        validate_local_uri(v, s, elem, local, xlocal, xuri, xsi_q,
                           fill, NULL);
        return;
    }
    /* §3.3.4.3 cl.4: the instance-specified type definition above
     * overrides the conditionally selected one; only without it does
     * an xs:error selection (no valid instances, §2.2.1.1) take
     * effect. */
    if (cta_err) {
        verrf_atk(v, elem, "type-resolution", local, "element", local,
                  "conditional type assignment selected xs:error");
        return;
    }
    /* moxml#579: the selected alternative's test-referenced attribute
     * names pass the strict attribute check undeclared (§3.12.4).
     * Set only for this element's assessment — the xsi:type path
     * above replaced the whole chain, so no exemption there. */
    {
        char** prev_exempt = v->cta_exempt;
        int prev_n = v->cta_exempt_n;
        v->cta_exempt =
            cta_alt ? cta_alt->test_attrs : NULL;
        v->cta_exempt_n = cta_alt ? cta_alt->test_attr_n : 0;
        validate_local(v, s, elem, local, eff_type, fill,
                       d->ns);
        v->cta_exempt = prev_exempt;
        v->cta_exempt_n = prev_n;
    }
}

/* Validate one element against a type name that comes from a
 * global declaration or a local particle: attributes and content
 * model for complex types, lexical text otherwise. */
static void validate_local(struct xsd_validator* v,
                           struct leptris_xsd_schema* s,
                           LeptrisElement elem, const char* local,
                           const char* type, const char* fill,
                           const char* tns_hint);
static void validate_local_uri(struct xsd_validator* v,
                               struct leptris_xsd_schema* s,
                               LeptrisElement elem, const char* local,
                               const char* type, const char* euri,
                               const char* xsi_q, const char* fill,
                               const char* tns_hint);

static void validate_local(struct xsd_validator* v,
                           struct leptris_xsd_schema* s,
                           LeptrisElement elem, const char* local,
                           const char* type, const char* fill,
                           const char* tns_hint) {
    const char* euri = NULL;
    leptris_element_expanded_name(elem, NULL, NULL, &euri);
    validate_local_uri(v, s, elem, local, type, euri, NULL, fill,
                       tns_hint);
}

static void validate_local_uri(struct xsd_validator* v,
                               struct leptris_xsd_schema* s,
                               LeptrisElement elem, const char* local,
                               const char* type, const char* euri,
                               const char* xsi_q, const char* fill,
                               const char* tns_hint) {
    if (!type) return;
    /* leptris#1958: the DECLARING unit's targetNamespace binds the
     * type reference first — a no-targetNamespace declaration's
     * unprefixed type must resolve to ITS OWN unit's component, not
     * the importing namespace's same-name type (schF1). The
     * instance-ns reference ladder remains the fallback (OOXML:
     * wp:extent -> a:CT_PositiveSize2D — #1687). */
    XsdCm* ct = tns_hint ? xsd_find_complex_ns(s, type, tns_hint) : NULL;
    if (!ct && (!euri || !euri[0]))
        ct = xsd_find_complex_ns_free_first(s, type);
    if (!ct) ct = xsd_find_complex_ref(s, type, euri);
    /* XSD 1.0 §2.7/§3.3.3 (leptris-rs#26, particlesIa002/
     * QFE1700g2): a NILLED element must have neither character nor
     * element content — the content model is not applied (the nil
     * value replaces it; the oracle rejects whitespace too).
     * Attributes are unaffected by nil and still validate. */
    const char* nilattr = leptris_element_attribute_ns(
        elem, "http://www.w3.org/2001/XMLSchema-instance", "nil");
    if (nilattr && strcmp(nilattr, "true") == 0) {
        const char* ntext = leptris_element_text(elem);
        if (ntext && *ntext)
            verrfk(v, "content-model", "element", local,
                   "character content is not allowed: the element "
                   "is nilled");
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(elem));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            verrfk(v, "content-model", "element", local,
                   "element content is not allowed: the element "
                   "is nilled");
            break;
        }
        if (ct) {
            const XsdTypeAttrs* nrows =
                xsd_find_type_attrs_ns(s, type, euri);
            if (!nrows) nrows = xsd_find_type_attrs(s, type);
            validate_attributes(v, s, elem, nrows, ct);
        } else {
            static const XsdTypeAttrs k_no_attrs;
            validate_attributes(v, s, elem, &k_no_attrs, NULL);
        }
        return;
    }
    if (ct) {
        const XsdTypeAttrs* rows =
            xsd_find_type_attrs_ns(s, type, euri);
        if (!rows) rows = xsd_find_type_attrs(s, type);
        if (!rows) {
            /* #2182: an attribute-less complexType runs the strict
             * walk with zero declared rows — anything not
             * xsi-admitted is invalid (the open044.n1 shape: the
             * included doc's bare alpha rejects xml:lang). */
            static const XsdTypeAttrs k_no_attrs;
            rows = &k_no_attrs;
        }
        validate_attributes(v, s, elem, rows, ct);
        validate_children(v, s, elem, ct);
        /* XSD 1.1 §3.13.6: {assertions} accumulate down the
         * derivation chain — t3 extending t2 restricting t1 runs
         * t1's and t2's asserts too (assert007's chess .n rows
         * hinge on exactly that: the every-checks live on t1, the
         * instance types t3). #2154 walks the ext/res base chain;
         * depth-capped like the attribute-admission walk (the
         * corpus's cyclic redefine shapes must not spin here). */
        int chain_hops = 0;
        for (XsdCm* t = ct; t; ) {
            xsd_eval_assert_list(v, s, t, elem, local, fill);
            XsdCm* next = NULL;
            if (t->ext_base)
                next = xsd_find_complex_ns(s, t->ext_base,
                                           t->ext_base_ns
                                               ? t->ext_base_ns
                                               : xsd_target_ns(s));
            else if (t->res_base)
                next = xsd_find_complex_ns(s, t->res_base,
                                           t->res_base_ns
                                               ? t->res_base_ns
                                               : xsd_target_ns(s));
            if (!next && t->ext_base)
                next = xsd_find_complex(s, t->ext_base);
            if (!next && t->res_base)
                next = xsd_find_complex(s, t->res_base);
            if (!next && t->assert_base)
                next = xsd_find_complex_ns(s, t->assert_base,
                                           t->assert_base_ns
                                               ? t->assert_base_ns
                                               : xsd_target_ns(s));
            if (!next && t->assert_base)
                next = xsd_find_complex(s, t->assert_base);
            t = next;
            if (++chain_hops > 8) break;
        }
        if (ct->text_type) {
            /* empty content is still a lexical value — it faces the
             * facets (reG17: '' must fail the pattern). leptris-rs#35:
             * an EMPTY element is satisfied BY its fixed/default
             * value (§3.12.2) — the fill substitutes for the read. */
            const char* text = leptris_element_text(elem);
            if (!text) text = "";
            if (fill && !*text) text = fill;
            text_valid(v, s, ct->text_type, text, ct->ns,
                       "element", local, elem);
        }
        return;
    }

    /* the ur-types accept any content and any attributes — no
     * constraints at all (include2_0's xsd:anyType particle carries
     * a qualified attribute the strict walk would reject) */
    if (strcmp(type, "anyType") == 0 ||
        strcmp(type, "xs:anyType") == 0 ||
        strcmp(type, "anySimpleType") == 0 ||
        strcmp(type, "xs:anySimpleType") == 0)
        return;

    /* simple-typed element: attributes face the strict walk (no
     * declared rows), text must be lexically valid */
    if (strncmp(type, "xs:", 3) == 0 ||
        xsd_find_simple_pub(s, type)) {
        static const XsdTypeAttrs k_no_attrs;
        validate_attributes(v, s, elem, &k_no_attrs, NULL);
        const char* text = leptris_element_text(elem);
        if (!text) text = "";
        if (fill && !*text) text = fill;
        text_valid(v, s, type, text, euri, "element", local,
                   elem);
        return;
    }
    /* libxml2-compat (#1691 ns1_0/seq-dubl-elem1): an UNPREFIXED
     * type reference whose local name matches a built-in datatype
     * resolves to it (type='string', type='base64Binary' in
     * namespaced schemas). Per QName rules the name is
     * namespace-less; the strict ladder correctly fails it — this
     * fallback is the oracle's reading, and only fires when the
     * name IS a builtin (anything else stays loud below). */
    if (type[0] != ':') {
        char builtin[256];
        if (snprintf(builtin, sizeof(builtin), "xs:%s", type) <
                (int)sizeof(builtin) &&
            xsd_builtin_known(builtin)) {
            static const XsdTypeAttrs k_no_attrs;
            validate_attributes(v, s, elem, &k_no_attrs, NULL);
            const char* text = leptris_element_text(elem);
            if (!text) text = "";
            if (fill && !*text) text = fill;
            text_valid(v, s, builtin, text, euri, "element",
                       local, elem);
            return;
        }
    }
    /* neither complex nor simple nor builtin: the schema is
     * incomplete — loud, never a silent accept (#1615 class) */
    if (xsi_q) {
        char msg[352];
        snprintf(msg, sizeof(msg),
                 "xsi:type '%s' does not resolve to a type in the "
                 "schema",
                 xsi_q);
        verrf_atk(v, elem, "type-resolution", local, "element", local,
                  msg);
        return;
    }
    verrf_atk(v, elem, "type-resolution", local, "element", local,
              "type not found in the schema");
}

/* ---- slice 6: identity constraints -------------------------------- */

/* A key tuple: the field values of one selected node. */
typedef struct xsd_key_tuple {
    char* values[8];
    size_t count;
    struct xsd_key_tuple* next;
} XsdKeyTuple;

/* All tuples collected for one constraint name, keyed by name. */
typedef struct xsd_key_table {
    char* name;
    XsdKeyTuple* tuples;
    XsdKeyTuple* tail;
    struct xsd_key_table* next;
} XsdKeyTable;

static void tables_free(XsdKeyTable* t);

static XsdKeyTable* table_find(XsdKeyTable* head, const char* name) {
    for (; head; head = head->next)
        if (strcmp(head->name, name) == 0) return head;
    return NULL;
}

static char* xsd_dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

/* Ensure the named table exists (possibly empty) — §3.11.5: an
 * eligible constraint contributes a node table even when its target
 * node set is empty, so a keyref against it still has a table to
 * fail in (#2156). */
static void table_ensure(XsdKeyTable** head, const char* name) {
    if (table_find(*head, name)) return;    XsdKeyTable* t = (XsdKeyTable*)calloc(1, sizeof(*t));
    if (!t) return;
    size_t n = strlen(name) + 1;
    t->name = (char*)malloc(n);
    if (!t->name) {
        free(t);
        return;
    }
    memcpy(t->name, name, n);
    t->next = *head;
    *head = t;
}

static void table_add(XsdKeyTable** head, const char* name,
                      char* const* values, size_t count) {
    table_ensure(head, name);
    XsdKeyTable* t = table_find(*head, name);
    if (!t) return;
    XsdKeyTuple* tup = (XsdKeyTuple*)calloc(1, sizeof(*tup));
    if (!tup) return;
    for (size_t i = 0; i < count && i < 8; i++)
        tup->values[i] = xsd_dup_str(values[i]);
    tup->count = count < 8 ? count : 8;
    if (t->tail)
        t->tail->next = tup;
    else
        t->tuples = tup;
    t->tail = tup;
}

/* ---- #2156: the §3.11.6 subset walker ---------------------------- */

/* Does one normalized step match element e? The step's namespace was
 * resolved at capture time against the SCHEMA document — instance
 * namespaces are only ever compared, never consulted. */
static int ic_step_elem_match(const XsdIcStep* st, LeptrisElement e) {
    if (st->any_ns || st->is_self) return 1;
    const char* local = NULL, *uri = NULL;
    leptris_element_expanded_name(e, &local, NULL, &uri);
    if (!st->local) return 0;
    if (!local || strcmp(local, st->local) != 0) return 0;
    const char* have = (uri && uri[0]) ? uri : NULL;
    if (!st->ns) return have == NULL;
    return have && strcmp(st->ns, have) == 0;
}

typedef struct ic_nodes {
    LeptrisElement* v;
    size_t n;
    size_t cap;
} ic_nodes;

static void ic_nodes_push(ic_nodes* l, LeptrisElement e) {
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        LeptrisElement* nv =
            (LeptrisElement*)realloc(l->v, nc * sizeof(*nv));
        if (!nv) return;
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n++] = e;
}

/* Walk one path branch from ctx: steps[depth] applies to ctx's
 * children; a node whose match completes the path is pushed. */
static void ic_collect(LeptrisElement ctx, const XsdIcPath* p, int depth,
                       ic_nodes* out) {
    if (depth == p->step_count) {
        ic_nodes_push(out, ctx);
        return;
    }
    const XsdIcStep* st = &p->steps[depth];
    if (st->is_self) {
        ic_collect(ctx, p, depth + 1, out);
        return;
    }
    if (st->is_attr) return; /* selectors carry no attribute steps */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(ctx));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        if (ic_step_elem_match(st, ce)) ic_collect(ce, p, depth + 1, out);
    }
}

/* The leading './/' seed: a descendant that matches the FIRST step
 * continues the chain through its own children (XPath descendant:: —
 * the seed node itself is the step-0 match, not a new context). */
static void ic_collect_seed(LeptrisElement d, const XsdIcPath* p,
                            ic_nodes* out) {
    if (!ic_step_elem_match(&p->steps[0], d)) return;
    if (p->step_count == 1) {
        ic_nodes_push(out, d);
        return;
    }
    ic_collect(d, p, 1, out);
}

static void ic_collect_branch(LeptrisElement ctx, const XsdIcPath* p,
                              ic_nodes* out) {
    if (!p->desc) {
        ic_collect(ctx, p, 0, out);
        return;
    }
    /* every descendant (never the context itself), exactly once */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(ctx));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        ic_collect_seed(ce, p, out);
        ic_collect_branch(ce, p, out);
    }
}

/* One field value relative to a selected node; NULL = no node
 * matched (§3.11.4 cl. 3: the key-sequence is shorter and the node
 * leaves the qualified set). */
static char* ic_field_walk(LeptrisElement node, const XsdIcPath* p,
                           int depth) {
    for (; depth < p->step_count; depth++) {
        const XsdIcStep* st = &p->steps[depth];
        if (st->is_self) continue;
        if (st->is_attr) {
            for (LeptrisAttribute a =
                     leptris_element_first_attribute(node);
                 a; a = leptris_attribute_next(a)) {
                const char* raw = leptris_attribute_get_name(a);
                if (!raw) continue;
                const char* colon = strchr(raw, ':');
                const char* al = colon ? colon + 1 : raw;
                if (st->any_ns)
                    return xsd_dup_str(
                        leptris_attribute_get_value(node, a));
                if (!st->local || strcmp(al, st->local) != 0) continue;
                if (st->ns) {
                    const char* au = leptris_attribute_namespace_uri(a);
                    if (!au || strcmp(au, st->ns) != 0) continue;
                } else if (colon) {
                    /* '@x' never matches a prefixed attribute */
                    continue;
                }
                return xsd_dup_str(leptris_attribute_get_value(node, a));
            }
            return NULL;
        }
        LeptrisElement hit = NULL;
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(node));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement ce = (LeptrisElement)c;
            if (ic_step_elem_match(st, ce)) {
                hit = ce;
                break;
            }
        }
        if (!hit) return NULL;
        node = hit;
    }
    return xsd_dup_str(leptris_element_text(node));
}

static char* ic_field_value(LeptrisElement node, const XsdIcPath* p) {
    if (!p->desc) return ic_field_walk(node, p, 0);
    /* './/' in a field: the first descendant matching the FIRST step
     * continues the chain (same seed semantics as the selector) */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(node));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        char* got = NULL;
        if (ic_step_elem_match(&p->steps[0], ce))
            got = ic_field_walk(ce, p, 1);
        if (!got) got = ic_field_value(ce, p);
        if (got) return got;
    }
    return NULL;
}

/* Evaluate one IC against the scoping element. pass1: collect
 * key/unique tuples and enforce uniqueness (key: also non-empty
 * fields). pass2: keyref tuples resolve against collected tables.
 * The selector/field paths are pre-normalized at capture with
 * SCHEMA-side namespace resolution (#2156) — the general XPath
 * engine (whose prefix context is the INSTANCE's) is no longer
 * consulted, which is what silently skipped every namespaced
 * constraint before. */
static void validate_ics(struct xsd_validator* v,
                         struct leptris_xsd_schema* s,
                         LeptrisElement elem, const char* elem_name,
                         XsdKeyTable** tables, int pass) {
    const XsdElementIcs* ics = xsd_find_element_ics(s, elem_name);
    if (!ics) return;
    /* §3.11.4: uniqueness is judged within ONE scoping element
     * instance's qualified node set — chap{1,2,3,4} + appx{1,2,3,4}
     * (id040.v01) is legal. The keyref NODE TABLE stays a
     * document-wide union (§3.11.5's descendant merge). */
    XsdKeyTable* scope = NULL;
    for (XsdIc* ic = ics->constraints; ic; ic = ic->next) {
        int is_ref = ic->kind == 2;
        if (pass == 1 && is_ref) continue;
        if (pass == 2 && !is_ref) continue;
        if (!ic->name || ic->sel_count == 0) continue;
        if (pass == 1) {
            /* §3.11.5: the (possibly EMPTY) node table exists for
             * every key/unique — a keyref against it must fail. */
            table_ensure(tables, ic->name);
        }
        ic_nodes sel = {NULL, 0, 0};
        for (int b = 0; b < ic->sel_count; b++)
            ic_collect_branch(elem, &ic->sel[b], &sel);
        for (size_t i = 0; i < sel.n; i++) {
            LeptrisElement node = sel.v[i];
            if (!node) continue;
            /* §3.11.4 cl. 1: skipped nodes leave the target node
             * set (wild101: the note inside skip content). */
            if (ic_is_skipped(v, node)) continue;
            /* field values: string of each field expression */
            char* vals[8] = {0};
            size_t nv = 0;
            int missing = 0;
            for (size_t f = 0; f < ic->field_count && f < 8; f++) {
                char* sv = ic_field_value(node, &ic->fpaths[f]);
                if (!sv) {
                    missing = 1;
                    break;
                }
                vals[nv++] = sv;
            }
            for (size_t k = 0; k < nv; k++)
                if (!vals[k] || !vals[k][0]) missing = 1;
            if (missing) {
                if (ic->kind == 0)
                    verrfk(v, "identity-constraint", "key", ic->name,
                           "a selected node is missing a field value");
                for (size_t k = 0; k < nv; k++) free(vals[k]);
                continue;
            }
            if (pass == 1) {
                /* uniqueness within THIS scoping instance */
                XsdKeyTable* t = table_find(scope, ic->name);
                int dup = 0;
                if (t) {
                    for (XsdKeyTuple* tup = t->tuples; tup;
                         tup = tup->next) {
                        int eq = tup->count == nv;
                        for (size_t k = 0; eq && k < nv; k++)
                            if (strcmp(tup->values[k], vals[k]) != 0)
                                eq = 0;
                        if (eq) {
                            dup = 1;
                            break;
                        }
                    }
                }
                if (dup) {
                    verrfk(v, "identity-constraint",
                           ic->kind == 0 ? "key" : "unique",
                           ic->name, "duplicate key tuple");
                }
                table_add(&scope, ic->name, vals, nv);
                table_add(tables, ic->name, vals, nv);
            } else {
                /* keyref: resolve later (deferred to pass 2 end) —
                 * store under the REFER name */
                table_add(tables, ic->refer ? ic->refer : "",
                          vals, nv);
            }
            for (size_t k = 0; k < nv; k++) free(vals[k]);
        }
        free(sel.v);
    }
    tables_free(scope);
}

/* Pass 2 tail: every keyref tuple must appear in the referenced
 * key/unique table. */
static void resolve_keyrefs(struct xsd_validator* v,
                            struct leptris_xsd_schema* s,
                            XsdKeyTable* all, XsdKeyTable* refs) {
    (void)s;
    for (XsdKeyTable* rt = refs; rt; rt = rt->next) {
        XsdKeyTable* target = table_find(all, rt->name);
        if (!target) {
            /* §3.11.4 cl. 4.3: the referenced key's node table must
             * be associated with the scoping element — a missing
             * table satisfies nothing (the old lax skip silently
             * accepted every dangling keyref, #2156). */
            verrfk(v, "identity-constraint", "keyref", rt->name,
                   "reference target has no node table");
            continue;
        }
        for (XsdKeyTuple* want = rt->tuples; want; want = want->next) {
            int found = 0;
            for (XsdKeyTuple* have = target->tuples; have;
                 have = have->next) {
                int eq = have->count == want->count;
                for (size_t k = 0; eq && k < want->count; k++)
                    if (strcmp(have->values[k], want->values[k]) != 0)
                        eq = 0;
                if (eq) { found = 1; break; }
            }
            if (!found)
                verrfk(v, "identity-constraint", "keyref", rt->name,
                      "reference does not resolve to any key");
        }
    }
}

static void tables_free(XsdKeyTable* t) {
    while (t) {
        XsdKeyTable* n = t->next;
        XsdKeyTuple* tup = t->tuples;
        while (tup) {
            XsdKeyTuple* tn = tup->next;
            for (size_t k = 0; k < tup->count; k++) free(tup->values[k]);
            free(tup);
            tup = tn;
        }
        free(t->name);
        free(t);
        t = n;
    }
}

/* Walk every element carrying constraints in both passes. */
static void ic_walk(struct xsd_validator* v, struct leptris_xsd_schema* s,
                    LeptrisElement elem, XsdKeyTable** keys,
                    XsdKeyTable** refs, int pass) {
    const char* local = NULL;
    leptris_element_expanded_name(elem, &local, NULL, NULL);
    if (local) validate_ics(v, s, elem, local, pass == 1 ? keys : refs, pass);
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        ic_walk(v, s, (LeptrisElement)c, keys, refs, pass);
    }
}

struct xsd_validator* xsd_validator_new(struct leptris_xsd_schema* s) {
    struct xsd_validator* v =
        (struct xsd_validator*)calloc(1, sizeof(*v));
    if (v) v->s = s;
    return v;
}

void xsd_validator_free(struct xsd_validator* v) {
    if (!v) return;
    struct ic_skipped* sk = v->skipped;
    while (sk) {
        struct ic_skipped* sn = sk->next;
        free(sk);
        sk = sn;
    }
    struct xsd_error* e = v->errors;
    while (e) {
        struct xsd_error* n = e->next;
        free(e->text);
        free(e);
        e = n;
    }
    free(v);
}

int xsd_validator_run(struct xsd_validator* v, LeptrisDocument doc) {
    if (!v || !doc) return -1;
    v->doc = doc; /* XSD 1.1: assertion evaluation context */
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return -1;
    validate_element(v, v->s, root, 0);

    /* slice 6: identity constraints — collect (pass 1), resolve
     * keyrefs (pass 2). */
    XsdKeyTable* keys = NULL;
    XsdKeyTable* refs = NULL;
    ic_walk(v, v->s, root, &keys, &refs, 1);
    ic_walk(v, v->s, root, &keys, &refs, 2);
    resolve_keyrefs(v, v->s, keys, refs);
    tables_free(keys);
    tables_free(refs);

    return v->count == 0 ? 1 : 0;
}

size_t xsd_validator_error_count(struct xsd_validator* v) {
    return v ? v->count : 0;
}

const char* xsd_validator_error_at(struct xsd_validator* v, size_t i) {
    if (!v) return NULL;
    struct xsd_error* e = v->errors;
    for (size_t k = 0; e && k < i; k++) e = e->next;
    return e ? e->text : NULL;
}

/* #1841: the whole node — the report materializer reads the
 * structured fields beside the text. */
const struct xsd_error* xsd_validator_error_entry(
    struct xsd_validator* v, size_t i) {
    if (!v) return NULL;
    struct xsd_error* e = v->errors;
    for (size_t k = 0; e && k < i; k++) e = e->next;
    return e;
}
