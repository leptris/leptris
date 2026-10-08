/* xsd/datatypes.c — #1075 slice 2: built-in datatype lexical
 * validity and user simpleType restriction checking.
 *
 * Built-ins covered here are the tier-1 family: string/Name family,
 * boolean, the numeric family (decimal, integer + its derivations,
 * float, double), the date/time family, duration, hex/base64Binary,
 * anyURI, QName. User simpleTypes validate their base first, then
 * their facets: pattern (the rng_regex engine — the XSD pattern
 * subset is exactly its dialect), enumeration, min/max inclusive/
 * exclusive, the length family, total/fractionDigits. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "xsd_internal.h"

/* rng_regex.c — the XSD pattern dialect (libxml2-compatible). */
extern int rng_regex_matches(const char* pat, const char* text);

/* ---- character-class helpers ------------------------------------- */

static int xsd_ncname_start(char c) {
    return isalpha((unsigned char)c) || c == '_' ||
           (unsigned char)c >= 0x80;
}
static int xsd_ncname_char(char c) {
    return xsd_ncname_start(c) || isdigit((unsigned char)c) ||
           c == '-' || c == '.';
}
static int xsd_is_ncname(const char* s) {
    if (!s || !*s || !xsd_ncname_start(*s)) return 0;
    for (const char* p = s + 1; *p; p++)
        if (!xsd_ncname_char(*p)) return 0;
    return 1;
}
static int xsd_digits_only(const char* s) {
    if (!s || !*s) return 0;
    for (const char* p = s; *p; p++)
        if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}
/* [-+]?digits — the shared integer lexical shape. */
static int xsd_signed_integer(const char* s) {
    if (!s || !*s) return 0;
    if (*s == '+' || *s == '-') s++;
    return xsd_digits_only(s);
}

/* ---- numeric range table (the integer derivations) --------------- */

typedef struct {
    const char* name;
    long long lo, hi;
    int integer; /* 1 = integer lexical; 0 = decimal lexical */
} NumType;

static const NumType k_numerics[] = {
    {"xs:integer", -9223372036854775807LL - 1, 9223372036854775807LL, 1},
    {"xs:nonNegativeInteger", 0, 9223372036854775807LL, 1},
    {"xs:positiveInteger", 1, 9223372036854775807LL, 1},
    {"xs:nonPositiveInteger", -9223372036854775807LL - 1, 0, 1},
    {"xs:negativeInteger", -9223372036854775807LL - 1, -1, 1},
    {"xs:long", -9223372036854775807LL - 1, 9223372036854775807LL, 1},
    {"xs:int", -2147483648LL, 2147483647LL, 1},
    {"xs:short", -32768, 32767, 1},
    {"xs:byte", -128, 127, 1},
    {"xs:unsignedLong", 0, 9223372036854775807LL, 1},
    {"xs:unsignedInt", 0, 4294967295LL, 1},
    {"xs:unsignedShort", 0, 65535, 1},
    {"xs:unsignedByte", 0, 255, 1},
    {NULL, 0, 0, 0},
};

/* XSD digits: no leading-zero rule beyond the sign for zero, but
 * "007" is a valid integer lexical in XSD (unlike most languages). */
static int value_in_range(const char* s, long long lo, long long hi) {
    if (!xsd_signed_integer(s)) return 0;
    /* Guard 64-bit overflow: XSD allows arbitrarily many digits —
     * values beyond long long are still "in" xs:integer (arbitrary
     * precision) but cannot sit in a bounded derivation's range.
     * Digit-count prefilter keeps strtoll well-defined. */
    const char* digits = (*s == '+' || *s == '-') ? s + 1 : s;
    size_t nd = strlen(digits);
    while (nd > 1 && digits[0] == '0') { digits++; nd--; }
    if (nd > 19) return 0;
    char* end = NULL;
    long long v = strtoll(s, &end, 10);
    if (end && *end) return 0;
    return v >= lo && v <= hi;
}

/* ---- date/time lexical shapes ------------------------------------ */

static int four_digits(const char** p) {
    if (!isdigit((unsigned char)(*p)[0]) ||
        !isdigit((unsigned char)(*p)[1]) ||
        !isdigit((unsigned char)(*p)[2]) ||
        !isdigit((unsigned char)(*p)[3]))
        return 0;
    *p += 4;
    return 1;
}
static int two_digits(const char** p) {
    if (!isdigit((unsigned char)(*p)[0]) ||
        !isdigit((unsigned char)(*p)[1]))
        return 0;
    *p += 2;
    return 1;
}
static int tz_optional(const char** p) {
    if (**p != 'Z' && **p != '+' && **p != '-') return 1; /* absent */
    if (**p == 'Z') { (*p)++; return **p == 0; }
    (*p)++;
    return two_digits(p) && *(*p) == ':' && (*p)++ && two_digits(p) &&
           **p == 0;
}
static int two_digits_bounded(const char** p, int lo, int hi) {
    if (!two_digits(p)) return 0;
    int v = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
    return v >= lo && v <= hi;
}
static int ymd(const char** p, int need_month, int need_day) {
    if (!four_digits(p)) return 0;
    if (need_month) {
        if (*(*p)++ != '-') return 0;
        if (!two_digits_bounded(p, 1, 12)) return 0;
    }
    if (need_day) {
        if (*(*p)++ != '-') return 0;
        if (!two_digits_bounded(p, 1, 31)) return 0;
    }
    return 1;
}
static int time_tail(const char** p) {
    if (!two_digits(p)) return 0;
    if (*(*p)++ != ':') return 0;
    if (!two_digits(p)) return 0;
    if (**p == ':') {
        (*p)++;
        if (!two_digits(p)) return 0;
        if (**p == '.') {
            (*p)++;
            if (!xsd_digits_only(*p)) return 0;
            while (isdigit((unsigned char)**p)) (*p)++;
        }
    }
    return tz_optional(p);
}

/* ---- the built-in table ------------------------------------------ */

int xsd_builtin_valid(const char* type, const char* v) {
    if (!type || !v) return 0;
    if (strcmp(type, "xs:string") == 0) return 1;
    if (strcmp(type, "xs:normalizedString") == 0) return 1;
    if (strcmp(type, "xs:token") == 0) {
        if (*v == 0) return 1;
        if (isspace((unsigned char)v[0]) ||
            isspace((unsigned char)v[strlen(v) - 1]))
            return 0;
        for (const char* p = v; *p; p++) {
            if (isspace((unsigned char)*p) &&
                isspace((unsigned char)p[1]))
                return 0;
            if (*p == '\n' || *p == '\r' || *p == '\t') return 0;
        }
        return 1;
    }
    if (strcmp(type, "xs:Name") == 0) {
        /* Name: NameStartChar (NameChar)* — NCName is the strict
         * ASCII/80 subset; libxml2 accepts ':' in Names. */
        if (!*v || !xsd_ncname_start(*v) && *v != ':') return 0;
        for (const char* p = v + 1; *p; p++)
            if (!xsd_ncname_char(*p) && *p != ':') return 0;
        return 1;
    }
    if (strcmp(type, "xs:NMTOKEN") == 0) {
        if (!*v) return 0;
        for (const char* p = v; *p; p++)
            if (!xsd_ncname_char(*p) && *p != ':') return 0;
        return 1;
    }
    if (strcmp(type, "xs:NCName") == 0 || strcmp(type, "xs:ID") == 0 ||
        strcmp(type, "xs:IDREF") == 0 || strcmp(type, "xs:ENTITY") == 0)
        return xsd_is_ncname(v);
    if (strcmp(type, "xs:language") == 0) {
        /* [a-zA-Z]{1,8}(-[a-zA-Z0-9]{1,8})* */
        const char* p = v;
        int n = 0;
        while (isalpha((unsigned char)*p) && n <= 8) { p++; n++; }
        if (n < 1 || n > 8 || (*p && *p != '-')) return 0;
        while (*p == '-') {
            p++;
            n = 0;
            while ((isalnum((unsigned char)*p)) && n <= 8) { p++; n++; }
            if (n < 1 || n > 8) return 0;
        }
        return *p == 0;
    }
    if (strcmp(type, "xs:anyURI") == 0) {
        /* Permissive (libxml2-compatible): no ASCII control
         * characters, no leading/trailing space. */
        if (*v && (unsigned char)v[0] <= 0x20) return 0;
        size_t n = strlen(v);
        if (n && (unsigned char)v[n - 1] <= 0x20) return 0;
        for (const char* p = v; *p; p++)
            if ((unsigned char)*p < 0x20) return 0;
        return 1;
    }
    if (strcmp(type, "xs:QName") == 0) {
        const char* colon = strchr(v, ':');
        if (!colon) return xsd_is_ncname(v);
        size_t plen = (size_t)(colon - v);
        char pfx[256];
        if (plen >= sizeof(pfx)) return 0;
        memcpy(pfx, v, plen);
        pfx[plen] = 0;
        return xsd_is_ncname(pfx) && xsd_is_ncname(colon + 1);
    }
    if (strcmp(type, "xs:boolean") == 0)
        return strcmp(v, "true") == 0 || strcmp(v, "false") == 0 ||
               strcmp(v, "0") == 0 || strcmp(v, "1") == 0;
    if (strcmp(type, "xs:decimal") == 0) {
        /* [-+]?((d+(.d*)?)|(.d+)) */
        const char* p = v;
        if (*p == '+' || *p == '-') p++;
        int lead = 0;
        while (isdigit((unsigned char)*p)) { p++; lead = 1; }
        int frac = 0;
        if (*p == '.') {
            p++;
            while (isdigit((unsigned char)*p)) { p++; frac = 1; }
        }
        if (*p) return 0;
        return lead || frac;
    }
    for (const NumType* t = k_numerics; t->name; t++) {
        if (strcmp(type, t->name) == 0) return value_in_range(v, t->lo, t->hi);
    }
    if (strcmp(type, "xs:float") == 0 || strcmp(type, "xs:double") == 0) {
        if (strcmp(v, "INF") == 0 || strcmp(v, "-INF") == 0 ||
            strcmp(v, "NaN") == 0)
            return 1;
        /* decimal or scientific */
        const char* p = v;
        if (*p == '+' || *p == '-') p++;
        int mant = 0;
        while (isdigit((unsigned char)*p)) { p++; mant = 1; }
        if (*p == '.') {
            p++;
            while (isdigit((unsigned char)*p)) { p++; mant = 1; }
        }
        if (!mant) return 0;
        if (*p == 'e' || *p == 'E') {
            p++;
            if (*p == '+' || *p == '-') p++;
            if (!xsd_digits_only(p)) return 0;
        } else if (*p) {
            return 0;
        }
        return 1;
    }
    if (strcmp(type, "xs:date") == 0) {
        const char* p = v;
        if (*p == '-') p++; /* BC years */
        if (!ymd(&p, 1, 1)) return 0;
        return tz_optional(&p);
    }
    if (strcmp(type, "xs:gYearMonth") == 0) {
        const char* p = v;
        if (*p == '-') p++;
        if (!ymd(&p, 1, 0)) return 0;
        return tz_optional(&p);
    }
    if (strcmp(type, "xs:gYear") == 0) {
        const char* p = v;
        if (*p == '-') p++;
        if (!four_digits(&p)) return 0;
        return tz_optional(&p);
    }
    if (strcmp(type, "xs:gMonthDay") == 0) {
        const char* p = v;
        if (p[0] != '-' || p[1] != '-') return 0;
        p += 2;
        if (!two_digits(&p)) return 0;
        if (*p == 0) return 1;
        if (*p++ != '-') return 0;
        return two_digits(&p) && *p == 0;
    }
    if (strcmp(type, "xs:gDay") == 0) {
        const char* p = v;
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        return two_digits(&p) && *p == 0;
    }
    if (strcmp(type, "xs:gMonth") == 0) {
        const char* p = v;
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        return two_digits(&p) && *p == 0;
    }
    if (strcmp(type, "xs:dateTime") == 0) {
        const char* p = v;
        if (*p == '-') p++;
        if (!ymd(&p, 1, 1)) return 0;
        if (*p != 'T' && *p != ' ') return 0;
        p++;
        return time_tail(&p);
    }
    if (strcmp(type, "xs:time") == 0) {
        const char* p = v;
        return time_tail(&p);
    }
    if (strcmp(type, "xs:duration") == 0 ||
        strcmp(type, "xs:dayTimeDuration") == 0 ||
        strcmp(type, "xs:yearMonthDuration") == 0) {
        /* PnYnMnDTnHnMnS, at least one component; optional sign. */
        const char* p = v;
        if (*p == '-' || *p == '+') p++;
        if (*p++ != 'P') return 0;
        int any = 0;
        int in_time = 0;
        for (;;) {
            if (*p == 'T') {
                if (in_time) return 0;
                in_time = 1;
                p++;
                continue;
            }
            if (!isdigit((unsigned char)*p)) break;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            char unit = *p;
            if (!unit) return 0;
            /* Months (date) and minutes (time) share 'M' — the
             * in_time flag disambiguates, so only the unambiguous
             * units are side-checked. */
            if (in_time && unit == 'Y') return 0;
            if (!in_time && (unit == 'H' || unit == 'S')) return 0;
            p++;
            any = 1;
        }
        return any && *p == 0;
    }
    if (strcmp(type, "xs:hexBinary") == 0) {
        size_t n = strlen(v);
        if (n % 2) return 0;
        for (const char* p = v; *p; p++)
            if (!isxdigit((unsigned char)*p)) return 0;
        return 1;
    }
    if (strcmp(type, "xs:base64Binary") == 0) {
        size_t n = strlen(v);
        if (!n) return 1;
        size_t i = 0;
        while (i + 4 <= n) {
            for (int k = 0; k < 4; k++) {
                char c = v[i + k];
                if (!isalnum((unsigned char)c) && c != '+' && c != '/' &&
                    !(k == 3 && c == '='))
                    return 0;
            }
            i += 4;
        }
        for (; i < n; i++) {
            char c = v[i];
            if (!isalnum((unsigned char)c) && c != '+' && c != '/' &&
                c != '=')
                return 0;
        }
        return 1;
    }
    return -1; /* not in the tier-1 table */
}

/* ---- facet checking on a captured simpleType --------------------- */

int xsd_simple_valid(const XsdSimple* simple, const char* v);

/* The base chain resolves to a built-in reference: validate the
 * lexical against it first. */
static int base_valid(const char* base, const char* v) {
    if (!base) return xsd_builtin_valid("xs:string", v);
    if (strncmp(base, "xs:", 3) == 0) return xsd_builtin_valid(base, v);
    return -1; /* local base — the resolver walks the chain */
}

static int facet_check(const XsdSimple* s, const char* v) {
    if (s->pattern && !rng_regex_matches(s->pattern, v)) return 0;
    if (s->enum_values) {
        int found = 0;
        for (XsdFacetValue* e = s->enum_values; e; e = e->next)
            if (strcmp(e->text, v) == 0) { found = 1; break; }
        if (!found) return 0;
    }
    /* Length family: character count (string-family semantics). */
    size_t n = strlen(v);
    if (s->length >= 0 && (long)n != s->length) return 0;
    if (s->min_length >= 0 && (long)n < s->min_length) return 0;
    if (s->max_length >= 0 && (long)n > s->max_length) return 0;

    /* Numeric min/max when the value parses as a double. */
    char* end = NULL;
    double d = strtod(v, &end);
    if (end && *end == 0 && end != v) {
        if (s->min_inclusive && !(d >= strtod(s->min_inclusive, NULL)))
            return 0;
        if (s->min_exclusive && !(d > strtod(s->min_exclusive, NULL)))
            return 0;
        if (s->max_inclusive && !(d <= strtod(s->max_inclusive, NULL)))
            return 0;
        if (s->max_exclusive && !(d < strtod(s->max_exclusive, NULL)))
            return 0;
    }
    if (s->total_digits >= 0) {
        long total = 0, frac = 0, seen_frac = 0;
        for (const char* p = v; *p; p++) {
            if (isdigit((unsigned char)*p)) {
                total++;
                if (seen_frac) frac++;
            } else if (*p == '.') {
                seen_frac = 1;
            }
        }
        if (total > s->total_digits) return 0;
        if (s->fraction_digits >= 0 && frac > s->fraction_digits)
            return 0;
    }
    return 1;
}

int xsd_simple_valid(const XsdSimple* simple, const char* v) {
    if (!simple || !v) return 0;
    int base = base_valid(simple->base, v);
    if (base == 0) return 0;
    /* base == -1 (local base) delegates to the resolver's chain in
     * the caller; facets still apply here. */
    return facet_check(simple, v);
}

/* Pattern/enumeration defaults for whitespace — slice 3 grows the
 * whitespace facet (collapse/replace) into the value pipeline. */
const char* xsd_simple_whitespace(const XsdSimple* simple) {
    return simple ? simple->whitespace : NULL;
}
