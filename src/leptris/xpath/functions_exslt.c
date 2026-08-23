/* xpath/functions_exslt.c — first-party EXSLT-style extension pack
 * (TODO.concurrency/06).
 *
 * The commonly-missed XPath 2.0 conveniences, implemented once in C
 * so every binding gets them instead of reimplementing per-language:
 *
 *   str:  replace, tokenize, split, concat, padding
 *   set:  distinct, intersection, difference, leading, trailing
 *   math: max, min, abs, sqrt, power
 *
 * Enabled per-document via leptris_exslt_enable(doc); the handlers
 * are native (full result types — nodesets and numbers, not the
 * string-only custom-fn bridge). Prefixed names ("str:replace")
 * cannot collide with the XPath 1.0 core: the registry lookup uses
 * the raw token text and no core function carries a prefix.
 *
 * str:tokenize / str:split build nodesets of synthetic XPathTextNode
 * entries; the nodeset's owns_synthetic_text flag frees them.
 * str:replace takes string arguments (EXSLT's parallel-nodeset form
 * is out of scope for v1).
 */
#include "functions.h"
#include "evaluator_internal.h"
#include "../leptris_internal.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- helpers --------------------------------------------------------- */

static struct leptris_xpath_result* result_string(const char* s) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_STRING);
    if (!r) return NULL;
    r->value.string_value = s ? leptris_strdup(s) : leptris_strdup("");
    if (!r->value.string_value) { xpath_result_free(r); return NULL; }
    return r;
}

static struct leptris_xpath_result* result_number(double d) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!r) return NULL;
    r->value.number_value = d;
    return r;
}

static struct leptris_xpath_result* result_empty_nodeset(void) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) return NULL;
    XPathNodeSet* ns = xpath_nodeset_new();
    if (!ns) { xpath_result_free(r); return NULL; }
    r->value.nodeset_value = ns;
    return r;
}

/* Evaluate arg i to an OWNED string ("" when absent). */
static char* arg_string(XPathContext* ctx, XPathASTNode** args,
                        size_t arg_count, size_t i) {
    if (i >= arg_count) return leptris_strdup("");
    struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
    if (!r) return leptris_strdup("");
    char* s = xpath_to_string(r);
    xpath_result_free(r);
    return s ? s : leptris_strdup("");
}

/* Evaluate arg i to a nodeset (borrowed from the result; caller
 * frees the result, nodes stay owned by it — copy pointers out
 * before freeing). NULL when absent/not a nodeset. */
static XPathNodeSet* arg_nodeset(XPathContext* ctx, XPathASTNode** args,
                                 size_t arg_count, size_t i,
                                 struct leptris_xpath_result** out_r) {
    if (i >= arg_count) return NULL;
    struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
    if (!r) return NULL;
    if (r->type != XPATH_RESULT_NODESET) {
        xpath_result_free(r);
        return NULL;
    }
    *out_r = r;
    return r->value.nodeset_value;
}

/* ---- str: functions --------------------------------------------------- */

/* str:replace(subject, search, replace) — plain string replace,
 * all non-overlapping occurrences. */
static struct leptris_xpath_result* exslt_str_replace(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    char* subject = arg_string(ctx, args, argc, 0);
    char* search = arg_string(ctx, args, argc, 1);
    char* replace = arg_string(ctx, args, argc, 2);
    if (!subject || !search) {
        free(subject); free(search); free(replace);
        return NULL;
    }
    if (!search[0] || !subject[0]) {
        free(search); free(replace);
        struct leptris_xpath_result* r = result_string(subject);
        free(subject);
        return r;
    }
    size_t slen = strlen(search);
    size_t rlen = replace ? strlen(replace) : 0;
    /* Count occurrences. */
    size_t n = 0;
    const char* p = subject;
    while ((p = strstr(p, search)) != NULL) { n++; p += slen; }
    /* Build output. */
    size_t out_len = strlen(subject) + n * (rlen > slen ? rlen - slen : 0);
    char* out = (char*)malloc(out_len + 1);
    if (!out) {
        free(subject); free(search); free(replace);
        return NULL;
    }
    char* w = out;
    p = subject;
    const char* hit;
    while ((hit = strstr(p, search)) != NULL) {
        memcpy(w, p, (size_t)(hit - p));
        w += hit - p;
        if (rlen) { memcpy(w, replace, rlen); w += rlen; }
        p = hit + slen;
    }
    strcpy(w, p);
    free(subject); free(search); free(replace);
    struct leptris_xpath_result* r = result_string(out);
    free(out);
    return r;
}

/* Shared builder: split subject by a delimiter set/pattern into a
 * nodeset of synthetic text nodes. */
static struct leptris_xpath_result* exslt_split_build(
        char* subject, const char* delims) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { free(subject); return NULL; }
    XPathNodeSet* ns = xpath_nodeset_new();
    if (!ns) { xpath_result_free(r); free(subject); return NULL; }
    ns->owns_synthetic_text = 1;
    r->value.nodeset_value = ns;

    if (!subject[0]) { free(subject); return r; }

    const char* start = subject;
    const char* p = subject;
    while (*p) {
        if (strchr(delims, *p)) {
            size_t len = (size_t)(p - start);
            XPathTextNode* t = (XPathTextNode*)calloc(1, sizeof(*t));
            if (!t) break;
            t->node_type = LEPTRIS_NODE_TEXT;
            t->content = (char*)malloc(len + 1);
            if (!t->content) { free(t); break; }
            memcpy(t->content, start, len);
            t->content[len] = '\0';
            xpath_nodeset_add(ns, t);
            start = p + 1;
        }
        p++;
    }
    /* Trailing token (subject non-empty guarantees at least one
     * segment; a trailing delimiter yields an empty final token). */
    XPathTextNode* t = (XPathTextNode*)calloc(1, sizeof(*t));
    if (t) {
        t->node_type = LEPTRIS_NODE_TEXT;
        size_t len = strlen(start);
        t->content = (char*)malloc(len + 1);
        if (t->content) {
            memcpy(t->content, start, len);
            t->content[len] = '\0';
            xpath_nodeset_add(ns, t);
        } else {
            free(t);
        }
    }
    free(subject);
    return r;
}

/* str:tokenize(string, delimiters) — split on any delimiter char. */
static struct leptris_xpath_result* exslt_str_tokenize(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    char* subject = arg_string(ctx, args, argc, 0);
    char* delims = arg_string(ctx, args, argc, 1);
    if (!subject || !delims || !delims[0]) {
        free(subject); free(delims);
        return NULL;
    }
    struct leptris_xpath_result* r = exslt_split_build(subject, delims);
    free(delims);
    return r;
}

/* Literal-pattern variant of the split builder for str:split
 * (EXSLT: the pattern is a whole string, not a character set —
 * str:tokenize is the charset flavor). */
static struct leptris_xpath_result* exslt_split_pattern(
        char* subject, const char* pattern) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { free(subject); return NULL; }
    XPathNodeSet* ns = xpath_nodeset_new();
    if (!ns) { xpath_result_free(r); free(subject); return NULL; }
    ns->owns_synthetic_text = 1;
    r->value.nodeset_value = ns;

    size_t plen = strlen(pattern);
    const char* start = subject;
    const char* p = subject;
    while (*p) {
        if (strncmp(p, pattern, plen) == 0) {
            size_t len = (size_t)(p - start);
            XPathTextNode* t = (XPathTextNode*)calloc(1, sizeof(*t));
            if (!t) break;
            t->node_type = LEPTRIS_NODE_TEXT;
            t->content = (char*)malloc(len + 1);
            if (!t->content) { free(t); break; }
            memcpy(t->content, start, len);
            t->content[len] = '\0';
            xpath_nodeset_add(ns, t);
            p += plen;
            start = p;
        } else {
            p++;
        }
    }
    /* Trailing token. */
    XPathTextNode* t = (XPathTextNode*)calloc(1, sizeof(*t));
    if (t) {
        t->node_type = LEPTRIS_NODE_TEXT;
        size_t len = strlen(start);
        t->content = (char*)malloc(len + 1);
        if (t->content) {
            memcpy(t->content, start, len);
            t->content[len] = '\0';
            xpath_nodeset_add(ns, t);
        } else {
            free(t);
        }
    }
    free(subject);
    return r;
}

/* str:split(string, pattern) — EXSLT's split: the pattern is a
 * literal string; empty pattern splits into individual characters. */
static struct leptris_xpath_result* exslt_str_split(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    char* subject = arg_string(ctx, args, argc, 0);
    char* pattern = arg_string(ctx, args, argc, 1);
    if (!subject || !pattern) {
        free(subject); free(pattern);
        return NULL;
    }
    struct leptris_xpath_result* r;
    if (!pattern[0]) {
        /* Per-character split (EXSLT: empty pattern = one node per
         * character). Build the nodeset directly — reusing the
         * delimiter path would make every character a delimiter. */
        if (!subject[0]) {
            r = result_empty_nodeset();
        } else {
            r = xpath_result_new(XPATH_RESULT_NODESET);
            if (r) {
                XPathNodeSet* ns = xpath_nodeset_new();
                if (!ns) { xpath_result_free(r); r = NULL; }
                else {
                    ns->owns_synthetic_text = 1;
                    r->value.nodeset_value = ns;
                    size_t len = strlen(subject);
                    for (size_t i = 0; i < len; i++) {
                        XPathTextNode* t = (XPathTextNode*)calloc(1, sizeof(*t));
                        if (!t) break;
                        t->node_type = LEPTRIS_NODE_TEXT;
                        t->content = (char*)malloc(2);
                        if (!t->content) { free(t); break; }
                        t->content[0] = subject[i];
                        t->content[1] = '\0';
                        xpath_nodeset_add(ns, t);
                    }
                }
            }
        }
    } else {
        r = exslt_split_pattern(subject, pattern);
    }
    free(pattern);
    return r;
}

/* str:concat(nodeset) — concatenate the string-values of all nodes
 * (the core concat() takes strings only). */
static struct leptris_xpath_result* exslt_str_concat(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    struct leptris_xpath_result* arg_r = NULL;
    XPathNodeSet* ns = arg_nodeset(ctx, args, argc, 0, &arg_r);
    if (!ns) {
        if (arg_r) xpath_result_free(arg_r);
        return result_string("");
    }
    size_t total = 0;
    for (size_t i = 0; i < ns->count; i++) {
        char* s = get_node_text(ns->nodes[i]);
        if (s) { total += strlen(s); free(s); }
    }
    char* out = (char*)malloc(total + 1);
    struct leptris_xpath_result* r = NULL;
    if (!out) { xpath_result_free(arg_r); return NULL; }
    char* w = out;
    for (size_t i = 0; i < ns->count; i++) {
        char* s = get_node_text(ns->nodes[i]);
        if (s) { strcpy(w, s); w += strlen(s); free(s); }
    }
    *w = '\0';
    r = result_string(out);
    free(out);
    xpath_result_free(arg_r);
    return r;
}

/* str:padding(number, char?) — char repeated n times. */
static struct leptris_xpath_result* exslt_str_padding(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    double n = 0;
    if (argc > 0) {
        struct leptris_xpath_result* r = evaluate_expr(ctx, args[0]);
        if (r) { n = xpath_to_number(r); xpath_result_free(r); }
    }
    if (n < 0) n = 0;
    if (n > 1000000) n = 1000000;  /* cap pathological padding */
    char* fill = arg_string(ctx, args, argc, 1);
    char c = (fill && fill[0]) ? fill[0] : ' ';
    free(fill);
    size_t len = (size_t)n;
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memset(out, c, len);
    out[len] = '\0';
    struct leptris_xpath_result* r = result_string(out);
    free(out);
    return r;
}

/* ---- set: functions ----------------------------------------------------
 * All operate on the string-values of nodeset entries, comparing by
 * pointer identity for membership (an element appears in two
 * nodesets as the same pointer). */

/* str:distinct(nodeset) — first occurrence of each string-value. */
static struct leptris_xpath_result* exslt_set_distinct(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    struct leptris_xpath_result* arg_r = NULL;
    XPathNodeSet* ns = arg_nodeset(ctx, args, argc, 0, &arg_r);
    if (!ns) return arg_r ? (xpath_result_free(arg_r), result_empty_nodeset())
                          : result_empty_nodeset();
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) { xpath_result_free(arg_r); return NULL; }
    XPathNodeSet* res = xpath_nodeset_new();
    if (!res) { xpath_result_free(out); xpath_result_free(arg_r); return NULL; }
    out->value.nodeset_value = res;

    for (size_t i = 0; i < ns->count; i++) {
        int seen = 0;
        for (size_t j = 0; j < res->count && !seen; j++) {
            if (res->nodes[j] == ns->nodes[i]) seen = 1;
        }
        if (!seen) xpath_nodeset_add(res, ns->nodes[i]);
    }
    xpath_result_free(arg_r);
    return out;
}

/* set:intersection(a, b) / set:difference(a, b) — entries of a that
 * appear (or do not appear) in b, document order preserved. */
static struct leptris_xpath_result* exslt_set_combo(
        XPathContext* ctx, XPathASTNode** args, size_t argc,
        int want_intersection) {
    struct leptris_xpath_result* ra = NULL;
    struct leptris_xpath_result* rb = NULL;
    XPathNodeSet* a = arg_nodeset(ctx, args, argc, 0, &ra);
    XPathNodeSet* b = arg_nodeset(ctx, args, argc, 1, &rb);
    struct leptris_xpath_result* out = NULL;
    XPathNodeSet* res = NULL;

    if (!a || !b) goto done;
    out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) goto done;
    res = xpath_nodeset_new();
    if (!res) { xpath_result_free(out); out = NULL; goto done; }
    out->value.nodeset_value = res;

    for (size_t i = 0; i < a->count; i++) {
        int in_b = 0;
        for (size_t j = 0; j < b->count && !in_b; j++) {
            if (a->nodes[i] == b->nodes[j]) in_b = 1;
        }
        if (in_b == want_intersection) xpath_nodeset_add(res, a->nodes[i]);
    }

done:
    if (ra) xpath_result_free(ra);
    if (rb) xpath_result_free(rb);
    if (!out) return result_empty_nodeset();
    return out;
}

static struct leptris_xpath_result* exslt_set_intersection(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_set_combo(ctx, args, argc, 1);
}

static struct leptris_xpath_result* exslt_set_difference(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_set_combo(ctx, args, argc, 0);
}

/* set:leading(a, b) — nodes of a preceding the FIRST node of b;
 * set:trailing(a, b) — nodes of a following it. */
static struct leptris_xpath_result* exslt_set_around(
        XPathContext* ctx, XPathASTNode** args, size_t argc,
        int want_leading) {
    struct leptris_xpath_result* ra = NULL;
    struct leptris_xpath_result* rb = NULL;
    XPathNodeSet* a = arg_nodeset(ctx, args, argc, 0, &ra);
    XPathNodeSet* b = arg_nodeset(ctx, args, argc, 1, &rb);
    struct leptris_xpath_result* out = NULL;
    XPathNodeSet* res = NULL;

    if (!a || !b || b->count == 0) goto done;
    out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) goto done;
    res = xpath_nodeset_new();
    if (!res) { xpath_result_free(out); out = NULL; goto done; }
    out->value.nodeset_value = res;

    /* b[0]'s position in a (b[0] may itself be an a-member). */
    size_t b0 = a->count;
    for (size_t j = 0; j < a->count; j++) {
        if (a->nodes[j] == b->nodes[0]) { b0 = j; break; }
    }
    for (size_t i = 0; i < a->count; i++) {
        if (a->nodes[i] == b->nodes[0]) continue;  /* boundary itself */
        int before_b = (i < b0);
        if (before_b == want_leading) xpath_nodeset_add(res, a->nodes[i]);
    }

done:
    if (ra) xpath_result_free(ra);
    if (rb) xpath_result_free(rb);
    if (!out) return result_empty_nodeset();
    return out;
}

static struct leptris_xpath_result* exslt_set_leading(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_set_around(ctx, args, argc, 1);
}

static struct leptris_xpath_result* exslt_set_trailing(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_set_around(ctx, args, argc, 0);
}

/* ---- math: functions --------------------------------------------------- */

/* math:max / math:min over a nodeset's numeric string-values. */
static struct leptris_xpath_result* exslt_math_extreme(
        XPathContext* ctx, XPathASTNode** args, size_t argc, int want_max) {
    struct leptris_xpath_result* arg_r = NULL;
    XPathNodeSet* ns = arg_nodeset(ctx, args, argc, 0, &arg_r);
    if (!ns) {
        if (arg_r) xpath_result_free(arg_r);
        return result_number(NAN);
    }
    double best = NAN;
    for (size_t i = 0; i < ns->count; i++) {
        char* s = get_node_text(ns->nodes[i]);
        if (s) {
            double v = atof(s);
            if (isnan(best) ||
                (want_max ? v > best : v < best)) best = v;
            free(s);
        }
    }
    xpath_result_free(arg_r);
    return result_number(best);
}

static struct leptris_xpath_result* exslt_math_max(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_math_extreme(ctx, args, argc, 1);
}

static struct leptris_xpath_result* exslt_math_min(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return exslt_math_extreme(ctx, args, argc, 0);
}

static double arg_number(XPathContext* ctx, XPathASTNode** args,
                         size_t argc, size_t i) {
    if (i >= argc) return NAN;
    struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
    if (!r) return NAN;
    double v = xpath_to_number(r);
    xpath_result_free(r);
    return v;
}

static struct leptris_xpath_result* exslt_math_abs(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return result_number(fabs(arg_number(ctx, args, argc, 0)));
}

static struct leptris_xpath_result* exslt_math_sqrt(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    double v = arg_number(ctx, args, argc, 0);
    return result_number(v < 0 ? NAN : sqrt(v));
}

static struct leptris_xpath_result* exslt_math_power(
        XPathContext* ctx, XPathASTNode** args, size_t argc) {
    return result_number(pow(arg_number(ctx, args, argc, 0),
                             arg_number(ctx, args, argc, 1)));
}

/* ---- registration ------------------------------------------------------ */

void leptris_exslt_register(XPathFunctionRegistry* reg) {
    xpath_function_registry_register(reg, "str:replace", exslt_str_replace, 3, 3);
    xpath_function_registry_register(reg, "str:tokenize", exslt_str_tokenize, 2, 2);
    xpath_function_registry_register(reg, "str:split", exslt_str_split, 2, 2);
    xpath_function_registry_register(reg, "str:concat", exslt_str_concat, 1, 1);
    xpath_function_registry_register(reg, "str:padding", exslt_str_padding, 1, 2);
    xpath_function_registry_register(reg, "set:distinct", exslt_set_distinct, 1, 1);
    xpath_function_registry_register(reg, "set:intersection", exslt_set_intersection, 2, 2);
    xpath_function_registry_register(reg, "set:difference", exslt_set_difference, 2, 2);
    xpath_function_registry_register(reg, "set:leading", exslt_set_leading, 2, 2);
    xpath_function_registry_register(reg, "set:trailing", exslt_set_trailing, 2, 2);
    xpath_function_registry_register(reg, "math:max", exslt_math_max, 1, 1);
    xpath_function_registry_register(reg, "math:min", exslt_math_min, 1, 1);
    xpath_function_registry_register(reg, "math:abs", exslt_math_abs, 1, 1);
    xpath_function_registry_register(reg, "math:sqrt", exslt_math_sqrt, 1, 1);
    xpath_function_registry_register(reg, "math:power", exslt_math_power, 2, 2);
}
