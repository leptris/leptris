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
#include "../unicode/unicode.h"
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

/* fn:collection() — no default collection is defined in this
 * build: a dynamic error (Saxon FODC0002 parity), catchable. */
static struct leptris_xpath_result* fn_collection(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)args;
    (void)n;
    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
             "No default collection has been defined");
    snprintf(ctx->error_code, sizeof(ctx->error_code), "FODC0002");
    return NULL;
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
    /* Node arguments atomize to their string value (#691 comment:
     * matches(//item[1], ...) — the scalar path returned NULL for
     * nodesets). */
    char* s = (r->type == XPATH_RESULT_NODESET)
                  ? leptris_xpath_result_string(r)
                  : scalar_str(r);
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
    const char* p = in;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        const char* st = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        if ((size_t)(p - st) == strlen(tok) &&
            strncmp(st, tok, (size_t)(p - st)) == 0) {
            out->value.boolean_value = 1;
            break;
        }
    }
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
#ifdef _WIN32
#define LEPTRIS_TLS __declspec(thread)
#else
#define LEPTRIS_TLS __thread
#endif
static LEPTRIS_TLS char last_qname_uri[512];
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


/* ---- dates & durations (TODO.xslt-full/05, first slice) ----
 * Value-level: constructors validate the lexical form lightly and
 * pass it through; extractors parse ISO 8601 fields. */

#include <time.h>

/* fn:doc(uri) — parse the file, return its root as a nodeset
 * (TODO.xslt-full/11). The document anchors on the eval context
 * (XPathContext.owned_docs): the nodeset borrows the root element,
 * so the doc must outlive the result — cleanup frees the anchors. */
#include "../dom/document_node.h"
static struct leptris_xpath_result* xq_anchor_owned_doc(
    XPathContext* ctx, struct leptris_document* doc, int fragment);
static struct leptris_xpath_result* fn_doc(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    char* path = re_str_arg(ctx, args, 0);
    if (!path) return NULL;
    LeptrisDocument doc = leptris_parse_file(path, NULL);
    free(path);
    if (!doc) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "doc(): cannot load document");
        snprintf(ctx->error_code, sizeof(ctx->error_code),
                 "FODC0002");
        return NULL;
    }
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        leptris_document_free(doc);
        return NULL;
    }
    struct leptris_xpath_result* out =
        xq_anchor_owned_doc(ctx, doc, 0);
    if (!out) leptris_document_free(doc);
    return out;
}

/* Anchor a caller-parsed document on the context's owned chain and
 * return its DOCUMENT NODE (XQuery: parse-xml and
 * parse-xml-fragment yield a document node, so /root and
 * absolute paths step from the document level, #692). fragment==1 additionally SPLICES
 * the wrapper's children to the document level (root = first
 * element, doc-children = the fragment chain) so absolute
 * paths hit the fragment nodes. The nodeset borrows; cleanup frees the document
 * with the context. */
static struct leptris_xpath_result* xq_anchor_owned_doc(
    XPathContext* ctx, struct leptris_document* doc, int fragment) {
    if (fragment) {
        LeptrisElement wrap = leptris_document_root(doc);
        if (wrap) {
            LeptrisNodeRef c = leptris_node_first_child(
                leptris_element_as_node(wrap));
            LeptrisElement first_elem = NULL;
            LeptrisNodeRef last = NULL;
            while (c) {
                LeptrisNodeRef next = leptris_node_get_next_sibling(c);
                if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT &&
                    !first_elem)
                    first_elem = (LeptrisElement)c;
                if (!last) doc->doc_children_head = c;
                else leptris_node_set_next_sibling(last, c);
                last = c;
                c = next;
            }
            doc->doc_children_tail = last;
            doc->new_dom_root = first_elem;
        }
    }
    if (ctx->n_owned_docs == ctx->cap_owned_docs) {
        ctx->cap_owned_docs = ctx->cap_owned_docs
                                  ? ctx->cap_owned_docs * 2 : 4;
        ctx->owned_docs = (struct leptris_document**)realloc(
            ctx->owned_docs,
            ctx->cap_owned_docs * sizeof(struct leptris_document*));
        if (!ctx->owned_docs) return NULL;
    }
    ctx->owned_docs[ctx->n_owned_docs++] = doc;
    LeptrisNode* dn = leptris_document_get_node(doc);
    if (!dn) return NULL;
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) return NULL;
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) {
        xpath_result_free(out);
        return NULL;
    }
    xpath_nodeset_add(out->value.nodeset_value, dn);
    return out;
}

/* fn:parse-xml (arg: XML string) -> document node sequence (#692).
 * Owned-doc anchored, like doc(). */
static struct leptris_xpath_result* fn_parse_xml(
    XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)n;
    char* src = re_str_arg(ctx, args, 0);
    if (!src) return NULL;
    LeptrisDocument doc = leptris_parse_string(src, strlen(src), NULL);
    free(src);
    if (!doc) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "parse-xml(): invalid XML");
        snprintf(ctx->error_code, sizeof(ctx->error_code), "FODC0006");
        return NULL;
    }
    return xq_anchor_owned_doc(ctx, doc, 0);
}

/* fn:parse-xml-fragment: multiple top-level nodes are legal — the
 * input is wrapped, and the returned sequence is the wrapper's
 * CHILDREN (borrowed; the owned doc anchors them). */
static struct leptris_xpath_result* fn_parse_xml_fragment(
    XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)n;
    char* src = re_str_arg(ctx, args, 0);
    if (!src) return NULL;
    size_t sl = strlen(src);
    const char* wrap_open = "<leptris:frag xmlns:leptris='urn:leptris:frag'>";
    const char* wrap_close = "</leptris:frag>";
    size_t ol = strlen(wrap_open), cl = strlen(wrap_close);
    char* wrapped = (char*)malloc(sl + ol + cl + 1);
    if (!wrapped) { free(src); return NULL; }
    memcpy(wrapped, wrap_open, ol);
    memcpy(wrapped + ol, src, sl);
    free(src);
    memcpy(wrapped + ol + sl, wrap_close, cl);
    wrapped[ol + sl + cl] = 0;
    LeptrisDocument doc = leptris_parse_string(wrapped, ol + sl + cl, NULL);
    free(wrapped);
    if (!doc) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "parse-xml-fragment(): invalid XML");
        snprintf(ctx->error_code, sizeof(ctx->error_code), "FODC0006");
        return NULL;
    }
    return xq_anchor_owned_doc(ctx, doc, 1);
}

static struct leptris_xpath_result* fn_passthrough_ctor(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(in); return NULL; }
    out->value.string_value = in ? in : leptris_strdup("");
    (void)n;
    return out;
}

/* Extract an integer field from an ISO date/dateTime/time/duration
 * string. which: 1 year 2 month 3 day 4 hours 5 minutes
 * 6 seconds 7 duration-days 8 duration-hours. */
static struct leptris_xpath_result* fn_date_field(XPathContext* ctx,
        XPathASTNode** args, size_t n, int which) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!out) { free(in); return NULL; }
    long v = 0;
    if (in) {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0;
        double se = 0;
        long dd = 0, dh = 0;
        char dur_sign = 0;
        const char* t = strstr(in, "T");
        if (sscanf(in, "P%ldDT%ldH", &dd, &dh) >= 1) dur_sign = 'P';
        if (dur_sign == 'P') {
            v = (which == 7) ? dd : (which == 8) ? dh : 0;
        } else if (sscanf(in, "%d-%d-%d", &y, &mo, &d) == 3) {
            if (t) sscanf(t, "T%d:%d:%lf", &h, &mi, &se);
            else sscanf(in, "%*d-%*d-%*dT%d:%d:%lf", &h, &mi, &se);
            v = (which == 1) ? y : (which == 2) ? mo : (which == 3) ? d
              : (which == 4) ? h : (which == 5) ? mi : (which == 6)
              ? (long)se : 0;
        } else if (sscanf(in, "%d:%d:%lf", &h, &mi, &se) == 3) {
            v = (which == 4) ? h : (which == 5) ? mi
              : (which == 6) ? (long)se : 0;
        }
    }
    out->value.number_value = (double)v;
    free(in);
    (void)n;
    return out;
}

#define DATE_FIELD(NAME, WHICH)                                   \
    static struct leptris_xpath_result* fn_##NAME(                \
            XPathContext* ctx, XPathASTNode** a, size_t n) {      \
        return fn_date_field(ctx, a, n, WHICH);                   \
    }

DATE_FIELD(year_from_dt, 1)
DATE_FIELD(month_from_dt, 2)
DATE_FIELD(day_from_dt, 3)
DATE_FIELD(hours_from_t, 4)
DATE_FIELD(minutes_from_t, 5)
DATE_FIELD(seconds_from_t, 6)
DATE_FIELD(days_from_dur, 7)
DATE_FIELD(hours_from_dur, 8)


/* ---- xs: atomic constructors (TODO.xslt-full/06; Saxon-HE 12.7
 * ground truth banked /tmp/probe9/g6.xsl) ---- value-level casts:
 * the numeric family converts through number() with xs:integer
 * truncating toward zero; xs:boolean follows the XPath boolean()
 * rules; xs:string/xs:anyURI take the string value (the same shape
 * as the date passthrough constructors above). */

static struct leptris_xpath_result* fn_xs_number_ctor(XPathContext* ctx,
        XPathASTNode** args, size_t n, int integer_only) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    double d;
    if (r->type == XPATH_RESULT_STRING) {
        /* XSD lexical form (#739): leading/trailing whitespace is
         * allowed, the rest must convert whole — a silent NaN on
         * invalid input is the silent-wrong class. */
        const char* s = r->value.string_value ? r->value.string_value
                                              : "";
        while (isspace((unsigned char)*s)) s++;
        const char* e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) e--;
        size_t len = (size_t)(e - s);
        int bad = (!len || len >= 120);
        char buf[120];
        if (!bad) {
            memcpy(buf, s, len);
            buf[len] = 0;
            if (integer_only) {
                const char* p = (buf[0] == '+' || buf[0] == '-')
                                    ? buf + 1 : buf;
                bad = !*p || strspn(p, "0123456789") != strlen(p);
            } else {
                char* endp = NULL;
                d = strtod(buf, &endp);
                bad = !endp || *endp;
            }
        }
        if (bad) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Invalid lexical form for xs:%s",
                     integer_only ? "integer" : "double");
            leptris_xpath_result_free(r);
            return NULL;
        }
        if (integer_only) d = strtod(buf, NULL);
    } else {
        d = leptris_xpath_result_number(r);
        if (integer_only) d = (d < 0) ? ceil(d) : floor(d);
    }
    leptris_xpath_result_free(r);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!out) return NULL;
    out->value.number_value = d;
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_xs_integer(XPathContext* ctx,
        XPathASTNode** a, size_t n) {
    return fn_xs_number_ctor(ctx, a, n, 1);
}
static struct leptris_xpath_result* fn_xs_double(XPathContext* ctx,
        XPathASTNode** a, size_t n) {
    return fn_xs_number_ctor(ctx, a, n, 0);
}

static struct leptris_xpath_result* fn_xs_boolean(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (!r) return NULL;
    int b = 0;
    if (r->type == XPATH_RESULT_NODESET)
        b = r->value.nodeset_value && r->value.nodeset_value->count > 0;
    else if (r->type == XPATH_RESULT_STRING) {
        /* XSD lexical mapping (#739): only true/1/false/0 (with
         * optional whitespace) cast from a string. */
        const char* s = r->value.string_value ? r->value.string_value
                                              : "";
        while (isspace((unsigned char)*s)) s++;
        const char* e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) e--;
        size_t len = (size_t)(e - s);
        if ((len == 4 && strncmp(s, "true", 4) == 0) ||
            (len == 1 && *s == '1'))
            b = 1;
        else if ((len == 5 && strncmp(s, "false", 5) == 0) ||
                 (len == 1 && *s == '0'))
            b = 0;
        else {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                     "Cannot cast '%s' to xs:boolean",
                     r->value.string_value);
            leptris_xpath_result_free(r);
            return NULL;
        }
    }
    else if (r->type == XPATH_RESULT_BOOLEAN)
        b = r->value.boolean_value;
    else {
        double d = r->value.number_value;
        b = (d == d) && d != 0;   /* NaN casts to false */
    }
    leptris_xpath_result_free(r);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (out) out->value.boolean_value = b;
    (void)n;
    return out;
}

/* ---- maps (TODO.xslt-full/08, first slice) ----
 * Value-level: a map is ONE synthetic text node whose content is
 * "\x03MAP" + per entry "\x02"key"\x01"value (insertion order) —
 * the XPATH_OP_MAP_CONSTRUCTOR encoding. Accessors decode here. */

extern XPathTextNode* xpath_synth_text(const char* content, size_t len);

/* Entry list + shared encoder — the ONE representation authority
 * for value-level maps (constructor, xsl:map, and the map:*
 * combiners all go through it). */
typedef struct {
    char** k;
    char** v;
    size_t n, cap;
} MapEntries;

static void map_entries_push(MapEntries* e, const char* k, size_t kn,
                             const char* v, size_t vn) {
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->k = (char**)realloc(e->k, e->cap * sizeof(char*));
        e->v = (char**)realloc(e->v, e->cap * sizeof(char*));
        if (!e->k || !e->v) { e->n = 0; e->cap = 0; return; }
    }
    e->k[e->n] = LEPTRIS_ALLOC_N(char, kn + 1);
    e->v[e->n] = LEPTRIS_ALLOC_N(char, vn + 1);
    if (!e->k[e->n] || !e->v[e->n]) return;
    if (kn) memcpy(e->k[e->n], k, kn);
    e->k[e->n][kn] = '\0';
    if (vn) memcpy(e->v[e->n], v, vn);
    e->v[e->n][vn] = '\0';
    e->n++;
}

static void map_entries_free(MapEntries* e) {
    for (size_t i = 0; i < e->n; i++) {
        if (e->k) free(e->k[i]);
        if (e->v) free(e->v[i]);
    }
    free(e->k);
    free(e->v);
    e->k = e->v = NULL;
    e->n = e->cap = 0;
}

/* Decode ONE map node's content (after the "\x03MAP" marker). */
static void map_entries_decode(MapEntries* e, const char* p) {
    while (p && *p == '\x02') {
        const char* ke = strchr(p + 1, '\x01');
        if (!ke) break;
        const char* ve = ke + 1;
        const char* en = ve;
        while (*en && *en != '\x02') en++;
        map_entries_push(e, p + 1, (size_t)(ke - (p + 1)),
                         ve, (size_t)(en - ve));
        p = en;
    }
}

/* Evaluate argument i into entries: a single map node, or a
 * sequence of map nodes (map:merge input). */
static void map_entries_arg(XPathContext* ctx, XPathASTNode** args,
                            size_t i, MapEntries* e) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
    if (!r) return;
    if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        for (size_t i = 0; i < r->value.nodeset_value->count; i++) {
            XPathTextNode* tn =
                (XPathTextNode*)r->value.nodeset_value->nodes[i];
            if (tn && tn->content &&
                strncmp(tn->content, "\x03MAP", 4) == 0)
                map_entries_decode(e, tn->content + 4);
        }
    }
    leptris_xpath_result_free(r);
}

/* Encode entries into the map representation string ("\x03MAP" +
 * per entry "\x02"key"\x01"value). Returns malloc'd content. */
char* xpath_map_encode(const MapEntries* e) {
    size_t cap = 32, len = 4;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    memcpy(buf, "\x03MAP", 4);
    for (size_t i = 0; i < e->n; i++) {
        size_t kn = strlen(e->k[i]), vn = strlen(e->v[i]);
        while (len + kn + vn + 3 > cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = '\x02';
        if (kn) { memcpy(buf + len, e->k[i], kn); len += kn; }
        buf[len++] = '\x01';
        if (vn) { memcpy(buf + len, e->v[i], vn); len += vn; }
    }
    buf[len] = '\0';
    return buf;
}

/* Encode entries into the map VALUE (nodeset of one synthetic
 * text node) — the shape map:* results and xsl:map produce. */
struct leptris_xpath_result* xpath_map_value(const MapEntries* e) {
    char* content = xpath_map_encode(e);
    if (!content) return NULL;
    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) { free(content); return NULL; }
    out->owns_synthetic_text = 1;
    out->is_sequence = 1;
    XPathTextNode* tn = xpath_synth_text(content, strlen(content));
    free(content);
    if (!tn) { xpath_nodeset_free(out); return NULL; }
    xpath_nodeset_add(out, tn);
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) { xpath_nodeset_free(out); return NULL; }
    r->value.nodeset_value = out;
    return r;
}

/* Incremental builder over the shared representation — the XSLT
 * exec's xsl:map seam (entries arrive one instruction at a time). */
void* xpath_map_builder_new(void) {
    MapEntries* e = (MapEntries*)calloc(1, sizeof(*e));
    return e;
}

void xpath_map_builder_add(void* b, const char* k, const char* v) {
    MapEntries* e = (MapEntries*)b;
    if (!e || !k) return;
    map_entries_push(e, k, strlen(k), v ? v : "", v ? strlen(v) : 0);
}

struct leptris_xpath_result* xpath_map_builder_finish(void* b) {
    MapEntries* e = (MapEntries*)b;
    if (!e) return NULL;
    struct leptris_xpath_result* r = xpath_map_value(e);
    map_entries_free(e);
    free(e);
    return r;
}

static struct leptris_xpath_result* fn_map_get(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* key = re_str_arg(ctx, args, 1);
    char* val = NULL;
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (r) {
        if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
            r->value.nodeset_value->count > 0) {
            XPathTextNode* tn =
                (XPathTextNode*)r->value.nodeset_value->nodes[0];
            if (tn && tn->content &&
                strncmp(tn->content, "\x03MAP", 4) == 0) {
                const char* p = tn->content + 4;
                while (*p == '\x02') {
                    const char* ke = strchr(p + 1, '\x01');
                    if (!ke) break;
                    size_t klen = (size_t)(ke - (p + 1));
                    const char* ve = ke + 1;
                    const char* en = ve;
                    while (*en && *en != '\x02') en++;
                    if (key && klen == strlen(key) &&
                        memcmp(p + 1, key, klen) == 0) {
                        val = LEPTRIS_ALLOC_N(char,
                                              (size_t)(en - ve) + 1);
                        if (val) {
                            memcpy(val, ve, (size_t)(en - ve));
                            val[en - ve] = '\0';
                        }
                        break;
                    }
                    p = en;
                }
            }
        }
        leptris_xpath_result_free(r);
    }
    free(key);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (out) out->value.string_value = val ? val : leptris_strdup("");
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_map_size(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    size_t count = 0;
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count > 0) {
        XPathTextNode* tn = (XPathTextNode*)r->value.nodeset_value->nodes[0];
        if (tn && tn->content && strncmp(tn->content, "\x03MAP", 4) == 0) {
            const char* p = tn->content + 4;
            while (*p == '\x02') {
                count++;
                p++;
                const char* ke = strchr(p, '\x01');
                if (!ke) break;
                p = ke + 1;
                while (*p && *p != '\x02') p++;
            }
        }
    }
    if (r) leptris_xpath_result_free(r);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NUMBER);
    if (out) out->value.number_value = (double)count;
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_map_keys(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) return NULL;
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) return out;
    out->value.nodeset_value->owns_synthetic_text = 1;
    out->value.nodeset_value->is_sequence = 1;
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count > 0) {
        XPathTextNode* tn = (XPathTextNode*)r->value.nodeset_value->nodes[0];
        if (tn && tn->content && strncmp(tn->content, "\x03MAP", 4) == 0) {
            const char* p = tn->content + 4;
            while (*p == '\x02') {
                const char* ke = strchr(p + 1, '\x01');
                if (!ke) break;
                XPathTextNode* kn =
                    xpath_synth_text(p + 1, (size_t)(ke - (p + 1)));
                if (kn) xpath_nodeset_add(out->value.nodeset_value, kn);
                const char* en = ke + 1;
                while (*en && *en != '\x02') en++;
                p = en;
            }
        }
    }
    if (r) leptris_xpath_result_free(r);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_map_contains(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* got = fn_map_get(ctx, args, n);
    int found = got && got->type == XPATH_RESULT_STRING &&
                got->value.string_value &&
                got->value.string_value[0] != '\0';
    if (got) leptris_xpath_result_free(got);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (out) out->value.boolean_value = found;
    return out;
}

static struct leptris_xpath_result* fn_map_put(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* key = re_str_arg(ctx, args, 1);
    char* val = (n >= 3) ? re_str_arg(ctx, args, 2) : leptris_strdup("");
    MapEntries e = {0};
    map_entries_arg(ctx, args, 0, &e);
    int replaced = 0;
    for (size_t i = 0; i < e.n; i++) {
        if (key && strcmp(e.k[i], key) == 0) {
            free(e.v[i]);
            e.v[i] = val ? val : leptris_strdup("");
            val = NULL;   /* ownership moved */
            replaced = 1;
            break;
        }
    }
    if (!replaced && key)
        map_entries_push(&e, key, strlen(key),
                         val ? val : "", val ? strlen(val) : 0);
    free(val);
    free(key);
    struct leptris_xpath_result* out = xpath_map_value(&e);
    map_entries_free(&e);
    return out;
}

static struct leptris_xpath_result* fn_map_remove(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* key = re_str_arg(ctx, args, 1);
    MapEntries src = {0};
    map_entries_arg(ctx, args, 0, &src);
    MapEntries dst;
    memset(&dst, 0, sizeof(dst));
    for (size_t i = 0; i < src.n; i++) {
        if (key && strcmp(src.k[i], key) == 0) continue;
        map_entries_push(&dst, src.k[i], strlen(src.k[i]),
                         src.v[i], strlen(src.v[i]));
    }
    free(key);
    struct leptris_xpath_result* out = xpath_map_value(&dst);
    map_entries_free(&src);
    map_entries_free(&dst);
    (void)n;
    return out;
}

/* map:merge: sequence of maps; on duplicate keys the LAST wins. */
static struct leptris_xpath_result* fn_map_merge(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    MapEntries e = {0};
    map_entries_arg(ctx, args, 0, &e);
    MapEntries dst;
    memset(&dst, 0, sizeof(dst));
    for (size_t i = 0; i < e.n; i++) {
        int last = 1;
        for (size_t j = i + 1; j < e.n; j++)
            if (strcmp(e.k[i], e.k[j]) == 0) { last = 0; break; }
        if (last)
            map_entries_push(&dst, e.k[i], strlen(e.k[i]),
                             e.v[i], strlen(e.v[i]));
    }
    map_entries_free(&e);
    struct leptris_xpath_result* out = xpath_map_value(&dst);
    map_entries_free(&dst);
    (void)n;
    return out;
}

/* ---- arrays (TODO.xslt-full/08C) ----
 * Value-level arrays ride the map representation with positional
 * keys "1".."n": every accessor is the map operation with a
 * formatted index — one representation, two vocabularies. */

/* Shared lookup core extracted from fn_map_get: evaluates args[0],
 * returns the entry value for `key` (malloc'd "" when absent). */
char* xpath_map_lookup_result(struct leptris_xpath_result* r,
                              const char* key) {
    char* val = NULL;
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count > 0) {
        XPathTextNode* tn =
            (XPathTextNode*)r->value.nodeset_value->nodes[0];
        if (tn && tn->content &&
            strncmp(tn->content, "\x03MAP", 4) == 0) {
            const char* p = tn->content + 4;
            while (*p == '\x02') {
                const char* ke = strchr(p + 1, '\x01');
                if (!ke) break;
                const char* ve = ke + 1;
                const char* en = ve;
                while (*en && *en != '\x02') en++;
                if (key && (size_t)(ke - (p + 1)) == strlen(key) &&
                    memcmp(p + 1, key, strlen(key)) == 0) {
                    val = LEPTRIS_ALLOC_N(char, (size_t)(en - ve) + 1);
                    if (val) {
                        memcpy(val, ve, (size_t)(en - ve));
                        val[en - ve] = '\0';
                    }
                    break;
                }
                p = en;
            }
        }
    }
    return val;
}

static char* map_lookup_core(XPathContext* ctx, XPathASTNode** args,
                             const char* key) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    char* val = xpath_map_lookup_result(r, key);
    if (r) leptris_xpath_result_free(r);
    return val;
}

static struct leptris_xpath_result* fn_array_get(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    long idx = 1;
    struct leptris_xpath_result* ir = xpath_evaluate(ctx, args[1]);
    if (ir) {
        idx = (long)leptris_xpath_result_number(ir);
        leptris_xpath_result_free(ir);
    }
    char key[24];
    snprintf(key, sizeof(key), "%ld", idx);
    char* val = map_lookup_core(ctx, args, key);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (out) out->value.string_value = val ? val : leptris_strdup("");
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_array_append(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    MapEntries e = {0};
    map_entries_arg(ctx, args, 0, &e);
    char key[24];
    snprintf(key, sizeof(key), "%zu", e.n + 1);
    char* v = re_str_arg(ctx, args, 1);
    map_entries_push(&e, key, strlen(key), v ? v : "",
                     v ? strlen(v) : 0);
    free(v);
    struct leptris_xpath_result* out = xpath_map_value(&e);
    map_entries_free(&e);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_array_put(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    long idx = 1;
    struct leptris_xpath_result* ir = xpath_evaluate(ctx, args[1]);
    if (ir) {
        idx = (long)leptris_xpath_result_number(ir);
        leptris_xpath_result_free(ir);
    }
    char key[24];
    snprintf(key, sizeof(key), "%ld", idx);
    MapEntries e = {0};
    map_entries_arg(ctx, args, 0, &e);
    char* v = re_str_arg(ctx, args, 2);
    int replaced = 0;
    for (size_t i = 0; i < e.n; i++) {
        if (strcmp(e.k[i], key) == 0) {
            free(e.v[i]);
            e.v[i] = v ? v : leptris_strdup("");
            v = NULL;
            replaced = 1;
            break;
        }
    }
    if (!replaced)
        map_entries_push(&e, key, strlen(key), v ? v : "",
                         v ? strlen(v) : 0);
    free(v);
    struct leptris_xpath_result* out = xpath_map_value(&e);
    map_entries_free(&e);
    (void)n;
    return out;
}

/* ---- parse-json (TODO.xslt-full/08D) ----
 * Compact recursive-descent JSON parser producing the shared
 * map/array representation: objects → keyed entries, arrays →
 * positional keys, scalars → their string form (value-level). */

typedef struct { const char* p; int ok; } JsonScan;

static void json_ws(JsonScan* s) {
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')
        s->p++;
}

/* Parse a JSON string literal into `out` (malloc'd, escapes
 * resolved for the common set). */
static int json_string(JsonScan* s, char** out) {
    *out = NULL;
    if (*s->p != '"') { s->ok = 0; return 0; }
    s->p++;
    size_t cap = 16, len = 0;
    char* b = (char*)malloc(cap);
    if (!b) { s->ok = 0; return 0; }
    while (*s->p && *s->p != '"') {
        char c = *s->p++;
        if (c == '\\' && *s->p) {
            char e = *s->p++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == 'b') c = '\b';
            else if (e == 'f') c = '\f';
            else c = e;   /* \\ " / uXXXX (kept verbatim v1) */
        }
        if (len + 2 > cap) {
            cap *= 2;
            char* nb = (char*)realloc(b, cap);
            if (!nb) { free(b); s->ok = 0; return 0; }
            b = nb;
        }
        b[len++] = c;
    }
    if (*s->p != '"') { free(b); s->ok = 0; return 0; }
    s->p++;
    b[len] = '\0';
    *out = b;
    return 1;
}

/* Member value under `key`: scalars land directly; nested
 * containers are encoded as value strings (flat maps/arrays are
 * exact; nested-container RE-building is the documented v1 limit). */
static void json_value(JsonScan* s, void* b, const char* key);
static struct leptris_xpath_result* json_root(JsonScan* s);

static void json_value(JsonScan* s, void* b, const char* key) {
    json_ws(s);
    if (*s->p == '{' || *s->p == '[') {
        struct leptris_xpath_result* sub = json_root(s);
        if (sub) {
            char* sv = leptris_xpath_result_string(sub);
            if (b && key) xpath_map_builder_add(b, key, sv ? sv : "");
            free(sv);
            leptris_xpath_result_free(sub);
            return;
        }
        s->ok = 0;
        return;
    }
    if (*s->p == '"') {
        char* v = NULL;
        if (json_string(s, &v)) {
            if (b && key) xpath_map_builder_add(b, key, v);
            free(v);
            return;
        }
        s->ok = 0;
        return;
    }
    const char* st = s->p;
    while (*s->p && *s->p != ',' && *s->p != '}' && *s->p != ']' &&
           *s->p != ' ' && *s->p != '\n')
        s->p++;
    if (s->p == st) { s->ok = 0; return; }
    size_t n = (size_t)(s->p - st);
    char* v = LEPTRIS_ALLOC_N(char, n + 1);
    if (v) {
        memcpy(v, st, n);
        v[n] = '\0';
        if (b && key) xpath_map_builder_add(b, key, v);
        free(v);
    }
}

/* Root value: a container's members parse DIRECTLY into the result
 * (never wrapped under a key — wrapping embedded a whole encoding
 * inside another, and the size/get delimiter scans then counted the
 * nested entries too). */
static struct leptris_xpath_result* json_root(JsonScan* s) {
    json_ws(s);
    if (*s->p == '{' || *s->p == '[') {
        int obj = (*s->p == '{');
        s->p++;
        void* b = xpath_map_builder_new();
        if (!b) { s->ok = 0; return NULL; }
        size_t idx = 1;
        json_ws(s);
        if ((obj && *s->p != '}') || (!obj && *s->p != ']')) {
            for (;;) {
                json_ws(s);
                char k[32];
                if (obj) {
                    char* ks = NULL;
                    if (!json_string(s, &ks)) break;
                    snprintf(k, sizeof(k), "%s", ks);
                    free(ks);
                    json_ws(s);
                    if (*s->p == ':') s->p++;
                } else {
                    snprintf(k, sizeof(k), "%zu", idx++);
                }
                json_value(s, b, k);
                json_ws(s);
                if (*s->p == ',') { s->p++; continue; }
                break;
            }
        }
        if ((obj && *s->p == '}') || (!obj && *s->p == ']')) s->p++;
        else s->ok = 0;
        return xpath_map_builder_finish(b);
    }
    if (*s->p == '"') {
        char* v = NULL;
        struct leptris_xpath_result* out = NULL;
        if (json_string(s, &v)) {
            out = xpath_result_new(XPATH_RESULT_STRING);
            if (out) out->value.string_value = v;
            else free(v);
        }
        return out;
    }
    const char* st = s->p;
    while (*s->p && *s->p != ' ' && *s->p != '\n' && *s->p != '}' &&
           *s->p != ']' && *s->p != ',')
        s->p++;
    if (s->p == st) { s->ok = 0; return NULL; }
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (out) {
        out->value.string_value = LEPTRIS_ALLOC_N(
            char, (size_t)(s->p - st) + 1);
        if (out->value.string_value) {
            memcpy(out->value.string_value, st, (size_t)(s->p - st));
            out->value.string_value[s->p - st] = '\0';
        }
    }
    return out;
}

static struct leptris_xpath_result* fn_parse_json(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = NULL;
    if (in) {
        JsonScan s = { in, 1 };
        out = json_root(&s);
        free(in);
    }
    if (!out) {
        out = xpath_result_new(XPATH_RESULT_NODESET);
        if (out) out->value.nodeset_value = xpath_nodeset_new();
    }
    (void)n;
    return out;
}

/* json-to-xml (08 final slice): canonical fn: vocabulary. Values
 * classify by lexical form; the XML is built as text and parsed
 * into a scratch doc; the ROOT ELEMENT rides the nodeset. */
static void xml_esc(char** buf, size_t* len, size_t* cap,
                    const char* s) {
    for (const char* p = s ? s : ""; *p; p++) {
        const char* rep = NULL;
        if (*p == '&') rep = "&amp;";
        else if (*p == '<') rep = "&lt;";
        else if (*p == '>') rep = "&gt;";
        else if (*p == '"') rep = "&quot;";
        size_t need = rep ? strlen(rep) : 1;
        while (*len + need + 1 > *cap) {
            *cap *= 2;
            char* nb = (char*)realloc(*buf, *cap);
            if (!nb) return;
            *buf = nb;
        }
        if (rep) { memcpy(*buf + *len, rep, need); *len += need; }
        else (*buf)[(*len)++] = *p;
    }
    (*buf)[*len] = '\0';
}

static void xml_add(char** buf, size_t* len, size_t* cap,
                    const char* s) {
    size_t need = strlen(s);
    while (*len + need + 1 > *cap) {
        *cap *= 2;
        char* nb = (char*)realloc(*buf, *cap);
        if (!nb) return;
        *buf = nb;
    }
    memcpy(*buf + *len, s, need);
    *len += need;
    (*buf)[*len] = '\0';
}

static const char* jtype_of(const char* v) {
    if (!v || !*v) return "string";
    if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0)
        return "boolean";
    if (strcmp(v, "null") == 0) return "null";
    char* end = NULL;
    strtod(v, &end);
    return (end && *end == '\0') ? "number" : "string";
}

static struct leptris_xpath_result* fn_json_to_xml(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) { free(in); return NULL; }
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) { free(in); return out; }
    if (!in) return out;
    JsonScan s = { in, 1 };
    struct leptris_xpath_result* root = json_root(&s);
    free(in);
    if (!root) return out;
    size_t cap = 256, len = 0;
    char* xml = (char*)malloc(cap);
    if (!xml) { leptris_xpath_result_free(root); return out; }
    xml[0] = '\0';
    if (root->type == XPATH_RESULT_STRING &&
        root->value.string_value) {
        const char* ty = jtype_of(root->value.string_value);
        xml_add(&xml, &len, &cap, "<string>");
        xml_esc(&xml, &len, &cap, root->value.string_value);
        xml_add(&xml, &len, &cap, "</string>");
        (void)ty;
    } else if (root->type == XPATH_RESULT_NODESET &&
               root->value.nodeset_value &&
               root->value.nodeset_value->count > 0) {
        XPathTextNode* tn =
            (XPathTextNode*)root->value.nodeset_value->nodes[0];
        MapEntries e = {0};
        if (tn && tn->content &&
            strncmp(tn->content, "\x03MAP", 4) == 0)
            map_entries_decode(&e, tn->content + 4);
        xml_add(&xml, &len, &cap,
                "<map xmlns=\"http://www.w3.org/2005/xpath-functions\">");
        for (size_t i = 0; i < e.n; i++) {
            const char* tag = jtype_of(e.v[i]);
            xml_add(&xml, &len, &cap, "<");
            xml_add(&xml, &len, &cap, tag);
            xml_add(&xml, &len, &cap, " key=\"");
            xml_esc(&xml, &len, &cap, e.k[i]);
            xml_add(&xml, &len, &cap, "\">");
            xml_esc(&xml, &len, &cap, e.v[i]);
            xml_add(&xml, &len, &cap, "</");
            xml_add(&xml, &len, &cap, tag);
            xml_add(&xml, &len, &cap, ">");
        }
        xml_add(&xml, &len, &cap, "</map>");
        map_entries_free(&e);
    }
    leptris_xpath_result_free(root);
    LeptrisDocument doc = leptris_parse_string(xml, len, NULL);
    free(xml);
    if (doc) {
        LeptrisElement rel = leptris_document_root(doc);
        if (rel) xpath_nodeset_add(out->value.nodeset_value,
                                   (LeptrisNodeRef)rel);
    }
    (void)n;
    return out;
}

/* ---- registration (OCP: one call from the standard init) ---- */

void xpath_register_fn31(XPathFunctionRegistry* registry);

/* xml-to-json (08): walk the canonical fn: vocabulary back into
 * the shared map representation — members add under @key; nested
 * map/array elements recurse; scalars take the element text. */
static void fnxml_walk(LeptrisElement el, void* b);

static void fnxml_children(LeptrisElement el, void* b) {
    for (LeptrisElement c = leptris_element_first_child_any(el); c;
         c = (LeptrisElement)leptris_element_next_sibling_any(c)) {
        if (leptris_node_get_type((LeptrisNodeRef)c) ==
            LEPTRIS_NODE_TYPE_ELEMENT)
            fnxml_walk(c, b);
    }
}

static void fnxml_walk(LeptrisElement el, void* b) {
    const char* name = leptris_element_name(el);
    if (!name) return;
    const char* key = leptris_element_attribute(el, "key");
    if (strcmp(name, "map") == 0 || strcmp(name, "array") == 0) {
        void* sub = xpath_map_builder_new();
        fnxml_children(el, sub);
        struct leptris_xpath_result* r = xpath_map_builder_finish(sub);
        if (r) {
            char* sv = leptris_xpath_result_string(r);
            if (b && key) xpath_map_builder_add(b, key, sv ? sv : "");
            free(sv);
            leptris_xpath_result_free(r);
        }
        return;
    }
    const char* t = leptris_element_text(el);
    if (b && key) xpath_map_builder_add(b, key, t ? t : "");
}

static struct leptris_xpath_result* fn_xml_to_json(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) { if (r) leptris_xpath_result_free(r); return NULL; }
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) {
        if (r) leptris_xpath_result_free(r);
        return out;
    }
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        for (size_t i = 0; i < r->value.nodeset_value->count; i++) {
            LeptrisNodeRef nd = r->value.nodeset_value->nodes[i];
            if (!nd || leptris_node_get_type(nd) !=
                           LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            void* b = xpath_map_builder_new();
            fnxml_children((LeptrisElement)nd, b);
            struct leptris_xpath_result* m = xpath_map_builder_finish(b);
            if (m && m->value.nodeset_value &&
                m->value.nodeset_value->count > 0) {
                /* Ownership transfer: m's nodeset OWNS the synthetic
                 * map node — moving the pointer to `out` without
                 * disowning left a dangling free (map:get read a
                 * freed node and found nothing). */
                xpath_nodeset_add(out->value.nodeset_value,
                                  m->value.nodeset_value->nodes[0]);
                m->value.nodeset_value->count = 0;
                m->value.nodeset_value->owns_synthetic_text = 0;
                out->value.nodeset_value->owns_synthetic_text = 1;
                out->value.nodeset_value->is_sequence = 1;
            }
            if (m) leptris_xpath_result_free(m);
        }
    }
    if (r) leptris_xpath_result_free(r);
    (void)n;
    return out;
}

/* fn:serialize (08 final): method json over the shared map
 * representation — keys double-quoted, values escaped when their
 * lexical form is not number/boolean/null. Other methods are v1
 * string() of the item (the XML method is document-level). */
static void json_out(char** buf, size_t* len, size_t* cap,
                     const char* s) {
    for (const char* p = s ? s : ""; *p; p++) {
        const char* rep = NULL;
        if ((unsigned char)*p == '"') rep = "\\\"";
        else if (*p == '\\') rep = "\\\\";
        else if (*p == '\n') rep = "\\n";
        else if (*p == '\t') rep = "\\t";
        else if (*p == '\r') rep = "\\r";
        size_t need = rep ? strlen(rep) : 1;
        while (*len + need + 1 > *cap) {
            *cap *= 2;
            char* nb = (char*)realloc(*buf, *cap);
            if (!nb) return;
            *buf = nb;
        }
        if (rep) { memcpy(*buf + *len, rep, need); *len += need; }
        else (*buf)[(*len)++] = *p;
    }
}

static struct leptris_xpath_result* fn_serialize(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* item = xpath_evaluate(ctx, args[0]);
    if (!item) return NULL;
    int json = (n >= 2);
    char* method = NULL;
    if (n >= 2) {
        char* key = leptris_strdup("method");
        struct leptris_xpath_result* opt =
            xpath_evaluate(ctx, args[1]);
        char* m = xpath_map_lookup_result(opt, key);
        free(key);
        if (opt) leptris_xpath_result_free(opt);
        json = m && strcmp(m, "json") == 0;
        free(m);
        (void)method;
    }
    if (!json) {
        /* v1: non-json methods take the string value. */
        char* sv = leptris_xpath_result_string(item);
        leptris_xpath_result_free(item);
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (out) out->value.string_value = sv ? sv : leptris_strdup("");
        return out;
    }
    MapEntries e = {0};
    if (item->type == XPATH_RESULT_NODESET && item->value.nodeset_value &&
        item->value.nodeset_value->count > 0) {
        XPathTextNode* tn =
            (XPathTextNode*)item->value.nodeset_value->nodes[0];
        if (tn && tn->content &&
            strncmp(tn->content, "\x03MAP", 4) == 0)
            map_entries_decode(&e, tn->content + 4);
    }
    leptris_xpath_result_free(item);
    size_t cap = 64, len = 1;
    char* buf = (char*)malloc(cap);
    if (!buf) { map_entries_free(&e); return NULL; }
    buf[0] = '{';
    for (size_t i = 0; i < e.n; i++) {
        if (i) {
            while (len + 2 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            buf[len++] = ',';
        }
        while (len + 4 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[len++] = '"';
        json_out(&buf, &len, &cap, e.k[i]);
        while (len + 4 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[len++] = '"';
        buf[len++] = ':';
        const char* ty = jtype_of(e.v[i]);
        if (strcmp(ty, "string") == 0) {
            while (len + 2 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            buf[len++] = '"';
            json_out(&buf, &len, &cap, e.v[i]);
            while (len + 2 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            buf[len++] = '"';
        } else {
            json_out(&buf, &len, &cap, e.v[i]);
        }
    }
    while (len + 2 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
    buf[len++] = '}';
    buf[len] = '\0';
    map_entries_free(&e);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(buf); return NULL; }
    out->value.string_value = buf;
    return out;
}

/* ---- function items: metadata + HOF combiners (TODO 07B) ---- */

/* Shared call seam (evaluator_operators.c): dispatch a closure
 * content string with string arguments. */
extern struct leptris_xpath_result* xpath_call_function_item(
    XPathContext* ctx, const char* cc, char** argv, size_t argc);

/* Closure content of a function-item argument (first sequence
 * member's text), or NULL. Caller frees. */
static char* fn_item_content(XPathContext* ctx, XPathASTNode** args,
                             size_t i) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
    if (!r) return NULL;
    char* out = NULL;
    if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        r->value.nodeset_value->count > 0) {
        out = get_node_text(r->value.nodeset_value->nodes[0]);
    }
    xpath_result_free(r);
    return out;
}

static int fn_result_truthy(const struct leptris_xpath_result* r) {
    if (!r) return 0;
    switch (r->type) {
        case XPATH_RESULT_BOOLEAN:
            return r->value.boolean_value;
        case XPATH_RESULT_NUMBER:
            return r->value.number_value != 0.0 &&
                   !isnan(r->value.number_value);
        case XPATH_RESULT_STRING:
            return r->value.string_value && r->value.string_value[0];
        case XPATH_RESULT_NODESET:
            return r->value.nodeset_value &&
                   r->value.nodeset_value->count > 0;
        default:
            return 0;
    }
}

/* fn:function-lookup(name, arity): the named function as an item,
 * or the empty sequence. The name may carry an fn: prefix (the
 * XPath 3.x argument is a QName in the fn namespace). */
static struct leptris_xpath_result* fn_function_lookup(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)n;
    struct leptris_xpath_result* nr = xpath_evaluate(ctx, args[0]);
    if (!nr) return NULL;
    char* name = scalar_str(nr);
    xpath_result_free(nr);
    if (!name) return NULL;

    double arity = 0;
    struct leptris_xpath_result* ar = xpath_evaluate(ctx, args[1]);
    if (ar) {
        if (ar->type == XPATH_RESULT_NUMBER) {
            arity = ar->value.number_value;
        } else {
            char* s = scalar_str(ar);
            if (s) { arity = atof(s); free(s); }
        }
        xpath_result_free(ar);
    }

    const char* bare = name;
    if (strncmp(bare, "fn:", 3) == 0) bare += 3;

    struct leptris_xpath_result* out = seq_new();
    if (out) {
        XPathFunctionDef* def = ctx->function_registry
            ? xpath_function_registry_get(ctx->function_registry, bare)
            : NULL;
        if (def && arity >= (double)def->min_args &&
            (def->max_args < 0 || arity <= (double)def->max_args)) {
            char ref[192];
            snprintf(ref, sizeof(ref), "\x03" "FR%s#%ld", bare,
                     arity < 0 ? 0 : (long)arity);
            seq_push_str(out, ref);
        }
    }
    free(name);
    return out;
}

/* fn:function-name($f): fn:local-name for named references; the
 * empty sequence for anonymous closures. */
static struct leptris_xpath_result* fn_function_name(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)n;
    char* cc = fn_item_content(ctx, args, 0);
    struct leptris_xpath_result* out;
    if (cc && strncmp(cc, "\x03" "FR", 3) == 0) {
        const char* nm = cc + 3;
        const char* hash = strchr(nm, '#');
        size_t len = hash ? (size_t)(hash - nm) : strlen(nm);
        out = xpath_result_new(XPATH_RESULT_STRING);
        if (out) {
            char* buf = LEPTRIS_ALLOC_N(char, len + 4);
            if (buf) {
                memcpy(buf, "fn:", 3);
                memcpy(buf + 3, nm, len);
                buf[3 + len] = 0;
                out->value.string_value = buf;
            }
        }
    } else {
        out = seq_new();
    }
    free(cc);
    return out;
}

/* fn:function-arity($f): #N for named references; the closure's
 * parameter count for inline functions. */
static struct leptris_xpath_result* fn_function_arity(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)n;
    char* cc = fn_item_content(ctx, args, 0);
    if (!cc) return NULL;
    double arity = 0;
    if (strncmp(cc, "\x03" "FR", 3) == 0) {
        const char* hash = strchr(cc + 3, '#');
        arity = hash ? atof(hash + 1) : 0;
    } else if (strncmp(cc, "\x03" "FN", 3) == 0) {
        const char* p = cc + 4;
        const char* pe = strchr(p, '\x02');
        if (pe && pe > p) {
            arity = 1;
            for (const char* q = p; q < pe; q++)
                if (*q == '\x01') arity++;
        }
    }
    free(cc);
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_NUMBER);
    if (out) out->value.number_value = arity;
    return out;
}

/* fn:for-each(sequence, $f) */
static struct leptris_xpath_result* fn_for_each(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = collect_items(ctx, args, 1, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 1);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[1] = { items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 1);
            if (r) {
                char* s = xpath_to_string(r);
                seq_push_str(out, s ? s : "");
                free(s);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    free_items(items, cnt);
    return out;
}

/* fn:filter(sequence, $f) — members whose mapped result is truthy */
static struct leptris_xpath_result* fn_filter(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = collect_items(ctx, args, 1, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 1);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[1] = { items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 1);
            if (r) {
                if (fn_result_truthy(r)) seq_push_str(out, items[k]);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    free_items(items, cnt);
    return out;
}

/* fn:fold-left(sequence, zero, $f) — f(acc, item) left to right */
static struct leptris_xpath_result* fn_fold_left(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = collect_items(ctx, args, 1, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 2);
    struct leptris_xpath_result* zr = xpath_evaluate(ctx, args[1]);
    char* acc = zr ? xpath_to_string(zr) : NULL;
    if (zr) xpath_result_free(zr);
    if (!acc) acc = leptris_strdup("");
    if (cc && acc) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[2] = { acc, items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            char* ns = r ? xpath_to_string(r) : NULL;
            if (r) xpath_result_free(r);
            free(acc);
            acc = ns ? ns : leptris_strdup("");
        }
    }
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(acc); acc = NULL; }
    if (out) out->value.string_value = acc;
    else free(acc);
    free(cc);
    free_items(items, cnt);
    return out;
}

/* fn:fold-right(sequence, zero, $f) — f(item, acc) right to left */
static struct leptris_xpath_result* fn_fold_right(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = collect_items(ctx, args, 1, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 2);
    struct leptris_xpath_result* zr = xpath_evaluate(ctx, args[1]);
    char* acc = zr ? xpath_to_string(zr) : NULL;
    if (zr) xpath_result_free(zr);
    if (!acc) acc = leptris_strdup("");
    if (cc && acc) {
        for (size_t k = cnt; k > 0; k--) {
            char* argv[2] = { items[k - 1], acc };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            char* ns = r ? xpath_to_string(r) : NULL;
            if (r) xpath_result_free(r);
            free(acc);
            acc = ns ? ns : leptris_strdup("");
        }
    }
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (out) out->value.string_value = acc;
    else free(acc);
    free(cc);
    free_items(items, cnt);
    return out;
}

/* fn:for-each-pair(seq1, seq2, $f) — zip; the shorter input wins */
static struct leptris_xpath_result* fn_for_each_pair(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t c1, c2;
    char** xs = collect_items(ctx, args, 1, 0, &c1);
    if (!xs) return NULL;
    char** ys = collect_items(ctx, args, 1, 1, &c2);
    char* cc = fn_item_content(ctx, args, 2);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        size_t cnt = c1 < c2 ? c1 : c2;
        for (size_t k = 0; k < cnt; k++) {
            char* argv[2] = { xs[k], ys[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            if (r) {
                char* s = xpath_to_string(r);
                seq_push_str(out, s ? s : "");
                free(s);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    free_items(xs, c1);
    free_items(ys, c2);
    return out;
}

/* fn:apply($f, array) — call with the array's members as the
 * argument list. */
static struct leptris_xpath_result* fn_apply(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    char* cc = fn_item_content(ctx, args, 0);
    if (!cc) return NULL;
    MapEntries e = {0};
    map_entries_arg(ctx, args, 1, &e);
    struct leptris_xpath_result* out = NULL;
    if (e.n) {
        char** argv = (char**)calloc(e.n, sizeof(char*));
        if (argv) {
            size_t argc = 0;
            for (size_t i = 0; i < e.n; i++) {
                long idx = strtol(e.k[i], NULL, 10);
                if (idx >= 1 && (size_t)idx <= e.n)
                    argv[idx - 1] = e.v[i];
            }
            for (size_t i = 0; i < e.n; i++)
                if (argv[i]) argv[argc++] = argv[i];
            out = xpath_call_function_item(ctx, cc, argv, argc);
            free(argv);
        }
    }
    map_entries_free(&e);
    free(cc);
    return out;
}

/* map:for-each(map, $f) — f(key, value) per entry, in entry order */
static struct leptris_xpath_result* fn_map_for_each(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    MapEntries e = {0};
    map_entries_arg(ctx, args, 0, &e);
    char* cc = fn_item_content(ctx, args, 1);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        for (size_t i = 0; i < e.n; i++) {
            char* argv[2] = { e.k[i], e.v[i] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            if (r) {
                char* s = xpath_to_string(r);
                seq_push_str(out, s ? s : "");
                free(s);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    map_entries_free(&e);
    return out;
}

/* Array members in positional order: entries keyed 1..n. */
static char** array_members_arg(XPathContext* ctx, XPathASTNode** args,
                                size_t i, size_t* out_n) {
    *out_n = 0;
    MapEntries e = {0};
    map_entries_arg(ctx, args, i, &e);
    if (!e.n) { map_entries_free(&e); return NULL; }
    char** members = (char**)calloc(e.n, sizeof(char*));
    if (!members) { map_entries_free(&e); return NULL; }
    for (size_t j = 0; j < e.n; j++) {
        long idx = strtol(e.k[j], NULL, 10);
        if (idx >= 1 && (size_t)idx <= e.n && !members[idx - 1])
            members[idx - 1] = leptris_strdup(e.v[j]);
    }
    /* map_entries_free zeroes e.n — capture it first. */
    size_t n_members = e.n;
    map_entries_free(&e);
    size_t cnt = 0;
    for (size_t j = 0; j < n_members; j++)
        if (members[j]) members[cnt++] = members[j];
    if (!cnt) { free(members); return NULL; }
    *out_n = cnt;
    return members;
}

/* array:for-each(array, $f) */
static struct leptris_xpath_result* fn_array_for_each(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = array_members_arg(ctx, args, 0, &cnt);
    if (!items) return seq_new();
    char* cc = fn_item_content(ctx, args, 1);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[1] = { items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 1);
            if (r) {
                char* s = xpath_to_string(r);
                seq_push_str(out, s ? s : "");
                free(s);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    free_items(items, cnt);
    return out;
}

/* array:filter(array, $f) — members whose mapped result is truthy */
static struct leptris_xpath_result* fn_array_filter(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = array_members_arg(ctx, args, 0, &cnt);
    if (!items) return seq_new();
    char* cc = fn_item_content(ctx, args, 1);
    struct leptris_xpath_result* out = seq_new();
    if (cc && out) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[1] = { items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 1);
            if (r) {
                if (fn_result_truthy(r)) seq_push_str(out, items[k]);
                xpath_result_free(r);
            }
        }
    }
    free(cc);
    free_items(items, cnt);
    return out;
}

/* array:fold-left(array, zero, $f) — f(acc, member), left to right */
static struct leptris_xpath_result* fn_array_fold_left(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = array_members_arg(ctx, args, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 2);
    struct leptris_xpath_result* zr = xpath_evaluate(ctx, args[1]);
    char* acc = zr ? xpath_to_string(zr) : NULL;
    if (zr) xpath_result_free(zr);
    if (!acc) acc = leptris_strdup("");
    if (cc && acc) {
        for (size_t k = 0; k < cnt; k++) {
            char* argv[2] = { acc, items[k] };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            char* ns = r ? xpath_to_string(r) : NULL;
            if (r) xpath_result_free(r);
            free(acc);
            acc = ns ? ns : leptris_strdup("");
        }
    }
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (out) out->value.string_value = acc;
    else free(acc);
    free(cc);
    free_items(items, cnt);
    return out;
}

/* array:fold-right(array, zero, $f) — f(member, acc), right to left */
static struct leptris_xpath_result* fn_array_fold_right(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    size_t cnt;
    char** items = array_members_arg(ctx, args, 0, &cnt);
    if (!items) return NULL;
    char* cc = fn_item_content(ctx, args, 2);
    struct leptris_xpath_result* zr = xpath_evaluate(ctx, args[1]);
    char* acc = zr ? xpath_to_string(zr) : NULL;
    if (zr) xpath_result_free(zr);
    if (!acc) acc = leptris_strdup("");
    if (cc && acc) {
        for (size_t k = cnt; k > 0; k--) {
            char* argv[2] = { items[k - 1], acc };
            struct leptris_xpath_result* r =
                xpath_call_function_item(ctx, cc, argv, 2);
            char* ns = r ? xpath_to_string(r) : NULL;
            if (r) xpath_result_free(r);
            free(acc);
            acc = ns ? ns : leptris_strdup("");
        }
    }
    struct leptris_xpath_result* out =
        xpath_result_new(XPATH_RESULT_STRING);
    if (out) out->value.string_value = acc;
    else free(acc);
    free(cc);
    free_items(items, cnt);
    return out;
}

/* ---- sequence/doc tail (#691) ---- */

/* First node of args[i], or the context node when the argument is
 * absent. Nodes are document-owned; the nodeset container holding
 * them may be freed after the pointer is taken. */
static void* node_arg_or_ctx(XPathContext* ctx, XPathASTNode** args,
                             size_t n, size_t i) {
    if (n > i) {
        struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
        void* nd = NULL;
        if (r && r->type == XPATH_RESULT_NODESET &&
            r->value.nodeset_value && r->value.nodeset_value->count)
            nd = r->value.nodeset_value->nodes[0];
        leptris_xpath_result_free(r);
        if (nd) return nd;
    }
    return ctx->context_node;
}

static struct leptris_xpath_result* fn_innermost(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) { leptris_xpath_result_free(r); return NULL; }
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) {
        xpath_result_free(out);
        leptris_xpath_result_free(r);
        return NULL;
    }
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        XPathNodeSet* src = r->value.nodeset_value;
        for (size_t i = 0; i < src->count; i++) {
            int covered = 0;
            for (LeptrisElement p = leptris_node_parent(src->nodes[i]);
                 p && !covered;
                 p = leptris_node_parent((LeptrisNodeRef)p))
                for (size_t j = 0; j < src->count; j++)
                    if (src->nodes[j] == (void*)p) { covered = 1; break; }
            if (!covered)
                xpath_nodeset_add(out->value.nodeset_value, src->nodes[i]);
        }
    }
    leptris_xpath_result_free(r);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_outermost(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[0]);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_NODESET);
    if (!out) { leptris_xpath_result_free(r); return NULL; }
    out->value.nodeset_value = xpath_nodeset_new();
    if (!out->value.nodeset_value) {
        xpath_result_free(out);
        leptris_xpath_result_free(r);
        return NULL;
    }
    if (r && r->type == XPATH_RESULT_NODESET && r->value.nodeset_value) {
        XPathNodeSet* src = r->value.nodeset_value;
        char* covered = (char*)calloc(src->count ? src->count : 1, 1);
        if (!covered) {
            leptris_xpath_result_free(r);
            return out;
        }
        /* A node is dropped when some set member sits below it:
         * walk every member's ancestor chain and mark matches. */
        for (size_t j = 0; j < src->count; j++)
            for (LeptrisElement p = leptris_node_parent(src->nodes[j]); p;
                 p = leptris_node_parent((LeptrisNodeRef)p))
                for (size_t i = 0; i < src->count; i++)
                    if (src->nodes[i] == (void*)p) covered[i] = 1;
        for (size_t i = 0; i < src->count; i++)
            if (!covered[i])
                xpath_nodeset_add(out->value.nodeset_value, src->nodes[i]);
        free(covered);
    }
    leptris_xpath_result_free(r);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_has_children(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    void* nd = node_arg_or_ctx(ctx, args, n, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out) return NULL;
    out->value.boolean_value = nd && leptris_node_first_child(nd) != NULL;
    return out;
}

static struct leptris_xpath_result* fn_nilled(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    void* nd = node_arg_or_ctx(ctx, args, n, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out) return NULL;
    out->value.boolean_value = 0;
    if (nd && leptris_node_get_type(nd) == LEPTRIS_NODE_TYPE_ELEMENT) {
        const char* v = leptris_element_attribute((LeptrisElement)nd,
                                                  "xsi:nil");
        out->value.boolean_value = v && strcmp(v, "true") == 0;
    }
    return out;
}

static int path_append(char** b, size_t* len, size_t* cap,
                       const char* s) {
    size_t sl = strlen(s);
    if (*len + sl + 1 > *cap) {
        size_t nc = *cap ? *cap : 64;
        while (nc < *len + sl + 1) nc *= 2;
        char* g = (char*)realloc(*b, nc);
        if (!g) return 0;
        *b = g;
        *cap = nc;
    }
    memcpy(*b + *len, s, sl);
    *len += sl;
    return 1;
}

static struct leptris_xpath_result* fn_path(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    void* nd = node_arg_or_ctx(ctx, args, n, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) return NULL;
    out->value.string_value = leptris_strdup("");
    if (!nd || leptris_node_get_type(nd) != LEPTRIS_NODE_TYPE_ELEMENT)
        return out;
    /* Climb to the root, then emit downward with same-name
     * position predicates where siblings repeat the name. */
    LeptrisElement chain[256];
    size_t depth = 0;
    for (LeptrisElement e = (LeptrisElement)nd; e && depth < 256;
         e = leptris_node_parent((LeptrisNodeRef)e))
        chain[depth++] = e;
    char* b = NULL;
    size_t bl = 0, bc = 0;
    for (size_t i = depth; i-- > 0;) {
        const char* nm = leptris_element_get_name(chain[i]);
        path_append(&b, &bl, &bc, "/");
        path_append(&b, &bl, &bc, nm ? nm : "*");
        size_t same = 0, pos = 0;
        for (LeptrisNodeRef s = leptris_node_first_child(
                 (LeptrisNodeRef)leptris_node_parent((LeptrisNodeRef)chain[i]));
             s; s = leptris_node_next_sibling(s)) {
            if (leptris_node_get_type(s) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            const char* snm = leptris_element_get_name((LeptrisElement)s);
            if (!nm || (snm && strcmp(snm, nm) == 0)) {
                same++;
                if (s == (LeptrisNodeRef)chain[i]) pos = same;
            }
        }
        if (same > 1) {
            char idx[32];
            snprintf(idx, sizeof(idx), "[%zu]", pos);
            path_append(&b, &bl, &bc, idx);
        }
    }
    if (b) {
        free(out->value.string_value);
        b[bl] = 0;
        out->value.string_value = b;
    }
    return out;
}

/* In-memory documents carry no base or document URI. */
static struct leptris_xpath_result* fn_base_uri(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    node_arg_or_ctx(ctx, args, n, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) return NULL;
    out->value.string_value = leptris_strdup("");
    return out;
}

static struct leptris_xpath_result* fn_uri_constant(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)ctx; (void)args; (void)n;
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) return NULL;
    out->value.string_value = leptris_strdup("");
    return out;
}

static struct leptris_xpath_result* fn_doc_available(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* path = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out) { free(path); return NULL; }
    out->value.boolean_value = 0;
    if (path) {
        LeptrisDocument d = leptris_parse_file(path, NULL);
        if (d) {
            out->value.boolean_value = 1;
            leptris_document_free(d);
        }
        free(path);
    }
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_json_doc(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    (void)n;
    char* path = re_str_arg(ctx, args, 0);
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    free(path);
    struct leptris_xpath_result* out = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* buf = (char*)malloc(sz > 0 ? (size_t)sz + 1 : 1);
        if (buf) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            buf[got] = 0;
            JsonScan s = { buf, 1 };
            out = json_root(&s);
            free(buf);
        }
        fclose(f);
    }
    if (!out) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                 "json-doc(): cannot load JSON");
        snprintf(ctx->error_code, sizeof(ctx->error_code), "FODC0002");
        return NULL;
    }
    return out;
}

/* ---- scalar tail (#691 slice 2) ---- */

/* Like re_str_arg, but an EMPTY SEQUENCE argument yields NULL
 * (fn:compare's empty-operand rule; re_str_arg stringifies empty
 * nodesets to ""). */
static char* re_str_arg_opt(XPathContext* ctx, XPathASTNode** args,
                            size_t i) {
    struct leptris_xpath_result* r = xpath_evaluate(ctx, args[i]);
    if (!r) return NULL;
    if (r->type == XPATH_RESULT_NODESET &&
        (!r->value.nodeset_value || r->value.nodeset_value->count == 0)) {
        leptris_xpath_result_free(r);
        return NULL;
    }
    char* s = (r->type == XPATH_RESULT_NODESET)
                  ? leptris_xpath_result_string(r)
                  : scalar_str(r);
    leptris_xpath_result_free(r);
    return s;
}

static struct leptris_xpath_result* fn_compare(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* a = re_str_arg_opt(ctx, args, 0);
    char* b = re_str_arg_opt(ctx, args, 1);
    struct leptris_xpath_result* out = NULL;
    if (a && b) {  /* an empty operand yields the empty sequence */
        out = xpath_result_new(XPATH_RESULT_NUMBER);
        if (out)
            out->value.number_value =
                strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0 ? 1 : 0;
    } else {
        out = xpath_result_new(XPATH_RESULT_NODESET);
        if (out) out->value.nodeset_value = xpath_nodeset_new();
    }
    free(a);
    free(b);
    (void)n;
    return out;
}

static struct leptris_xpath_result* fn_codepoint_equal(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* a = re_str_arg(ctx, args, 0);
    char* b = re_str_arg(ctx, args, 1);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!out) { free(a); free(b); return NULL; }
    out->value.boolean_value =
        a && b && strcmp(a, b) == 0;  /* empty operand -> false */
    free(a);
    free(b);
    (void)n;
    return out;
}

#ifdef LEPTRIS_HAS_UTF8PROC
static struct leptris_xpath_result* fn_normalize_unicode(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* in = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(in); return NULL; }
    out->value.string_value = in;
    if (n >= 2) {
        char* form = re_str_arg(ctx, args, 1);
        leptris_unicode_normalization_t f = LEPTRIS_UNICODE_NFC;
        if (form) {
            if (strcmp(form, "NFD") == 0) f = LEPTRIS_UNICODE_NFD;
            else if (strcmp(form, "NFKC") == 0) f = LEPTRIS_UNICODE_NFKC;
            else if (strcmp(form, "NFKD") == 0) f = LEPTRIS_UNICODE_NFKD;
            free(form);
        }
        size_t ol = 0;
        char* norm = leptris_unicode_normalize(in ? in : "",
                                               in ? strlen(in) : 0, f, &ol);
        if (norm) {
            free(in);
            out->value.string_value = norm;
        }
    }
    return out;
}
#endif

static struct leptris_xpath_result* fn_resolve_qname(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    char* qn = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(qn); return NULL; }
    out->value.string_value = leptris_strdup(qn ? qn : "");
    LeptrisElement e = NULL;
    if (n >= 2) {
        struct leptris_xpath_result* r = xpath_evaluate(ctx, args[1]);
        if (r && r->type == XPATH_RESULT_NODESET &&
            r->value.nodeset_value && r->value.nodeset_value->count) {
            void* nd = r->value.nodeset_value->nodes[0];
            if (leptris_node_get_type(nd) == LEPTRIS_NODE_TYPE_ELEMENT)
                e = (LeptrisElement)nd;
        }
        leptris_xpath_result_free(r);
    }
    if (qn) {
        const char* colon = strchr(qn, ':');
        char prefix[128];
        const char* uri = NULL;
        if (colon) {
            size_t pl = (size_t)(colon - qn);
            if (pl < sizeof(prefix)) {
                memcpy(prefix, qn, pl);
                prefix[pl] = 0;
                uri = e ? leptris_element_namespace_for_prefix(e, prefix)
                        : NULL;
            }
        } else {
            uri = e ? leptris_element_namespace_for_prefix(e, "") : NULL;
        }
        snprintf(last_qname_uri, sizeof(last_qname_uri), "%s",
                 uri ? uri : "");
    }
    free(qn);
    return out;
}

static struct leptris_xpath_result* fn_environment_variable(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* name = re_str_arg(ctx, args, 0);
    struct leptris_xpath_result* out = xpath_result_new(XPATH_RESULT_STRING);
    if (!out) { free(name); return NULL; }
    const char* v = name ? getenv(name) : NULL;
    out->value.string_value = leptris_strdup(v ? v : "");
    free(name);
    (void)n;
    return out;
}

#ifdef _WIN32
extern char** _environ;   /* MSVC CRT (MBCS build) */
#define LEPTRIS_ENVIRON _environ
#else
extern char** environ;
#define LEPTRIS_ENVIRON environ
#endif

static struct leptris_xpath_result* fn_available_env_vars(XPathContext* ctx,
        XPathASTNode** args, size_t n) {
    struct leptris_xpath_result* out = seq_new();
    if (!out) return NULL;
    for (char** e = LEPTRIS_ENVIRON; e && *e; e++) {
        const char* eq = strchr(*e, '=');
        if (!eq) continue;
        size_t nl = (size_t)(eq - *e);
        char* name = (char*)malloc(nl + 1);
        if (!name) continue;
        memcpy(name, *e, nl);
        name[nl] = 0;
        seq_push_str(out, name);
        free(name);
    }
    (void)ctx; (void)args; (void)n;
    return out;
}

void xpath_register_fn31(XPathFunctionRegistry* registry) {
    if (!registry) return;
    xpath_function_registry_register(registry, "exists", fn_exists, 1, 1);
    xpath_function_registry_register(registry, "empty", fn_empty, 1, 1);
    xpath_function_registry_register(registry, "head", fn_head, 1, 1);
    xpath_function_registry_register(registry, "tail", fn_tail, 1, 1);
    xpath_function_registry_register(registry, "reverse", fn_reverse, 1, 1);
    xpath_function_registry_register(registry, "unordered", fn_unordered, 1, 1);
    xpath_function_registry_register(registry, "doc", fn_doc, 1, 1);
    xpath_function_registry_register(registry, "parse-xml",
                                     fn_parse_xml, 1, 1);
    xpath_function_registry_register(registry, "parse-xml-fragment",
                                     fn_parse_xml_fragment, 1, 1);
    xpath_function_registry_register(registry, "collection",
                                     fn_collection, 0, 1);
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

    xpath_function_registry_register(registry, "function-lookup", fn_function_lookup, 2, 2);
    xpath_function_registry_register(registry, "function-name", fn_function_name, 1, 1);
    xpath_function_registry_register(registry, "function-arity", fn_function_arity, 1, 1);
    xpath_function_registry_register(registry, "for-each", fn_for_each, 2, 2);
    xpath_function_registry_register(registry, "filter", fn_filter, 2, 2);
    xpath_function_registry_register(registry, "fold-left", fn_fold_left, 3, 3);
    xpath_function_registry_register(registry, "fold-right", fn_fold_right, 3, 3);
    xpath_function_registry_register(registry, "for-each-pair", fn_for_each_pair, 3, 3);
    xpath_function_registry_register(registry, "apply", fn_apply, 2, 2);

    xpath_function_registry_register(registry, "map:for-each", fn_map_for_each, 2, 2);

    xpath_function_registry_register(registry, "array:for-each", fn_array_for_each, 2, 2);
    xpath_function_registry_register(registry, "array:filter", fn_array_filter, 2, 2);
    xpath_function_registry_register(registry, "array:fold-left", fn_array_fold_left, 3, 3);
    xpath_function_registry_register(registry, "array:fold-right", fn_array_fold_right, 3, 3);

    xpath_function_registry_register(registry, "math:sqrt", fn_sqrt, 1, 1);
    xpath_function_registry_register(registry, "innermost", fn_innermost, 1, 1);
    xpath_function_registry_register(registry, "outermost", fn_outermost, 1, 1);
    xpath_function_registry_register(registry, "has-children", fn_has_children, 0, 1);
    xpath_function_registry_register(registry, "nilled", fn_nilled, 0, 1);
    xpath_function_registry_register(registry, "path", fn_path, 0, 1);
    xpath_function_registry_register(registry, "base-uri", fn_base_uri, 0, 1);
    xpath_function_registry_register(registry, "document-uri", fn_uri_constant, 0, 1);
    xpath_function_registry_register(registry, "static-base-uri", fn_uri_constant, 0, 0);
    xpath_function_registry_register(registry, "doc-available", fn_doc_available, 1, 1);
    xpath_function_registry_register(registry, "json-doc", fn_json_doc, 1, 1);
    xpath_function_registry_register(registry, "compare", fn_compare, 2, 3);
    xpath_function_registry_register(registry, "codepoint-equal", fn_codepoint_equal, 2, 2);
#ifdef LEPTRIS_HAS_UTF8PROC
    xpath_function_registry_register(registry, "normalize-unicode", fn_normalize_unicode, 1, 2);
#endif
    xpath_function_registry_register(registry, "resolve-QName", fn_resolve_qname, 2, 2);
    xpath_function_registry_register(registry, "environment-variable", fn_environment_variable, 1, 1);
    xpath_function_registry_register(registry, "available-environment-variables", fn_available_env_vars, 0, 0);
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
    /* Dates & durations (05, first slice) — canonical xs: prefix. */
    xpath_function_registry_register(registry, "xs:date", fn_passthrough_ctor, 1, 1);
    /* Atomic constructors (06) — canonical xs: prefix. */
    xpath_function_registry_register(registry, "xs:string", fn_passthrough_ctor, 1, 1);
    xpath_function_registry_register(registry, "xs:anyURI", fn_passthrough_ctor, 1, 1);
    xpath_function_registry_register(registry, "xs:integer", fn_xs_integer, 1, 1);
    xpath_function_registry_register(registry, "xs:double", fn_xs_double, 1, 1);
    xpath_function_registry_register(registry, "xs:decimal", fn_xs_double, 1, 1);
    xpath_function_registry_register(registry, "xs:boolean", fn_xs_boolean, 1, 1);
    xpath_function_registry_register(registry, "xs:dateTime", fn_passthrough_ctor, 1, 1);
    xpath_function_registry_register(registry, "xs:time", fn_passthrough_ctor, 1, 1);
    xpath_function_registry_register(registry, "xs:duration", fn_passthrough_ctor, 1, 1);
    xpath_function_registry_register(registry, "year-from-dateTime", fn_year_from_dt, 1, 1);
    xpath_function_registry_register(registry, "month-from-dateTime", fn_month_from_dt, 1, 1);
    xpath_function_registry_register(registry, "day-from-dateTime", fn_day_from_dt, 1, 1);
    xpath_function_registry_register(registry, "hours-from-time", fn_hours_from_t, 1, 1);
    xpath_function_registry_register(registry, "minutes-from-time", fn_minutes_from_t, 1, 1);
    xpath_function_registry_register(registry, "seconds-from-time", fn_seconds_from_t, 1, 1);
    xpath_function_registry_register(registry, "days-from-duration", fn_days_from_dur, 1, 1);
    xpath_function_registry_register(registry, "hours-from-duration", fn_hours_from_dur, 1, 1);
    xpath_function_registry_register(registry, "matches", fn_matches, 2, 3);
    xpath_function_registry_register(registry, "replace", fn_replace, 3, 4);
    xpath_function_registry_register(registry, "tokenize", fn_tokenize, 2, 3);
    /* Maps (08, first slice) — canonical map: prefix. */
    xpath_function_registry_register(registry, "map:get", fn_map_get, 2, 2);
    xpath_function_registry_register(registry, "map:size", fn_map_size, 1, 1);
    xpath_function_registry_register(registry, "map:keys", fn_map_keys, 1, 1);
    xpath_function_registry_register(registry, "map:contains", fn_map_contains, 2, 2);
    xpath_function_registry_register(registry, "map:put", fn_map_put, 3, 3);
    xpath_function_registry_register(registry, "map:remove", fn_map_remove, 2, 2);
    xpath_function_registry_register(registry, "map:merge", fn_map_merge, 1, 1);
    /* Arrays (08C) — canonical array: prefix, over the map repr. */
    xpath_function_registry_register(registry, "array:size", fn_map_size, 1, 1);
    xpath_function_registry_register(registry, "array:get", fn_array_get, 2, 2);
    xpath_function_registry_register(registry, "array:append", fn_array_append, 2, 2);
    xpath_function_registry_register(registry, "array:put", fn_array_put, 3, 3);
    /* JSON (08D). */
    xpath_function_registry_register(registry, "parse-json", fn_parse_json, 1, 1);
    xpath_function_registry_register(registry, "json-to-xml", fn_json_to_xml, 1, 1);
    xpath_function_registry_register(registry, "xml-to-json", fn_xml_to_json, 1, 1);
    xpath_function_registry_register(registry, "serialize", fn_serialize, 1, 2);
}
