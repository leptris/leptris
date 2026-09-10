/* sch/schematron.c — native Schematron (lane 16, phases 1+2).
 *
 * Schema IR (pattern/rule/assert/report/@context/@test) parsed
 * from the ISO schematron vocabulary; evaluation rides the
 * engine's XPath; results are emitted as an SVRL document.
 * The 2025-edition feature set lands phase-by-phase behind the
 * specs (abstract patterns, phases, diagnostics follow).
 */

#include "../leptris_internal.h"
#include "../../include/leptris.h"
#include "../dom/element.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

typedef struct {
    char* name;
    char* value;
    int bare;   /* params substitute verbatim (names/paths) */
} SchLet;

/* Substitute $var references in an expression with their string
 * values (the engine's value-level variable model). */
static char* sch_substitute_typed(const char* expr,
                                  const SchLet* lets, size_t nlets,
                                  int bare_mode);

/* Bare mode: values substitute VERBATIM (params name element/
 * attribute names; @context is a path). Typed mode: numeric
 * values substitute bare, others as quoted string literals. */
static char* sch_substitute(const char* expr, const SchLet* lets,
                            size_t nlets) {
    return sch_substitute_typed(expr, lets, nlets, 0);
}

static char* sch_substitute_bare(const char* expr,
                                 const SchLet* lets, size_t nlets) {
    return sch_substitute_typed(expr, lets, nlets, 1);
}

static char* sch_substitute_typed(const char* expr,
                                  const SchLet* lets, size_t nlets,
                                  int bare_mode) {
    if (!expr) return NULL;
    size_t cap = strlen(expr) + 64;
    char* out = (char*)calloc(cap, 1);
    size_t ol = 0;
    for (const char* p = expr; *p;) {
        if (*p == '$') {
            const char* q = p + 1;
            /* NCName: letters, digits, '_', '-', '.' (a '-' in a
             * let name like $local-name must stay in the scan). */
            while (*q && (isalnum((unsigned char)*q) || *q == '_' ||
                          *q == '-' || *q == '.'))
                q++;
            size_t nl = (size_t)(q - p - 1);
            const char* val = NULL;
            int val_bare = 0;
            for (size_t i = 0; i < nlets; i++)
                if (strlen(lets[i].name) == nl &&
                    strncmp(lets[i].name, p + 1, nl) == 0) {
                    val = lets[i].value;
                    val_bare = lets[i].bare;
                    break;
                }
            if (val && val[0]) {
                /* Numeric values substitute BARE (XPath number
                 * coercion semantics; quoting a numeric let turns
                 * '10' >= @n into a string-compare shape the
                 * engine mishandles with element contexts). */
                char* endp = NULL;
                (void)strtod(val, &endp);
                if (bare_mode || val_bare ||
                    (endp && *endp == 0 && endp != val)) {
                    if (ol + strlen(val) + 2 >= cap) {
                        while (ol + strlen(val) + 2 >= cap)
                            cap *= 2;
                        char* no = (char*)realloc(out, cap);
                        if (!no) {
                            free(out);
                            return NULL;
                        }
                        out = no;
                    }
                    memcpy(out + ol, val, strlen(val));
                    ol += strlen(val);
                    p = q;
                    continue;
                }
                size_t vl = strlen(val);
                while (ol + vl + 2 >= cap) {
                    cap *= 2;
                    char* no = (char*)realloc(out, cap);
                    if (!no) { free(out); return NULL; }
                    out = no;
                }
                /* quote the value into a string literal */
                out[ol++] = '\'';
                memcpy(out + ol, val, vl);
                ol += vl;
                out[ol++] = '\'';
                p = q;
                continue;
            }
        }
        if (ol + 2 >= cap) {
            cap *= 2;
            char* no = (char*)realloc(out, cap);
            if (!no) { free(out); return NULL; }
            out = no;
        }
        out[ol++] = *p++;
    }
    out[ol] = 0;
    return out;
}

static char* sch_dup(const char* s);

typedef struct {
    int is_report;
    char* test;
    char* message;
} SchAssert;

typedef struct {
    char* context;
    SchAssert* asserts;
    size_t n;
} SchRule;


typedef struct {
    char* id;
    SchRule* rules;
    size_t n;
    /* phase 3+4: schema-level lets apply to every pattern;
     * abstract patterns are instantiated via is-a + params. */
    int active;   /* 0 when a phase selection excludes it */
} SchPattern;

struct leptris_schematron {
    SchPattern* patterns;
    size_t n;
    SchLet* lets;   /* schema-level */
    size_t nlets;
    char* error;
};

static void sch_set_error(struct leptris_schematron* s,
                          const char* msg) {
    free(s->error);
    s->error = sch_dup(msg);
}

static char* sch_dup(const char* s) {
    size_t n = strlen(s) + 1;
    char* r = (char*)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

/* Local-name compare (namespace-agnostic: the ISO vocabulary is
 * the only thing inside a schematron schema in our subset). */
static int sch_is(LeptrisElement e, const char* name) {
    const char* n = leptris_element_name(e);
    if (!n) return 0;
    const char* colon = strchr(n, ':');
    return strcmp(colon ? colon + 1 : n, name) == 0;
}

static const char* sch_attr(LeptrisElement e, const char* name) {
    for (struct leptris_attribute* a =
             leptris_element_get_first_attribute(e);
         a; a = leptris_attr_next(a)) {
        const char* an = attr_cname(a);
        if (!an) continue;
        const char* colon = strchr(an, ':');
        if (strcmp(colon ? colon + 1 : an, name) == 0)
            /* public accessor returns the entity-DECODED value;
             * attr_cvalue is the raw lexical form. */
            return leptris_attribute_get_value(e, a);
    }
    return NULL;
}

/* Literal-aware scan: does the expression still reference an
 * (undefined) variable $name? (Used after substitution.) */
static int sch_has_dollar_ref(const char* e) {
    char q = 0;
    for (; e && *e; e++) {
        if (q) { if (*e == q) q = 0; continue; }
        if (*e == '\'' || *e == '"') { q = *e; continue; }
        if (*e == '$') {
            const char* c = e + 1;
            if (isalnum((unsigned char)*c) || *c == '_') return 1;
        }
    }
    return 0;
}

static struct leptris_schematron* sch_parse_doc(LeptrisDocument doc,
                                                const char* sel_phase,
                                                char** err) {
    struct leptris_schematron* s =
        (struct leptris_schematron*)calloc(1, sizeof(*s));
    if (!s) {
        *err = sch_dup("out of memory");
        return NULL;
    }
    LeptrisElement root = leptris_document_root(doc);
    if (!root || !sch_is(root, "schema")) {
        sch_set_error(s, "root element must be schema");
        *err = sch_dup(s->error);
        leptris_schematron_free((LeptrisSchematron)s);
        return NULL;
    }
    const char* qb = sch_attr(root, "queryBinding");
    if (qb && strcmp(qb, "xslt") != 0 && strcmp(qb, "xslt2") != 0 &&
        strcmp(qb, "xslt3") != 0) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "unsupported queryBinding '%s' (xslt supported)",
                 qb);
        sch_set_error(s, msg);
        *err = sch_dup(s->error);
        leptris_schematron_free((LeptrisSchematron)s);
        return NULL;
    }
    /* ISO 2016 §5.4.13: with no explicit phase selection the
     * processor uses @defaultPhase. */
    const char* eff_phase = sel_phase;
    if (!eff_phase) {
        const char* dp = sch_attr(root, "defaultPhase");
        if (dp && dp[0]) eff_phase = dp;
    }
    sel_phase = eff_phase;

    /* schema-level <let name= value=> */
    for (LeptrisElement let = leptris_element_first_child_any(root); let;
         let = leptris_element_next_sibling_any(let)) {
        if (!sch_is(let, "let")) continue;
        const char* ln = sch_attr(let, "name");
        const char* lv = sch_attr(let, "value");
        /* Element-content let (ISO 2016 §5.4.5 cl. 2): the
         * variable value is the content's string value (a quoted
         * literal for substitution). */
        const char* text_val = NULL;
        if (ln && !lv) {
            text_val = leptris_element_text(let);
            if (!text_val || !text_val[0]) continue;
        }
        if (!ln || (!lv && !text_val)) continue;
        /* ISO 2016 §5.4.5: multiply-defined variable in the same
         * scope is an error. */
        int dup = 0;
        for (size_t i = 0; i < s->nlets; i++)
            if (strcmp(s->lets[i].name, ln) == 0) { dup = 1; break; }
        if (dup) {
            char msg[160];
            snprintf(msg, sizeof msg,
                     "variable '%s' multiply defined in schema scope",
                     ln);
            sch_set_error(s, msg);
            *err = sch_dup(s->error);
            leptris_schematron_free((LeptrisSchematron)s);
            return NULL;
        }
        SchLet* nl = (SchLet*)realloc(s->lets,
                                      (s->nlets + 1) * sizeof(SchLet));
        if (!nl) continue;
        s->lets = nl;
        s->lets[s->nlets].name = sch_dup(ln);
        if (lv) {
            s->lets[s->nlets].value = sch_substitute(
                lv, s->lets, s->nlets);
            /* let @value is an XPath EXPRESSION — verbatim */
            s->lets[s->nlets].bare = 1;
        } else {
            /* quote the content text as a string literal */
            size_t n = strlen(text_val);
            char* q = (char*)malloc(n + 3);
            if (q) {
                q[0] = '\'';
                memcpy(q + 1, text_val, n);
                q[n + 1] = '\'';
                q[n + 2] = 0;
                s->lets[s->nlets].value = q;
            } else {
                s->lets[s->nlets].value = sch_dup(text_val);
            }
            s->lets[s->nlets].bare = 0;
        }
        s->nlets++;
    }
    /* pass 1: collect ABSTRACT pattern bodies by id. */
    struct { char* id; LeptrisElement elem; } abst[32];
    size_t nabst = 0;
    for (LeptrisElement pat = leptris_element_first_child_any(root); pat;
         pat = leptris_element_next_sibling_any(pat)) {
        if (!sch_is(pat, "pattern")) continue;
        const char* ia = sch_attr(pat, "is-abstract");
        const char* iab = sch_attr(pat, "abstract");
        if (((ia && strcmp(ia, "true") == 0) ||
             (iab && strcmp(iab, "true") == 0)) && nabst < 32) {
            const char* pid = sch_attr(pat, "id");
            if (pid) {
                abst[nabst].id = sch_dup(pid);
                abst[nabst].elem = pat;
                nabst++;
            }
        }
    }
    /* walk patterns -> rules -> assert/report */
    for (LeptrisElement pat = leptris_element_first_child_any(root); pat;
         pat = leptris_element_next_sibling_any(pat)) {
        if (!sch_is(pat, "pattern")) continue;
        const char* abst_flag = sch_attr(pat, "is-abstract");
        const char* abst_flag2 = sch_attr(pat, "abstract");
        if ((abst_flag && strcmp(abst_flag, "true") == 0) ||
            (abst_flag2 && strcmp(abst_flag2, "true") == 0))
            continue;
        const char* isa = sch_attr(pat, "is-a");
        LeptrisElement src = pat;
        SchLet pl[32];
        size_t npl = 0;
        if (isa) {
            /* instantiate the abstract pattern with <param>
             * overrides merged over its defaults */
            LeptrisElement def = NULL;
            const char* defid = NULL;
            for (size_t i = 0; i < nabst; i++)
                if (strcmp(abst[i].id, isa) == 0) {
                    def = abst[i].elem;
                    defid = abst[i].id;
                    break;
                }
            if (!def) continue;
            /* defaults: abstract <param name= value=> */
            for (LeptrisElement prm =
                     leptris_element_first_child_any(def);
                 prm;
                 prm = leptris_element_next_sibling_any(prm)) {
                if (!sch_is(prm, "param")) continue;
                const char* pn = sch_attr(prm, "name");
                const char* pv = sch_attr(prm, "value");
                if (pn && pv && npl < 32) {
                    pl[npl].name = sch_dup(pn);
                    pl[npl].value = sch_dup(pv);
                    pl[npl].bare = 1;
                    npl++;
                }
            }
            /* instance overrides */
            for (LeptrisElement prm =
                     leptris_element_first_child_any(pat);
                 prm;
                 prm = leptris_element_next_sibling_any(prm)) {
                if (!sch_is(prm, "param")) continue;
                const char* pn = sch_attr(prm, "name");
                const char* pv = sch_attr(prm, "value");
                if (!pn || !pv) continue;
                int found = 0;
                for (size_t i = 0; i < npl; i++)
                    if (strcmp(pl[i].name, pn) == 0) {
                        free(pl[i].value);
                        pl[i].value = sch_dup(pv);
                        found = 1;
                        break;
                    }
                if (!found && npl < 32) {
                    pl[npl].name = sch_dup(pn);
                    pl[npl].value = sch_dup(pv);
                    pl[npl].bare = 1;
                    npl++;
                }
            }
            (void)defid;
            src = def;
        }
        const char* phase_active = (const char*)1; /* default on */
        (void)phase_active;
        SchPattern p = {0};
        p.active = 1;
        if (sel_phase) {
            p.active = 0;
            /* a phase activates THIS pattern when the schema's
             * phase lists its id (instance id or is-a ref). */
            const char* own = sch_attr(pat, "id");
            const char* ref = isa ? isa : own;
            for (LeptrisElement ph = leptris_element_first_child_any(root);
                 ph && !p.active;
                 ph = leptris_element_next_sibling_any(ph)) {
                if (!sch_is(ph, "phase")) continue;
                const char* fid = sch_attr(ph, "id");
                if (!fid || strcmp(fid, sel_phase) != 0) continue;
                for (LeptrisElement ac =
                         leptris_element_first_child_any(ph);
                     ac; ac = leptris_element_next_sibling_any(ac)) {
                    if (!sch_is(ac, "active")) continue;
                    const char* ap = sch_attr(ac, "pattern");
                    if (ap && ref && strcmp(ap, ref) == 0) {
                        p.active = 1;
                        break;
                    }
                }
            }
        }
        const char* pid = sch_attr(pat, "id");
        p.id = sch_dup(pid ? pid : (isa ? isa : ""));
        /* Rule-level let stack: a FRESH OWNED array per pattern
         * (deep copies of schema lets + params; rule lets shadow
         * in place). One owner — freed once after the rules. */
        SchLet rl[64];
        size_t nrl = 0;
        for (size_t i = 0; i < s->nlets && nrl < 64; i++) {
            rl[nrl].name = sch_dup(s->lets[i].name);
            rl[nrl].value = sch_dup(s->lets[i].value);
            rl[nrl].bare = s->lets[i].bare;
            nrl++;
        }
        /* phase-level lets: in scope while that phase is selected
         * (ISO 2016 §5.4.7); shadow schema lets. */
        if (sel_phase) {
            for (LeptrisElement ph =
                     leptris_element_first_child_any(root);
                 ph;
                 ph = leptris_element_next_sibling_any(ph)) {
                if (!sch_is(ph, "phase")) continue;
                const char* fid = sch_attr(ph, "id");
                if (!fid || strcmp(fid, sel_phase) != 0) continue;
                for (LeptrisElement ple =
                         leptris_element_first_child_any(ph);
                     ple;
                     ple = leptris_element_next_sibling_any(ple)) {
                    if (!sch_is(ple, "let")) continue;
                    const char* ln = sch_attr(ple, "name");
                    const char* lv = sch_attr(ple, "value");
                    if (!ln || !lv) continue;
                    size_t phdefs = 0;
                    for (LeptrisElement ple2 =
                             leptris_element_first_child_any(ph);
                         ple2;
                         ple2 = leptris_element_next_sibling_any(ple2)) {
                        if (!sch_is(ple2, "let")) continue;
                        const char* other = sch_attr(ple2, "name");
                        if (other && strcmp(other, ln) == 0) phdefs++;
                    }
                    if (phdefs > 1) {
                        char msg[160];
                        snprintf(msg, sizeof msg,
                                 "variable '%s' multiply defined in"
                                 " phase",
                                 ln);
                        sch_set_error(s, msg);
                        *err = sch_dup(s->error);
                        for (size_t i = 0; i < nrl; i++) {
                            free(rl[i].name);
                            free(rl[i].value);
                        }
                        leptris_schematron_free((LeptrisSchematron)s);
                        return NULL;
                    }
                    char* sub = sch_substitute(lv, rl, nrl);
                    int found = 0;
                    for (size_t i = 0; i < nrl; i++)
                        if (strcmp(rl[i].name, ln) == 0) {
                            free(rl[i].value);
                            rl[i].value = sub ? sub : sch_dup(lv);
                            rl[i].bare = 1;
                            found = 1;
                            break;
                        }
                    if (found || nrl >= 64) {
                        if (!found) free(sub);
                        continue;
                    }
                    rl[nrl].name = sch_dup(ln);
                    rl[nrl].value = sub ? sub : sch_dup(lv);
                    rl[nrl].bare = 1;
                    nrl++;
                }
                break;
            }
        }
        for (size_t i = 0; i < npl && nrl < 64; i++) {
            rl[nrl].name = sch_dup(pl[i].name);
            rl[nrl].value = sch_dup(pl[i].value);
            rl[nrl].bare = pl[i].bare;
            nrl++;
        }
        /* pattern-level <let> children (ISO 2016 §5.4.6): shadow
         * schema lets by name, visible to every rule below. A name
         * defined TWICE in the same pattern is an error; a name not
         * present at schema level also becomes globally visible to
         * LATER patterns (the suite's "pattern variable has global
         * scope"), without clobbering the schema binding. */
        for (LeptrisElement pl2 = leptris_element_first_child_any(src);
             pl2;
             pl2 = leptris_element_next_sibling_any(pl2)) {
            if (!sch_is(pl2, "let")) continue;
            const char* ln = sch_attr(pl2, "name");
            const char* lv = sch_attr(pl2, "value");
            if (!ln || !lv) continue;
            /* duplicate = same name on more than one let in THIS
             * pattern */
            size_t defs = 0;
            for (LeptrisElement pl3 =
                     leptris_element_first_child_any(src);
                 pl3;
                 pl3 = leptris_element_next_sibling_any(pl3)) {
                if (!sch_is(pl3, "let")) continue;
                const char* other = sch_attr(pl3, "name");
                if (other && strcmp(other, ln) == 0) defs++;
            }
            /* Corpus semantics (multiply-defined-globally cases):
             * a pattern let REDEFINING a global with an IDENTICAL
             * value is a global redefinition error; a DIFFERENT
             * value shadows (pattern-scoped) instead. */
            int global_same = 0;
            for (size_t i = 0; i < s->nlets; i++)
                if (strcmp(s->lets[i].name, ln) == 0 &&
                    strcmp(s->lets[i].value, lv) == 0) {
                    global_same = 1;
                    break;
                }
            if (global_same) {
                char msg[160];
                snprintf(msg, sizeof msg,
                         "variable '%s' multiply defined globally",
                         ln);
                sch_set_error(s, msg);
                *err = sch_dup(s->error);
                for (size_t i = 0; i < nrl; i++) {
                    free(rl[i].name);
                    free(rl[i].value);
                }
                for (size_t i = 0; i < npl; i++) {
                    free(pl[i].name);
                    free(pl[i].value);
                }
                free(p.id);
                leptris_schematron_free((LeptrisSchematron)s);
                return NULL;
            }
            if (defs > 1) {
                char msg[160];
                snprintf(msg, sizeof msg,
                         "variable '%s' multiply defined in pattern",
                         ln);
                sch_set_error(s, msg);
                *err = sch_dup(s->error);
                for (size_t i = 0; i < nrl; i++) {
                    free(rl[i].name);
                    free(rl[i].value);
                }
                for (size_t i = 0; i < npl; i++) {
                    free(pl[i].name);
                    free(pl[i].value);
                }
                free(p.id);
                leptris_schematron_free((LeptrisSchematron)s);
                return NULL;
            }
            char* sub = sch_substitute(lv, rl, nrl);
            int found = 0;
            for (size_t i = 0; i < nrl; i++)
                if (strcmp(rl[i].name, ln) == 0) {
                    free(rl[i].value);
                    rl[i].value = sub ? sub : sch_dup(lv);
                    rl[i].bare = 1;
                    found = 1;
                    break;
                }
            if (!found && nrl < 64) {
                rl[nrl].name = sch_dup(ln);
                rl[nrl].value = sub ? sub : sch_dup(lv);
                rl[nrl].bare = 1;
                nrl++;
            } else if (!found) {
                free(sub);
            }
            /* global visibility for later patterns when new */
            int global_known = 0;
            for (size_t i = 0; i < s->nlets; i++)
                if (strcmp(s->lets[i].name, ln) == 0) {
                    global_known = 1;
                    break;
                }
            if (!global_known) {
                SchLet* nl = (SchLet*)realloc(
                    s->lets, (s->nlets + 1) * sizeof(SchLet));
                if (nl) {
                    s->lets = nl;
                    s->lets[s->nlets].name = sch_dup(ln);
                    s->lets[s->nlets].value = sch_dup(
                        sub ? sub : lv);
                    s->lets[s->nlets].bare = 1;
                    s->nlets++;
                }
            }
        }
        /* pattern/@documents referencing an undefined variable
         * is an error (undefined-07). */
        {
            const char* docs = sch_attr(src, "documents");
            if (docs) {
                char* sdocs = sch_substitute(docs, rl, nrl);
                if (sch_has_dollar_ref(sdocs ? sdocs : docs)) {
                    sch_set_error(
                        s, "undefined variable in @documents");
                    *err = sch_dup(s->error);
                    free(sdocs);
                    for (size_t i = 0; i < nrl; i++) {
                        free(rl[i].name);
                        free(rl[i].value);
                    }
                    for (size_t i = 0; i < npl; i++) {
                        free(pl[i].name);
                        free(pl[i].value);
                    }
                    free(p.id);
                    leptris_schematron_free((LeptrisSchematron)s);
                    return NULL;
                }
                free(sdocs);
            }
        }
        /* Abstract rules (ISO 2016 §5.5.4): collected, not fired;
         * <extends rule=> inside THIS pattern's rules pulls in
         * their asserts/reports. Cross-pattern extends is an
         * error. */
        LeptrisElement abst_rules[32];
        size_t nabst_rules = 0;
        for (LeptrisElement rule =
                 leptris_element_first_child_any(src);
             rule;
             rule = leptris_element_next_sibling_any(rule)) {
            const char* ab = sch_attr(rule, "abstract");
            if (sch_is(rule, "rule") && ab &&
                strcmp(ab, "true") == 0 && nabst_rules < 32)
                abst_rules[nabst_rules++] = rule;
        }
        for (LeptrisElement rule =
                 leptris_element_first_child_any(src);
             rule;
             rule = leptris_element_next_sibling_any(rule)) {
            if (!sch_is(rule, "rule")) continue;
            {
                const char* ab = sch_attr(rule, "abstract");
                if (ab && strcmp(ab, "true") == 0) continue;
            }
            /* Per-rule OWNED snapshot of the let stack: rule lets
             * shadow base lets inside the snapshot only — the base
             * array stays immutable for the next rule (freeing or
             * mutating it here corrupted rule 2+: SIGABRT on the
             * two-rules-under-a-schema-let shape). */
            SchLet snap[128];
            size_t nsnap = 0;
            for (size_t i = 0; i < nrl && nsnap < 128; i++) {
                snap[nsnap].name = sch_dup(rl[i].name);
                snap[nsnap].value = sch_dup(rl[i].value);
                snap[nsnap].bare = rl[i].bare;
                nsnap++;
            }
            for (LeptrisElement rl2 =
                     leptris_element_first_child_any(rule);
                 rl2;
                 rl2 = leptris_element_next_sibling_any(rl2)) {
                if (!sch_is(rl2, "let")) continue;
                const char* ln = sch_attr(rl2, "name");
                const char* lv = sch_attr(rl2, "value");
                if (!ln || !lv || nsnap >= 128) continue;
                size_t rdefs = 0;
                for (LeptrisElement rl3 =
                         leptris_element_first_child_any(rule);
                     rl3;
                     rl3 = leptris_element_next_sibling_any(rl3)) {
                    if (!sch_is(rl3, "let")) continue;
                    const char* other = sch_attr(rl3, "name");
                    if (other && strcmp(other, ln) == 0) rdefs++;
                }
                if (rdefs > 1) {
                    char msg[160];
                    snprintf(msg, sizeof msg,
                             "variable '%s' multiply defined in rule",
                             ln);
                    sch_set_error(s, msg);
                    *err = sch_dup(s->error);
                    for (size_t i = 0; i < nsnap; i++) {
                        free(snap[i].name);
                        free(snap[i].value);
                    }
                    for (size_t i = 0; i < nrl; i++) {
                        free(rl[i].name);
                        free(rl[i].value);
                    }
                    for (size_t i = 0; i < npl; i++) {
                        free(pl[i].name);
                        free(pl[i].value);
                    }
                    free(p.id);
                    leptris_schematron_free((LeptrisSchematron)s);
                    return NULL;
                }
                char* sub = sch_substitute(lv, snap, nsnap);
                int found = 0;
                for (size_t i = 0; i < nsnap; i++)
                    if (strcmp(snap[i].name, ln) == 0) {
                        free(snap[i].name);
                        free(snap[i].value);
                        snap[i].name = sch_dup(ln);
                        snap[i].value = sub ? sub : sch_dup(lv);
                        snap[i].bare = 1;
                        found = 1;
                        break;
                    }
                if (found) continue;
                snap[nsnap].name = sch_dup(ln);
                snap[nsnap].value = sub;
                snap[nsnap].bare = 1;
                nsnap++;
            }
            const char* ctx = sch_attr(rule, "context");
            if (!ctx) {
                for (size_t i = 0; i < nsnap; i++) {
                    free(snap[i].name);
                    free(snap[i].value);
                }
                continue;
            }
            SchRule r = {0};
            r.context = sch_substitute_bare(ctx, snap, nsnap);
            if (sch_has_dollar_ref(r.context)) {
                sch_set_error(s, "undefined variable in rule context");
                *err = sch_dup(s->error);
                free(r.context);
                for (size_t i = 0; i < nsnap; i++) {
                    free(snap[i].name);
                    free(snap[i].value);
                }
                for (size_t i = 0; i < nrl; i++) {
                    free(rl[i].name);
                    free(rl[i].value);
                }
                for (size_t i = 0; i < npl; i++) {
                    free(pl[i].name);
                    free(pl[i].value);
                }
                free(p.id);
                leptris_schematron_free((LeptrisSchematron)s);
                return NULL;
            }
            /* sch:value-of/@select references must resolve too
             * (nested in assert/report content — walk the rule
             * subtree). */
            LeptrisElement stack[128];
            size_t nstack = 0;
            if (nstack < 128) stack[nstack++] = rule;
            while (nstack > 0) {
                LeptrisElement c = stack[--nstack];
                for (LeptrisElement k =
                         leptris_element_first_child_any(c);
                     k;
                     k = leptris_element_next_sibling_any(k)) {
                    if (nstack < 128) stack[nstack++] = k;
                }
                if (c == rule) continue;
                const char* sel = NULL;
                if (sch_is(c, "value-of"))
                    sel = sch_attr(c, "select");
                else if (sch_is(c, "name"))
                    sel = sch_attr(c, "path");
                if (!sel) continue;
                char* ssel = sch_substitute(sel, snap, nsnap);
                if (sch_has_dollar_ref(ssel ? ssel : sel)) {
                    sch_set_error(
                        s, "undefined variable in value-of/@select");
                    *err = sch_dup(s->error);
                    free(ssel);
                    free(r.context);
                    for (size_t i = 0; i < nsnap; i++) {
                        free(snap[i].name);
                        free(snap[i].value);
                    }
                    for (size_t i = 0; i < nrl; i++) {
                        free(rl[i].name);
                        free(rl[i].value);
                    }
                    for (size_t i = 0; i < npl; i++) {
                        free(pl[i].name);
                        free(pl[i].value);
                    }
                    free(p.id);
                    leptris_schematron_free((LeptrisSchematron)s);
                    return NULL;
                }
                free(ssel);
            }
            /* assert/report sources: the rule's own children PLUS
             * the asserts of abstract rules pulled in via
             * <extends rule=> (same pattern only). */
            LeptrisElement srcs[64];
            size_t nsrcs = 0;
            for (LeptrisElement c =
                     leptris_element_first_child_any(rule);
                 c && nsrcs < 64;
                 c = leptris_element_next_sibling_any(c)) {
                if (sch_is(c, "extends")) {
                    const char* rid = sch_attr(c, "rule");
                    if (!rid) continue;
                    LeptrisElement target = NULL;
                    for (size_t z = 0; z < nabst_rules; z++) {
                        const char* aid =
                            sch_attr(abst_rules[z], "id");
                        if (aid && strcmp(aid, rid) == 0) {
                            target = abst_rules[z];
                            break;
                        }
                    }
                    if (!target) {
                        char msg[160];
                        snprintf(msg, sizeof msg,
                                 "extends of unknown or foreign rule"
                                 " '%s'",
                                 rid);
                        sch_set_error(s, msg);
                        *err = sch_dup(s->error);
                        free(r.context);
                        for (size_t i = 0; i < nsnap; i++) {
                            free(snap[i].name);
                            free(snap[i].value);
                        }
                        for (size_t i = 0; i < nrl; i++) {
                            free(rl[i].name);
                            free(rl[i].value);
                        }
                        for (size_t i = 0; i < npl; i++) {
                            free(pl[i].name);
                            free(pl[i].value);
                        }
                        free(p.id);
                        leptris_schematron_free((LeptrisSchematron)s);
                        return NULL;
                    }
                    for (LeptrisElement ac =
                             leptris_element_first_child_any(target);
                         ac && nsrcs < 64;
                         ac = leptris_element_next_sibling_any(ac)) {
                        int rep = sch_is(ac, "report");
                        if (rep || sch_is(ac, "assert"))
                            srcs[nsrcs++] = ac;
                    }
                } else {
                    srcs[nsrcs++] = c;
                }
            }
            for (size_t si = 0; si < nsrcs; si++) {
                LeptrisElement asser = srcs[si];
                int is_report = sch_is(asser, "report");
                if (!is_report && !sch_is(asser, "assert"))
                    continue;
                const char* test = sch_attr(asser, "test");
                if (!test) continue;
                const char* mtext = leptris_element_text(asser);
                char* msg = mtext ? sch_dup(mtext) : NULL;
                char* stest = sch_substitute(test, snap, nsnap);
                if (sch_has_dollar_ref(stest ? stest : test)) {
                    sch_set_error(s, "undefined variable in assert");
                    *err = sch_dup(s->error);
                    free(msg);
                    free(stest);
                    free(r.context);
                    for (size_t z = 0; z < r.n; z++) {
                        free(r.asserts[z].test);
                        free(r.asserts[z].message);
                    }
                    free(r.asserts);
                    for (size_t i = 0; i < nsnap; i++) {
                        free(snap[i].name);
                        free(snap[i].value);
                    }
                    for (size_t i = 0; i < nrl; i++) {
                        free(rl[i].name);
                        free(rl[i].value);
                    }
                    for (size_t i = 0; i < npl; i++) {
                        free(pl[i].name);
                        free(pl[i].value);
                    }
                    free(p.id);
                    leptris_schematron_free((LeptrisSchematron)s);
                    return NULL;
                }
                SchAssert* na = (SchAssert*)realloc(
                    r.asserts, (r.n + 1) * sizeof(SchAssert));
                if (!na) {
                    free(msg);
                    free(stest);
                    continue;
                }
                r.asserts = na;
                r.asserts[r.n].is_report = is_report;
                r.asserts[r.n].test = stest ? stest : sch_dup(test);
                r.asserts[r.n].message = msg ? msg : sch_dup("");
                r.n++;
            }
            for (size_t i = 0; i < nsnap; i++) {
                free(snap[i].name);
                free(snap[i].value);
            }
            SchRule* nr = (SchRule*)realloc(
                p.rules, (p.n + 1) * sizeof(SchRule));
            if (!nr) {
                free(r.context);
                for (size_t k = 0; k < r.n; k++) {
                    free(r.asserts[k].test);
                    free(r.asserts[k].message);
                }
                free(r.asserts);
                continue;
            }
            p.rules = nr;
            p.rules[p.n++] = r;
        }
        SchPattern* np = (SchPattern*)realloc(
            s->patterns, (s->n + 1) * sizeof(SchPattern));
        if (!np) {
            free(p.id);
            for (size_t k = 0; k < p.n; k++) {
                free(p.rules[k].context);
                for (size_t m = 0; m < p.rules[k].n; m++) {
                    free(p.rules[k].asserts[m].test);
                    free(p.rules[k].asserts[m].message);
                }
                free(p.rules[k].asserts);
            }
            free(p.rules);
            continue;
        }
        s->patterns = np;
        s->patterns[s->n++] = p;
        for (size_t i = 0; i < nrl; i++) {
            free(rl[i].name);
            free(rl[i].value);
        }
        for (size_t i = 0; i < npl; i++) {
            free(pl[i].name);
            free(pl[i].value);
        }
    }
    for (size_t i = 0; i < nabst; i++) free(abst[i].id);
    return s;
}

LEPTRIS_API LeptrisSchematron leptris_schematron_parse(
    const char* schema, size_t len, LeptrisStatus* status) {
    return leptris_schematron_parse_phase(schema, len, NULL,
                                          status);
}

LEPTRIS_API LeptrisSchematron leptris_schematron_parse_phase(
    const char* schema, size_t len, const char* phase_id,
    LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!schema) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(schema, len, &st);
    if (!doc) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    char* err = NULL;
    struct leptris_schematron* s =
        sch_parse_doc(doc, phase_id && phase_id[0] ? phase_id : NULL,
                      &err);
    leptris_document_free(doc);
    if (!s) {
        leptris_set_error(LEPTRIS_ERROR_PARSE, err ? err : "bad");
        free(err);
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    return (LeptrisSchematron)s;
}

LEPTRIS_API LeptrisSchematron leptris_schematron_parse_file(
    const char* path, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!path) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        if (status) *status = LEPTRIS_ERROR_IO;
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = (char*)malloc(sz > 0 ? (size_t)sz + 1 : 1);
    if (!buf) {
        fclose(fp);
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    LeptrisSchematron s =
        leptris_schematron_parse(buf, rd, status);
    free(buf);
    return s;
}

LEPTRIS_API void leptris_schematron_free(LeptrisSchematron sch) {
    struct leptris_schematron* s = (struct leptris_schematron*)sch;
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) {
        free(s->patterns[i].id);
        for (size_t k = 0; k < s->patterns[i].n; k++) {
            free(s->patterns[i].rules[k].context);
            for (size_t m = 0; m < s->patterns[i].rules[k].n;
                 m++) {
                free(s->patterns[i].rules[k].asserts[m].test);
                free(s->patterns[i].rules[k].asserts[m].message);
            }
            free(s->patterns[i].rules[k].asserts);
        }
        free(s->patterns[i].rules);
    }
    for (size_t i = 0; i < s->nlets; i++) {
        free(s->lets[i].name);
        free(s->lets[i].value);
    }
    free(s->lets);
    free(s->patterns);
    free(s->error);
    free(s);
}

LEPTRIS_API const char* leptris_schematron_error(
    LeptrisSchematron sch) {
    struct leptris_schematron* s = (struct leptris_schematron*)sch;
    return s ? s->error : NULL;
}

/* ---- evaluation ---- */

static LeptrisElement sch_add_child(LeptrisDocument d,
                                    LeptrisElement parent,
                                    const char* name) {
    LeptrisElement e = leptris_element_create(d, name);
    if (!e) return NULL;
    if (parent)
        leptris_element_append_child(parent, e);
    return e;
}

/* Runs every pattern; returns an SVRL document and writes the
 * failed-assert count. NULL doc on allocation failure. */
static LeptrisDocument sch_run(struct leptris_schematron* s,
                               LeptrisDocument inst,
                               size_t* failed) {
    LeptrisDocument svrl = leptris_document_create();
    if (!svrl) return NULL;
    LeptrisElement root =
        sch_add_child(svrl, NULL, "svrl:schematron-output");
    if (!root) {
        leptris_document_free(svrl);
        return NULL;
    }
    leptris_element_set_attribute(
        root, "xmlns:svrl",
        "http://purl.oclc.org/dsdl/schematron");
    leptris_document_set_root(svrl, root);
    *failed = 0;
    for (size_t i = 0; i < s->n; i++) {
        SchPattern* p = &s->patterns[i];
        if (!p->active) continue;
        /* Within a pattern a node fires at most ONE rule — the
         * FIRST whose context matches it (ISO 2016 §5.5.5). */
        LeptrisNodeRef* matched = NULL;
        size_t nmatched = 0, capmatched = 0;
        for (size_t k = 0; k < p->n; k++) {
            SchRule* r = &p->rules[k];
            /* ISO Schematron @context is an XSLT pattern: a
             * relative pattern matches at ANY depth (pattern
             * `item` ≡ `//item`, `@id` ≡ `//@id`, `comment()` ≡
             * `//comment()`), `/` is the document node. Prefix
             * relative patterns with //; absolute and
             * parenthesized expressions pass through. */
            const char* ce = r->context;
            char* cbuf = NULL;
            const char* cexpr = ce;
            if (ce[0] != '/' && ce[0] != '(') {
                cbuf = (char*)malloc(strlen(ce) + 3);
                if (cbuf) {
                    cbuf[0] = '/';
                    cbuf[1] = '/';
                    memcpy(cbuf + 2, ce, strlen(ce) + 1);
                    cexpr = cbuf;
                }
            }
            LeptrisXPathResult ctxr = leptris_xpath_eval(
                inst, NULL, cexpr);
            free(cbuf);
            if (!ctxr) continue;
            size_t cnt = 0;
            LeptrisNodeRef* nodes = NULL;
            LeptrisXPathNodeKind* kinds = NULL;
            if (leptris_xpath_result_type(ctxr) ==
                LEPTRIS_XPATH_NODESET) {
                size_t total = leptris_xpath_result_get_nodes_ex(
                    ctxr, NULL, NULL, (size_t)-1);
                if (total) {
                    nodes = (LeptrisNodeRef*)calloc(
                        total, sizeof(LeptrisNodeRef));
                    kinds = (LeptrisXPathNodeKind*)calloc(
                        total, sizeof(LeptrisXPathNodeKind));
                    if (nodes && kinds)
                        cnt = leptris_xpath_result_get_nodes_ex(
                            ctxr, nodes, kinds, total);
                }
            }
            for (size_t m = 0; m < cnt; m++) {
                if (!nodes || !nodes[m]) continue;
                int seen = 0;
                for (size_t q = 0; q < nmatched; q++)
                    if (matched[q] == nodes[m]) { seen = 1; break; }
                if (seen) continue;
                if (nmatched == capmatched) {
                    size_t nc = capmatched ? capmatched * 2 : 16;
                    LeptrisNodeRef* nm = (LeptrisNodeRef*)realloc(
                        matched, nc * sizeof(LeptrisNodeRef));
                    if (nm) { matched = nm; capmatched = nc; }
                }
                if (nmatched < capmatched)
                    matched[nmatched++] = nodes[m];
                /* Asserts evaluate with the matched node as
                 * context. Non-element contexts (attribute,
                 * comment, text, PI) evaluate from their parent;
                 * the DOCUMENT node evaluates as itself —
                 * leptris_document_node works as an eval context
                 * (child axis sees the root element). */
                LeptrisElement eval_ctx = NULL;
                if (cexpr[0] == '/' && cexpr[1] == 0) {
                    /* "/" is the document node: tests' child axis
                     * must see the root element. */
                    eval_ctx =
                        (LeptrisElement)leptris_document_node(inst);
                } else if (kinds[m] == LEPTRIS_XPATH_NODE_ELEMENT) {
                    eval_ctx = (LeptrisElement)nodes[m];
                } else if (nodes[m] ==
                           (LeptrisNodeRef)leptris_document_node(
                               inst)) {
                    eval_ctx =
                        (LeptrisElement)leptris_document_node(inst);
                } else {
                    eval_ctx = leptris_node_parent(nodes[m]);
                    if (!eval_ctx)
                        eval_ctx = (LeptrisElement)
                            leptris_document_node(inst);
                    if (!eval_ctx)
                        eval_ctx = leptris_document_root(inst);
                }
                for (size_t a = 0; a < r->n; a++) {
                    SchAssert* as = &r->asserts[a];
                    LeptrisXPathResult tr = leptris_xpath_eval(
                        inst, eval_ctx, as->test);
                    int truthy = tr ? leptris_xpath_result_boolean(tr)
                                    : 0;                    int fire = as->is_report ? truthy : !truthy;
                    if (tr) leptris_xpath_result_free(tr);
                    if (!fire) continue;
                    LeptrisElement op = sch_add_child(
                        svrl, root,
                        as->is_report ? "svrl:successful-report"
                                      : "svrl:failed-assert");
                    if (!op) continue;
                    leptris_element_set_attribute(op, "test",
                                                  as->test);
                    char* loc =
                        leptris_node_get_xpath(nodes[m]);
                    if (loc) {
                        leptris_element_set_attribute(op,
                                                      "location",
                                                      loc);
                        leptris_free_string(loc);
                    }
                    LeptrisElement tx = sch_add_child(
                        svrl, op, "svrl:text");
                    if (tx)
                        leptris_element_set_text(tx, as->message);
                    if (!as->is_report) (*failed)++;
                }
            }
            free(nodes);
            free(kinds);
            leptris_xpath_result_free(ctxr);
        }
        free(matched);
    }
    return svrl;
}

LEPTRIS_API LeptrisDocument leptris_schematron_validate(
    LeptrisSchematron sch, LeptrisDocument doc) {
    struct leptris_schematron* s = (struct leptris_schematron*)sch;
    if (!s || !doc) return NULL;
    size_t failed = 0;
    return sch_run(s, doc, &failed);
}

LEPTRIS_API int leptris_schematron_valid(LeptrisSchematron sch,
                                         LeptrisDocument doc) {
    struct leptris_schematron* s = (struct leptris_schematron*)sch;
    if (!s || !doc) return 0;
    size_t failed = 0;
    LeptrisDocument svrl = sch_run(s, doc, &failed);
    if (svrl) leptris_document_free(svrl);
    return failed == 0 ? 1 : 0;
}
