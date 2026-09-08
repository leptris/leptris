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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    RngGrammar* g;
    char err[512];
    int failed;
} RngVal;

static void fail(RngVal* v, LeptrisElement e, const char* fmt,
                 const char* a, const char* b) {
    if (v->failed) return;
    v->failed = 1;
    int line = e ? leptris_node_line((LeptrisNodeRef)e) : 0;
    char msg[256];
    if (a && b) snprintf(msg, sizeof(msg), fmt, a, b);
    else if (a) snprintf(msg, sizeof(msg), fmt, a);
    else snprintf(msg, sizeof(msg), "%s", fmt);
    snprintf(v->err, sizeof(v->err), "%d:0: error: %s", line, msg);
}

static RngDefine* find_define(RngGrammar* g, const char* name) {
    for (RngDefine* d = g->defines; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
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

/* Does `p` (a pattern that may contain attribute patterns among its
 * children) consume the attribute `name`? */
static int pattern_consumes_attr(RngVal* v, RngPattern* p,
                                 LeptrisElement e, const char* name) {
    for (RngPattern* c = p->first_child; c; c = c->next) {
        switch (c->kind) {
            case RNG_ATTRIBUTE:
                if (c->name && strcmp(c->name, name) == 0) return 1;
                break;
            case RNG_OPTIONAL: case RNG_ZERO_OR_MORE:
            case RNG_GROUP: case RNG_INTERLEAVE: case RNG_CHOICE:
                if (pattern_consumes_attr(v, c, e, name)) return 1;
                break;
            case RNG_REF: {
                RngDefine* d = find_define(v->g, c->name);
                if (d && d->body && pattern_consumes_attr(v, d->body, e, name))
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
                const char* got = leptris_element_attribute(e, c->name);
                if (!got) {
                    fail(v, e, "attribute \"%s\" is required but missing",
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
            fail(v, e, "attribute \"%s\" not allowed", attr_cname(a), NULL);
            return 0;
        }
    }
    return check_required_attrs(v, content, e);
}

/* --- sequence matching ------------------------------------------- */

static int element_ok(RngVal* v, RngPattern* p, LeptrisElement e);

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
        case RNG_DATA: {
            if (ws_only(t)) return 0;
            if (p->datatype && strcmp(p->datatype, "integer") == 0) {
                const char* q = t;
                if (*q == '-' || *q == '+') q++;
                if (!*q) return 0;
                for (; *q; q++)
                    if (!isdigit((unsigned char)*q)) return 0;
            }
            return 1;
        }
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
            for (RngPattern* c = p->first_child; c; c = c->next) {
                size_t r = match_seq(v, c, kids, n, idx);
                if (r != (size_t)-1) return r;
                v->failed = 0;   /* try next alternative */
                v->err[0] = 0;
            }
            return (size_t)-1;
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
            /* Phase-2 simplification: any order, each child once. */
            size_t count = 0;
            for (RngPattern* c = p->first_child; c; c = c->next) count++;
            for (size_t k = 0; k < count; k++) {
                int matched_any = 0;
                size_t ci = 0;
                for (RngPattern* c = p->first_child; c;
                     c = c->next, ci++) {
                    size_t r = match_seq(v, c, kids, n, idx);
                    if (r != (size_t)-1) {
                        matched_any = 1;
                        idx = r;
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
            return r == (size_t)-1 ? idx : r;
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
                fail(v, NULL, "reference to undefined pattern", p->name, NULL);
                return (size_t)-1;
            }
            return match_seq(v, d->body, kids, n, idx);
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

static int element_ok(RngVal* v, RngPattern* p, LeptrisElement e) {
    if (!names_match(p, e)) return 0;
    if (!match_attrs(v, p, e)) return 0;

    if (!has_element_patterns(p->first_child)) {
        /* Text-level content: each non-attribute child is a leaf. */
        for (RngPattern* c = p->first_child; c; c = c->next) {
            if (c->kind == RNG_ATTRIBUTE) continue;
            if (c->kind == RNG_EMPTY) {
                if (!all_text_ws(e)) {
                    fail(v, e, "element \"%s\" must be empty", p->name, NULL);
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
                            fail(v, e, "element \"%s\": content mismatch",
                                 p->name, NULL);
                            return 0;
                        }
                    }
                    continue;
                }
                fail(v, e, "element \"%s\": content mismatch", p->name, NULL);
                return 0;
            }
        }
        {
            LeptrisNodeRef sub[64];
            size_t sn = elem_children(e, sub, 64);
            if (sn) {
                fail(v, (LeptrisElement)sub[0],
                     "element \"%s\" not allowed here",
                     leptris_element_name((LeptrisElement)sub[0]), NULL);
                return 0;
            }
        }
        return 1;
    }

    LeptrisNodeRef kids[64];
    size_t n = elem_children(e, kids, 64);
    /* The content model is a sibling LIST — fold each pattern. */
    size_t r = 0;
    for (RngPattern* c = p->first_child; c; c = c->next) {
        r = match_seq(v, c, kids, n, r);
        if (r == (size_t)-1) return 0;
    }
    if (r == (size_t)-1) return 0;
    if (r != n) {
        fail(v, (LeptrisElement)kids[r],
             "element \"%s\" not allowed here",
             leptris_element_name((LeptrisElement)kids[r]), NULL);
        return 0;
    }
    return 1;
}

int rng_validate_document(struct leptris_relaxng* rng, LeptrisDocument doc) {
    if (!rng || !rng->grammar || !rng->grammar->start || !doc) return 0;
    RngVal v;
    memset(&v, 0, sizeof(v));
    v.g = rng->grammar;

    LeptrisElement root = leptris_document_root(doc);
    if (!root) return 0;

    RngPattern* start = rng->grammar->start;
    /* Unwrap the implicit GROUP wrapper. */
    if (start->kind == RNG_GROUP && start->first_child &&
        !start->first_child->next)
        start = start->first_child;
    if (start->kind != RNG_ELEMENT) return 0;   /* phase-2 subset */

    int ok = element_ok(&v, start, root);
    if (!ok && !v.failed) {
        fail(&v, root, "element \"%s\" not allowed here",
             leptris_element_name(root), NULL);
    }
    rng->error = v.failed ? leptris_strdup(v.err) : NULL;
    return ok && !v.failed;
}
