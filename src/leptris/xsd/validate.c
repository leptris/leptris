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
#include "../../include/leptris/dtd.h"
#include "xsd_internal.h"
#include "../dom/element.h"

extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count,
                             int* oc_from, unsigned char* oc_attr,
                             unsigned char* wc_attr,
                             XsdCm** hit_particle);
extern int xsd_builtin_valid(const char* type, const char* v);
extern int xsd_builtin_known(const char* type);
extern int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                                  const XsdSimple* t, const char* v);
/* #2184: the §3.2.5 unparsed-entity integrity — ENTITY/ENTITIES
 * values resolve against the instance's DTD entity table (the
 * notation_name marks an UNPARSED entity). Internal dtd model. */
#include "../dtd/model.h"



struct leptris_xsd_schema; /* opaque here; compile.c owns the struct */

/* The validator reaches the compiled model through accessors the
 * compiler exports (compile.c defines the struct). */
extern const XsdElementDecl* xsd_find_element_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri);
extern const XsdElementDecl* xsd_find_element(
    struct leptris_xsd_schema* s, const char* name);
/* #2184: the facet-free union-member path (defined below) — the
 * weak-wildcard governing-type law reads it (a member of a
 * facet-free union is validly derived from the union). */
static int xsd_union_member_path_fwd(struct leptris_xsd_schema* s,
                                     const char* union_name,
                                     const char* target);
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
extern int xsd_any_attr_matches(const char* grammar, const char* uri,
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
    /* #2184 §3.3.5 / §C.4: the ID bookkeeping — TableA (the
     * uniqueness entries) and the pool (every ID and IDREF value seen
     * this run). Chaining hash; carriers are borrowed DOM pointers. */
    struct id_seen* id_buckets[128];
    struct id_ref* id_pool;
    struct id_ref* id_pool_tail;
    /* #2184 §3.11.4: the type each validated element was ASSESSED
     * against (validate_local_uri is the chokepoint) — the keyref
     * tuple comparison reads the field terminal's type from here to
     * compare in the VALUE space (zone206-302). Names are owned
     * copies; the element pointers are borrowed DOM refs. */
    struct ic_type_rec* type_recs;
    size_t type_rec_n, type_rec_cap;
};

struct ic_type_rec {
    LeptrisElement e;
    char* type;
};

/* #2184: a string copy helper for the map above (declared before
 * its first use at the record sites). */
static char* xsd_dup_str(const char* s);

/* #2184: record the assessed type of one element (last write
 * wins). */
static void xsd_record_element_type(struct xsd_validator* v,
                                    LeptrisElement elem,
                                    const char* type) {
    if (!v || !elem || !type || !type[0]) return;
    for (size_t i = 0; i < v->type_rec_n; i++)
        if (v->type_recs[i].e == elem) {
            free(v->type_recs[i].type);
            v->type_recs[i].type = xsd_dup_str(type);
            return;
        }
    if (v->type_rec_n == v->type_rec_cap) {
        size_t nc = v->type_rec_cap ? v->type_rec_cap * 2 : 64;
        struct ic_type_rec* nr = (struct ic_type_rec*)realloc(
            v->type_recs, nc * sizeof(*nr));
        if (!nr) return;
        v->type_recs = nr;
        v->type_rec_cap = nc;
    }
    v->type_recs[v->type_rec_n].e = elem;
    v->type_recs[v->type_rec_n].type = xsd_dup_str(type);
    v->type_rec_n++;
}

/* The recorded type of a validated element (NULL when the main
 * walk never typed it — wildcard-admitted and undeclared elements
 * stay string-compared). */
static const char* xsd_recorded_type(struct xsd_validator* v,
                                     LeptrisElement elem) {
    if (!v || !elem) return NULL;
    for (size_t i = 0; i < v->type_rec_n; i++)
        if (v->type_recs[i].e == elem) return v->type_recs[i].type;
    return NULL;
}

/* One TableA entry: a value that participates in ID uniqueness.
 * Attribute-carried IDs scope to the document (id010.n01: the
 * defaulted para001 and the explicit para001 on two paras conflict);
 * content-carried IDs scope to the carrier's PARENT (id003.v01:
 * sibling <id>zzz</id> twins are one denotation; id008.v02: the same
 * value under two different parents is a duplicate). */
struct id_seen {
    char* v;
    LeptrisElement carrier;
    int is_content;
    struct id_seen* next;
};

/* One pool entry: every ID or IDREF value seen this run, in
 * document order. At the end of the run every is_ref entry must
 * match some !is_ref entry (§C.4 integrity — ii04-ii18, id005-008). */
struct id_ref {
    char* v;
    int is_ref;
    LeptrisElement carrier;
    struct id_ref* next;
};

/* #2184: record an ID value in TableA; 1 = ok, 0 = the value
 * repeats from a different scope (the document is invalid). A repeat
 * from the SAME scope (one element's two attributes, one parent's
 * sibling content) is the same denotation — legal. */
static int xsd_id_value_record(struct xsd_validator* v, const char* val,
                               LeptrisElement carrier, int via_content) {
    if (!v || !val || !val[0]) return 1;
    LeptrisElement scope = carrier;
    if (via_content && carrier)
        scope = leptris_element_parent(carrier);
    unsigned h = 2166136261u;
    for (const char* p = val; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 16777619u;
    }
    struct id_seen** b = &v->id_buckets[h % 128];
    for (struct id_seen* n = *b; n; n = n->next) {
        if (strcmp(n->v, val) != 0) continue;
        if (n->is_content != via_content) continue;
        if (n->carrier == scope) return 1; /* the same denotation */
        return 0; /* a different scope carried the same ID */
    }
    struct id_seen* n = (struct id_seen*)malloc(sizeof(*n));
    if (!n) return 1;
    n->v = strdup(val);
    if (!n->v) {
        free(n);
        return 1;
    }
    n->carrier = scope;
    n->is_content = via_content;
    n->next = *b;
    *b = n;
    return 1;
}

/* #2184: pool one value (is_ref = an IDREF awaiting resolution). */
static void xsd_id_pool_record(struct xsd_validator* v, const char* val,
                               LeptrisElement carrier, int is_ref) {
    if (!v || !val || !val[0]) return;
    struct id_ref* n = (struct id_ref*)malloc(sizeof(*n));
    if (!n) return;
    n->v = strdup(val);
    if (!n->v) {
        free(n);
        return;
    }
    n->is_ref = is_ref;
    n->carrier = carrier;
    n->next = NULL;
    if (v->id_pool_tail)
        v->id_pool_tail->next = n;
    else
        v->id_pool = n;
    v->id_pool_tail = n;
}

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
     * $value binds the typed value; #2183 runs the facet lane
     * ABSENT FOCUS (§3.16.4 — context item, position and last are
     * undefined; referencing them raises XPDY0002 and the facet
     * does not hold). */
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
 * cannot COMPILE or EVALUATE is not enforced; #2183 refines the
 * error half — an ASSESSED dynamic error (the eval reports a spec
 * error code: FOAR0001 div-by-zero, FODC0002 doc() miss, XPDY0002)
 * fails the assertion (§3.13.4), while an unassessable expression
 * (no code) stays not-enforced. #2183 also roots '/' and '//' at
 * the VALIDATED ELEMENT's subtree (§3.13.4 — d4_3_15ii14/15). A
 * compiled query that evaluates FALSE still fails the element.
 * Returns the number of FAILED asserts. */
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
            char ecode[32];
            LeptrisXPathResult r = leptris_xquery_eval_params_ec(
                a->compiled, v->doc, elem, names, sels, 1,
                (unsigned)(LEPTRIS_XQUERY_ASSERT_DATA_MODEL |
                           LEPTRIS_XQUERY_ASSERT_ROOTED),
                ecode, sizeof(ecode));
            if (r) {
                ebv = leptris_xpath_result_boolean(r);
                evaluated = 1;
                leptris_xpath_result_free(r);
            } else if (ecode[0]) {
                /* #2183 §3.13.4: an assessed dynamic error (FOAR0001
                 * div-by-zero, FODC0002 doc() miss, XPDY0002) fails
                 * the assertion — the instance is invalid, never a
                 * crash */
                evaluated = 1;
            }
        } else {
            const char* names[1] = {"value"};
            const char* sels[1] = {"()"};
            char ecode[32];
            LeptrisXPathResult r = leptris_xquery_eval_params_ec(
                a->compiled, v->doc, elem, names, sels, 1,
                (unsigned)(LEPTRIS_XQUERY_ASSERT_DATA_MODEL |
                           LEPTRIS_XQUERY_ASSERT_ROOTED),
                ecode, sizeof(ecode));
            if (r) {
                ebv = leptris_xpath_result_boolean(r);
                evaluated = 1;
                leptris_xpath_result_free(r);
            } else if (ecode[0]) {
                evaluated = 1;   /* assessed error = assertion false */
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

/* Float/double value-space equality (moxml#588, #2207): the
 * specials compare as VALUES, not spellings — "INF"/"+INF" are one
 * value (overflow forms like "1E999" land there too), "-INF" is
 * its own, and NaN identifies with itself (the simple022 identity
 * reading — §3.2.5 spells NaN one way, enumeration admits it).
 * Finite values compare numerically, so the exponential spellings
 * agree. Both lexicals must be xs:double-shaped first: strtod
 * alone would admit the C hex/infinity spellings the datatype
 * rejects. */
static int xsd_float_double_value_equal(const char* a, const char* b) {
    if (!xsd_builtin_valid("xs:double", a) ||
        !xsd_builtin_valid("xs:double", b))
        return 0;
    int na = strcmp(a, "NaN") == 0;
    int nb = strcmp(b, "NaN") == 0;
    if (na || nb) return na && nb;
    return strtod(a, NULL) == strtod(b, NULL);
}

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
        return xsd_float_double_value_equal(a, b);
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
                if (xsd_any_attr_matches(grammar, uri, unit)) {
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
        if (xsd_any_attr_matches(grammar, uri, unit)) {
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

/* #2184: the xsi:type override law — declared below (§3.3.4.3).
 * strict=1 (the wildcard-slot excess position) skips the cast arm. */
static int xsi_type_override_fails_pos(struct leptris_xsd_schema* s,
                                       const char* xlocal,
                                       const char* xuri,
                                       const char* sel_type,
                                       const char* euri,
                                       const char* tns_hint,
                                       int strict);

/* #2184: is THIS child beyond the model's window for its own name?
 * The count of the model tree's same-name element particles against
 * the count of same-name preceding siblings + this one (wild062's
 * second f rides the wildcard slot; the single v of
 * XsiTypeSimpleTypeRestrictsText does not). */
static int xsd_child_exceeds_model(struct leptris_xsd_schema* s,
                                   XsdCm* model, LeptrisElement ce,
                                   const char* cl) {
    int model_n = 0;
    if (model) {
        XsdCm* stack[64];
        int sp = 0;
        stack[sp++] = model;
        while (sp > 0) {
            XsdCm* p = stack[--sp];
            for (; p; p = p->next) {
                if (p->kind == XSD_CM_ELEMENT && p->name &&
                    strcmp(p->name, cl) == 0)
                    model_n++;
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
    int inst_n = 1; /* this child */
    for (LeptrisNodeRef n = leptris_node_previous_sibling(
             leptris_element_as_node(ce));
         n; n = leptris_node_previous_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* nl = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &nl, NULL,
                                      NULL);
        if (nl && strcmp(nl, cl) == 0) inst_n++;
    }
    return inst_n > model_n;
}

/* #2184: the xsi:type override law — declared below (§3.3.4.3). */
static int xsi_type_override_fails(struct leptris_xsd_schema* s,
                                   const char* xlocal, const char* xuri,
                                   const char* sel_type, const char* euri,
                                   const char* tns_hint);

/* #2184 (§3.4.4.3 dynamic EDC): the same-name element particle's
 * TYPE through the assessed type's DERIVATION chain — the governing
 * declaration the wildcard-resolved global must derive from. Depth-
 * and hop-capped; particles without a declared type return NULL
 * (posture). */
static const char* xsd_chain_particle_type(struct leptris_xsd_schema* s,
                                           XsdCm* ct, const char* cl,
                                           const char* cu) {
    int hops = 0;
    XsdCm* cm = ct;
    while (cm && hops <= 16) {
        XsdCm* stack[64];
        int sp = 0;
        stack[sp++] = cm->first_child;
        while (sp > 0) {
            XsdCm* p = stack[--sp];
            for (; p; p = p->next) {
                int child_ns = cu && cu[0];
                int pns_ok =
                    p->ns ? (child_ns ? strcmp(p->ns, cu) == 0 : 1)
                          : !child_ns;
                if (p->kind == XSD_CM_ELEMENT && p->name &&
                    strcmp(p->name, cl) == 0 && pns_ok && p->type)
                    return p->type;
                if (p->kind == XSD_CM_GROUP_REF) {
                    XsdGroupDef* g = xsd_find_group(s, p->name);
                    if (g && g->model && sp < 64)
                        stack[sp++] = g->model;
                }
                if (sp < 64 && p->first_child)
                    stack[sp++] = p->first_child;
            }
        }
        const char* tns = xsd_target_ns(s);
        XsdCm* next = NULL;
        if (cm->res_base) {
            next = xsd_find_complex_ns(
                s, cm->res_base,
                cm->res_base_ns ? cm->res_base_ns : tns);
            if (!next) next = xsd_find_complex(s, cm->res_base);
        }
        if (!next && cm->assert_base) {
            next = xsd_find_complex_ns(
                s, cm->assert_base,
                cm->assert_base_ns ? cm->assert_base_ns : tns);
            if (!next) next = xsd_find_complex(s, cm->assert_base);
        }
        if (!next && cm->ext_base) {
            next = xsd_find_complex_ns(
                s, cm->ext_base,
                cm->ext_base_ns ? cm->ext_base_ns : tns);
            if (!next) next = xsd_find_complex(s, cm->ext_base);
        }
        cm = next;
        hops++;
    }
    return NULL;
}

/* #2221: the same-named element particle whose window a WILDCARD-slot
 * child's trace exhausted (§3.3.4.3 cl. 4.2 — the child's xsi:type
 * substitutes STRICTLY against this particle's selected type,
 * wild062's second f). First-name-match order mirrors the pre-trace
 * DFS posture; UPA-valid models carry exactly one such particle. */
static const XsdCm* wildcard_slot_particle(
    struct leptris_xsd_schema* s, const XsdCm* model, const char* cl,
    const char* cu) {
    if (!model) return NULL;
    const XsdCm* stack[64];
    int sp = 0;
    stack[sp++] = model;
    while (sp > 0) {
        const XsdCm* p = stack[--sp];
        for (; p; p = p->next) {
            int child_ns = cu && cu[0];
            int pns_ok =
                p->ns ? (!child_ns || strcmp(p->ns, cu) == 0)
                      : !child_ns;
            if (p->kind == XSD_CM_ELEMENT && p->name &&
                strcmp(p->name, cl) == 0 &&
                (p->type || p->alternatives) && pns_ok)
                return p;
            if (p->kind == XSD_CM_GROUP_REF) {
                XsdGroupDef* g = xsd_find_group(s, p->name);
                if (g && g->model && sp < 64)
                    stack[sp++] = g->model;
            }
            if (sp < 64 && p->first_child)
                stack[sp++] = p->first_child;
        }
    }
    return NULL;
}

/* The weak-wildcard GOVERNING-TYPE law (#2239), shared by both of its
 * routing sites: a wildcard-admitted child resolves its GLOBAL
 * declaration, and that declaration's type must be validly derived
 * from the shadowed same-name particle's — ONE-directional, through
 * the facet-free union member path when the particle types a union
 * (§3.4.4.3 dynamic EDC; wild064's decimal over the integer is NOT a
 * derivation, wild066's date over the date/time union rides the
 * member path). A shadowed particle carrying no type (alternatives
 * only) or a declaration with none imposes no check. */
static void weak_wildcard_governing_check(
    struct xsd_validator* v, struct leptris_xsd_schema* s,
    LeptrisElement ce, const char* cl, const char* cu,
    const char* shadow_type, const XsdElementDecl* wcd) {
    int govt_ok = !shadow_type || !wcd->type ||
                  xsd_type_derives_from(s, wcd->type, shadow_type);
    if (!govt_ok)
        govt_ok = xsd_union_member_path_fwd(s, shadow_type,
                                            wcd->type);
    if (!govt_ok)
        verrf_atk(v, ce, "element-declarations-consistent", cl,
                  "element", cl,
                  "the wildcard-admitted element's governing type is "
                  "not compatible with the shadowed particle's type");
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
    /* #2184 §3.4.2.3 (cos-ct): an ELEMENT-ONLY content type admits
     * whitespace between particles but no character content — the
     * all-extension merge lets interleaved instances reach the
     * counting match (all306.v01 against the element-only all307),
     * so the text walk must run for every non-mixed particle model,
     * not only the empty-content variety. */
    if (!model->empty_content && !model->empty_upgraded && !ct->mixed &&
        !ct->text_type) {
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(elem));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_TEXT &&
                leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_CDATA)
                continue;
            const char* t = leptris_text_node_get_content(c);
            if (!t) continue;
            int ws_only = 1;
            for (const char* q = t; *q; q++)
                if (*q != ' ' && *q != '\t' && *q != '\n' && *q != '\r')
                    ws_only = 0;
            if (ws_only) continue;
            const char* pn = NULL;
            leptris_element_expanded_name(elem, &pn, NULL, NULL);
            verrf_atk(v, elem, "content-model", pn ? pn : "", "element",
                      "#text",
                      "character content is not allowed: the content "
                      "type is element-only");
            break;
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
    /* #2151: the attribution trace — children attributed to the open
     * content (oc_attr[i]) type by the wildcard's processContents
     * instead of the name-matched particle (open047.v3: the second
     * <b> rides the skip wildcard and is NOT type-checked;
     * open025.n1: the first <i> still faces its declared type). The
     * interleaved walk attributes per child (§3.4.4.3 cl. 3.3 greedy
     * particle-first), so a declared particle after a wildcard span
     * still governs its own instance. */
    int oc_from = -1;
    unsigned char* oc_attr = NULL;
    if (n > 0) {
        oc_attr = (unsigned char*)calloc(n, 1);
        if (!oc_attr) {
            free(names);
            free(uris);
            return;
        }
    }
    unsigned char* wc_attr = (unsigned char*)calloc(n ? n : 1, 1);
    if (!wc_attr) {
        free(names);
        free(uris);
        free(oc_attr);
        return;
    }
    /* #2221: the NFA trace — WHICH particle consumed each child
     * (position-exact typing; the first-name-match DFS below stays
     * as the fallback for untraced walks). */
    XsdCm** hit = NULL;
    if (n > 0) {
        hit = (XsdCm**)calloc(n, sizeof(*hit));
        if (!hit) {
            free(names);
            free(uris);
            free(oc_attr);
            free(wc_attr);
            return;
        }
    }
    int ok = xsd_content_valid(s, model, xsd_target_ns(s), names, uris,
                               n, &oc_from, oc_attr, wc_attr, hit);
    /* the trace is authoritative only for a FULL (accepted) walk —
     * a dying walk's tail entries are NULL and the fallback DFS
     * keeps the pre-trace posture */
    int traced = (ok == 1);
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
        free(oc_attr);
        free(wc_attr);
        free(hit);
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
        /* #2151: children attributed to the open content (the
         * trace's per-child walk — §3.4.4.3 cl. 3.3) type by the
         * wildcard's processContents, and the name-matched particle
         * does NOT govern them; a governed child keeps facing its
         * declared type even after a wildcard span (interleave). */
        int oc_attributed =
            oc_attr ? oc_attr[child_i] != 0
                    : (oc_from >= 0 && (int)child_i >= oc_from &&
                       model->open_any);
        /* #2184: the NFA attribution trace — this child advanced ONLY
         * through wildcard transitions (the same-name particles could
         * not take it): the weak-wildcard slot. */
        int wc_admitted = wc_attr ? wc_attr[child_i] != 0 : 0;
        child_i++;
        /* #2221: the position-exact trace — the particle the NFA
         * actually matched for THIS child (XSD 1.0 §3.9.3). When the
         * walk was fully traced, it outranks every name-keyed scan:
         * an element-particle hit governs the child, a wildcard hit
         * routes the child through its own processContents, and NO
         * other wildcard elsewhere in the model may gate or skip a
         * child its slot never reached. */
        XsdCm* hitp =
            (traced && hit) ? hit[child_i - 1] : NULL;
        /* #1879: wildcard processContents gates the child — skip
         * imposes no constraint at all (even a declared child);
         * strict demands a declaration when no particle/global
         * types it; lax validates declared children only. */
        int wpc = oc_attributed ? model->open_any->pc_level
                                : (hitp && hitp->kind == XSD_CM_ANY
                                       ? hitp->pc_level
                                       : (hitp ? -1
                                               : model_wildcard_pc(
                                                     model, cu,
                                                     xsd_target_ns(s),
                                                     cl)));
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
        if (hitp && hitp->kind == XSD_CM_ELEMENT &&
            (hitp->type || hitp->alternatives)) {
            /* #2221: the traced particle governs — position-exact,
             * no first-name-match collapse across same-named
             * particles/windows. A particle carrying no type (a ref,
             * which resolves to a global declaration) falls through
             * to the global lookup below, like the DFS path. */
            ptype = hitp->type;
            pmatch = hitp;
            pfill = hitp->fixed_value ? hitp->fixed_value
                                      : hitp->default_value;
        } else if (model && !oc_attributed && !hitp) {
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
                } else if (ptype &&
                           xsi_type_override_fails_pos(
                               s, xlocal, xuri, ptype, cu,
                               ct ? ct->ns : NULL,
                               xsd_child_exceeds_model(s, model, ce,
                                                       cl))) {
                    /* #2184 (§3.3.4.3 cl. 4.2): the explicit type
                     * must substitute for the particle's selected
                     * type — STRICT derivation when the same-name
                     * particle's window is exhausted and this child
                     * rides the wildcard slot (wild062's second f),
                     * the substitutability arms (cast included)
                     * in-window */
                    verrf_atk(v, ce, "type-resolution", cl, "element",
                              cl,
                              "xsi:type does not override the "
                              "selected type definition");
                } else {
                    validate_local_uri(v, s, ce, cl, xlocal, xuri, cxsi,
                                       pfill, NULL);
                }
            } else if (wc_admitted) {
                /* #2184 (§3.8.6 weak-wildcard assessment): the
                 * shadowed particle's window is spent — this element
                 * rides the wildcard. Its GLOBAL declaration types it
                 * (strict) or — lax — types it when one resolves, and
                 * the resolved type must stay COMPATIBLE with the
                 * shadowed particle's (the governing-type law: IBM
                 * s3_8_6's integer over the local string is neither's
                 * derivation, so the instance fails). This is the
                 * UNTRACED routing site: a fully-accepted walk routes
                 * its weak-wildcard children through the traced
                 * wildcard slot below; a dying walk keeps the
                 * first-name-match posture here. */
                const XsdElementDecl* wcd =
                    xsd_find_element_child_ns(s, cl, cu);
                if (wcd) {
                    /* §3.4.4.3's dynamic EDC, one-directional, with
                     * the facet-free union member path (wild066). */
                    weak_wildcard_governing_check(v, s, ce, cl, cu,
                                                  ptype, wcd);
                    validate_element_decl(v, s, ce, 0, wcd);
                } else if (ltype) {
                    validate_local(v, s, ce, cl, ltype, lfill,
                                   ct ? ct->ns : NULL);
                }
                /* no declaration claims the name: tier-1 lax (the
                 * pinned moxml#460 posture) */
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
            /* #2221: a traced WILDCARD-slot child carrying xsi:type
             * substitutes STRICTLY against the same-named particle's
             * selected type (§3.3.4.3 cl. 4.2 — wild062's second f,
             * wild063.v2's byte: the trace proves the window is
             * exhausted, so the strict arm rides unconditionally).
             * Without a particle type the global-declaration path
             * below keeps handling it. */
            int wxsi_handled = 0;
            const char* wxsi =
                (hitp && hitp->kind == XSD_CM_ANY && !oc_attributed)
                    ? leptris_element_attribute_ns(
                          ce,
                          "http://www.w3.org/2001/XMLSchema-instance",
                          "type")
                    : NULL;
            if (wxsi && wxsi[0]) {
                const XsdCm* pw =
                    wildcard_slot_particle(s, model, cl, cu);
                if (pw && (pw->type || pw->alternatives)) {
                    wxsi_handled = 1;
                    if (pw->type) {
                        char xuri[512], xlocal[256];
                        xsd_resolve_qname_inst(
                            ce, wxsi, xuri, sizeof(xuri), xlocal,
                            sizeof(xlocal));
                        if (!xlocal[0]) {
                            verrf_atk(v, ce, "type-resolution", cl,
                                      "element", cl,
                                      "xsi:type prefix is not bound "
                                      "in the instance");
                        } else if (xsi_type_override_fails_pos(
                                       s, xlocal, xuri, pw->type, cu,
                                       ct ? ct->ns : NULL, 1)) {
                            verrf_atk(
                                v, ce, "type-resolution", cl,
                                "element", cl,
                                "xsi:type does not override the "
                                "selected type definition");
                        } else {
                            validate_local_uri(
                                v, s, ce, cl, xlocal, xuri, wxsi,
                                pw->fixed_value ? pw->fixed_value
                                                : pw->default_value,
                                ct ? ct->ns : NULL);
                        }
                    } else {
                        /* alternative-only particle: the selected
                         * type has no base to substitute against —
                         * the named type assesses as given */
                        char xuri[512], xlocal[256];
                        xsd_resolve_qname_inst(
                            ce, wxsi, xuri, sizeof(xuri), xlocal,
                            sizeof(xlocal));
                        if (!xlocal[0]) {
                            verrf_atk(v, ce, "type-resolution", cl,
                                      "element", cl,
                                      "xsi:type prefix is not bound "
                                      "in the instance");
                        } else {
                            validate_local_uri(v, s, ce, cl, xlocal,
                                               xuri, wxsi, NULL,
                                               ct ? ct->ns : NULL);
                        }
                    }
                }
            }
            if (!wxsi_handled) {
            const XsdElementDecl* cd =
                xsd_find_element_child_ns(s, cl, cu);
            if (cd) {
                if (traced && wc_admitted) {
                    /* #2239 x #2221, the TRACED weak-wildcard slot: the
                     * trace proves the same-name particle's window is
                     * spent, so the resolved global declaration faces
                     * the governing-type law against THAT particle's
                     * type — one-directional, with the facet-free
                     * union member path (wild064's decimal over the
                     * integer is not a derivation; wild066's date over
                     * the date/time union rides the member path). The
                     * shadow is found through the derivation CHAIN
                     * (wild068: the restricting type's model carries
                     * no e particle at all — the shadowed union-typed
                     * particle lives in the base), the same lookup the
                     * untraced posture uses. */
                    const char* govt =
                        ct ? xsd_chain_particle_type(s, ct, cl, cu)
                           : NULL;
                    weak_wildcard_governing_check(v, s, ce, cl, cu,
                                                  govt, cd);
                } else {
                /* §3.4.4.3 (dynamic EDC, #2184): a wildcard-admitted
                 * element resolves to a GLOBAL declaration; when the
                 * governing type's derivation chain declares the same
                 * name, the global's type must be validly derived
                 * from that declaration's type (wild062's time vs the
                 * governing date, wild068's duration vs the union). */
                const char* govt = ct ? xsd_chain_particle_type(
                                            s, ct, cl, cu)
                                      : NULL;
                if (govt && cd->type &&
                    !xsd_type_derives_from(s, cd->type, govt)) {
                    verrf_atk(v, ce, "element-declarations-consistent",
                              cl, "element", cl,
                              "the wildcard-resolved declaration's "
                              "type is not validly derived from the "
                              "governing declaration's");
                }
                }
                validate_element_decl(v, s, ce, 0, cd);
            } else if (ltype) {
                /* nothing global claims the namespaced child —
                 * the unqualified particle's type governs
                 * (wp.xsd corpus posture) */
                validate_local(v, s, ce, cl, ltype, lfill,
                               ct ? ct->ns : NULL);
            }
            }
        }
        /* wildcard-admitted children without a declaration pass:
         * strict and lax alike admit undeclared names (the pinned
         * moxml#460 notQName semantics — tier-1 lax direction) */
    }
    free(oc_attr);
    free(wc_attr);
    free(hit);
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

/* The builtin a type NAME resolves to through the restriction
 * ladder ("xs:double" for a chain rooted there): the name itself
 * when it is a builtin spelling, NULL when the chain carries no
 * builtin base or the local type never resolves. The fixed-value
 * comparator uses this to pick the value-space rule. */
static const char* fixed_terminal_builtin(struct leptris_xsd_schema* s,
                                          const char* type,
                                          const char* ns) {
    if (!type) return NULL;
    if (strncmp(type, "xs:", 3) == 0) return type;
    const XsdSimple* st = xsd_find_simple_ns_pub(s, type, ns);
    for (int d = 0; st && d < 32; d++) {
        if (st->base && strncmp(st->base, "xs:", 3) == 0)
            return st->base;
        st = assertion_base_next(s, st);
    }
    return NULL;
}

/* moxml#588 (#2207): float/double fixed constraints are VALUE
 * constraints (XSD 1.0 §3.2.3/§4.3.1.2, 1.1 §3.2.3.1) — the
 * comparator picks the datatype's value-space rule by terminal. */
static int fixed_terminal_is_float_double(const char* term) {
    return term && (strcmp(term, "xs:float") == 0 ||
                    strcmp(term, "xs:double") == 0);
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
 * posture — never reject what we cannot assess). #2183: the facet
 * lane runs ABSENT FOCUS (§3.16.4 — no context item: position(),
 * last() and relative paths raise XPDY0002) and an evaluated
 * dynamic error is a FAILURE (the engine distinguishes "assessed
 * and raised" via the error-code channel from "could not assess",
 * which stays not-enforced). A false EBV does not hold. */
static int assertion_ebv(struct xsd_validator* v,
                         struct leptris_xsd_schema* s,
                         XsdAssertionFacet* f,
                         LeptrisElement elem, const char* select) {
    (void)elem; /* §3.16.4: absent focus — the element is not the
                 * context item */
    if (!f->tried) {
        f->tried = 1;
        size_t pl = f->ns_prolog ? strlen(f->ns_prolog) : 0;
        /* #2183: the facet's static base URI — fn:doc in the test
         * resolves relative URIs against the SCHEMA document's
         * directory (the capture side carries no schema) */
        const char* base_dir = xsd_schema_base_dir(s);
        int have_base = base_dir && base_dir[0] &&
                        !strchr(base_dir, '"');
        size_t bl = have_base ? strlen(base_dir) + 32 : 0;
        size_t tl = f->test ? strlen(f->test) : 0;
        char* wrapped = (char*)malloc(pl + bl + tl + 64);
        if (wrapped) {
            size_t o = 0;
            if (pl) {
                memcpy(wrapped + o, f->ns_prolog, pl);
                o += pl;
                wrapped[o++] = ' ';
            }
            if (have_base) {
                int w = snprintf(wrapped + o, bl,
                                 "declare base-uri \"%s/\";",
                                 base_dir);
                if (w > 0 && (size_t)w < bl) o += (size_t)w;
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
    char ecode[32];
    LeptrisXPathResult r = leptris_xquery_eval_params_ec(
        f->q, v->doc, NULL, names, sels, 1,
        (unsigned)(LEPTRIS_XQUERY_ASSERT_DATA_MODEL |
                   LEPTRIS_XQUERY_ASSERT_ABSENT_FOCUS),
        ecode, sizeof(ecode));
    int ebv = 0;
    if (r) {
        ebv = leptris_xpath_result_boolean(r);
        leptris_xpath_result_free(r);
    } else if (ecode[0]) {
        /* assessed and raised (XPDY0002 on position()/last()/dot,
         * FODC0002 on a doc() miss, FOAR0001 on integer div-by-0):
         * §3.16.4 — the facet does not hold */
        ebv = 0;
    } else {
        /* engine could not assess: not enforced (facet posture) */
        ebv = 1;
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
            if (!assertion_ebv(v, s, f, elem, select)) return 0;
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

/* XSD 1.1 xs:anyAttribute/@notQName hit test (moxml#460, #2184):
 * ##defined resolves against the schema's top-level attribute
 * declarations; prefix-less literals match no-namespace attribute
 * names; the capture-canon "{uri}local" form matches the instance
 * attribute's (namespace, local) pair directly; legacy raw prefixed
 * spellings are skipped (capture resolves them). */
static int attr_not_qname_hits(const char* tokens,
                               struct leptris_xsd_schema* s,
                               const char* local, const char* uri) {
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
        /* capture-canon "{uri}local": exact (namespace, local) */
        if (start[0] == '{') {
            const char* close = memchr(start, '}', len);
            if (close) {
                size_t ulen = (size_t)(close - start - 1);
                size_t llen = len - ulen - 2;
                if (uri && strlen(uri) == ulen &&
                    strncmp(start + 1, uri, ulen) == 0 &&
                    strlen(local) == llen &&
                    strncmp(close + 1, local, llen) == 0)
                    return 1;
            }
            continue;
        }
        if (memchr(start, ':', len)) continue; /* prefixed: legacy */
        if (uri == NULL && strlen(local) == len &&
            strncmp(local, start, len) == 0)
            return 1;
    }
    return 0;
}

/* #2184: the @notNamespace exclusion hit test over a canon list
 * (URIs + ##local; ##targetNamespace defends legacy raw captures).
 * target_ns may be NULL. */
static int attr_not_ns_hits(const char* tokens, const char* uri,
                            const char* target_ns) {
    if (!tokens) return 0;
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

/* #2184: the effective attribute wildcard's admission for one
 * instance attribute — positive grammar minus the notNamespace and
 * notQName exclusions. One canon wildcard per row: the extension
 * union and the attributeGroup intersection fold at capture. */
static int attr_wc_admits(struct leptris_xsd_schema* s,
                          const XsdTypeAttrs* ta, const char* uri,
                          const char* local,
                          const char* tns_fallback) {
    if (!ta->any_attr_ns && !ta->any_attr_not_qname &&
        !ta->any_attr_not_ns)
        return 0;
    if (!xsd_any_attr_matches(ta->any_attr_ns, uri, tns_fallback))
        return 0;
    if (ta->any_attr_not_ns &&
        attr_not_ns_hits(ta->any_attr_not_ns, uri, tns_fallback))
        return 0;
    if (ta->any_attr_not_qname &&
        attr_not_qname_hits(ta->any_attr_not_qname, s, local, uri))
        return 0;
    return 1;
}


/* xs:anyAttribute grammar: ##any (or an absent attribute),
 * ##other, ##targetNamespace, ##local, or a space-separated URI
 * list. target_ns may be NULL or "" — an ABSENT unit tns: ##other
 * then matches every attribute that HAS a namespace (it excludes
 * only ##local), ##targetNamespace matches nothing. */
int xsd_any_attr_matches(const char* grammar, const char* uri,
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
 * whiteSpace and compare; moxml#588 (#2207): float/double then
 * compare in the datatype's VALUE space — the infinity family's
 * spellings ("INF"/"+INF"/"1E999") all satisfy a fixed constraint
 * naming any one of them, and non-numeric terminals keep the
 * lexical comparison. */
static int attr_fixed_matches(struct leptris_xsd_schema* s,
                              const char* type, const char* ns,
                              const char* val, const char* fixed) {
    if (!type) return 0;
    char norm_v[512];
    char norm_f[512];
    const char* cv = val;
    const char* cf = fixed;
    if (type_collapses_ws(s, type, ns)) {
        if (strlen(val) >= sizeof(norm_v)) return 0;
        if (strlen(fixed) >= sizeof(norm_f)) return 0;
        if (!xsd_collapse_str(val, norm_v, sizeof(norm_v))) return 0;
        if (!xsd_collapse_str(fixed, norm_f, sizeof(norm_f))) return 0;
        cv = norm_v;
        cf = norm_f;
    }
    if (strcmp(cv, cf) == 0) return 1;
    if (fixed_terminal_is_float_double(fixed_terminal_builtin(s, type, ns)))
        return xsd_float_double_value_equal(cv, cf);
    return 0;
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

/* ---- #2184: the ID/IDREF bookkeeping ------------------------------- */

/* What one lexical value contributes when assessed against a simple
 * type: 0 nothing, 1 an ID value, 2 an IDREF value. A UNION decides
 * by its FIRST accepting member (the datatype's {actual member} —
 * id006's "23" rides the integer arm, ii17's "true" the boolean arm,
 * and neither registers); a RESTRICTION walks its base chain
 * (id007's pattern-restricted arms are ID/IDREF derivatives); LISTS
 * are itemized by the caller. Unresolved names classify as 0 — the
 * compile-time leniency posture, never a silent constraint. */
static int id_item_accepts(struct leptris_xsd_schema* s, const char* name,
                           const XsdSimple* def, const char* v,
                           int depth);
static int id_classify_value(struct leptris_xsd_schema* s,
                             const XsdSimple* st, const char* name,
                             const char* v, int depth) {
    if (!s || depth > 8) return 0;
    if (name && strncmp(name, "xs:", 3) == 0) {
        if (strcmp(name, "xs:ID") == 0 || strcmp(name, "xsd:ID") == 0)
            return 1;
        if (strcmp(name, "xs:IDREF") == 0 ||
            strcmp(name, "xsd:IDREF") == 0)
            return 2;
        if (strcmp(name, "xs:ENTITY") == 0 ||
            strcmp(name, "xsd:ENTITY") == 0)
            return 3; /* the §3.2.5 unparsed-entity arm */
        return 0;
    }
    if (!st && name) st = xsd_find_simple_pub(s, name);
    if (!st) return 0;
    if (st->deriv == XSD_SIMPLE_UNION) {
        /* the FIRST accepting member is the {actual member}: its
         * classification governs, 0 included — "true" rides the
         * boolean arm of unionOfIDs even though the ID arm would
         * also accept it (ii17/id006). */
        for (size_t i = 0; i < st->member_count; i++)
            if (id_item_accepts(s, st->members[i], NULL, v, depth))
                return id_classify_value(s, NULL, st->members[i], v,
                                         depth + 1);
        for (size_t i = 0; i < st->member_def_count; i++)
            if (id_item_accepts(s, NULL, st->member_defs[i], v, depth))
                return id_classify_value(s, st->member_defs[i], NULL, v,
                                         depth + 1);
        return 0;
    }
    if (st->deriv == XSD_SIMPLE_LIST) return 0; /* caller itemizes */
    /* RESTRICT: the base chain decides. */
    if (st->base_def) return id_classify_value(s, st->base_def, NULL, v,
                                               depth + 1);
    if (st->base) return id_classify_value(s, NULL, st->base, v,
                                           depth + 1);
    return 0;
}

static int id_item_accepts(struct leptris_xsd_schema* s, const char* name,
                           const XsdSimple* def, const char* v,
                           int depth) {
    if (!s || depth > 8) return 0;
    if (name && strncmp(name, "xs:", 3) == 0)
        return xsd_builtin_valid(name, v);
    if (def) return xsd_simple_valid_chain(s, def, v);
    if (name) {
        const XsdSimple* next = xsd_find_simple_pub(s, name);
        if (next) return xsd_simple_valid_chain(s, next, v);
    }
    return 1; /* unresolved: accept, and classification stays 0 */
}

/* The chain's LIST definition (NULL = not a list): a restriction of
 * a list keeps the list's value space (the #1882 posture). */
static const XsdSimple* id_chain_list_def(struct leptris_xsd_schema* s,
                                          const XsdSimple* t) {
    int depth = 0;
    while (t && depth++ < 32) {
        if (t->deriv == XSD_SIMPLE_LIST) return t;
        if (!t->base) {
            if (t->base_def) {
                t = t->base_def;
                continue;
            }
            return NULL;
        }
        if (strncmp(t->base, "xs:", 3) == 0) return NULL;
        t = xsd_find_simple_pub(s, t->base);
    }
    return NULL;
}

/* #2184 (§3.2.5): the value names a DECLARED UNPARSED entity of the
 * instance's DTD (a notation-carrying external entity). */
static int xsd_unparsed_entity_declared(struct xsd_validator* v,
                                        const char* name) {
    if (!v || !v->doc || !name || !name[0]) return 0;
    LeptrisDTD* dtd = leptris_document_get_dtd(v->doc);
    if (!dtd) return 0;
    DTDEntityDecl* e = ttdtd_lookup_entity(dtd, name);
    return e && e->notation_name;
}

/* The ENTITY/ENTITIES integrity arm: itemize the value, every name
 * must resolve to a declared unparsed entity (id018.n01's undeclared
 * entity1 in the default-filled ENTITIES, id021.n02's entity4). */
static void xsd_entity_bookkeep(struct xsd_validator* v,
                                const char* value, LeptrisElement carrier) {
    char* dup = strdup(value);
    if (!dup) return;
    for (char* p = dup; *p;) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char* end = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        char was = *end;
        *end = 0;
        if (!xsd_unparsed_entity_declared(v, p))
            verrf_atk(v, carrier, "entity-resolution", "ENTITY",
                      "ENTITY", "ENTITY",
                      "value does not name a declared unparsed entity");
        *end = was;
        p = end;
    }
    free(dup);
}

/* Assess one (type reference, value) pair into the bookkeeping.
 * carrier = the element that carries the value; via_content marks a
 * CONTENT-carried value (the uniqueness scope is the parent). Lists
 * itemize (every classified item joins the pool; the first ID
 * item also joins TableA — id004/id005), unions decide per member
 * (id006/id007/ii17). Lexical failures never reach here: the
 * callers gate on text_valid. */
static void xsd_id_bookkeep(struct xsd_validator* v,
                            struct leptris_xsd_schema* s,
                            const char* type, const char* ns,
                            const char* value, LeptrisElement carrier,
                            int via_content) {
    /* the collapsed-value buffer lives at FUNCTION scope: the pointer
     * escapes into every record/classify call below, and a nested
     * block would make each of those a stack-use-after-scope (the
     * Linux ASAN leg's finding — macOS Release builds never see it). */
    char norm[512];
    if (!v || !s || !type || !value || !value[0]) return;
    /* The token-family datatypes collapse their lexical before the
     * [normalized value] exists (§3.2.4.2) — the tables see the
     * normalized form: v16's trailing-newline idref resolves as b2,
     * id001.n02's " aaa " duplicates para1's aaa. */
    if (strlen(value) < sizeof(norm) &&
        type_collapses_ws(s, type, ns) &&
        xsd_collapse_str(value, norm, sizeof(norm)))
        value = norm;
    const XsdSimple* st = NULL;
    if (strncmp(type, "xs:", 3) == 0 || strncmp(type, "xsd:", 4) == 0) {
        if (strcmp(type, "xs:ID") == 0 || strcmp(type, "xsd:ID") == 0) {
            if (!xsd_id_value_record(v, value, carrier, via_content))
                verrf_atk(v, carrier, "attribute-use", "ID", "attribute",
                          "ID", "duplicate ID value");
            xsd_id_pool_record(v, value, carrier, 0);
        } else if (strcmp(type, "xs:IDREF") == 0 ||
                   strcmp(type, "xsd:IDREF") == 0) {
            xsd_id_pool_record(v, value, carrier, 1);
        } else if (strcmp(type, "xs:IDREFS") == 0 ||
                   strcmp(type, "xsd:IDREFS") == 0) {
            char* dup = strdup(value);
            if (!dup) return;
            for (char* p = dup; *p;) {
                while (*p && isspace((unsigned char)*p)) p++;
                if (!*p) break;
                char* end = p;
                while (*end && !isspace((unsigned char)*end)) end++;
                char was = *end;
                *end = 0;
                xsd_id_pool_record(v, p, carrier, 1);
                *end = was;
                p = end;
            }
            free(dup);
        } else if (strcmp(type, "xs:ENTITY") == 0 ||
                   strcmp(type, "xsd:ENTITY") == 0 ||
                   strcmp(type, "xs:ENTITIES") == 0 ||
                   strcmp(type, "xsd:ENTITIES") == 0) {
            /* §3.2.5: every value names a declared unparsed entity */
            xsd_entity_bookkeep(v, value, carrier);
        }
        return;
    }
    st = xsd_find_simple_ns_pub(s, type, ns);
    if (!st) st = xsd_find_simple_pub(s, type);
    if (!st) return;
    const XsdSimple* ldef = id_chain_list_def(s, st);
    if (ldef) {
        char* dup = strdup(value);
        if (!dup) return;
        int first_id = 1;
        for (char* p = dup; *p;) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char* end = p;
            while (*end && !isspace((unsigned char)*end)) end++;
            char was = *end;
            *end = 0;
            int c = id_classify_value(s, ldef->item_def, ldef->item_type,
                                      p, 0);
            if (c == 1) {
                xsd_id_pool_record(v, p, carrier, 0);
                /* the FIRST ID item of a list value joins TableA with
                 * document-wide scope (id004.n01's zzz as two id
                 * elements' first items is a duplicate even under one
                 * parent); the value's other items only feed the
                 * pool (id004.v01's zzz/bbb repeats are legal). */
                if (first_id &&
                    !xsd_id_value_record(v, p, carrier, 0))
                    verrf_atk(v, carrier, "attribute-use", "ID",
                              "attribute", "ID", "duplicate ID value");
                first_id = 0;
            } else if (c == 2) {
                xsd_id_pool_record(v, p, carrier, 1);
            } else if (c == 3) {
                xsd_entity_bookkeep(v, p, carrier);
            }
            *end = was;
            p = end;
        }
        free(dup);
        return;
    }
    int c = id_classify_value(s, st, NULL, value, 0);
    if (c == 1) {
        if (!xsd_id_value_record(v, value, carrier, via_content))
            verrf_atk(v, carrier, "attribute-use", "ID", "attribute",
                      "ID", "duplicate ID value");
        xsd_id_pool_record(v, value, carrier, 0);
    } else if (c == 2) {
        xsd_id_pool_record(v, value, carrier, 1);
    } else if (c == 3) {
        xsd_entity_bookkeep(v, value, carrier);
    }
}

/* §C.4 integrity (end of run): every IDREF/IDREFS value the run
 * recorded must match an ID registered in the same document —
 * the ii-family rows (ii04 ads_, ii06 asd, ii15/ii17/ii18 true,
 * id005's hhh, id008's ddd, id016's defaulted para001). */
static void xsd_idrefs_resolve(struct xsd_validator* v) {
    if (!v) return;
    for (struct id_ref* r = v->id_pool; r; r = r->next) {
        if (!r->is_ref) continue;
        int found = 0;
        for (struct id_ref* p = v->id_pool; p && !found; p = p->next)
            if (!p->is_ref && strcmp(p->v, r->v) == 0) found = 1;
        if (!found && r->carrier)
            verrf_atk(v, r->carrier, "IDREF-resolution", "IDREF",
                      "IDREF", "IDREF",
                      "value does not resolve to any ID in the document");
    }
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
            /* #2184 §3.4.4.2: an absent attribute is satisfied by its
             * {value constraint} — a DEFAULT (id010/id016) or a FIXED
             * (id011/id013: the fixed acts as the default) value
             * joins the bookkeeping like an instance-supplied one. */
            const char* constraint = a->def ? a->def : a->fixed;
            if (constraint && constraint[0])
                xsd_id_bookkeep(v, s, a->type, ta ? ta->ns : NULL,
                                constraint, elem, 0);
            continue;
        }
        /* #2185: use="prohibited" removes the attribute from the
         * type's {attribute uses} (XSD 1.0 §3.4.2) — an instance
         * attribute the row names is rejected (attruse_0_2). In an
         * EXTENSION the prohibited row contributes nothing: the
         * folded base use with the same expanded name governs (the
         * derivation-ok-extension_0 twin — its instance carries the
         * attribute and stays valid), so a same-name co-row makes
         * this row inert. */
        if (a->prohibited) {
            int inert = 0;
            for (const XsdAttrDecl* b = ta->attrs; b && !inert;
                 b = b->next) {
                if (b == a || !b->name || !a->name) continue;
                int bn = b->ns && b->ns[0];
                int an = a->ns && a->ns[0];
                if (strcmp(b->name, a->name) != 0 || bn != an) continue;
                if (bn && strcmp(b->ns, a->ns) != 0) continue;
                inert = 1;
            }
            if (!inert) {
                verrf_atk(v, elem, "attribute-use", a->name,
                          "attribute", a->name, "prohibited but present");
                continue;
            }
        }
        /* #2184: ID/IDREF bookkeeping rides a lexically VALID value —
         * TableA uniqueness plus the resolution pool (§C.4). */
        if (text_valid(v, s, a->type, val, ta ? ta->ns : NULL,
                       "attribute", a->name, elem))
            xsd_id_bookkeep(v, s, a->type, ta ? ta->ns : NULL, val, elem,
                            0);
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
            attr_not_qname_hits(ta->any_attr_not_qname, s, local,
                                auri)) {
            /* XSD 1.1 notQName: the wildcard never admits this
             * name, declared or not (moxml#460). */
            verrf_atk(v, elem, "attribute-use", local, "attribute",
                      local, "excluded by notQName");
            continue;
        }
        /* #2184: ONE effective wildcard per row — the canon grammar
         * minus its notNamespace/notQName exclusions. The extension
         * union and the attributeGroup intersection fold at
         * capture; each wildcard resolves against its DECLARING
         * unit's tns (recorded at capture), the merged schema's tns
         * is the fallback. */
        if (ta && attr_wc_admits(s, ta, auri, local,
                                 ta->any_attr_unit_ns
                                     ? ta->any_attr_unit_ns
                                     : xsd_target_ns(s))) {
            /* #2184: a wildcard-admitted attribute whose GLOBAL
             * declaration is ID/IDREF-typed joins the bookkeeping
             * (attZ014's a/b; id002's admitted id-two) — the
             * lexical verdict stays the wildcard's (lax passes
             * unresolved names). */
            const XsdAttrDecl* g = xsd_find_top_attr(s, local);
            if (g && g->type && g->type[0]) {
                const char* aval =
                    leptris_attribute_get_value(elem, at);
                if (aval)
                    xsd_id_bookkeep(v, s, g->type, xsd_target_ns(s),
                                    aval, elem, 0);
            }
            continue;
        }
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
     * ID-derived both count. 1.1 lifted the constraint.
     * #2184: two or more DECLARED ID-typed uses on one type cannot
     * be a legal 1.0 schema (the constraint is 1.0-only), so the
     * schema is 1.1 by syntax evidence (the #2149 posture) and the
     * rule does not apply. A single declared ID beside a
     * wildcard-admitted second is NOT such evidence — the rs#73
     * pinned shapes keep the 1.0 verdict. A schema the author
     * explicitly pinned to 1.0 (@version / vc:maxVersion="1.0")
     * keeps the gate whatever its table carries. */
    if (!xsd_schema_version_11(s)) {
        int id_count = 0;
        for (XsdAttrDecl* a = ta->attrs; a; a = a->next)
            if (attr_type_is_id(s, a->type)) id_count++;
        /* #2184: the declared-evidence skip belongs to UNMARKED
         * schemas only — a schema the author pinned to 1.0 keeps the
         * gate whatever its table carries. */
        if (xsd_schema_pinned_10(s) || id_count < 2) {
            for (LeptrisAttribute at = leptris_element_first_attribute(elem);
                 at && id_count < 2; at = leptris_attribute_next(at)) {
                const char* qn = leptris_attribute_get_name(at);
                if (!qn || strncmp(qn, "xmlns", 5) == 0) continue;
                const char* colon = strchr(qn, ':');
                const char* local = colon ? colon + 1 : qn;
                const char* auri = leptris_attribute_namespace_uri(at);
                int declared = 0;
                for (XsdAttrDecl* a = ta->attrs; a && !declared;
                     a = a->next)
                    if (attr_row_admits(a, local, qn, auri))
                        declared = 1;
                if (declared) continue;
                if (auri && strcmp(auri,
                                   "http://www.w3.org/2001/"
                                   "XMLSchema-instance")
                        == 0)
                    continue;
                if (ta->any_attr_not_qname &&
                    attr_not_qname_hits(ta->any_attr_not_qname, s, local,
                                        auri))
                    continue;
                int admitted =
                    attr_wc_admits(s, ta, auri, local,
                                   ta->any_attr_unit_ns
                                       ? ta->any_attr_unit_ns
                                       : xsd_target_ns(s));
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

/* #2184 (§3.3.4.3 cl. 4): an instance-specified type definition must
 * OVERRIDE the selected type definition — S ·validly substitutable·
 * for T (§3.3.4.2). Tier-1 direction: a SIMPLE instance-specified
 * type is substitutable only for a SIMPLE selected type
 * (§3.3.4.2's third arm); there is no simple-for-complex arm, so a
 * simple xsi:type against a COMPLEX selected type never overrides —
 * the element is locally invalid (cl. 4 fails) and assessment falls
 * back to the selected type (§3.3.4.3 Note, [type fallback]).
 * Complex-over-complex keeps the pinned lax direction (derivation-ok
 * walking is not entered here). The name-resolution ladder mirrors
 * validate_local_uri's (tns hint, free-first, ref-uri, plain). */

/* #2184 (§3.3.4.3 cl. 4.2 / §2.2.4 substitutability): can `target`
 * (a type name) substitute for the simple type `t`? A facet-free
 * union-member path substitutes; a hop carrying restriction facets
 * (pattern/enumeration) blocks (simple016.n01's date behind dt's
 * pattern vs v01's direct time member). */
static const char* xsd_strip_prefix(const char* n) {
    const char* c = n ? strchr(n, ':') : NULL;
    return c ? c + 1 : n;
}

static int xsd_union_member_path(struct leptris_xsd_schema* s,
                                 const XsdSimple* t, const char* target,
                                 int depth) {
    if (!t || depth > 16) return 0;
    const char* ul = xsd_strip_prefix(target);
    if (t->name && strcmp(xsd_strip_prefix(t->name), ul) == 0)
        return 1;
    if (t->patterns || t->enum_values) return 0;
    for (size_t i = 0; i < t->member_count; i++) {
        const char* ml = xsd_strip_prefix(t->members[i]);
        if (strcmp(ml, ul) == 0) return 1;
        const XsdSimple* m = xsd_find_simple_pub(s, ml);
        if (m && xsd_union_member_path(s, m, target, depth + 1))
            return 1;
    }
    for (size_t i = 0; i < t->member_def_count; i++) {
        const XsdSimple* m = t->member_defs[i];
        if (m && m->name && strcmp(xsd_strip_prefix(m->name), ul) == 0)
            return 1;
        if (m && xsd_union_member_path(s, m, target, depth + 1))
            return 1;
    }
    if (t->base_def)
        return xsd_union_member_path(s, t->base_def, target,
                                     depth + 1);
    if (t->base) {
        const XsdSimple* b =
            xsd_find_simple_pub(s, xsd_strip_prefix(t->base));
        if (b) return xsd_union_member_path(s, b, target, depth + 1);
    }
    return 0;
}

/* The weak-wildcard law's union arm: resolve `union_name` (a
 * particle type name — a local union, an inline synth slot, or a
 * builtin spelling) and answer whether `target` reaches it through a
 * FACET-FREE member path (the xsi:type lane's law). */
static int xsd_union_member_path_fwd(struct leptris_xsd_schema* s,
                                     const char* union_name,
                                     const char* target) {
    if (!union_name || !target) return 0;
    const XsdSimple* sm = xsd_find_simple_pub(s, union_name);
    if (!sm) return 0; /* builtins carry no union members */
    return xsd_union_member_path(s, sm, target, 0);
}

static int xsi_type_override_fails_pos(struct leptris_xsd_schema* s,
                                       const char* xlocal,
                                       const char* xuri,
                                       const char* sel_type,
                                       const char* euri,
                                       const char* tns_hint,
                                       int strict) {
    if (!xlocal || !xlocal[0]) return 0;
    /* the named type must resolve SIMPLE */
    int named_simple = 0;
    if (xuri && strcmp(xuri, "http://www.w3.org/2001/XMLSchema") == 0) {
        /* the XSD namespace: only the builtin table answers (an
         * unknown builtin name is the complex008 unresolvable
         * class — the downstream resolution path reports it) */
        char builtin[300];
        if (snprintf(builtin, sizeof(builtin), "xs:%s", xlocal) <
            (int)sizeof(builtin))
            named_simple = xsd_builtin_known(builtin);
    } else if (!xuri || !xuri[0]) {
        /* unprefixed: the §1691 canon — builtin local names
         * resolve; a user simpleType of the same name is next */
        char builtin[300];
        if (snprintf(builtin, sizeof(builtin), "xs:%s", xlocal) <
            (int)sizeof(builtin))
            named_simple = xsd_builtin_known(builtin);
        if (!named_simple)
            named_simple = xsd_find_simple_pub(s, xlocal) != NULL;
    } else {
        named_simple = xsd_find_simple_ns_pub(s, xlocal, xuri) != NULL;
    }
    if (!named_simple) return 0;
    /* the selected type must resolve COMPLEX */
    if (!sel_type || !sel_type[0]) return 0;
    XsdCm* sel = tns_hint ? xsd_find_complex_ns(s, sel_type, tns_hint)
                          : NULL;
    if (!sel && (!euri || !euri[0]))
        sel = xsd_find_complex_ns_free_first(s, sel_type);
    if (!sel) sel = xsd_find_complex_ref(s, sel_type, euri);
    if (!sel) sel = xsd_find_complex(s, sel_type);
    if (sel) return 1; /* simple over COMPLEX: never overrides */
    /* #2184 (§3.3.4.3 cl. 4.2): BOTH resolve SIMPLE — the explicit
     * type must be validly SUBSTITUTABLE for the selected one
     * (§2.2.4's substitutability, not bare derivation): a member of
     * the selected union substitutes (simple016.v01's time over the
     * union), but a member reached THROUGH a restriction-carrying
     * type does not (simple016.n01's date behind the pattern facet).
     * Complex-over-complex keeps the pinned lax direction: the
     * selection resolved complex above. */
    {
        char xfull[300];
        const char* xname = xlocal;
        if ((!xuri || !xuri[0] ||
             strcmp(xuri, "http://www.w3.org/2001/XMLSchema") == 0)) {
            if (snprintf(xfull, sizeof(xfull), "xs:%s", xlocal) <
                (int)sizeof(xfull))
                xname = xfull;
        }
        int resolved_sel = 0;
        char sfull[300];
        const char* sname = sel_type;
        if (strncmp(sel_type, "xs:", 3) == 0) {
            resolved_sel = 1;
        } else if (xsd_find_simple_pub(s, sel_type)) {
            resolved_sel = 1;
        } else if (snprintf(sfull, sizeof(sfull), "xs:%s", sel_type) <
                       (int)sizeof(sfull) &&
                   xsd_builtin_exists(sfull)) {
            resolved_sel = 1;
            sname = sfull;
        }
        if (!resolved_sel) return 0; /* posture */
        const XsdSimple* sel_sm =
            strncmp(sname, "xs:", 3) == 0
                ? NULL
                : xsd_find_simple_pub(s, sname);
        if (sel_sm && xsd_union_member_path(s, sel_sm, xname, 0))
            return 0; /* substitutable through a facet-free path */
        if (!xsd_type_derives_from(s, xname, sname) && !strict) {
            /* the cast arm (§3.16.6.4): every value of the nominated
             * atomic's space casts to the string family (the
             * integer-over-string gate of XsiTypeSimpleTypeRestrictsText;
             * never for a wildcard-slot excess — strict above) */
            if (strcmp(sname, "xs:string") == 0 ||
                strcmp(sname, "xs:normalizedString") == 0)
                return 0;
        }
        return !xsd_type_derives_from(s, xname, sname);
    }
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
    /* #2184 (§3.3.4): an ABSTRACT declaration is never validated
     * against directly — only its substitution members stand for
     * instances (subsgroup001.n1's appendixContent). */
    if (d->abstract) {
        verrf_atk(v, elem, "element-decl", local, "element", local,
                  "the declaration is abstract");
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
                /* moxml#588 (#2207): float/double content compares
                 * in the datatype's VALUE space — the same §4.3.1.2
                 * rule the attribute side rides (the infinity
                 * family's spellings and the overflow forms are one
                 * value). Non-float/double terminals fall through
                 * to the typed walks below, unchanged. */
                const char* term =
                    fixed_terminal_builtin(s, d->type, d->ns);
                if (fixed_terminal_is_float_double(term)) {
                    char tv[512], tf[512];
                    const char* tp = text ? text : "";
                    if (strlen(tp) < sizeof(tv) &&
                        strlen(d->fixed) < sizeof(tf) &&
                        xsd_collapse_str(tp, tv, sizeof(tv)) &&
                        xsd_collapse_str(d->fixed, tf, sizeof(tf)))
                        match = xsd_float_double_value_equal(tv, tf);
                }
            }
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
        /* #2184 (§3.3.4.3 cl. 4): the instance-specified type must
         * OVERRIDE the selected type definition. A SIMPLE named type
         * against a COMPLEX selected type never overrides
         * (§3.3.4.2's substitutability has no such arm): the element
         * is locally invalid, and — per the cl. 4 Note — assessment
         * falls back to the selected type instead of returning. The
         * complex005 shapes pin the verdict. */
        if (xsi_type_override_fails_pos(s, xlocal, xuri, eff_type, guri,
                                        NULL, 0)) {
            verrf_atk(v, elem, "type-resolution", local, "element",
                      local,
                      "xsi:type does not override the selected type "
                      "definition");
        } else {
            validate_local_uri(v, s, elem, local, xlocal, xuri, xsi_q,
                               fill, NULL);
            return;
        }
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
    /* #2184 §3.11.4: the assessed type rides the element for the
     * identity-constraint value-space comparison (the keyref field
     * terminal looks it up). */
    xsd_record_element_type(v, elem, type);
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
    /* #2184 (wild042): xsi:nil's VALUE must spell a boolean —
     * "true"/"1"/"false"/"0" (§3.2.7.1 cl. 2 / §c-props-correct).
     * The admission walk stays silent (the xsi wildcard admits the
     * attribute; skip imposes no constraint), but a NON-boolean
     * lexical leaves the element unassessable against nil semantics
     * — the instance is invalid regardless of the declared type's
     * nillability. */
    if (nilattr && *nilattr &&
        strcmp(nilattr, "true") != 0 && strcmp(nilattr, "1") != 0 &&
        strcmp(nilattr, "false") != 0 && strcmp(nilattr, "0") != 0) {
        verrfk(v, "content-model", "element", local,
               "xsi:nil must be a boolean lexical, not the value "
               "given");
        return;
    }
    /* XSD boolean lexical: "true" and "1" both spell true (#2184 —
     * the saxon All rows write xsi:nil="1") */
    if (nilattr && (strcmp(nilattr, "true") == 0 ||
                    strcmp(nilattr, "1") == 0)) {
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
            if (text_valid(v, s, ct->text_type, text, ct->ns,
                           "element", local, elem))
                /* #2184: content-carried IDs/IDREFs join the
                 * bookkeeping (id008's pseudoID values, ii04's
                 * restrict, the §3.12.2 fills id014/id015). A
                 * simpleContent EXTENSION classifies against its BASE
                 * simple type (ct->ext_base — v15's listOfIDs list);
                 * text_type keeps its pinned lexical posture. */
                xsd_id_bookkeep(v, s,
                                ct->text_ext_base
                                             ? ct->text_ext_base
                                             : ct->text_type,
                                ct->ns, text, elem, 1);
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
        if (text_valid(v, s, type, text, euri, "element", local,
                       elem))
            xsd_id_bookkeep(v, s, type, euri, text, elem, 1);
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
            if (text_valid(v, s, builtin, text, euri, "element",
                           local, elem))
                xsd_id_bookkeep(v, s, builtin, euri, text, elem, 1);
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

/* A key tuple: the field values of one selected node, with each
 * field's ASSESSED TYPE (recorded by the main walk) beside it —
 * the keyref comparison is value-space, §3.11.4 (#2184). */
typedef struct xsd_key_tuple {
    char* values[8];
    char* types[8];
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
                      char* const* values, char* const* types,
                      size_t count) {
    table_ensure(head, name);
    XsdKeyTable* t = table_find(*head, name);
    if (!t) return;
    XsdKeyTuple* tup = (XsdKeyTuple*)calloc(1, sizeof(*tup));
    if (!tup) return;
    for (size_t i = 0; i < count && i < 8; i++)
        tup->values[i] = xsd_dup_str(values[i]);
    if (types)
        for (size_t i = 0; i < count && i < 8; i++)
            tup->types[i] = xsd_dup_str(types[i]);
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
 * leaves the qualified set). term_out receives the field's terminal
 * ELEMENT (NULL for attribute finals and misses) so the keyref
 * comparison can read the assessed type (#2184). */
static char* ic_field_walk(LeptrisElement node, const XsdIcPath* p,
                           int depth, LeptrisElement* term_out) {
    if (term_out) *term_out = (LeptrisElement)0;
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
    if (term_out) *term_out = node;
    return xsd_dup_str(leptris_element_text(node));
}

static char* ic_field_value(LeptrisElement node, const XsdIcPath* p,
                            LeptrisElement* term_out) {
    if (!p->desc) return ic_field_walk(node, p, 0, term_out);
    /* './/' in a field: the first descendant matching the FIRST step
     * continues the chain (same seed semantics as the selector) */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(node));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        char* got = NULL;
        LeptrisElement term = (LeptrisElement)0;
        if (ic_step_elem_match(&p->steps[0], ce))
            got = ic_field_walk(ce, p, 1, &term);
        if (!got) {
            got = ic_field_value(ce, p, &term);
        }
        if (got) {
            if (term_out) *term_out = term;
            return got;
        }
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
            char* vtyp[8] = {0};
            size_t nv = 0;
            int missing = 0;
            for (size_t f = 0; f < ic->field_count && f < 8; f++) {
                LeptrisElement term = (LeptrisElement)0;
                char* sv = ic_field_value(node, &ic->fpaths[f], &term);
                if (!sv) {
                    missing = 1;
                    break;
                }
                /* #2184: the terminal's ASSESSED type rides the
                 * tuple (the main walk recorded it) — attribute
                 * finals and untyped nodes stay NULL (string
                 * identity). */
                const char* tn = term ? xsd_recorded_type(v, term)
                                      : NULL;
                if (tn) vtyp[nv] = xsd_dup_str(tn);
                vals[nv++] = sv;
            }
            for (size_t k = 0; k < nv; k++)
                if (!vals[k] || !vals[k][0]) missing = 1;
            if (missing) {
                if (ic->kind == 0)
                    verrfk(v, "identity-constraint", "key", ic->name,
                           "a selected node is missing a field value");
                for (size_t k = 0; k < nv; k++) {
                    free(vals[k]);
                    free(vtyp[k]);
                }
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
                table_add(&scope, ic->name, vals, vtyp, nv);
                table_add(tables, ic->name, vals, vtyp, nv);
            } else {
                /* keyref: resolve later (deferred to pass 2 end) —
                 * store under the REFER name */
                table_add(tables, ic->refer ? ic->refer : "", vals,
                          vtyp, nv);
            }
            for (size_t k = 0; k < nv; k++) {
                free(vals[k]);
                free(vtyp[k]);
            }
        }
        free(sel.v);
    }
    tables_free(scope);
}

/* ---- #2184 §3.11.4: keyref tuples compare in the VALUE space -----
 *
 * The string lexical is not the identity: xs:time values carry a
 * timezone (12:00:00Z == 13:00:00+01:00; 24:00:00 == 00:00:00; a
 * tz-absent lexical never equals a tz-carrying one), durations
 * compare by total months/seconds, instants by their UTC reading,
 * numerics by value. The field terminal's ASSESSED type drives the
 * dispatch — types the main walk never recorded (wildcard-admitted
 * nodes) stay string-compared. */

/* xs:time reader: hh:mm:ss[.frac][Z|±hh:mm]. timeOnDay folds
 * 24:00:00 (and 24:00:00.0) to midnight — the equality identity —
 * while OTHER 24hh spellings stay malformed. Returns 0 on a
 * malformed lexical. */
static int ic_time_value(const char* v, double* out, int* has_tz) {
    *has_tz = 0;
    const char* p = v;
    int d[3];
    for (int i = 0; i < 3; i++) {
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
            return 0;
        d[i] = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (i < 2) {
            if (*p != ':') return 0;
            p++;
        }
    }
    double frac = 0;
    if (*p == '.') {
        p++;
        double f = 0.1;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            frac += (*p++ - '0') * f;
            f /= 10.0;
            digits++;
        }
        if (!digits) return 0;
    }
    if (d[0] > 24 || d[1] > 59 || d[2] > 60) return 0;
    if (d[0] == 24 && (d[1] != 0 || d[2] != 0)) return 0;
    long tzo = 0;
    if (*p == 'Z') {
        p++;
        *has_tz = 1;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        p++;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
            return 0;
        int zh = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        long zm = 0;
        if (*p == ':') {
            p++;
            if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
                return 0;
            zm = (p[0] - '0') * 10 + (p[1] - '0');
            p += 2;
        }
        if (zh > 14 || zm > 59) return 0;
        tzo = sign * (zh * 60 + zm);
        *has_tz = 1;
    }
    if (*p) return 0;
    double tod = (d[0] == 24 ? 0.0
                             : d[0] * 3600.0 + d[1] * 60.0 + d[2]) +
                 frac;
    *out = tod - (double)tzo * 60.0;
    return 1;
}

/* The tuple slot's value-space identity: 1 equal. `ta`/`tb` are the
 * recorded assessed type names (either may be NULL). */
static int ic_value_space_equal(struct leptris_xsd_schema* s,
                                const char* ta, const char* tb,
                                const char* a, const char* b) {
    if (!ta || !tb || strcmp(ta, tb) != 0) return 0;
    /* canonicalize the recorded name to its terminal builtin */
    char term[128];
    term[0] = 0;
    if (strncmp(ta, "xs:", 3) == 0) {
        snprintf(term, sizeof(term), "%s", ta);
    } else {
        const XsdSimple* st =
            s ? xsd_find_simple_pub(s, ta) : NULL;
        int hops = 0;
        while (st && hops++ < 32) {
            if (!st->base) break;
            if (strncmp(st->base, "xs:", 3) == 0) {
                snprintf(term, sizeof(term), "%s", st->base);
                break;
            }
            st = xsd_find_simple_pub(s, st->base);
        }
    }
    if (!term[0]) return 0;
    if (strcmp(term, "xs:time") == 0) {
        double ka, kb;
        int tza, tzb;
        if (!ic_time_value(a, &ka, &tza) ||
            !ic_time_value(b, &kb, &tzb))
            return 0;
        /* a timezone-less lexical never equals a tz-carrying one */
        return tza == tzb && ka == kb;
    }
    /* the shared facet table answers the rest of the ordered
     * space: instants (dateTime/date/dateTimeStamp), durations
     * (total months/seconds), numerics (exact decimals) */
    int c = xsd_facet_value_compare(term, a, b);
    return c == 0;
}

/* Pass 2 tail: every keyref tuple must appear in the referenced
 * key/unique table. */
static void resolve_keyrefs(struct xsd_validator* v,
                            struct leptris_xsd_schema* s,
                            XsdKeyTable* all, XsdKeyTable* refs) {
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
                    if (strcmp(have->values[k], want->values[k]) != 0 &&
                        !ic_value_space_equal(
                            s, have->types[k], want->types[k],
                            have->values[k], want->values[k]))
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
            for (size_t k = 0; k < tup->count; k++) {
                free(tup->values[k]);
                free(tup->types[k]);
            }
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
    for (size_t b = 0; b < sizeof(v->id_buckets) / sizeof(v->id_buckets[0]);
         b++) {
        struct id_seen* n = v->id_buckets[b];
        while (n) {
            struct id_seen* nn = n->next;
            free(n->v);
            free(n);
            n = nn;
        }
    }
    /* #2184: the assessed-type records (§3.11.4 value-space
     * comparison) */
    for (size_t i = 0; i < v->type_rec_n; i++)
        free(v->type_recs[i].type);
    free(v->type_recs);
    {
        struct id_ref* n = v->id_pool;
        while (n) {
            struct id_ref* nn = n->next;
            free(n->v);
            free(n);
            n = nn;
        }
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

    /* #2184 §C.4: the IDREF half of the bookkeeping — every recorded
     * IDREF/IDREFS value resolves against the run's ID table. */
    xsd_idrefs_resolve(v);

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
