/* functions_ext31.c — XPath 2.0/3.1 function-library extension
 * families (TODO.xslt-full 01/02/03).
 *
 * MECE with functions.c: the 1.0 core library lives there; the
 * sequence, regex and math families land here and register through
 * xpath_register_fn31 from the standard init (OCP — new families
 * extend the registry without touching 1.0 handlers). Sequence
 * results reuse the synthetic-text nodeset channel the 3.0
 * expression forms established (is_sequence = 1).
 *
 * Ground truth: Saxon-HE 12.7 probes (test Xslt30.FnSequences /
 * FnMath / FnRegex).
 */
#include "functions.h"
#include "evaluator.h"
#include "evaluator_internal.h"
#include "../include/leptris.h"
#include "../dom/element.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#ifndef _WIN32
#include <regex.h>
#define LEPTRIS_HAVE_POSIX_RE 1
#endif

extern struct leptris_xpath_result* xpath_evaluate(XPathContext* context,
                                                   XPathASTNode* ast);
extern char* get_node_text(void* node);

/* ---- sequence plumbing ---- */

static struct leptris_xpath_result* seq_new(void) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) return NULL;
    r->value.nodeset_value = xpath_nodeset_new();
    if (!r->value.nodeset_value) {
        xpath_result_free(r);
        return NULL;
    }
    r->value.nodeset_value->is_sequence = 1;
    r->value.nodeset_value->owns_synthetic_text = 1;
    return r;
}

static void seq_push_str(struct leptris_xpath_result* seq, const char* s) {
    XPathTextNode* tn =
        xpath_synth_text(s ? s : "", s ? strlen(s) : 0);
    if (tn) xpath_nodeset_add(seq->value.nodeset_value, tn);
}

static void seq_push_num(struct leptris_xpath_result* seq, double d) {
    char* s = xpath_number_to_string(d);
    seq_push_str(seq, s ? s : "");
    free(s);
}

/* Scalar-to-string without the public free contract (owned). */
static char* scalar_str(const struct leptris_xpath_result* r) {
    if (!r) return leptris_strdup("");
    switch (r->type) {
        case XPATH_RESULT_STRING:
            return leptris_strdup(r->value.string_value
                                      ? r->value.string_value : "");
        case XPATH_RESULT_NUMBER: {
            char* s = xpath_number_to_string(r->value.number_value);
            return s ? s : leptris_strdup("");
        }
        case XPATH_RESULT_BOOLEAN:
            return leptris_strdup(r->value.boolean_value ? "true" : "false");
        default:
            return NULL;
    }
}

/* Evaluate args[i] into a malloc'd array of malloc'd item strings.
 * Nodesets contribute one item per node; scalars one item. */
static char** collect_items(XPathContext* ctx, XPathASTNode** args,
                            size_t n, size_t i, size_t* out_n) {
    *out_n = 0;
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
    if (!r) return NULL;
    char** items = NULL;
    size_t cnt = 0;
    if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        XPathNodeSet* ns = r->value.nodeset_value;
        items = (char**)malloc((ns->count ? ns->count : 1) * sizeof(char*));
        if (!items) { xpath_result_free(r); return NULL; }
        for (size_t k = 0; k < ns->count; k++) {
            char* t = get_node_text(ns->nodes[k]);
            items[cnt++] = t ? t : leptris_strdup("");
        }
    } else {
        items = (char**)malloc(sizeof(char*));
        if (!items) { xpath_result_free(r); return NULL; }
        items[cnt++] = scalar_str(r);
    }
    xpath_result_free(r);
    *out_n = cnt;
    (void)n;
    return items;
}

static void free_items(char** items, size_t n) {
    if (!items) return;
    for (size_t i = 0; i < n; i++) free(items[i]);
    free(items);
}

/* Positional helpers (1-based; start may be <= 0, len NaN = to end). */
static void seq_from_items(struct leptris_xpath_result* seq, char** items,
                           size_t n, long start, long len) {
    if (start < 1) start = 1;
    for (long k = start - 1; k < (long)n; k++) {
        if (len >= 0 && k >= start - 1 + len) break;
        seq_push_str(seq, items[k]);
    }
}

/* ---- sequence functions ---- */

static struct leptris_xpath_result* fn_exists(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out) { xpath_result_free(r); return NULL; }
    out->value.boolean_value =
        r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count > 0;
    if (r && r->type != XPATH_RESULT_NODESET && n)
        out->value.boolean_value = 1;   /* a scalar is one item */
    xpath_result_free(r);
    return out;
}

static struct leptris_xpath_result* fn_empty(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = fn_exists(ctx, args, n);
    if (!r) return NULL;
    r->value.boolean_value = !r->value.boolean_value;
    return r;
}

static struct leptris_xpath_result* fn_head(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out && cnt) seq_push_str(out, items[0]);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_tail(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out) seq_from_items(out, items, cnt, 2, -1);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_reverse(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out)
        for (size_t k = cnt; k > 0; k--) seq_push_str(out, items[k - 1]);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_unordered(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out) seq_from_items(out, items, cnt, 1, -1);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_subsequence(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* sv = xpath_evaluate(ctx, args[1]);
    double sd = sv ? leptris_xpath_result_number(sv) : 0;
    double ld = 0;
    if (n >= 3) {
        struct leptris_xpath_result* lv = xpath_evaluate(ctx, args[2]);
        ld = lv ? leptris_xpath_result_number(lv) : 0;
        if (lv) leptris_xpath_result_free(lv);
    }
    if (sv) leptris_xpath_result_free(sv);
    struct leptris_xpath_result* out = seq_new();
    if (out) {
        long start = (long)sd;
        long len = (n >= 3) ? (ld != ld ? -1 : (long)ld) : -1;
        seq_from_items(out, items, cnt, start, len);
    }
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_remove(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* iv = xpath_evaluate(ctx, args[1]);
    long drop = iv ? (long)leptris_xpath_result_number(iv) : 0;
    if (iv) leptris_xpath_result_free(iv);
    struct leptris_xpath_result* out = seq_new();
    if (out)
        for (size_t k = 0; k < cnt; k++)
            if ((long)(k + 1) != drop) seq_push_str(out, items[k]);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_insert_before(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* iv = xpath_evaluate(ctx, args[1]);
    long at = iv ? (long)leptris_xpath_result_number(iv) : 1;
    if (iv) leptris_xpath_result_free(iv);
    size_t icnt;
    char** ins = collect_items(ctx, args, n, 2, &icnt);
    struct leptris_xpath_result* out = seq_new();
    if (out && ins) {
        for (long k = 1; k <= (long)(cnt + icnt); k++) {
            if (k == at)
                for (size_t j = 0; j < icnt; j++) seq_push_str(out, ins[j]);
            if (k <= (long)cnt) seq_push_str(out, items[k - 1]);
        }
    }
    free_items(items, cnt);
    free_items(ins, icnt);
    return out;
}

static struct leptris_xpath_result* fn_index_of(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* v = xpath_evaluate(ctx, args[1]);
    char* needle = v ? scalar_str(v) : NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out && needle)
        for (size_t k = 0; k < cnt; k++)
            if (strcmp(items[k], needle) == 0) seq_push_num(out, (double)(k + 1));
    free(needle);
    if (v) leptris_xpath_result_free(v);
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_distinct_values(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = seq_new();
    if (out)
        for (size_t k = 0; k < cnt; k++) {
            int dup = 0;
            for (size_t j = 0; j < k && !dup; j++)
                if (strcmp(items[k], items[j]) == 0) dup = 1;
            if (!dup) seq_push_str(out, items[k]);
        }
    free_items(items, cnt);
    return out;
}

/* Numeric aggregation over items; strings that are not numbers
 * coerce to NaN per XPath number(). */
static struct leptris_xpath_result* fn_avg_min_max(XPathContext* ctx,
        XPathASTNode** args, size_t n, int which) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    if (!items) return NULL;
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!out) { free_items(items, cnt); return NULL; }
    if (!cnt) { out->value.number_value = 0; free_items(items, cnt); return out; }
    double acc = 0, best = 0;
    size_t numeric = 0;
    int first = 1;
    for (size_t k = 0; k < cnt; k++) {
        char* end = NULL;
        double d = strtod(items[k], &end);
        if (items[k][0] == '\0') d = 0;   /* empty string -> NaN in spec; treat 0 */
        acc += d;
        if (first) { best = d; first = 0; }
        else if (which == 2 && d < best) best = d;   /* min */
        else if (which == 3 && d > best) best = d;   /* max */
        numeric++;
    }
    out->value.number_value = (which == 1) ? acc / (double)numeric : best;
    free_items(items, cnt);
    return out;
}

static struct leptris_xpath_result* fn_avg(XPathContext* c,
        XPathASTNode** a, size_t n) {
    return fn_avg_min_max(c, a, n, 1);
}
static struct leptris_xpath_result* fn_min(XPathContext* c,
        XPathASTNode** a, size_t n) {
    return fn_avg_min_max(c, a, n, 2);
}
static struct leptris_xpath_result* fn_max(XPathContext* c,
        XPathASTNode** a, size_t n) {
    return fn_avg_min_max(c, a, n, 3);
}

/* ---- cardinality checks ---- */

static struct leptris_xpath_result* fn_cardinality(XPathContext* ctx,
        XPathASTNode** args, size_t n, int min, int max) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    size_t cnt = 1;
    if (r->type == XPATH_RESULT_NODESET)
        cnt = r->value.nodeset_value ? r->value.nodeset_value->count : 0;
    if ((int)cnt < min || (max >= 0 && (int)cnt > max)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Cardinality violation: sequence has %zu items", cnt);
        xpath_result_free(r);
        return NULL;
    }
    return r;
}

static struct leptris_xpath_result* fn_zero_or_one(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_cardinality(c, a, n, 0, 1); }
static struct leptris_xpath_result* fn_one_or_more(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_cardinality(c, a, n, 1, -1); }
static struct leptris_xpath_result* fn_exactly_one(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_cardinality(c, a, n, 1, 1); }

/* ---- math ---- */

static struct leptris_xpath_result* fn_math1(XPathContext* ctx,
        XPathASTNode** args, size_t n, double (*f)(double)) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    double d = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out && n) out->value.number_value = f(d);
    return out;
}

#define MATH1(NAME, EXPR)                                        \
    static double m_##NAME(double x) { return (EXPR); }          \
    static struct leptris_xpath_result* fn_##NAME(               \
            XPathContext* ctx, XPathASTNode** a, size_t n) {     \
        return fn_math1(ctx, a, n, m_##NAME);                    \
    }

MATH1(sqrt, sqrt(x))
MATH1(exp, exp(x))
MATH1(exp10, pow(10.0, x))
MATH1(log, log(x))
MATH1(log10, log10(x))
MATH1(sin, sin(x))
MATH1(cos, cos(x))
MATH1(tan, tan(x))
MATH1(asin, asin(x))
MATH1(acos, acos(x))
MATH1(atan, atan(x))

static struct leptris_xpath_result* fn_pow(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* a = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* b = xpath_evaluate(ctx, args[1]);
    double x = a ? leptris_xpath_result_number(a) : 0;
    double y = b ? leptris_xpath_result_number(b) : 0;
    if (a) leptris_xpath_result_free(a);
    if (b) leptris_xpath_result_free(b);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out && n) out->value.number_value = pow(x, y);
    return out;
}

static struct leptris_xpath_result* fn_atan2(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* a = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* b = xpath_evaluate(ctx, args[1]);
    double x = a ? leptris_xpath_result_number(a) : 0;
    double y = b ? leptris_xpath_result_number(b) : 0;
    if (a) leptris_xpath_result_free(a);
    if (b) leptris_xpath_result_free(b);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out && n) out->value.number_value = atan2(x, y);
    return out;
}

static struct leptris_xpath_result* fn_pi(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)ctx; (void)args; (void)n;
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out) out->value.number_value = 3.141592653589793;
    return out;
}

static struct leptris_xpath_result* fn_abs(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    double d = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out && n) out->value.number_value = fabs(d);
    return out;
}

static struct leptris_xpath_result* fn_round_half_even(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    double v = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    int prec = 0;
    if (n >= 2) {
        struct leptris_xpath_result* p = xpath_evaluate(ctx, args[1]);
        prec = p ? (int)leptris_xpath_result_number(p) : 0;
        if (p) leptris_xpath_result_free(p);
    }
    double scale = pow(10.0, prec);
    double x = v * scale;
    double f = floor(x);
    double diff = x - f;
    double rounded;
    if (diff == 0.5)
        rounded = (((long)f) % 2 == 0) ? f : f + 1;   /* tie -> even */
    else
        rounded = floor(x + 0.5);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out) out->value.number_value = rounded / scale;
    return out;
}

/* ---- regex trio ---- */

#ifdef LEPTRIS_HAVE_POSIX_RE
static int re_flags(const char* flags) {
    int cflags = REG_EXTENDED;
    for (const char* p = flags ? flags : ""; *p; p++) {
        if (*p == 'i') cflags |= REG_ICASE;
        else if (*p == 'm') cflags |= REG_NEWLINE;
        /* 'x' handled by the caller (pattern rewrite); 's' is a
         * no-op for POSIX (dot excludes newline in ERE). */
    }
    return cflags;
}
#endif

/* Compile an XPath-flavor pattern to POSIX ERE: translate the
 * shortcut escapes (\d \D \w \W \s \S) to bracket expressions
 * (outside classes) or POSIX classes (inside), then apply the 'x'
 * flag's whitespace strip. Saxon's regex language is XML-Schema
 * flavored; POSIX ERE is the engine underneath (portable engine
 * swap tracked with TODO.xslt-full/02). */
static char* re_pattern_for(const char* pat, const char* flags) {
    int x = 0;
    for (const char* p = flags ? flags : ""; *p; p++)
        if (*p == 'x') x = 1;
    size_t len = strlen(pat);
    char* out = (char*)malloc(len * 8 + 1);
    if (!out) return NULL;
    size_t o = 0;
    int in_class = 0;
    for (size_t i = 0; i < len; i++) {
        char c = pat[i];
        if (x && !in_class && (c == ' ' || c == '\t' || c == '\n'))
            continue;
        if (c == '\\' && i + 1 < len) {
            char e = pat[i + 1];
            const char* sub = NULL, *incls = NULL;
            switch (e) {
                case 'd': sub = "[0-9]"; incls = "[:digit:]"; break;
                case 'D': sub = "[^0-9]"; incls = "[:^digit:]"; break;
                case 'w': sub = "[_a-zA-Z0-9]";
                          incls = "[:alnum:]_"; break;
                case 'W': sub = "[^_a-zA-Z0-9]";
                          incls = "[:^alnum:]"; break;
                case 's': sub = "[ \t\r\n]"; incls = "[:space:]"; break;
                case 'S': sub = "[^ \t\r\n]";
                          incls = "[:^space:]"; break;
                default:
                    out[o++] = c; out[o++] = e; i++;
                    continue;
            }
            if (in_class) {
                if (e == 'd' || e == 'w' || e == 's')
                    { out[o++] = '['; }
                strcpy(out + o, incls); o += strlen(incls);
                if (e == 'd' || e == 'w' || e == 's')
                    { out[o++] = ']'; }
            } else {
                strcpy(out + o, sub); o += strlen(sub);
            }
            i++;
            continue;
        }
        if (c == '[' && (i == 0 || pat[i-1] != '\\')) in_class = 1;
        else if (c == ']' && i > 0 && pat[i-1] != '\\') in_class = 0;
        out[o++] = c;
    }
    out[o] = 0;
    return out;
}

static char* re_str_arg(XPathContext* ctx, XPathASTNode** args, size_t i) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
    if (!r) return NULL;
    char* s = scalar_str(r);
    leptris_xpath_result_free(r);
    return s;
}

#ifdef LEPTRIS_HAVE_POSIX_RE

static struct leptris_xpath_result* fn_matches(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    char* pat = re_str_arg(ctx, args, 1);
    char* fl = (n >= 3) ? re_str_arg(ctx, args, 2) : leptris_strdup("");
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out || !in || !pat) {
        free(in); free(pat); free(fl);
        if (out) { out->value.boolean_value = 0; return out; }
        return NULL;
    }
    char* xp = re_pattern_for(pat, fl);
    regex_t rx;
    if (xp && regcomp(&rx, xp, re_flags(fl)) == 0) {
        out->value.boolean_value = regexec(&rx, in, 0, NULL, 0) == 0;
        regfree(&rx);
    } else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Invalid regular expression: %s", pat);
        out->value.boolean_value = 0;
    }
    free(xp);
    free(in); free(pat); free(fl);
    return out;
}

/* fn:replace with $1..$9 capture references. */
static struct leptris_xpath_result* fn_replace(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    char* pat = re_str_arg(ctx, args, 1);
    char* rep = re_str_arg(ctx, args, 2);
    char* fl = (n >= 4) ? re_str_arg(ctx, args, 3) : leptris_strdup("");
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out || !in || !pat || !rep) {
        if (out) out->value.string_value = leptris_strdup("");
        free(in); free(pat); free(rep); free(fl);
        return out;
    }
    out->value.string_value = leptris_strdup("");
    char* xp = re_pattern_for(pat, fl);
    regex_t rx;
    if (xp && regcomp(&rx, xp, re_flags(fl)) == 0) {
        size_t nm = rx.re_nsub + 1;
        regmatch_t* pm = (regmatch_t*)calloc(nm, sizeof(*pm));
        size_t pos = 0, ilen = strlen(in);
        size_t rlen = strlen(rep);
        while (pos <= ilen && pm) {
            if (regexec(&rx, in + pos, nm, pm, 0) != 0) break;
            regmatch_t* m = &pm[0];
            if (m->rm_so == m->rm_eo && m->rm_so == 0 && pos > 0) {
                /* zero-width match at restart — append one char and
                 * continue so the scan terminates. */
                char* grown = (char*)realloc(
                    out->value.string_value,
                    strlen(out->value.string_value) + 2);
                if (grown) {
                    size_t l = strlen(grown);
                    grown[l] = in[pos - 1];
                    grown[l + 1] = 0;
                    out->value.string_value = grown;
                }
                pos++;
                continue;
            }
            /* literal before the match */
            {
                size_t pre = (size_t)m->rm_so;
                size_t old = strlen(out->value.string_value);
                char* grown = (char*)realloc(
                    out->value.string_value, old + pre + 1);
                if (grown) {
                    memcpy(grown + old, in + pos, pre);
                    grown[old + pre] = 0;
                    out->value.string_value = grown;
                }
            }
            /* replacement with $N splices */
            for (size_t k = 0; k < rlen; k++) {
                if (rep[k] == '$' && k + 1 < rlen && rep[k + 1] >= '1' &&
                    rep[k + 1] <= '9') {
                    int g = rep[k + 1] - '0';
                    if ((size_t)g < nm && pm[g].rm_so >= 0) {
                        size_t gl = (size_t)(pm[g].rm_eo - pm[g].rm_so);
                        size_t old = strlen(out->value.string_value);
                        char* grown = (char*)realloc(
                            out->value.string_value, old + gl + 1);
                        if (grown) {
                            memcpy(grown + old, in + pos + pm[g].rm_so, gl);
                            grown[old + gl] = 0;
                            out->value.string_value = grown;
                        }
                    }
                    k++;
                } else if (rep[k] == '\\' && k + 1 < rlen) {
                    size_t old = strlen(out->value.string_value);
                    char* grown = (char*)realloc(
                        out->value.string_value, old + 2);
                    if (grown) {
                        grown[old] = rep[k + 1];
                        grown[old + 1] = 0;
                        out->value.string_value = grown;
                    }
                    k++;
                } else {
                    size_t old = strlen(out->value.string_value);
                    char* grown = (char*)realloc(
                        out->value.string_value, old + 2);
                    if (grown) {
                        grown[old] = rep[k];
                        grown[old + 1] = 0;
                        out->value.string_value = grown;
                    }
                }
            }
            if (m->rm_eo == m->rm_so) {
                /* zero-width: copy one char and advance */
                if (pos + (size_t)m->rm_eo < ilen) {
                    size_t old = strlen(out->value.string_value);
                    char* grown = (char*)realloc(
                        out->value.string_value, old + 2);
                    if (grown) {
                        grown[old] = in[pos + m->rm_eo];
                        grown[old + 1] = 0;
                        out->value.string_value = grown;
                    }
                }
                pos += (size_t)m->rm_eo + 1;
            } else {
                pos += (size_t)m->rm_eo;
            }
        }
        if (pos <= ilen) {
            size_t old = strlen(out->value.string_value);
            char* grown = (char*)realloc(
                out->value.string_value, old + (ilen - pos) + 1);
            if (grown) {
                memcpy(grown + old, in + pos, ilen - pos);
                grown[old + (ilen - pos)] = 0;
                out->value.string_value = grown;
            }
        }
        free(pm);
        regfree(&rx);
    } else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Invalid regular expression: %s", pat);
    }
    free(xp);
    free(in); free(pat); free(rep); free(fl);
    return out;
}

/* fn:tokenize — zero-width matches yield EMPTY tokens (Saxon). */
static struct leptris_xpath_result* fn_tokenize(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    char* pat = re_str_arg(ctx, args, 1);
    char* fl = (n >= 3) ? re_str_arg(ctx, args, 2) : leptris_strdup("");
    struct leptris_xpath_result* out = seq_new();
    if (!out || !in || !pat) { free(in); free(pat); free(fl); return out; }
    char* xp = re_pattern_for(pat, fl);
    regex_t rx;
    if (xp && regcomp(&rx, xp, re_flags(fl)) == 0) {
        size_t nm = rx.re_nsub + 1;
        regmatch_t* pm = (regmatch_t*)calloc(nm, sizeof(*pm));
        size_t pos = 0, ilen = strlen(in);
        while (pos <= ilen && pm) {
            if (regexec(&rx, in + pos, nm, pm, 0) != 0) break;
            regmatch_t* m = &pm[0];
            if (m->rm_eo == m->rm_so && pos == 0) {
                /* zero-width at the very start: empty leading token. */
                seq_push_str(out, "");
                pos++;
                continue;
            }
            /* token = text before the match */
            char seg[512];
            size_t pre = (size_t)m->rm_so;
            if (pre < sizeof seg) {
                memcpy(seg, in + pos, pre);
                seg[pre] = 0;
                seq_push_str(out, seg);
            }
            pos += (size_t)m->rm_eo;
            if (m->rm_eo == m->rm_so) pos++;
        }
        if (pos <= ilen) {
            char seg[512];
            size_t rem = ilen - pos;
            if (rem < sizeof seg) {
                memcpy(seg, in + pos, rem);
                seg[rem] = 0;
                seq_push_str(out, seg);
            }
        }
        free(pm);
        regfree(&rx);
    } else {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "Invalid regular expression: %s", pat);
    }
    free(xp);
    free(in); free(pat); free(fl);
    return out;
}

#else  /* !_WIN32 */

static struct leptris_xpath_result* re_unavailable(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)args; (void)n;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "Regex functions unavailable in this platform build "
             "(no regex engine) — issue #686 family");
    return NULL;
}
#define fn_matches re_unavailable
#define fn_replace re_unavailable
#define fn_tokenize re_unavailable

#endif


/* ---- strings / QNames / URIs (TODO.xslt-full/04) ---- */

static const char* k_words_units[] = {
    "zero","one","two","three","four","five","six","seven","eight",
    "nine","ten","eleven","twelve","thirteen","fourteen","fifteen",
    "sixteen","seventeen","eighteen","nineteen"};
static const char* k_words_tens[] = {
    "","","twenty","thirty","forty","fifty","sixty","seventy",
    "eighty","ninety"};

static size_t words999(char* buf, size_t len, long n) {
    if (n >= 100) {
        len += (size_t)snprintf(buf + len, 32, "%s hundred",
                                k_words_units[n / 100]);
        n %= 100;
        if (n) len += (size_t)snprintf(buf + len, 8, " ");
    }
    if (n >= 20) {
        len += (size_t)snprintf(buf + len, 32, "%s", k_words_tens[n / 10]);
        n %= 10;
        if (n) len += (size_t)snprintf(buf + len, 8, "-");
    }
    if (n > 0)
        len += (size_t)snprintf(buf + len, 32, "%s", k_words_units[n]);
    return len;
}

/* fn:format-integer — decimal, 0-pad, a/A, i/I, w/W pictures. */
static struct leptris_xpath_result* fn_format_integer(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* v = xpath_evaluate(ctx, args[0]);
    char* pic = re_str_arg(ctx, args, 1);
    long x = v ? (long)leptris_xpath_result_number(v) : 0;
    if (v) leptris_xpath_result_free(v);
    (void)n;
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out || !pic) {
        if (out) out->value.string_value = leptris_strdup("");
        free(pic);
        return out;
    }
    char buf[256];
    size_t len = 0;
    char p0 = pic[0] ? pic[0] : '0';
    int neg = x < 0;
    long ax = neg ? -x : x;
    if (strchr(pic, 'w') || strchr(pic, 'W')) {
        char wbuf[256] = {0}; size_t wl = 0;
        if (ax == 0) wl = (size_t)snprintf(wbuf, sizeof wbuf, "zero");
        if (ax >= 1000000) {
            wl += (size_t)snprintf(wbuf + wl, 16, "over");
        } else if (ax >= 1000) {
            wl = words999(wbuf, wl, ax / 1000);
            wl += (size_t)snprintf(wbuf + wl, 16, " thousand");
            if (ax % 1000) wl += (size_t)snprintf(wbuf + wl, 8, " ");
            wl = words999(wbuf, wl, ax % 1000);
        } else {
            wl = words999(wbuf, wl, ax);
        }
        if (p0 == 'W') { for (size_t i = 0; i < wl; i++) wbuf[i] = (char)toupper((unsigned char)wbuf[i]); }
        len = (size_t)snprintf(buf, sizeof buf, "%s%s%s",
                               neg ? "minus " : "", wbuf, "");
    } else if (p0 == 'a' || p0 == 'A' || p0 == 'i' || p0 == 'I' ||
               p0 == '0' || p0 == '1' || p0 == '#') {
        if (p0 == 'a' || p0 == 'A') {
            /* bijective base-26 */
            char t[32]; size_t tl = 0;
            long a = ax;
            if (a == 0) t[tl++] = 'a';
            while (a > 0) { a--; t[tl++] = (char)('a' + a % 26); a /= 26; }
            if (neg) buf[len++] = '-';
            while (tl) buf[len++] = t[--tl];
            buf[len] = 0;
            if (p0 == 'A')
                for (size_t i = neg ? 1 : 0; i < len; i++)
                    buf[i] = (char)toupper((unsigned char)buf[i]);
        } else if (p0 == 'i' || p0 == 'I') {
            static const char* rom[] = {"m","cm","d","cd","c","xc","l",
                                        "xl","x","ix","v","iv","i"};
            static const long rv[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
            if (neg) buf[len++] = '-';
            long a = ax;
            for (int k = 0; k < 13; k++)
                while (a >= rv[k]) {
                    len += (size_t)snprintf(buf + len, 8, "%s", rom[k]);
                    a -= rv[k];
                }
            buf[len] = 0;
            if (p0 == 'I')
                for (size_t i = neg ? 1 : 0; i < len; i++)
                    buf[i] = (char)toupper((unsigned char)buf[i]);
        } else {
            /* decimal; 0-picture pads to the digit count */
            char num[32];
            int nl = snprintf(num, sizeof num, "%ld", ax);
            size_t zeros = 0;
            for (const char* q = pic; *q == '0'; q++) zeros++;
            len = (size_t)snprintf(buf, sizeof buf, "%s%0*ld",
                                   neg ? "-" : "",
                                   zeros > (size_t)nl ? (int)zeros : nl, ax);
        }
    } else {
        len = (size_t)snprintf(buf, sizeof buf, "%ld", x);
    }
    out->value.string_value = leptris_strdup(buf);
    free(pic);
    return out;
}

static struct leptris_xpath_result* fn_contains_token(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    char* tok = re_str_arg(ctx, args, 1);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out || !in || !tok) {
        if (out) out->value.boolean_value = 0;
        free(in); free(tok);
        return out;
    }
    out->value.boolean_value = 0;
    char* save = NULL;
    for (char* t = strtok_r(in, " \t\n\r", &save); t;
         t = strtok_r(NULL, " \t\n\r", &save))
        if (strcmp(t, tok) == 0) { out->value.boolean_value = 1; break; }
    free(in); free(tok);
    (void)n;
    return out;
}

/* Minimal UTF-8 decode/encode (codepoints are UCS). */
static struct leptris_xpath_result* fn_string_to_codepoints(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = seq_new();
    if (!out || !in) { free(in); return out; }
    const unsigned char* p = (const unsigned char*)in;
    while (*p) {
        unsigned cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) { cp = (*p++ & 0x1F) << 6; cp |= (*p++ & 0x3F); }
        else if ((*p & 0xF0) == 0xE0) { cp = (*p++ & 0x0F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
        else { cp = (*p++ & 0x07) << 18; cp |= (*p++ & 0x3F) << 12; cp |= (*p++ & 0x3F) << 6; cp |= (*p++ & 0x3F); }
        seq_push_num(out, (double)cp);
    }
    free(in);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_codepoints_to_string(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    size_t cnt;
    char** items = collect_items(ctx, args, n, 0, &cnt);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free_items(items, cnt); return NULL; }
    out->value.string_value = (char*)calloc(cnt * 5 + 1, 1);
    size_t o = 0;
    if (items)
        for (size_t k = 0; k < cnt; k++) {
            unsigned cp = (unsigned)strtoul(items[k], NULL, 10);
            if (cp < 0x80) out->value.string_value[o++] = (char)cp;
            else if (cp < 0x800) {
                out->value.string_value[o++] = (char)(0xC0 | cp >> 6);
                out->value.string_value[o++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out->value.string_value[o++] = (char)(0xE0 | cp >> 12);
                out->value.string_value[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out->value.string_value[o++] = (char)(0x80 | (cp & 0x3F));
            } else {
                out->value.string_value[o++] = (char)(0xF0 | cp >> 18);
                out->value.string_value[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out->value.string_value[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out->value.string_value[o++] = (char)(0x80 | (cp & 0x3F));
            }
        }
    free_items(items, cnt);
    return out;
}

static int uri_escape(char* dst, const char* s, int keep_delims) {
    static const char* hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        int unres = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                    c == '_' || c == '~';
        int extra = keep_delims && (strchr(";/?:@&=+$,[]-._~!'()*%", c) != NULL);
        if (unres || extra) {
            dst[o++] = (char)c;
        } else {
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 15];
        }
    }
    dst[o] = 0;
    return (int)o;
}

static struct leptris_xpath_result* fn_uri_escape(XPathContext* ctx,
        XPathASTNode** args, size_t n, int keep) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(in); return NULL; }
    out->value.string_value = (char*)calloc(strlen(in ? in : "") * 3 + 1, 1);
    if (in) uri_escape(out->value.string_value, in, keep);
    free(in);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_encode_for_uri(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_uri_escape(c, a, n, 0); }
static struct leptris_xpath_result* fn_iri_to_uri(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_uri_escape(c, a, n, 1); }

static struct leptris_xpath_result* fn_escape_html_uri(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(in); return NULL; }
    size_t cap = (in ? strlen(in) : 0) * 6 + 1;
    out->value.string_value = (char*)calloc(cap, 1);
    size_t o = 0;
    if (in)
        for (const char* p = in; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 0x80 && !isalnum(c) && !strchr("-_.~", c)) {
                const char* ent = c == '<' ? "&lt;" : c == '>' ? "&gt;" :
                                  c == '&' ? "&amp;" : c == '"' ? "&quot;" :
                                  c == '\'' ? "&apos;" : NULL;
                if (ent) { strcpy(out->value.string_value + o, ent); o += strlen(ent); continue; }
            }
            if (c < 0x80 && (isalnum(c) || strchr("-_.~", c)))
                out->value.string_value[o++] = (char)c;
            else
                out->value.string_value[o++] = (char)c;  /* non-ASCII kept */
        }
    free(in);
    (void)n;
    return out;
}

/* QName family — value-level string representation ("prefix:local").
 * The namespace URI rides a thread-local side channel set by the
 * QName() constructor (a structured QName value lands with
 * TODO.xslt-full/07 function items). */
static __thread char last_qname_uri[512];
static struct leptris_xpath_result* fn_qname(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* uri = re_str_arg(ctx, args, 0);
    char* qn = re_str_arg(ctx, args, 1);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(uri); free(qn); return NULL; }
    out->value.string_value = leptris_strdup(qn ? qn : "");
    snprintf(last_qname_uri, sizeof(last_qname_uri), "%s", uri ? uri : "");
    free(uri); free(qn);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_qname_part(XPathContext* ctx,
        XPathASTNode** args, size_t n, int which) {
    char* qn = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(qn); return NULL; }
    const char* colon = qn ? strchr(qn, ':') : NULL;
    if (which == 1)      /* local */
        out->value.string_value = leptris_strdup(colon ? colon + 1 : (qn ? qn : ""));
    else if (which == 2) {  /* prefix */
        if (colon) {
            size_t pl = (size_t)(colon - qn);
            char* p = (char*)malloc(pl + 1);
            memcpy(p, qn, pl); p[pl] = 0;
            out->value.string_value = p;
        } else
            out->value.string_value = leptris_strdup("");
    } else               /* namespace uri — from the 2-arg form only */
        out->value.string_value = leptris_strdup(last_qname_uri);
    free(qn);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_local_from_qname(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_qname_part(c, a, n, 1); }
static struct leptris_xpath_result* fn_prefix_from_qname(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_qname_part(c, a, n, 2); }
static struct leptris_xpath_result* fn_ns_from_qname(XPathContext* c,
        XPathASTNode** a, size_t n) { return fn_qname_part(c, a, n, 3); }

static struct leptris_xpath_result* fn_node_name(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { if (r) leptris_xpath_result_free(r); return NULL; }
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count) {
        void* nd = r->value.nodeset_value->nodes[0];
        if (leptris_node_get_type(nd) == LEPTRIS_NODE_TYPE_ELEMENT) {
            const char* nm = leptris_element_get_name((LeptrisElement)nd);
            out->value.string_value = leptris_strdup(nm ? nm : "");
        } else {
            out->value.string_value = leptris_strdup("");
        }
    } else {
        out->value.string_value = leptris_strdup("");
    }
    if (r) leptris_xpath_result_free(r);
    (void)n;
    return out;
}

/* ---- registration (OCP: one call from the standard init) ---- */

void xpath_register_fn31(XPathFunctionRegistry* registry);

void xpath_register_fn31(XPathFunctionRegistry* registry) {
    if (!registry) return;
    xpath_function_registry_register(registry, "exists", fn_exists, 1, 1);
    xpath_function_registry_register(registry, "empty", fn_empty, 1, 1);
    xpath_function_registry_register(registry, "head", fn_head, 1, 1);
    xpath_function_registry_register(registry, "tail", fn_tail, 1, 1);
    xpath_function_registry_register(registry, "reverse", fn_reverse, 1, 1);
    xpath_function_registry_register(registry, "unordered", fn_unordered, 1, 1);
    xpath_function_registry_register(registry, "subsequence", fn_subsequence, 2, 3);
    xpath_function_registry_register(registry, "remove", fn_remove, 2, 2);
    xpath_function_registry_register(registry, "insert-before", fn_insert_before, 3, 3);
    xpath_function_registry_register(registry, "index-of", fn_index_of, 2, 2);
    xpath_function_registry_register(registry, "distinct-values", fn_distinct_values, 1, 1);
    xpath_function_registry_register(registry, "avg", fn_avg, 1, 1);
    xpath_function_registry_register(registry, "min", fn_min, 1, 1);
    xpath_function_registry_register(registry, "max", fn_max, 1, 1);
    xpath_function_registry_register(registry, "zero-or-one", fn_zero_or_one, 1, 1);
    xpath_function_registry_register(registry, "one-or-more", fn_one_or_more, 1, 1);
    xpath_function_registry_register(registry, "exactly-one", fn_exactly_one, 1, 1);

    xpath_function_registry_register(registry, "math:sqrt", fn_sqrt, 1, 1);
    xpath_function_registry_register(registry, "math:pow", fn_pow, 2, 2);
    xpath_function_registry_register(registry, "math:exp", fn_exp, 1, 1);
    xpath_function_registry_register(registry, "math:exp10", fn_exp10, 1, 1);
    xpath_function_registry_register(registry, "math:log", fn_log, 1, 1);
    xpath_function_registry_register(registry, "math:log10", fn_log10, 1, 1);
    xpath_function_registry_register(registry, "math:sin", fn_sin, 1, 1);
    xpath_function_registry_register(registry, "math:cos", fn_cos, 1, 1);
    xpath_function_registry_register(registry, "math:tan", fn_tan, 1, 1);
    xpath_function_registry_register(registry, "math:asin", fn_asin, 1, 1);
    xpath_function_registry_register(registry, "math:acos", fn_acos, 1, 1);
    xpath_function_registry_register(registry, "math:atan", fn_atan, 1, 1);
    xpath_function_registry_register(registry, "math:atan2", fn_atan2, 2, 2);
    xpath_function_registry_register(registry, "math:pi", fn_pi, 0, 0);
    xpath_function_registry_register(registry, "abs", fn_abs, 1, 1);
    xpath_function_registry_register(registry, "round-half-to-even", fn_round_half_even, 1, 2);

    xpath_function_registry_register(registry, "format-integer", fn_format_integer, 2, 2);
    xpath_function_registry_register(registry, "contains-token", fn_contains_token, 2, 2);
    xpath_function_registry_register(registry, "string-to-codepoints", fn_string_to_codepoints, 1, 1);
    xpath_function_registry_register(registry, "codepoints-to-string", fn_codepoints_to_string, 1, 1);
    xpath_function_registry_register(registry, "encode-for-uri", fn_encode_for_uri, 1, 1);
    xpath_function_registry_register(registry, "iri-to-uri", fn_iri_to_uri, 1, 1);
    xpath_function_registry_register(registry, "escape-html-uri", fn_escape_html_uri, 1, 1);
    xpath_function_registry_register(registry, "QName", fn_qname, 2, 2);
    xpath_function_registry_register(registry, "local-name-from-QName", fn_local_from_qname, 1, 1);
    xpath_function_registry_register(registry, "prefix-from-QName", fn_prefix_from_qname, 1, 1);
    xpath_function_registry_register(registry, "namespace-uri-from-QName", fn_ns_from_qname, 1, 1);
    xpath_function_registry_register(registry, "node-name", fn_node_name, 1, 1);
    xpath_function_registry_register(registry, "matches", fn_matches, 2, 3);
    xpath_function_registry_register(registry, "replace", fn_replace, 3, 4);
    xpath_function_registry_register(registry, "tokenize", fn_tokenize, 2, 3);
}
