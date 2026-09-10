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
            while (*q && (isalnum((unsigned char)*q) || *q == '_'))
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
    /* schema-level <let name= value=> */
    for (LeptrisElement let = leptris_element_first_child_any(root); let;
         let = leptris_element_next_sibling_any(let)) {
        if (!sch_is(let, "let")) continue;
        const char* ln = sch_attr(let, "name");
        const char* lv = sch_attr(let, "value");
        if (!ln || !lv) continue;
        SchLet* nl = (SchLet*)realloc(s->lets,
                                      (s->nlets + 1) * sizeof(SchLet));
        if (!nl) continue;
        s->lets = nl;
        s->lets[s->nlets].name = sch_dup(ln);
        s->lets[s->nlets].value = sch_substitute(
            lv, s->lets, s->nlets);
        s->lets[s->nlets].bare = 0;
        s->nlets++;
    }
    /* pass 1: collect ABSTRACT pattern bodies by id. */
    struct { char* id; LeptrisElement elem; } abst[32];
    size_t nabst = 0;
    for (LeptrisElement pat = leptris_element_first_child_any(root); pat;
         pat = leptris_element_next_sibling_any(pat)) {
        if (!sch_is(pat, "pattern")) continue;
        const char* ia = sch_attr(pat, "is-abstract");
        if (ia && strcmp(ia, "true") == 0 && nabst < 32) {
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
        if (abst_flag && strcmp(abst_flag, "true") == 0) continue;
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
        for (size_t i = 0; i < npl && nrl < 64; i++) {
            rl[nrl].name = sch_dup(pl[i].name);
            rl[nrl].value = sch_dup(pl[i].value);
            rl[nrl].bare = pl[i].bare;
            nrl++;
        }
        for (LeptrisElement rule =
                 leptris_element_first_child_any(src);
             rule;
             rule = leptris_element_next_sibling_any(rule)) {
            if (!sch_is(rule, "rule")) continue;
            size_t nrule = nrl;
            for (LeptrisElement rl2 =
                     leptris_element_first_child_any(rule);
                 rl2;
                 rl2 = leptris_element_next_sibling_any(rl2)) {
                if (!sch_is(rl2, "let")) continue;
                const char* ln = sch_attr(rl2, "name");
                const char* lv = sch_attr(rl2, "value");
                if (!ln || !lv || nrule >= 64) continue;
                char* sub = sch_substitute(lv, rl, nrule);
                int found = 0;
                for (size_t i = 0; i < nrule; i++)
                    if (strcmp(rl[i].name, ln) == 0) {
                        /* rule let shadows: free the old entry */
                        free(rl[i].name);
                        free(rl[i].value);
                        rl[i].name = sch_dup(ln);
                        rl[i].value = sub ? sub : sch_dup(lv);
                        rl[i].bare = 0;
                        found = 1;
                        break;
                    }
                if (found) continue;
                rl[nrule].name = sch_dup(ln);
                rl[nrule].value = sub;
                rl[nrule].bare = 0;
                nrule++;
            }
            const char* ctx = sch_attr(rule, "context");
            if (!ctx) continue;
            SchRule r = {0};
            r.context = sch_substitute_bare(ctx, rl, nrule);
            for (LeptrisElement asser =
                     leptris_element_first_child_any(rule);
                 asser;
                 asser = leptris_element_next_sibling_any(asser)) {
                int is_report = sch_is(asser, "report");
                if (!is_report && !sch_is(asser, "assert"))
                    continue;
                const char* test = sch_attr(asser, "test");
                if (!test) continue;
                const char* mtext = leptris_element_text(asser);
                char* msg = mtext ? sch_dup(mtext) : NULL;
                char* stest = sch_substitute(test, rl, nrule);
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
            /* free this rule's appended lets */
            for (size_t i = nrl; i < nrule; i++) {
                free(rl[i].name);
                free(rl[i].value);
            }
            for (size_t i = 0; i < nrl; i++) {
                free(rl[i].name);
                free(rl[i].value);
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
        for (size_t k = 0; k < p->n; k++) {
            SchRule* r = &p->rules[k];
            LeptrisXPathResult ctxr = leptris_xpath_eval(
                inst, NULL, r->context);
            if (!ctxr) continue;
            size_t cnt = 0;
            LeptrisElement* nodes = NULL;
            if (leptris_xpath_result_type(ctxr) ==
                LEPTRIS_XPATH_NODESET) {
                size_t total = leptris_xpath_result_get_nodes_ex(
                    ctxr, NULL, NULL, (size_t)-1);
                /* Schematron contexts are evaluated from the
                 * DOCUMENT; the engine's NULL context is the root
                 * ELEMENT, so a relative first step naming the
                 * root matches the root itself. */
                LeptrisElement root = leptris_document_root(inst);
                const char* ce = r->context;
                if (total == 0 && root && ce[0] != '/' &&
                    ce[0] != '(' && strchr(ce, '/') == NULL) {
                    const char* rn = leptris_element_name(root);
                    const char* lb = strchr(ce, '[');
                    size_t nl = lb ? (size_t)(lb - ce) : strlen(ce);
                    if (rn && strlen(rn) == nl &&
                        strncmp(ce, rn, nl) == 0) {
                        total = 1;
                        nodes = (LeptrisElement*)calloc(
                            1, sizeof(LeptrisElement));
                        if (nodes) {
                            nodes[0] = root;
                            cnt = 1;
                        }
                    }
                }
                if (total && !cnt) {
                    nodes = (LeptrisElement*)calloc(
                        total, sizeof(LeptrisElement));
                    if (nodes)
                        cnt = leptris_xpath_result_get_nodes(
                            ctxr, nodes, total);
                }
            }
            for (size_t m = 0; m < cnt; m++) {
                if (!nodes || !nodes[m]) continue;
                for (size_t a = 0; a < r->n; a++) {
                    SchAssert* as = &r->asserts[a];
                    LeptrisXPathResult tr = leptris_xpath_eval(
                        inst, nodes[m], as->test);
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
                        leptris_node_get_xpath((LeptrisNodeRef)nodes[m]);
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
            leptris_xpath_result_free(ctxr);
        }
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
