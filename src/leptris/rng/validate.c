/* rng/validate.c — core-subset validator (#878 phase 2).
 *
 * Matches an instance document against the pattern IR. Simplified
 * RELAX NG semantics sufficient for the core subset: element/
 * attribute/text/data/value/choice/group/interleave/repeats/ref.
 * Child-sequence matching is greedy left-to-right (a full hedge
 * automaton arrives in phase 3 with the official suite); the
 * attribute rule is exact: every instance attribute must be
 * consumed and every required attribute pattern satisfied.
 */
#include "rng_internal.h"
#include "../dom/element.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    RngGrammar* g;
    struct leptris_relaxng* r;  /* the public handle (for #878
                                    per-error list). */
    char err[512];
    int failed;
    int depth;   /* ref-recursion guard (cyclic defines) */
    /* #878 sub-fix #3: the verdict pass runs quiet (no error
     * recording — speculative backtracking would duplicate);
     * rng_diagnose walks the failure and emits the Jing-shaped
     * per-element list. */
    int quiet;
    void* diag_pool;  /* diagnose allocations, freed on exit */
} RngVal;

/* #1126: one kind-aware emitter through dom/diag. The KIND
 * selects the Jing reporting convention (which column, which
 * vocabulary family). The verdict pass stays quiet — speculative
 * backtracking must not record — so the diagnose walk is the only
 * recorder; the legacy first-message bookkeeping runs in both. */
static void diag(RngVal* v, LeptrisDiagKind kind, LeptrisElement e,
                 const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static void diag(RngVal* v, LeptrisDiagKind kind, LeptrisElement e,
                 const char* fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    int line = 0;
    if (e) {
        LeptrisSourcePosition pos;
        leptris_node_source_position((LeptrisNodeRef)e, &pos);
        line = pos.line;
    }

    if (!v->quiet && v->r) {
        leptris_diag_emit(&v->r->diags, &v->r->diag_count,
                          &v->r->diag_cap, kind, (LeptrisNodeRef)e,
                          "%s", msg);
    }
    if (!v->failed) {
        v->failed = 1;
        snprintf(v->err, sizeof(v->err), "%d:0: error: %s", line, msg);
    }
}

/* #1137: SPECULATIVE probes (backtracking alternatives, optional
 * fallbacks) must never poison the matcher's v->failed short-circuit
 * — an omitted <optional> child probes, fails on the name, falls
 * back to zero-match, and the rest of the model must still run.
 * Record in the diagnose pass only; no verdict bookkeeping. */
static void diag_probe(RngVal* v, LeptrisDiagKind kind, LeptrisElement e,
                       const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static void diag_probe(RngVal* v, LeptrisDiagKind kind, LeptrisElement e,
                       const char* fmt, ...) {
    if (v->quiet || !v->r) return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    leptris_diag_emit(&v->r->diags, &v->r->diag_count,
                      &v->r->diag_cap, kind, (LeptrisNodeRef)e,
                      "%s", msg);
}

/* "an integer" / "a string" — Jing's article. */
static const char* art(const char* dt) {
    if (!dt || !*dt) return "a";
    char c = (char)tolower((unsigned char)dt[0]);
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
               ? "an" : "a";
}

static RngDefine* find_define(RngGrammar* g, const char* name) {
    for (RngDefine* d = g->defines; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

/* --- data leaves + <param> facets ------------------------------- */

/* XSD length is in characters, not bytes. */
static size_t utf8_len(const char* s) {
    size_t n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) n++;
    return n;
}

static int data_lexical_ok(const RngPattern* data, const char* t) {
    if (data->datatype && strcmp(data->datatype, "integer") == 0) {
        const char* q = t;
        if (*q == '-' || *q == '+') q++;
        if (!*q) return 0;
        for (; *q; q++)
            if (!isdigit((unsigned char)*q)) return 0;
    }
    return 1;
}

static int param_ok(const RngPattern* prm, const char* t) {
    const char* name = prm->name ? prm->name : "";
    const char* val = prm->value ? prm->value : "";
    if (strcmp(name, "pattern") == 0)
        return rng_regex_matches(val, t);
    if (strcmp(name, "minLength") == 0)
        return utf8_len(t) >= (size_t)atol(val);
    if (strcmp(name, "maxLength") == 0)
        return utf8_len(t) <= (size_t)atol(val);
    if (strcmp(name, "length") == 0)
        return utf8_len(t) == (size_t)atol(val);
    if (strcmp(name, "minInclusive") == 0)
        return strtod(t, NULL) >= strtod(val, NULL);
    if (strcmp(name, "maxInclusive") == 0)
        return strtod(t, NULL) <= strtod(val, NULL);
    if (strcmp(name, "minExclusive") == 0)
        return strtod(t, NULL) > strtod(val, NULL);
    if (strcmp(name, "maxExclusive") == 0)
        return strtod(t, NULL) < strtod(val, NULL);
    return 1;
}

static int data_matches(const RngPattern* data, const char* t) {
    if (!data_lexical_ok(data, t)) return 0;
    for (const RngPattern* c = data->first_child; c; c = c->next)
        if (c->kind == RNG_PARAM && !param_ok(c, t)) return 0;
    return 1;
}

static int ws_only(const char* s) {
    for (; s && *s; s++)
        if (!isspace((unsigned char)*s)) return 0;
    return 1;
}

/* Collect element children (phase 2 matches elements positionally;
 * text handled by TEXT/DATA/VALUE/MIXED at the sequence level). */
static size_t elem_children(LeptrisElement e, LeptrisNodeRef* out,
                            size_t cap) {
    size_t n = 0;
    for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)e); c;
         c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT &&
            n < cap)
            out[n++] = c;
    }
    return n;
}

static int all_text_ws(LeptrisElement e) {
    for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)e); c;
         c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_TEXT) {
            const char* t =
                leptris_text_node_get_content(c);
            if (!ws_only(t ? t : "")) return 0;
        }
    }
    return 1;
}

static int names_match(const RngPattern* p, LeptrisElement e) {
    if (p->any_name) {
        if (p->ns) {
            const char* uri = leptris_element_get_namespace_uri(e);
            if (!uri || strcmp(p->ns, uri) != 0) return 0;
        }
        return 1;
    }
    const char* n = leptris_element_name(e);
    if (!n || strcmp(p->name, n) != 0) return 0;
    if (p->ns) {
        const char* uri = leptris_element_get_namespace_uri(e);
        if (!uri || strcmp(p->ns, uri) != 0) return 0;
    }
    return 1;
}

/* --- attribute matching (exact) ---------------------------------- */
static int match_attrs(RngVal* v, RngPattern* content, LeptrisElement e);
static int token_matches_leaf(const char* tok, RngPattern* leaf);

/* Does the attribute pattern's content (value/data leaves, also
 * behind refs and choices - metanorma's Alignments) accept the
 * instance value? Empty content implies <text/> (any value). */
static int attr_leaves_satisfied(RngVal* v, RngPattern* head,
                                 const char* value, int* has_leaf,
                                 int depth);
static int attr_content_satisfied(RngVal* v, RngPattern* attr,
                                  const char* value) {
    int has_leaf = 0;
    if (attr_leaves_satisfied(v, attr->first_child, value, &has_leaf, 0))
        return 1;
    return !has_leaf;
}

static int attr_leaves_satisfied(RngVal* v, RngPattern* head,
                                 const char* value, int* has_leaf,
                                 int depth) {
    if (depth > 32) return 0;
    for (RngPattern* c = head; c; c = c->next) {
        switch (c->kind) {
            case RNG_CHOICE:
                if (attr_leaves_satisfied(v, c->first_child, value,
                                          has_leaf, depth + 1))
                    return 1;
                continue;
            case RNG_VALUE:
            case RNG_DATA:
                *has_leaf = 1;
                if (token_matches_leaf(value, c)) return 1;
                continue;
            case RNG_REF: {
                RngDefine* d = find_define(v->g, c->name);
                if (d && d->body &&
                    attr_leaves_satisfied(v, d->body, value, has_leaf,
                                          depth + 1))
                    return 1;
                continue;
            }
            case RNG_ONE_OR_MORE: case RNG_ZERO_OR_MORE:
            case RNG_OPTIONAL: case RNG_GROUP:
                if (attr_leaves_satisfied(v, c->first_child, value,
                                          has_leaf, depth + 1))
                    return 1;
                continue;
            default:
                continue;
        }
    }
    return 0;
}

/* Does any pattern in the LIST starting at `head` consume the
 * attribute `name`? A define body is a SIBLING LIST (add_define
 * stores the wrap's first child with the rest chained via ->next),
 * so the scan is over list members - not their children. */
static int list_consumes_attr(RngVal* v, RngPattern* head,
                               LeptrisElement e, const char* name);

/* Does `p` (a pattern that may contain attribute patterns among its
 * children) consume the attribute `name`? */
static int pattern_consumes_attr(RngVal* v, RngPattern* p,
                                 LeptrisElement e, const char* name) {
    return list_consumes_attr(v, p->first_child, e, name);
}

static int list_consumes_attr(RngVal* v, RngPattern* head,
                               LeptrisElement e, const char* name) {
    for (RngPattern* c = head; c; c = c->next) {
        switch (c->kind) {
            case RNG_ATTRIBUTE:
                if (c->any_name) {
                    const char* got = leptris_element_attribute(e, name);
                    return attr_content_satisfied(v, c, got ? got : "");
                }
                if (c->name && strcmp(c->name, name) == 0) {
                    const char* got = leptris_element_attribute(e, name);
                    return attr_content_satisfied(v, c, got ? got : "");
                }
                break;
            case RNG_OPTIONAL: case RNG_ZERO_OR_MORE:
            case RNG_GROUP: case RNG_INTERLEAVE: case RNG_CHOICE:
                if (pattern_consumes_attr(v, c, e, name)) return 1;
                break;
            case RNG_REF: {
                RngDefine* d = find_define(v->g, c->name);
                if (d && d->body &&
                    list_consumes_attr(v, d->body, e, name))
                    return 1;
                break;
            }
            default: break;
        }
    }
    return 0;
}

/* Check the attribute patterns against e's actual attributes. */
static int check_required_attrs(RngVal* v, RngPattern* p, LeptrisElement e) {
    for (RngPattern* c = p->first_child; c; c = c->next) {
        switch (c->kind) {
            case RNG_ATTRIBUTE: {
                if (c->any_name) break;   /* wildcard: satisfiable */
                const char* got = leptris_element_attribute(e, c->name);
                if (!got) {
                    diag(v, LEPTRIS_DIAG_MISSING_REQUIRED_ATTR,  e,
        "attribute \"%s\" is required but missing",
                         c->name, NULL);
                    return 0;
                }
                break;
            }
            case RNG_GROUP: case RNG_INTERLEAVE: case RNG_ONE_OR_MORE:
                if (!check_required_attrs(v, c, e)) return 0;
                break;
            case RNG_CHOICE:
                /* Simplified: any alternative with all its attrs. */
                if (!check_required_attrs(v, c, e)) return 0;
                break;
            default: break;
        }
    }
    return 1;
}

static int match_attrs(RngVal* v, RngPattern* content, LeptrisElement e) {
    /* Every instance attribute must be consumed by the content. */
    for (struct leptris_attribute* a = leptris_element_get_first_attribute(e);
         a; a = leptris_attr_next(a)) {
        if (!pattern_consumes_attr(v, content, e, attr_cname(a))) {
            diag(v, LEPTRIS_DIAG_ATTR_NOT_ALLOWED,  e,
        "attribute \"%s\" not allowed", attr_cname(a), NULL);
            return 0;
        }
    }
    return check_required_attrs(v, content, e);
}

/* --- sequence matching ------------------------------------------- */

static int element_ok(RngVal* v, RngPattern* p, LeptrisElement e);
static size_t fold_list(RngVal* v, RngPattern* p, LeptrisNodeRef* kids,
                        size_t n, size_t idx);

/* Try to match `p` against the element child window [idx, n).
 * On success return the next index; on failure return (size_t)-1. */
static size_t match_seq(RngVal* v, RngPattern* p, LeptrisNodeRef* kids,
                        size_t n, size_t idx);

static int leaf_matches(RngVal* v, RngPattern* p, LeptrisNodeRef node) {
    LeptrisElement e = (LeptrisElement)node;
    switch (p->kind) {
        case RNG_ELEMENT:
            return element_ok(v, p, e);
        default:
            return 0;
    }
}

/* Text leaf checks against an element's trimmed text. */
static int text_leaf_ok(RngVal* v, RngPattern* p, LeptrisElement e) {
    const char* t = leptris_element_text(e);
    t = t ? t : "";
    switch (p->kind) {
        case RNG_TEXT:
            return 1;
        case RNG_VALUE:
            return strcmp(t, p->value ? p->value : "") == 0;
        case RNG_DATA:
            return data_matches(p, t);
        default:
            return 0;
    }
}

static size_t match_seq(RngVal* v, RngPattern* p, LeptrisNodeRef* kids,
                        size_t n, size_t idx) {
    if (v->failed) return (size_t)-1;
    switch (p->kind) {
        case RNG_ELEMENT:
            if (idx >= n) return (size_t)-1;
            if (!leaf_matches(v, p, kids[idx])) return (size_t)-1;
            return idx + 1;
        case RNG_CHOICE: {
            /* Alternatives that CONSUME a child win over zero-width
             * ones (text/empty): a leading <text/> branch would
             * otherwise end the choice without consuming and starve
             * the element branches behind it (inline models:
             * TextElement). */
            size_t r0 = (size_t)-1;
            for (RngPattern* c = p->first_child; c; c = c->next) {
                size_t r = match_seq(v, c, kids, n, idx);
                if (r != (size_t)-1) {
                    if (r > idx) return r;
                    if (r0 == (size_t)-1) r0 = r;
                }
                v->failed = 0;   /* try next alternative */
                v->err[0] = 0;
            }
            return r0;
        }
        case RNG_GROUP: {
            size_t i = idx;
            for (RngPattern* c = p->first_child; c; c = c->next) {
                i = match_seq(v, c, kids, n, i);
                if (i == (size_t)-1) return (size_t)-1;
            }
            return i;
        }
        case RNG_INTERLEAVE: {
            /* Any order; each child pattern is used exactly once and
             * all of them must match. */
            size_t count = 0;
            for (RngPattern* c = p->first_child; c; c = c->next) count++;
            int used[32] = {0};
            for (size_t k = 0; k < count; k++) {
                int matched_any = 0;
                size_t ci = 0;
                for (RngPattern* c = p->first_child; c;
                     c = c->next, ci++) {
                    if (ci < 32 && used[ci]) continue;
                    size_t r = match_seq(v, c, kids, n, idx);
                    if (r != (size_t)-1) {
                        matched_any = 1;
                        idx = r;
                        if (ci < 32) used[ci] = 1;
                        break;
                    }
                    v->failed = 0;
                    v->err[0] = 0;
                }
                if (!matched_any) return (size_t)-1;
            }
            return idx;
        }
        case RNG_OPTIONAL: {
            size_t r = match_seq(v, p->first_child, kids, n, idx);
            if (r == (size_t)-1) {
                /* The branch is skippable: an inner mismatch is
                 * speculative, not a failure — clear the latch like
                 * ZERO_OR_MORE/CHOICE do, or the stuck failure
                 * poisons every later match (regression since the
                 * #878 vocabulary pass made silent mismatches
                 * loud; optional-omitted documents validated
                 * false). */
                v->failed = 0;
                v->err[0] = 0;
                return idx;
            }
            return r;
        }
        case RNG_ZERO_OR_MORE: {
            for (;;) {
                size_t r = match_seq(v, p->first_child, kids, n, idx);
                if (r == (size_t)-1 || r == idx) {
                    v->failed = 0;
                    v->err[0] = 0;
                    return idx;
                }
                idx = r;
            }
        }
        case RNG_ONE_OR_MORE: {
            size_t r = match_seq(v, p->first_child, kids, n, idx);
            if (r == (size_t)-1) return (size_t)-1;
            idx = r;
            for (;;) {
                r = match_seq(v, p->first_child, kids, n, idx);
                if (r == (size_t)-1 || r == idx) {
                    v->failed = 0;
                    v->err[0] = 0;
                    return idx;
                }
                idx = r;
            }
        }
        case RNG_EMPTY:
        case RNG_ATTRIBUTE:
            /* Attributes are consumed at the element level. */
            return idx;
        case RNG_TEXT: case RNG_DATA: case RNG_VALUE:
            /* Text leaves match the PARENT element's content — handled
             * at the element level in phase 2 (see rng_validate_doc). */
            return idx;
        case RNG_REF: {
            RngDefine* d = find_define(v->g, p->name);
            if (!d || !d->body) {
                diag(v, LEPTRIS_DIAG_INVALID,  NULL,
        "reference to undefined pattern", p->name, NULL);
                return (size_t)-1;
            }
            if (v->depth > 200) {
                diag(v, LEPTRIS_DIAG_INVALID,  NULL,
        "reference \"%s\" recursion too deep",
                     p->name, NULL);
                return (size_t)-1;
            }
            v->depth++;
            /* A define body is a sibling LIST (implicit group):
             * match every member, not just the head. */
            size_t i = idx;
            for (RngPattern* b = d->body; b; b = b->next) {
                i = match_seq(v, b, kids, n, i);
                if (i == (size_t)-1) {
                    v->depth--;
                    return (size_t)-1;
                }
            }
            v->depth--;
            return i;
        }
        case RNG_MIXED: {
            /* mixed = interleave(content, text*) — phase 2: any order. */
            return match_seq(v, p->first_child, kids, n, idx);
        }
        default:
            return (size_t)-1;
    }
}

/* Element with text-leaf content (no element children expected). */
static int match_text_content(RngVal* v, RngPattern* content,
                              LeptrisElement e) {
    for (RngPattern* c = content; c; c = c->next) {
        if (c->kind == RNG_ATTRIBUTE) continue;
        if (text_leaf_ok(v, c, e)) return 1;
        if (c->kind == RNG_CHOICE) {
            for (RngPattern* alt = c->first_child; alt; alt = alt->next)
                if (text_leaf_ok(v, alt, e)) return 1;
        }
    }
    return 0;
}

static int element_ok(RngVal* v, RngPattern* p, LeptrisElement e);

/* Does the content contain element patterns (vs text leaves)? */
static int has_element_patterns(RngPattern* content) {
    for (RngPattern* c = content; c; c = c->next) {
        switch (c->kind) {
            case RNG_ELEMENT: case RNG_REF: case RNG_GROUP:
            case RNG_INTERLEAVE: case RNG_CHOICE: case RNG_OPTIONAL:
            case RNG_ZERO_OR_MORE: case RNG_ONE_OR_MORE: case RNG_MIXED:
                return 1;
            default: break;
        }
    }
    return 0;
}

/* <list>: the element text tokenizes on whitespace; every token
 * must match the child leaves (data/value), with oneOrMore/etc.
 * inside handled as at-least-one-token. */
static int token_matches_leaf(const char* tok, RngPattern* leaf) {
    if (!leaf) return 0;
    if (leaf->kind == RNG_TEXT) return 1;
    if (leaf->kind == RNG_VALUE)
        return leaf->value && strcmp(leaf->value, tok) == 0;
    if (leaf->kind == RNG_DATA)
        return data_matches(leaf, tok);
    return 0;
}

/* <list>: tokens (whitespace-split) match the child leaves
 * POSITIONALLY - RELAX NG list is a sequence, order matters.
 * Repeat wrappers consume greedily. */
static int list_ok(RngVal* v, RngPattern* p, LeptrisElement e) {
    const char* t = leptris_element_text(e);
    if (!t) return 0;

    RngPattern* leaves[32];
    size_t nl = 0;
    for (RngPattern* c = p->first_child; c && nl < 32; c = c->next)
        leaves[nl++] = c;

    const char* q = t;
    size_t li = 0;
    size_t tokens = 0;
    int consumed_any[32] = {0};
    for (;;) {
        while (*q && isspace((unsigned char)*q)) q++;
        if (!*q) break;
        const char* start = q;
        while (*q && !isspace((unsigned char)*q)) q++;
        size_t len = (size_t)(q - start);
        char tok[128];
        if (len >= sizeof(tok)) len = sizeof(tok) - 1;
        memcpy(tok, start, len);
        tok[len] = 0;
        tokens++;

        /* Close repeat wrappers that cannot consume this token. */
        while (li < nl) {
            RngPattern* w = leaves[li];
            if ((w->kind == RNG_ONE_OR_MORE || w->kind == RNG_ZERO_OR_MORE ||
                 w->kind == RNG_OPTIONAL) &&
                !token_matches_leaf(tok, w->first_child)) {
                li++;
            } else {
                break;
            }
        }

        if (li >= nl) {
            diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "list token \"%s\" not allowed", tok, NULL);
            return 0;
        }
        RngPattern* c = leaves[li];
        RngPattern* leaf =
            (c->kind == RNG_ONE_OR_MORE || c->kind == RNG_ZERO_OR_MORE ||
             c->kind == RNG_OPTIONAL)
                ? c->first_child : c;
        if (!token_matches_leaf(tok, leaf)) {
            diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "list token \"%s\" does not match", tok, NULL);
            return 0;
        }
        consumed_any[li] = 1;
        /* Plain/optional leaf consumed one token -> advance. */
        if (c->kind != RNG_ONE_OR_MORE && c->kind != RNG_ZERO_OR_MORE)
            li++;
    }
    /* Trailing required leaves (non-optional) must not remain. */
    for (size_t k = 0; k < nl; k++) {
        RngPattern* c = leaves[k];
        if (c->kind == RNG_ONE_OR_MORE && !consumed_any[k]) {
            diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "list: a oneOrMore leaf requires at least one token",
                 NULL, NULL);
            return 0;
        }
        /* Plain leaves past the consumed point are missing tokens. */
        if (c->kind != RNG_ONE_OR_MORE && c->kind != RNG_ZERO_OR_MORE &&
            c->kind != RNG_OPTIONAL && !consumed_any[k]) {
            diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "list: missing token for a required leaf", NULL, NULL);
            return 0;
        }
    }
    return tokens > 0;
}

/* Backtracking fold over the sibling pattern list: a CHOICE whose
 * trivially-succeeding alternative (empty) starves later patterns
 * retries its other alternatives (phase-3 correctness). */
static size_t fold_list(RngVal* v, RngPattern* p, LeptrisNodeRef* kids,
                        size_t n, size_t idx) {
    if (!p) return idx == n ? idx : (size_t)-1;
    if (p->kind == RNG_CHOICE) {
        for (RngPattern* a = p->first_child; a; a = a->next) {
            size_t r = match_seq(v, a, kids, n, idx);
            v->failed = 0;
            v->err[0] = 0;
            if (r != (size_t)-1) {
                size_t rr = fold_list(v, p->next, kids, n, r);
                if (rr != (size_t)-1) return rr;
            }
        }
        return (size_t)-1;
    }
    size_t r = match_seq(v, p, kids, n, idx);
    if (r == (size_t)-1) return (size_t)-1;
    return fold_list(v, p->next, kids, n, r);
}

static int element_ok(RngVal* v, RngPattern* p, LeptrisElement e) {
    if (!names_match(p, e)) {
        /* #878/#1137: surface the name mismatch in the diagnose
         * pass only — the verdict probe must not poison the
         * matcher's short-circuit. */
        diag_probe(v, LEPTRIS_DIAG_NOT_ALLOWED_HERE, e,
                   "element \"%s\" not allowed here",
                   leptris_element_name(e));
        return 0;
    }
    if (!match_attrs(v, p, e)) return 0;

    if (!has_element_patterns(p->first_child)) {
        /* Text-level content: each non-attribute child is a leaf. */
        for (RngPattern* c = p->first_child; c; c = c->next) {
            if (c->kind == RNG_ATTRIBUTE) continue;
            if (c->kind == RNG_LIST) {
                if (!list_ok(v, c, e)) return 0;
                continue;
            }
            if (c->kind == RNG_EMPTY) {
                if (!all_text_ws(e)) {
                    diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "element \"%s\" must be empty", p->name, NULL);
                    return 0;
                }
                continue;
            }
            if (!match_text_content(v, c, e) && !text_leaf_ok(v, c, e)) {
                /* oneOrMore/optional wrappers of text leaves */
                if (c->kind == RNG_ONE_OR_MORE || c->kind == RNG_OPTIONAL ||
                    c->kind == RNG_ZERO_OR_MORE) {
                    RngPattern* inner = c->first_child;
                    if (c->kind == RNG_ONE_OR_MORE) {
                        if (!inner || !text_leaf_ok(v, inner, e)) {
                            diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "element \"%s\": content mismatch",
                                 p->name, NULL);
                            return 0;
                        }
                    }
                    continue;
                }
                diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "element \"%s\": content mismatch", p->name, NULL);
                return 0;
            }
        }
        {
            LeptrisNodeRef sub[64];
            size_t sn = elem_children(e, sub, 64);
            if (sn) {
                diag(v, LEPTRIS_DIAG_NOT_ALLOWED_HERE,  (LeptrisElement)sub[0],
        "element \"%s\" not allowed here",
                     leptris_element_name((LeptrisElement)sub[0]), NULL);
                return 0;
            }
        }
        return 1;
    }

    LeptrisNodeRef kids[64];
    size_t n = elem_children(e, kids, 64);
    /* The content model is a sibling LIST with backtracking. */
    size_t r = fold_list(v, p->first_child, kids, n, 0);
    if (r == (size_t)-1) return 0;
    if (r != n) {
        diag(v, LEPTRIS_DIAG_NOT_ALLOWED_HERE,  (LeptrisElement)kids[r],
        "element \"%s\" not allowed here",
             leptris_element_name((LeptrisElement)kids[r]), NULL);
        return 0;
    }
    return 1;
}

/* =====================================================================
 * #878 sub-fix #3 — Jing-parity failure diagnosis.
 *
 * The verdict pass above runs with v->quiet set: speculative
 * backtracking makes mid-match fail() calls duplicate and mis-
 * attribute. When validation fails, this pass walks the instance
 * against the content model with derivative-style semantics
 * (position state = list of remaining items) and emits Jing's
 * message vocabulary, in Jing's order:
 *
 *   element "X" not allowed anywhere; expected <set>   (name never valid)
 *   element "X" not allowed here; expected <set>       (wrong position)
 *   element "X" not allowed yet; missing required element "A"
 *   element "P" incomplete; missing required element "A"
 *   element "P" incomplete; expected <set>
 *   element "P" missing required attribute "A"
 *   found attribute "A", but no attributes allowed here
 *   value of attribute "A" is invalid; must be equal to "v"
 *   character content of element "P" invalid; must be an integer
 *   character content of element "P" invalid; must be equal to "v"
 *   character content of element "P" invalid; token "t" invalid; ...
 * ===================================================================== */

/* Diagnose allocations share a leading link pointer so one free
 * walk can release both cell and pattern nodes. */
typedef struct DiagAny {
    struct DiagAny* link;
} DiagAny;

/* A remaining-position item list. */
typedef struct DiagCell {
    struct DiagAny hdr;
    RngPattern* item;
    struct DiagCell* next;
} DiagCell;

/* Synthetic patterns: shallow copies / repeat wrappers. */
typedef struct DiagPat {
    struct DiagAny hdr;
    RngPattern p;
} DiagPat;

static DiagCell* dcell_new(RngVal* v, RngPattern* item, DiagCell* next) {
    DiagCell* c = (DiagCell*)calloc(1, sizeof(DiagCell));
    if (!c) return NULL;
    c->hdr.link = (DiagAny*)v->diag_pool;
    v->diag_pool = c;
    c->item = item;
    c->next = next;
    return c;
}

static RngPattern* dpat_new(RngVal* v, RngPatternKind kind,
                            RngPattern* child) {
    DiagPat* d = (DiagPat*)calloc(1, sizeof(DiagPat));
    if (!d) return NULL;
    d->hdr.link = (DiagAny*)v->diag_pool;
    v->diag_pool = d;
    d->p.kind = kind;
    d->p.first_child = child;
    return &d->p;
}

static RngPattern* dpat_copy(RngVal* v, const RngPattern* src) {
    RngPattern* p = dpat_new(v, src->kind, src->first_child);
    if (!p) return NULL;
    p->name = src->name;
    p->ns = src->ns;
    p->datatype = src->datatype;
    p->datatype_lib = src->datatype_lib;
    p->value = src->value;
    p->define = src->define;
    return p;
}

static void diag_pool_free(RngVal* v) {
    DiagAny* p = (DiagAny*)v->diag_pool;
    while (p) {
        DiagAny* next = p->link;
        free(p);
        p = next;
    }
    v->diag_pool = NULL;
}

/* Resolve REF chains to their body (depth-guarded). */
static RngPattern* deref(RngVal* v, RngPattern* p) {
    int guard = 0;
    while (p && p->kind == RNG_REF && guard++ < 200) {
        if (p->define && p->define->body) p = p->define->body;
        else {
            RngDefine* d = find_define(v->g, p->name);
            if (!d || !d->body) return NULL;
            p = d->body;
        }
    }
    return p;
}

/* Nullable: can the item match zero occurrences? */
static int nullable_item(RngVal* v, RngPattern* p) {
    p = deref(v, p);
    if (!p) return 0;
    switch (p->kind) {
        case RNG_EMPTY: case RNG_TEXT: case RNG_OPTIONAL:
        case RNG_ZERO_OR_MORE: case RNG_MIXED: case RNG_LIST:
        case RNG_ATTRIBUTE:
            /* Attributes are matched at the element level; they
             * never make element CONTENT incomplete and never
             * block the content walk (Jing checks them at the
             * start-tag close, not in endTagDeriv). */
            return 1;
        case RNG_ONE_OR_MORE:
            return nullable_item(v, p->first_child);
        case RNG_CHOICE: {
            for (RngPattern* c = p->first_child; c; c = c->next)
                if (nullable_item(v, c)) return 1;
            return 0;
        }
        case RNG_GROUP: {
            for (RngPattern* c = p->first_child; c; c = c->next)
                if (!nullable_item(v, c)) return 0;
            return 1;
        }
        case RNG_INTERLEAVE: {
            for (RngPattern* c = p->first_child; c; c = c->next)
                if (!nullable_item(v, c)) return 0;
            return 1;
        }
        default:
            return 0;
    }
}

static int nullable_state(RngVal* v, DiagCell* s) {
    for (DiagCell* c = s; c; c = c->next)
        if (!nullable_item(v, c->item)) return 0;
    return 1;
}

/* FIRST element names of an item (dedup not done here). */
static void first_names_item(RngVal* v, RngPattern* p,
                             const char* out[32], int* n) {
    p = deref(v, p);
    if (!p || *n >= 31) return;
    switch (p->kind) {
        case RNG_ELEMENT:
            if (p->name) out[(*n)++] = p->name;
            return;
        case RNG_CHOICE: case RNG_GROUP: case RNG_INTERLEAVE:
        case RNG_OPTIONAL: case RNG_ZERO_OR_MORE: case RNG_ONE_OR_MORE:
        case RNG_MIXED:
            for (RngPattern* c = p->first_child; c; c = c->next)
                first_names_item(v, c, out, n);
            return;
        default:
            return;
    }
}

static void first_names_state(RngVal* v, DiagCell* s,
                              const char* out[32], int* n) {
    *n = 0;
    for (DiagCell* c = s; c; c = c->next)
        first_names_item(v, c->item, out, n);
    /* dedupe, keep first occurrence order */
    for (int i = 0; i < *n; i++)
        for (int j = i + 1; j < *n; j++)
            if (out[j] && out[i] && strcmp(out[i], out[j]) == 0) out[j] = NULL;
    int w = 0;
    for (int i = 0; i < *n; i++)
        if (out[i]) out[w++] = out[i];
    *n = w;
}

/* Is `x` a possible element name anywhere in the content subtree?
 * Ref bodies are sibling lists: every member counts. Depth-capped -
 * recursive grammars (section refs section) would otherwise loop. */
static int name_in_tree(RngVal* v, RngPattern* p, const char* x,
                        int depth) {
    if (!p || depth > 64) return 0;
    if (p->kind == RNG_REF) {
        RngDefine* d = find_define(v->g, p->name);
        if (!d || !d->body) return 0;
        for (RngPattern* m = d->body; m; m = m->next)
            if (name_in_tree(v, m, x, depth + 1)) return 1;
        return 0;
    }
    if (p->kind == RNG_ELEMENT)
        return p->any_name ||
               (p->name && strcmp(p->name, x) == 0);
    for (RngPattern* c = p->first_child; c; c = c->next)
        if (name_in_tree(v, c, x, depth + 1)) return 1;
    return 0;
}

/* Render the Jing "expected" set for a position: "the element
 * end-tag", "text", then element names — ", " separated, " or "
 * before the last. */
static void render_expected(RngVal* v, DiagCell* s, int text_ok,
                            char* out, size_t cap) {
    const char* names[32];
    int n = 0;
    first_names_state(v, s, names, &n);
    out[0] = 0;
    const char* parts[40];
    int heap_part[40];
    int np = 0;
    if (nullable_state(v, s)) {
        parts[np] = "the element end-tag";
        heap_part[np++] = 0;
    }
    if (text_ok) {
        parts[np] = "text";
        heap_part[np++] = 0;
    }
    for (int i = 0; i < n && np < 38; i++) {
        char buf[128];
        /* Jing prefixes only the FIRST element NAME; later ones
         * are bare: element "a" or "b" (end-tag/text don't count). */
        if (i == 0)
            snprintf(buf, sizeof(buf), "element \"%s\"", names[i]);
        else
            snprintf(buf, sizeof(buf), "\"%s\"", names[i]);
        char* dup = (char*)malloc(strlen(buf) + 1);
        if (!dup) break;
        strcpy(dup, buf);
        parts[np] = dup;
        heap_part[np++] = 1;
    }
    size_t w = 0;
    for (int i = 0; i < np && w < cap - 1; i++) {
        const char* sep = i == 0 ? "" : (i == np - 1 ? " or " : ", ");
        int wrote = snprintf(out + w, cap - w, "%s%s", sep, parts[i]);
        if (wrote < 0) break;
        w += (size_t)wrote;
    }
    for (int i = 0; i < np; i++)
        if (heap_part[i]) free((void*)parts[i]);
}

/* Consume element `x` at position `s`. Returns the new state (which
 * may be NULL = end of content) or NULL-with-*ok=0 when not
 * consumable. Consuming item i requires items before it to be
 * nullable (matched zero times). Repeat wrappers stay active. */
static DiagCell* d_state(RngVal* v, DiagCell* s, const char* x, int* ok) {
    *ok = 0;
    for (DiagCell* c = s; c; c = c->next) {
        RngPattern* item = c->item;
        RngPattern* d = deref(v, item);
        if (!d) return NULL;
        RngPattern* repl = NULL;
        int consumed = 0;
        switch (d->kind) {
            case RNG_ELEMENT:
                if ((d->name && strcmp(d->name, x) == 0) ||
                    d->any_name) { consumed = 1; }
                break;
            case RNG_ATTRIBUTE:
                /* Attributes are matched at the element level
                 * (diagnose_attrs); they never consume children
                 * and never gate content order. */
                continue;
            case RNG_CHOICE:
                for (RngPattern* a = d->first_child; a; a = a->next) {
                    int sub = 0;
                    DiagCell tmp; tmp.item = a; tmp.next = NULL;
                    d_state(v, &tmp, x, &sub);
                    if (sub) { consumed = 1; break; }
                }
                break;
            case RNG_GROUP: {
                /* Sequence inside an item: try prefix-nullable consume. */
                DiagCell* inner = NULL;
                for (RngPattern* ch = d->first_child; ch; ch = ch->next)
                    inner = dcell_new(v, ch, inner);
                /* build in order */
                DiagCell* rev = NULL;
                for (DiagCell* ic = inner; ic; ) {
                    DiagCell* nx = ic->next;
                    ic->next = rev; rev = ic; ic = nx;
                }
                int sub = 0;
                DiagCell* r = d_state(v, rev, x, &sub);
                if (sub) {
                    consumed = 1;
                    if (r) {
                        /* rebuild a GROUP holding the remainder */
                        RngPattern* kids = NULL;
                        for (DiagCell* ic = r; ic; ic = ic->next) {
                            RngPattern* cp = dpat_copy(v, ic->item);
                            if (!cp) { consumed = 0; break; }
                            cp->next = kids; kids = cp;
                        }
                        RngPattern* ord = NULL;
                        for (RngPattern* k = kids; k; ) {
                            RngPattern* nx = k->next;
                            k->next = ord; ord = k; k = nx;
                        }
                        repl = kids ? dpat_new(v, RNG_GROUP, ord) : NULL;
                    }
                }
                break;
            }
            case RNG_INTERLEAVE: {
                /* Any child may consume; the others stay. */
                for (RngPattern* a = d->first_child; a; a = a->next) {
                    DiagCell tmp; tmp.item = a; tmp.next = NULL;
                    int sub = 0;
                    d_state(v, &tmp, x, &sub);
                    if (!sub) continue;
                    consumed = 1;
                    /* Survivors = other children (this one consumed
                     * once; single-use children drop, repeats keep). */
                    RngPattern* kids = NULL;
                    for (RngPattern* o = d->first_child; o; o = o->next) {
                        if (o == a) continue;
                        RngPattern* cp = dpat_copy(v, o);
                        if (!cp) { consumed = 0; break; }
                        cp->next = kids; kids = cp;
                    }
                    RngPattern* ord = NULL;
                    for (RngPattern* k = kids; k; ) {
                        RngPattern* nx = k->next;
                        k->next = ord; ord = k; k = nx;
                    }
                    repl = ord ? dpat_new(v, RNG_INTERLEAVE, ord) : NULL;
                    break;
                }
                break;
            }
            case RNG_OPTIONAL:
            case RNG_ZERO_OR_MORE: {
                DiagCell tmp; tmp.item = d->first_child; tmp.next = NULL;
                int sub = 0;
                d_state(v, &tmp, x, &sub);
                if (sub) {
                    consumed = 1;
                    if (d->kind == RNG_ZERO_OR_MORE)
                        repl = item;  /* stays active */
                    /* OPTIONAL: occurrence used up */
                }
                break;
            }
            case RNG_ONE_OR_MORE: {
                DiagCell tmp; tmp.item = d->first_child; tmp.next = NULL;
                int sub = 0;
                d_state(v, &tmp, x, &sub);
                if (sub) {
                    consumed = 1;
                    repl = dpat_new(v, RNG_ZERO_OR_MORE, d->first_child);
                }
                break;
            }
            case RNG_MIXED: {
                /* Elements come from the inner wrapper; mixed stays. */
                DiagCell* inner = NULL;
                for (RngPattern* ch = d->first_child; ch; ch = ch->next)
                    inner = dcell_new(v, ch, inner);
                DiagCell* rev = NULL;
                for (DiagCell* ic = inner; ic; ) {
                    DiagCell* nx = ic->next;
                    ic->next = rev; rev = ic; ic = nx;
                }
                int sub = 0;
                d_state(v, rev, x, &sub);
                if (sub) { consumed = 1; repl = item; }
                break;
            }
            default:
                break;
        }
        if (consumed) {
            *ok = 1;
            DiagCell* tail = c->next;
            DiagCell* head = tail;
            if (repl) head = dcell_new(v, repl, tail);
            return head;
        }
        if (!nullable_item(v, item)) return NULL;  /* blocked here */
    }
    return NULL;  /* nothing left to consume */
}

/* The first required (non-nullable) element name in the state. */
static const char* first_required_name(RngVal* v, DiagCell* s) {
    for (DiagCell* c = s; c; c = c->next) {
        if (nullable_item(v, c->item)) continue;
        const char* names[32];
        int n = 0;
        DiagCell tmp; tmp.item = c->item; tmp.next = NULL;
        first_names_state(v, &tmp, names, &n);
        if (n > 0) return names[0];
        return NULL;
    }
    return NULL;
}

/* Deep-walk the element's content and emit Jing-shaped errors. */
static void diagnose_element(RngVal* v, RngPattern* p, LeptrisElement e);

/* Find the ELEMENT pattern for name `x` reachable from a content
 * list - through refs (bodies are sibling lists) and containers,
 * any depth (sections -> clause -> p). Depth-capped like
 * name_in_tree. */
static RngPattern* find_element_pattern(RngVal* v, RngPattern* p,
                                        const char* x, int depth) {
    if (!p || depth > 64) return NULL;
    if (p->kind == RNG_REF) {
        RngDefine* d = find_define(v->g, p->name);
        if (!d || !d->body) return NULL;
        for (RngPattern* m = d->body; m; m = m->next) {
            RngPattern* r = find_element_pattern(v, m, x, depth + 1);
            if (r) return r;
        }
        return NULL;
    }
    if (p->kind == RNG_ELEMENT)
        return (p->name && strcmp(p->name, x) == 0) ? p : NULL;
    for (RngPattern* c = p->first_child; c; c = c->next) {
        RngPattern* r = find_element_pattern(v, c, x, depth + 1);
        if (r) return r;
    }
    return NULL;
}

/* Diagnose a matched child element's own content (quiet verdict,
 * then recurse on failure). */
static void diagnose_child(RngVal* v, RngPattern* content,
                           LeptrisElement kid) {
    const char* x = leptris_element_name(kid);
    if (!x) return;
    RngPattern* found = NULL;
    for (RngPattern* c = content; c && !found; c = c->next)
        found = find_element_pattern(v, c, x, 0);
    if (!found) return;
    if (v->depth > 100) return;
    v->depth++;
    int saved_quiet = v->quiet;
    v->quiet = 1;
    int ok = element_ok(v, found, kid);
    v->quiet = saved_quiet;
    int saved_failed = v->failed;
    v->failed = 0;
    if (!ok) diagnose_element(v, found, kid);
    v->failed = saved_failed;
    v->depth--;
}

/* Collect attribute patterns reachable from `p`'s content list —
 * direct children plus define bodies behind refs (Root-Attributes)
 * and containers. `optional[i]` marks candidates reached through
 * optional/zeroOrMore/choice wrappers — they are never required.
 * Bounds: candidate list capped at 64; cycles cut by the
 * seen-set. */
static int collect_attr_patterns(RngVal* v, RngPattern* head,
                                 RngPattern** out, int* optional,
                                 int n, int cap, int opt,
                                 RngPattern** seen, int* nseen) {
    for (RngPattern* c = head; c && n < cap; c = c->next) {
        int visited = 0;
        for (int i = 0; i < *nseen; i++)
            if (seen[i] == c) { visited = 1; break; }
        if (visited) continue;
        if (*nseen < 128) seen[(*nseen)++] = c;
        if (c->kind == RNG_ATTRIBUTE) {
            out[n] = c;
            optional[n] = opt;
            n++;
        } else if (c->kind == RNG_OPTIONAL ||
                   c->kind == RNG_ZERO_OR_MORE ||
                   c->kind == RNG_GROUP ||
                   c->kind == RNG_INTERLEAVE ||
                   c->kind == RNG_CHOICE) {
            n = collect_attr_patterns(v, c->first_child, out,
                                      optional, n, cap,
                                      opt || c->kind != RNG_GROUP,
                                      seen, nseen);
        } else if (c->kind == RNG_REF) {
            RngDefine* d = find_define(v->g, c->name);
            if (d && d->body)
                n = collect_attr_patterns(v, d->body, out, optional,
                                          n, cap, opt, seen, nseen);
        }
    }
    return n;
}

/* Jing formatDataDerivFailures: VALUE leaves reachable from an
 * attribute's content - through refs (Alignments), choices and
 * repeat wrappers - deduped, then sorted by the renderer. */
static void collect_value_leaves(RngVal* v, RngPattern* head,
                                 const char* out[], int* n, int cap,
                                 int depth) {
    if (depth > 32) return;
    for (RngPattern* c = head; c && *n < cap; c = c->next) {
        switch (c->kind) {
            case RNG_VALUE:
                if (!c->value) continue;
                {
                    int dup = 0;
                    for (int i = 0; i < *n; i++)
                        if (strcmp(out[i], c->value) == 0) { dup = 1; break; }
                    if (!dup) out[(*n)++] = c->value;
                }
                continue;
            case RNG_CHOICE: case RNG_ONE_OR_MORE:
            case RNG_ZERO_OR_MORE: case RNG_OPTIONAL:
            case RNG_GROUP:
                collect_value_leaves(v, c->first_child, out, n, cap,
                                     depth + 1);
                continue;
            case RNG_REF: {
                RngDefine* d = find_define(v->g, c->name);
                if (d && d->body)
                    collect_value_leaves(v, d->body, out, n, cap,
                                         depth + 1);
                continue;
            }
            default:
                continue;
        }
    }
}

/* Quoted, sorted, comma-joined with " or "/" and " before the
 * last item - Jing's formatList over quoteValue'd strings. */
static void render_quoted_list(const char* items[], int n,
                               const char* lastjoin, char* out,
                               size_t cap) {
    for (int i = 1; i < n; i++) {
        const char* k = items[i];
        int j = i - 1;
        while (j >= 0 && strcmp(items[j], k) > 0) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = k;
    }
    size_t w = 0;
    for (int i = 0; i < n && w < cap - 1; i++) {
        const char* sep = i == 0 ? "" : (i == n - 1 ? " " : ", ");
        if (i == n - 1 && n > 1) {
            int wrote = snprintf(out + w, cap - w, "%s%s ", sep,
                                 lastjoin);
            if (wrote < 0) break;
            w += (size_t)wrote;
        } else {
            int wrote = snprintf(out + w, cap - w, "%s", sep);
            if (wrote < 0) break;
            w += (size_t)wrote;
        }
        int wrote = snprintf(out + w, cap - w, "\"%s\"", items[i]);
        if (wrote < 0) break;
        w += (size_t)wrote;
    }
    out[w < cap ? w : cap - 1] = 0;
}

/* Jing RequiredElementsFunction: the required element names of a
 * position. Element -> its name; choice -> branch intersection
 * (empty if a branch has none); interleave/oneOrMore/mixed ->
 * descend (union); member lists (groups, ref bodies) take the
 * leftmost non-nullable member and stop. AnyName elements
 * contribute nothing. */
static int req_names(RngVal* v, RngPattern* p, const char* out[8],
                     int depth) {
    if (!p || depth > 64) return 0;
    if (p->kind == RNG_REF) {
        RngDefine* d = find_define(v->g, p->name);
        if (!d || !d->body) return 0;
        for (RngPattern* m = d->body; m; m = m->next)
            if (!nullable_item(v, m))
                return req_names(v, m, out, depth + 1);
        return 0;
    }
    switch (p->kind) {
        case RNG_ELEMENT:
            if (p->name && !p->any_name) {
                out[0] = p->name;
                return 1;
            }
            return 0;
        case RNG_CHOICE: {
            const char* acc[8];
            int have = 0;
            for (RngPattern* c = p->first_child; c; c = c->next) {
                const char* tmp[8];
                int m = req_names(v, c, tmp, depth + 1);
                if (m == 0) return 0;
                if (!have) {
                    memcpy(acc, tmp, sizeof(acc));
                    have = m;
                    continue;
                }
                int w = 0;
                for (int i = 0; i < have; i++)
                    for (int j = 0; j < m; j++)
                        if (strcmp(acc[i], tmp[j]) == 0)
                            acc[w++] = acc[i];
                have = w;
                if (!have) return 0;
            }
            memcpy(out, acc, sizeof(acc));
            return have;
        }
        case RNG_INTERLEAVE: case RNG_ONE_OR_MORE: case RNG_MIXED: {
            int n = 0;
            for (RngPattern* c = p->first_child; c && n < 8;
                 c = c->next)
                n += req_names(v, c, out + n, depth + 1);
            return n;
        }
        case RNG_GROUP:
            for (RngPattern* c = p->first_child; c; c = c->next)
                if (!nullable_item(v, c))
                    return req_names(v, c, out, depth + 1);
            return 0;
        default:
            return 0;
    }
}

static void diagnose_attrs(RngVal* v, RngPattern* p, LeptrisElement e) {
    /* Attribute patterns include define bodies behind refs
     * (basicdoc's Root-Attributes) and containers - the verdict
     * path's list_consumes_attr semantics, mirrored here. */
    RngPattern* cands[64];
    int cand_opt[64];
    RngPattern* seen[128];
    int nseen = 0;
    int n_attr_patterns = collect_attr_patterns(
        v, p->first_child, cands, cand_opt, 0, 64, 0, seen, &nseen);
    /* Extra attributes. */
    for (struct leptris_attribute* a = leptris_element_get_first_attribute(e);
         a; a = leptris_attr_next(a)) {
        const char* name = attr_cname(a);
        int consumed = 0;
        for (int i = 0; i < n_attr_patterns && !consumed; i++)
            if (cands[i]->name && strcmp(cands[i]->name, name) == 0)
                consumed = 1;
        if (consumed) {
            /* Value constraint check. */
            for (int i = 0; i < n_attr_patterns; i++) {
                RngPattern* c = cands[i];
                if (!c->name || strcmp(c->name, name) != 0)
                    continue;
                const char* got = leptris_element_attribute(e, name);
                if (!attr_content_satisfied(v, c, got ? got : "")) {
                    /* Jing formatDataDerivFailures: every VALUE
                     * leaf reachable (refs, choices), deduped,
                     * sorted, or-joined. */
                    const char* vals[24];
                    int nv = 0;
                    collect_value_leaves(v, c->first_child, vals, &nv,
                                         24, 0);
                    if (nv > 0) {
                        char buf[256];
                        render_quoted_list(vals, nv, "or", buf,
                                           sizeof(buf));
                        diag(v, LEPTRIS_DIAG_ATTR_VALUE_INVALID,  e,
        "value of attribute \"%s\" is invalid; must be "
                             "equal to %s", name, buf);
                    } else {
                        for (RngPattern* leaf = c->first_child; leaf;
                             leaf = leaf->next) {
                            if (leaf->kind == RNG_DATA) {
                                char dtb[64];
                                snprintf(dtb, sizeof(dtb), "%s %s",
                                         art(leaf->datatype),
                                         leaf->datatype
                                             ? leaf->datatype : "string");
                                diag(v, LEPTRIS_DIAG_ATTR_VALUE_INVALID,  e,
        "value of attribute \"%s\" is invalid; "
                                      "must be %s",
                                      name, dtb, NULL);
                                break;
                            }
                        }
                    }
                }
                break;
            }
        } else if (n_attr_patterns == 0) {
            diag(v, LEPTRIS_DIAG_ATTR_NOT_ALLOWED,  e,
        "found attribute \"%s\", but no attributes "
                       "allowed here", name, NULL);
        } else {
            diag(v, LEPTRIS_DIAG_ATTR_NOT_ALLOWED,  e,
        "attribute \"%s\" not allowed", name, NULL);
        }
    }
    /* Missing required attributes. Optional wrappers (also choice
     * branches) never require presence. One Jing message:
     * singular, or the plural and-joined list. */
    {
        const char* missing[24];
        int nm = 0;
        for (int i = 0; i < n_attr_patterns && nm < 24; i++) {
            RngPattern* c = cands[i];
            if (!c->name || cand_opt[i]) continue;
            if (!leptris_element_attribute(e, c->name))
                missing[nm++] = c->name;
        }
        if (nm == 1) {
            diag(v, LEPTRIS_DIAG_MISSING_REQUIRED_ATTR,  e,
        "element \"%s\" missing required attribute \"%s\"",
                 p->name, missing[0]);
        } else if (nm > 1) {
            char list[256];
            render_quoted_list(missing, nm, "and", list, sizeof(list));
            diag(v, LEPTRIS_DIAG_MISSING_REQUIRED_ATTR,  e,
        "element \"%s\" missing required attributes %s",
                 p->name, list);
        }
    }
}

/* Text/data/value/list diagnosis with Jing wording. */
static void diagnose_text(RngVal* v, RngPattern* p, LeptrisElement e) {
    const char* t = leptris_element_text(e);
    t = t ? t : "";
    for (RngPattern* c = p->first_child; c; c = c->next) {
        if (c->kind == RNG_ATTRIBUTE) continue;
        if (c->kind == RNG_LIST) {
            /* Tokenize; the first offending token reports. */
            char buf[512];
            snprintf(buf, sizeof(buf), "%s", t);
            /* The leaf tokens must match (homogeneous in the core
             * subset): unwrap repeats to the first data/value. */
            RngPattern* leaf = c->first_child;
            while (leaf &&
                   (leaf->kind == RNG_ONE_OR_MORE ||
                    leaf->kind == RNG_ZERO_OR_MORE ||
                    leaf->kind == RNG_OPTIONAL))
                leaf = leaf->first_child;
            for (char* tok = strtok(buf, " \t\r\n"); tok;
                 tok = strtok(NULL, " \t\r\n")) {
                if (leaf && token_matches_leaf(tok, leaf)) continue;
                if (leaf && leaf->kind == RNG_VALUE && leaf->value)
                    diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "character content of element \"%s\" invalid; "
                          "token \"%s\" invalid; must be equal to \"%s\"",
                          p->name, tok, leaf->value);
                else if (leaf && leaf->kind == RNG_DATA) {
                    char dtb[64];
                    snprintf(dtb, sizeof(dtb), "%s %s", art(leaf->datatype),
                             leaf->datatype ? leaf->datatype : "string");
                    diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "character content of element \"%s\" invalid; "
                          "token \"%s\" invalid; must be %s",
                          p->name, tok, dtb);
                }
                else
                    diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "character content of element \"%s\" invalid; "
                         "token \"%s\" invalid", p->name, tok);
                return;
            }
            continue;
        }
        if (c->kind == RNG_VALUE) {
            if (strcmp(t, c->value ? c->value : "") != 0) {
                diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "character content of element \"%s\" invalid; "
                     "must be equal to \"%s\"", p->name,
                     c->value ? c->value : "");
                return;
            }
            continue;
        }
        if (c->kind == RNG_DATA) {
            if (!data_matches(c, t)) {
                char dtb[64];
                snprintf(dtb, sizeof(dtb), "%s %s", art(c->datatype),
                         c->datatype ? c->datatype : "string");
                diag(v, LEPTRIS_DIAG_CHAR_CONTENT_INVALID,  e,
        "character content of element \"%s\" invalid; "
                      "must be %s", p->name, dtb, NULL);
                return;
            }
            continue;
        }
    }
}

/* Push walk cells for one content item. Refs splice their define
 * body member by member — a body is a sibling LIST and the head
 * alone is not the body (Root-Attributes, DocumentBody). Attribute
 * members are element-level (diagnose_attrs) and never gate
 * content order. Bare groups flatten the same way (#878). Depth
 * caps recursive define splices. */
static DiagCell* cell_push(RngVal* v, RngPattern* item, DiagCell* state,
                           int depth) {
    if (!item || depth > 8) return state;
    if (item->kind == RNG_ATTRIBUTE) return state;
    if (item->kind == RNG_REF) {
        RngDefine* d = find_define(v->g, item->name);
        if (d && d->body)
            for (RngPattern* m = d->body; m; m = m->next)
                state = cell_push(v, m, state, depth + 1);
        return state;
    }
    RngPattern* g = deref(v, item);
    if (g && g->kind == RNG_GROUP)
        for (RngPattern* c = g->first_child; c; c = c->next)
            state = cell_push(v, c, state, depth + 1);
    else
        state = dcell_new(v, item, state);
    return state;
}

static void diagnose_element(RngVal* v, RngPattern* p, LeptrisElement e) {
    if (!p || !e) return;
    diagnose_attrs(v, p, e);

    if (!has_element_patterns(p->first_child)) {
        /* Text-level content. */
        LeptrisNodeRef sub[64];
        size_t sn = elem_children(e, sub, 64);
        if (sn) {
            diag(v, LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE,  (LeptrisElement)sub[0],
        "element \"%s\" not allowed anywhere; expected the "
                 "element end-tag or text",
                 leptris_element_name((LeptrisElement)sub[0]), NULL);
            return;
        }
        if (!all_text_ws(e)) diagnose_text(v, p, e);
        return;
    }

    /* Element-content walk. */
    LeptrisNodeRef kids[64];
    size_t n = elem_children(e, kids, 64);
    DiagCell* state = NULL;
    /* Cells see individual members: define bodies behind refs (the
     * sibling list) and bare GROUP children flatten; attributes
     * stay with diagnose_attrs (#878). */
    for (RngPattern* c = p->first_child; c; c = c->next)
        state = cell_push(v, c, state, 0);
    {
        DiagCell* rev = NULL;
        for (DiagCell* c = state; c; ) {
            DiagCell* nx = c->next;
            c->next = rev; rev = c; c = nx;
        }
        state = rev;
    }
    int has_mixed = 0;
    for (RngPattern* c = p->first_child; c; c = c->next)
        if (deref(v, c) && deref(v, c)->kind == RNG_MIXED) has_mixed = 1;

    for (size_t i = 0; i < n; i++) {
        LeptrisElement kid = (LeptrisElement)kids[i];
        const char* x = leptris_element_name(kid);
        if (!x) continue;
        int ok = 0;
        DiagCell* ns = d_state(v, state, x, &ok);
        if (ok) {
            state = ns;
            diagnose_child(v, p->first_child, kid);
            continue;
        }
        /* Not consumable here. Classify per Jing. */
        int in_model = 0;
        for (RngPattern* c = p->first_child; c && !in_model; c = c->next)
            in_model = name_in_tree(v, c, x, 0);
        char expected[512];
        render_expected(v, state, has_mixed, expected, sizeof(expected));
        if (in_model) {
            /* Maybe it comes later — after a missing required
             * prefix ("not allowed yet"). */
            DiagCell* tmp = state;
            const char* req = NULL;
            DiagCell* hit = NULL;
            int hit_ok = 0;
            while (tmp) {
                if (!req) {
                    const char* r0 = first_required_name(v, tmp);
                    if (r0 && strcmp(r0, x) != 0) req = r0;
                }
                DiagCell* r = d_state(v, tmp, x, &hit_ok);
                if (hit_ok) { hit = r; break; }
                tmp = tmp->next;
            }
            if (hit_ok && req) {
                diag(v, LEPTRIS_DIAG_NOT_ALLOWED_YET,  kid,
        "element \"%s\" not allowed yet; missing required "
                     "element \"%s\"", x, req);
                state = hit;  /* recovery: skip prefix, consume x */
                diagnose_child(v, p->first_child, kid);
                continue;
            }
            diag(v, LEPTRIS_DIAG_NOT_ALLOWED_HERE,  kid,
        "element \"%s\" not allowed here; expected %s",
                 x, expected);
        } else {
            diag(v, LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE,  kid,
        "element \"%s\" not allowed anywhere; expected %s",
                 x, expected);
        }
        /* Skip the offending child; position unchanged. */
    }
    if (!nullable_state(v, state)) {
        /* Jing matchEndTag: requiredElementNames() of the leftmost
         * non-nullable position - singular/plural "missing
         * required element(s)", else the expected-content set. */
        const char* req[8];
        int nr = 0;
        for (DiagCell* c = state; c; c = c->next) {
            if (nullable_item(v, c->item)) continue;
            nr = req_names(v, c->item, req, 0);
            break;
        }
        if (nr == 1) {
            diag(v, LEPTRIS_DIAG_INCOMPLETE,  e,
        "element \"%s\" incomplete; missing required "
                       "element \"%s\"", p->name, req[0]);
        } else if (nr > 1) {
            char list[256];
            render_quoted_list(req, nr, "and", list, sizeof(list));
            diag(v, LEPTRIS_DIAG_INCOMPLETE,  e,
        "element \"%s\" incomplete; missing required "
                       "elements %s", p->name, list);
        } else {
            char expected[512];
            render_expected(v, state, has_mixed, expected, sizeof(expected));
            diag(v, LEPTRIS_DIAG_INCOMPLETE,  e,
        "element \"%s\" incomplete; expected %s",
                 p->name, expected);
        }
    }
}

/* Entry: the verdict pass failed — walk and report. */
static void rng_diagnose(RngVal* v, struct leptris_relaxng* rng,
                         LeptrisDocument doc) {
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        diag(v, LEPTRIS_DIAG_INVALID,  NULL,
        "document has no root element", NULL, NULL);
        return;
    }
    RngPattern* start = rng->grammar->start;
    while (start) {
        RngPattern* d = deref(v, start);
        if (!d) break;
        if (d->kind == RNG_GROUP && d->first_child && !d->first_child->next) {
            start = d->first_child;
            continue;
        }
        start = d;
        break;
    }
    if (!start) return;
    if (start->kind == RNG_CHOICE) {
        /* Root-name mismatch: render the alternative names. */
        int matched = 0;
        const char* names[32];
        int n = 0;
        for (RngPattern* a = start->first_child; a; a = a->next) {
            RngPattern* alt = deref(v, a);
            if (alt->kind == RNG_GROUP && alt->first_child &&
                !alt->first_child->next)
                alt = deref(v, alt->first_child);
            if (alt->kind != RNG_ELEMENT || !alt->name) continue;
            if (names_match(alt, root)) {
                /* The root matched this alternative; its CONTENT is
                 * what failed. */
                diagnose_element(v, alt, root);
                matched = 1;
                break;
            }
            if (n < 31) names[n++] = alt->name;
        }
        if (!matched && n > 0) {
            char buf[512];
            size_t w = 0;
            for (int i = 0; i < n; i++) {
                const char* sep = i == 0 ? "" :
                                  (i == n - 1 ? " or " : ", ");
                /* First name carries the element prefix (Jing). */
                if (i == 0)
                    w += snprintf(buf + w, sizeof(buf) - w,
                                  "%selement \"%s\"", sep, names[i]);
                else
                    w += snprintf(buf + w, sizeof(buf) - w,
                                  "%s\"%s\"", sep, names[i]);
            }
            diag(v, LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE,  root,
        "element \"%s\" not allowed anywhere; expected %s",
                 leptris_element_name(root), buf);
        }
        return;
    }
    if (start->kind == RNG_ELEMENT) {
        if (names_match(start, root))
            diagnose_element(v, start, root);
        else
            diag(v, LEPTRIS_DIAG_NOT_ALLOWED_ANYWHERE,  root,
        "element \"%s\" not allowed anywhere; "
                          "expected element \"%s\"",
                 leptris_element_name(root),
                 start->name ? start->name : "?");
        return;
    }
    /* Other shapes (ref at start was deref'd above). */
    diagnose_element(v, start, root);
}

int rng_validate_document(struct leptris_relaxng* rng, LeptrisDocument doc) {
    if (!rng || !rng->grammar || !rng->grammar->start || !doc) return 0;
    RngVal v;
    memset(&v, 0, sizeof(v));
    v.g = rng->grammar;
    v.r = rng;
    /* #878 sub-fix #3: the verdict pass is quiet (speculative
     * backtracking must not record); the diagnose pass reports. */
    v.quiet = 1;

    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        rng->error = leptris_strdup("0:0: error: document has no root "
                                    "element");
        return 0;
    }

    RngPattern* start = rng->grammar->start;
    /* Unwrap the implicit GROUP wrapper and follow REF chains. */
    while ((start->kind == RNG_GROUP && start->first_child &&
            !start->first_child->next) ||
           start->kind == RNG_REF) {
        if (start->kind == RNG_REF) {
            RngDefine* d = find_define(v.g, start->name);
            if (!d || !d->body) {
                v.quiet = 0;
                diag(&v, LEPTRIS_DIAG_INVALID,  root,
        "reference to undefined pattern",
                     start->name, NULL);
                rng->error = leptris_strdup(v.err);
                return 0;
            }
            start = d->body;
        } else {
            start = start->first_child;
        }
    }

    int ok = 0;
    if (start->kind == RNG_CHOICE) {
        /* Combined defines merge into a choice at start; every
         * element alternative gets a chance. */
        for (RngPattern* a = start->first_child; a; a = a->next) {
            RngPattern* alt = a;
            while ((alt->kind == RNG_GROUP && alt->first_child &&
                    !alt->first_child->next) ||
                   alt->kind == RNG_REF) {
                if (alt->kind == RNG_REF) {
                    RngDefine* d = find_define(v.g, alt->name);
                    if (!d || !d->body) break;
                    alt = d->body;
                } else {
                    alt = alt->first_child;
                }
            }
            if (alt->kind != RNG_ELEMENT) continue;
            if (element_ok(&v, alt, root)) {
                ok = 1;
                break;
            }
            v.failed = 0;
            v.err[0] = 0;
        }
    } else if (start->kind == RNG_ELEMENT) {
        ok = element_ok(&v, start, root);
    }

    if (!ok) {
        /* Diagnose with Jing's vocabulary (records into the
         * per-error list). */
        v.quiet = 0;
        v.failed = 0;
        v.err[0] = 0;
        rng_diagnose(&v, rng, doc);
        diag_pool_free(&v);
    }

    /* leptris_rng_error (back-compat) carries the FIRST accumulated
     * message in the "line:column: error: msg" shape — the column
     * picked by the record's kind (Jing convention). */
    if (v.r && v.r->diag_count > 0) {
        char back[512];
        LeptrisDiag* d0 = &v.r->diags[0];
        int col = leptris_diag_kind_uses_end_col(d0->kind)
                      ? d0->col_end : d0->col_start;
        snprintf(back, sizeof(back), "%d:%d: error: %s",
                 d0->line, col, d0->message);
        rng->error = leptris_strdup(back);
    } else {
        rng->error = v.failed ? leptris_strdup(v.err) : NULL;
    }
    return ok;
}
