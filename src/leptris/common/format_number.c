/* format_number.c — JDK1.1 decimal-format pattern core (SSOT,
 * moved verbatim from xslt/xslt_functions.c; the XSLT layer keeps
 * only the xsl:decimal-format lookup and passes a spec). */
#include "format_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* fn_strdup_(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

typedef struct {
    const char* prefix; size_t prefix_len;
    const char* suffix; size_t suffix_len;
    int min_int, max_int;     /* leading '0's and total digits */
    int min_frac, max_frac;
    int has_decimal;
    int has_grouping;
    int group_size;            /* digit slots after the LAST grouping
                                * separator (JDK semantics: '#,#0' = 2) */
    int multiplier;            /* 1, 100, 1000 */
} PatternInfo;

static const char* find_split(const char* s, const char* end) {
    int depth = 0;
    for (const char* p = s; p < end; p++) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        else if (*p == ';' && depth == 0) return p;
    }
    return end;
}

/* Does the byte at p start the multi-byte-safe separator sep? */
static int sep_at(const char* p, const char* end, const char* sep) {
    if (!sep) return 0;
    size_t sl = strlen(sep);
    if ((size_t)(end - p) < sl) return 0;
    return memcmp(p, sep, sl) == 0;
}

static void parse_one(const char* s, const char* end, PatternInfo* pi,
                       const char* dsep, const char* gsep) {
    /* Single pass: classify each char into prefix/int/digit/frac/
     * suffix; %/mille/./0-#/, are tokens. The cursor lets the
     * outer loop know we belong to the int vs frac part. */
    int phase = 0;   /* 0=prefix, 1=int, 2=frac, 3=suffix */
    pi->prefix = s; pi->prefix_len = 0;
    pi->suffix = end; pi->suffix_len = 0;
    pi->min_int = 0; pi->max_int = 0;
    pi->min_frac = 0; pi->max_frac = 0;
    pi->has_decimal = 0;
    pi->has_grouping = 0;
    pi->group_size = 0;
    pi->multiplier = 1;
    /* Walk: count contiguous prefix chars; first '0' or '#' begins
     * the int part; '.' moves to frac; '%' (and ‰ — v1 aliased to
     * '%' since per-mille is stored as ASCII; multi-byte ‰ is
     * treated as a multiplier trigger in the more general form) set
     * multiplier; other chars before/after turn into prefix/suffix. */
    const char* p = s;
    while (p < end) {
        unsigned char uc = (unsigned char)*p;
        char c = (char)uc;
        if (phase == 0) {
            if (c == '0' || c == '#') { phase = 1; continue; }
            pi->prefix_len++;
            if (c == '%') pi->multiplier = 100;
            else if (uc == 0xE2 &&
                     p + 2 < end && p[1] == (char)0x80 &&
                     p[2] == (char)0xB0) {  /* UTF-8 ‰ */
                pi->multiplier = 1000; p += 2;
            } else if (sep_at(p, end, dsep)) { pi->has_decimal = 1; }
            p++; continue;
        }
        if (phase == 1) {
            if (c == '0') {
                pi->min_int++; pi->max_int++;
                if (pi->has_grouping) pi->group_size++;
                p++; continue;
            }
            if (c == '#') {
                pi->max_int++;
                if (pi->has_grouping) pi->group_size++;
                p++; continue;
            }
            /* When both separators are the same string, the
             * DECIMAL reading wins (§12.3 ambiguity — libxslt). */
            if (sep_at(p, end, dsep)) {
                pi->has_decimal = 1;
                phase = 2; p += strlen(dsep); continue;
            }
            if (sep_at(p, end, gsep)) {
                pi->has_grouping = 1;
                /* JDK grouping size: digit slots after the LAST
                 * separator — reset the count at each separator. */
                pi->group_size = 0;
                p += strlen(gsep); continue;
            }

            if (c == '%') { pi->multiplier = 100; pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break; }
            if (uc == 0xE2 && p + 2 < end && p[1] == (char)0x80 &&
                p[2] == (char)0xB0) {
                pi->multiplier = 1000; p += 2;
                pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
            }
            pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
        }
        if (phase == 2) {
            if (c == '0') { pi->min_frac++; pi->max_frac++; p++; continue; }
            if (c == '#') { pi->max_frac++; p++; continue; }
            pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
        }
        break;
    }
}

/* Returns 1 when an explicit negative subpattern was present. */
static int parse_pattern(const char* s, size_t len,
                          PatternInfo* pos, PatternInfo* neg,
                          const char* dsep, const char* gsep) {
    const char* end = s + len;
    const char* split = find_split(s, end);
    parse_one(s, split, pos, dsep, gsep);
    if (split < end) {
        parse_one(split + 1, end, neg, dsep, gsep);
        return 1;
    }
    *neg = *pos;
    return 0;
}

/* Format |abs_v| per the parsed pattern; returns OWNED string.
 * Separators are full strings (any UTF-8 character). */
static char* format_value(const PatternInfo* p, double abs_v,
                           const char* decimal_sep,
                           const char* grouping_sep,
                           char zero_digit) {
    size_t dsl = strlen(decimal_sep);
    size_t gsl = strlen(grouping_sep);
    int prec = p->max_frac;
    char body[256];
    if (prec > 0) snprintf(body, sizeof(body), "%.*f", prec, abs_v);
    else snprintf(body, sizeof(body), "%.0f", abs_v);

    char intpart[128] = "", fracpart[128] = "";
    const char* dot = strchr(body, '.');
    if (dot) {
        size_t il = (size_t)(dot - body);
        memcpy(intpart, body, il); intpart[il] = 0;
        snprintf(fracpart, sizeof(fracpart), "%s", dot + 1);
    } else {
        snprintf(intpart, sizeof(intpart), "%s", body);
    }
    /* Right-pad frac with zeros up to min_frac, then truncate to max_frac. */
    {
        size_t fl = strlen(fracpart);
        if ((int)fl < p->min_frac) {
            for (; (int)fl < p->min_frac && fl + 1 < sizeof(fracpart); fl++)
                fracpart[fl] = zero_digit;
            fracpart[fl] = 0;
        }
        if ((int)fl > p->max_frac) fracpart[p->max_frac] = 0;
        /* Optional fraction digits: trailing zeros beyond min_frac
         * are not shown ('#,##0.##' of an integral value has no
         * decimal part). */
        while (fl > 0 && (int)fl > p->min_frac &&
               fracpart[fl - 1] == zero_digit)
            fracpart[--fl] = 0;
    }
    /* Left-pad int with zeros to min_int. */
    if ((int)strlen(intpart) < p->min_int) {
        char tmp[128]; size_t t = 0;
        for (int i = 0; i < p->min_int - (int)strlen(intpart) && t < sizeof(tmp) - 1; i++)
            tmp[t++] = zero_digit;
        snprintf(tmp + t, sizeof(tmp) - t, "%s", intpart);
        snprintf(intpart, sizeof(intpart), "%s", tmp);
    }
    /* Insert the grouping separator every group_size digits from
     * the right — the size the PATTERN defines (digit slots after
     * the last separator; '#,#0' groups by 2, '#,##0' by 3). */
    int gsz = p->group_size > 0 ? p->group_size : 3;
    if (p->has_grouping) {
        size_t gl = strlen(intpart);
        char rev[320]; size_t ri = 0, cnt = 0;
        for (size_t i = gl; i-- > 0 && ri + gsl < sizeof(rev); ) {
            rev[ri++] = intpart[i];
            if (++cnt % (size_t)gsz == 0 && i > 0 &&
                ri + gsl < sizeof(rev)) {
                /* Store the separator REVERSED — the whole rev
                 * buffer is byte-reversed below, which restores
                 * multi-byte separators in order. */
                for (size_t k = gsl; k-- > 0; )
                    rev[ri++] = grouping_sep[k];
            }
        }
        /* reverse rev → intpart */
        for (size_t i = 0; i < ri && i + 1 < sizeof(intpart); i++)
            intpart[i] = rev[ri - 1 - i];
        intpart[ri < sizeof(intpart) ? ri : sizeof(intpart) - 1] = 0;
    }
    char out[512]; size_t o = 0;
    for (size_t i = 0; i < p->prefix_len && o + 1 < sizeof(out); i++)
        out[o++] = p->prefix[i];
    if (p->prefix_len && p->multiplier != 1 && p->multiplier == 100) {
        /* JDK1.1: % is appended at the start of the prefix segment
         * (rightmost prefix char), but we already absorbed it as a
         * prefix byte (kept verbatim). No further change. */
    }
    for (size_t i = 0; intpart[i] && o + 1 < sizeof(out); i++) out[o++] = intpart[i];
    if (p->has_decimal && fracpart[0] && o + dsl < sizeof(out)) {
        memcpy(out + o, decimal_sep, dsl);
        o += dsl;
    }
    for (size_t i = 0; fracpart[i] && o + 1 < sizeof(out); i++) out[o++] = fracpart[i];
    for (size_t i = 0; i < p->suffix_len && o + 1 < sizeof(out); i++)
        out[o++] = p->suffix[i];
    out[o] = 0;
    return fn_strdup_(out);
}

const LeptrisNumFormatSpec* leptris_format_number_default(void) {
    static const LeptrisNumFormatSpec d = {".", ",", "NaN", "Infinity",
                                            '0', '-'};
    return &d;
}

char* leptris_format_number_core(double value, const char* pattern,
                                 const LeptrisNumFormatSpec* spec) {
    const LeptrisNumFormatSpec* f = spec ? spec
                                         : leptris_format_number_default();
    if (!pattern || !*pattern) pattern = "0";
    const char* decimal_sep = f->decimal_sep;
    const char* grouping_sep = f->grouping_sep;
    const char* nan_str = f->nan;
    const char* inf_str = f->infinity;
    char zero_digit = f->zero_digit;
    if (value != value) return fn_strdup_(nan_str);
    int neg = (value < 0);
    PatternInfo pos = {0}, negi = {0};
    int has_neg = parse_pattern(pattern, strlen(pattern), &pos, &negi,
                                decimal_sep, grouping_sep);
    PatternInfo* use = neg ? (has_neg ? &negi : &pos) : &pos;
    if (neg && !has_neg) {
        char minus = f->minus_sign;
        size_t pl = use->prefix_len;
        char* nv = (char*)malloc(pl + 2);
        if (nv) { memcpy(nv, use->prefix, pl); nv[pl] = minus;
                  nv[pl+1] = 0; use->prefix = nv; use->prefix_len = pl + 1; }
    }
    double av = neg ? -value : value;
    if (use->multiplier != 1) av *= use->multiplier;
    if (av >= HUGE_VAL || av <= -HUGE_VAL) {
        if (neg && use->prefix_len) {
            char* s2 = (char*)malloc(use->prefix_len + strlen(inf_str) + 1);
            if (s2) { memcpy(s2, use->prefix, use->prefix_len);
                      memcpy(s2 + use->prefix_len, inf_str,
                             strlen(inf_str) + 1);
                      free((void*)use->prefix);
                      return s2; }
        }
        return fn_strdup_(inf_str);
    }
    char* out = format_value(use, av, decimal_sep, grouping_sep,
                             zero_digit);
    if (neg && use->prefix_len && !has_neg) free((void*)use->prefix);
    return out;
}
