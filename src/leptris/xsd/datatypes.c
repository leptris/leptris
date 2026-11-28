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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xsd_internal.h"

/* rng_regex.c — the XSD pattern dialect (libxml2-compatible). */
extern int rng_regex_matches(const char* pat, const char* text);

/* The terminal-builtin classes driving applicability. */
enum {
    XSD_TC_STRING = 0,  /* string family + anyURI */
    XSD_TC_BINARY,      /* hexBinary / base64Binary */
    XSD_TC_NAME,        /* QName / NOTATION */
    XSD_TC_NUMERIC,     /* decimal, integer derivations, float, double, precisionDecimal */
    XSD_TC_DATETIME,    /* dateTime, dateTimeStamp, time, date, g* */
    XSD_TC_DURATION,    /* duration, yearMonthDuration, dayTimeDuration */
    XSD_TC_BOOLEAN,
    XSD_TC_OTHER        /* anySimpleType/anyAtomicType/unknown — checks skip */
};


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
    int bounded; /* 0 = the derivation is unbounded (arbitrary
                  * precision) — only the lexical shape applies */
} NumType;

static const NumType k_numerics[] = {
    {"xs:integer", 0, 0, 0},
    {"xs:nonNegativeInteger", 0, 0, 0},
    {"xs:positiveInteger", 0, 0, 0},
    {"xs:nonPositiveInteger", 0, 0, 0},
    {"xs:negativeInteger", 0, 0, 0},
    {"xs:long", -9223372036854775807LL - 1, 9223372036854775807LL, 1},
    {"xs:int", -2147483648LL, 2147483647LL, 1},
    {"xs:short", -32768, 32767, 1},
    {"xs:byte", -128, 127, 1},
    {"xs:unsignedLong", 0, 0, 2}, /* 2^64-1: string-compared */
    {"xs:unsignedInt", 0, 4294967295LL, 1},
    {"xs:unsignedShort", 0, 65535, 1},
    {"xs:unsignedByte", 0, 255, 1},
    {NULL, 0, 0, 0},
};

/* #1883: the unbounded integer family keeps its SIGN class even
 * with arbitrary precision (positiveInteger excludes 0). */
static int unbounded_sign_ok(const char* name, const char* s) {
    int neg = *s == '-';
    int zero_only = 1;
    const char* d = (*s == '+' || *s == '-') ? s + 1 : s;
    for (; *d; d++)
        if (*d != '0') { zero_only = 0; break; }
    if (strcmp(name, "xs:integer") == 0) return 1;
    /* XSD §3.3.20 (leptris-rs#28, errE002): a negative sign is
     * legal where the value is ZERO — "-0" is a valid
     * nonNegativeInteger lexical */
    if (strcmp(name, "xs:nonNegativeInteger") == 0)
        return !neg || zero_only;
    if (strcmp(name, "xs:positiveInteger") == 0)
        return !neg && !zero_only;
    if (strcmp(name, "xs:nonPositiveInteger") == 0)
        return neg || zero_only;
    if (strcmp(name, "xs:negativeInteger") == 0)
        return neg && !zero_only;
    return 1;
}

/* unsignedLong's bound is 2^64-1 — beyond long long. Decimal
 * string compare against the exact bound. */
static int ulong_in_range(const char* s) {
    if (!xsd_signed_integer(s)) return 0;
    if (*s == '-') return 0;
    const char* digits = *s == '+' ? s + 1 : s;
    while (*digits == '0' && digits[1]) digits++;
    size_t nd = strlen(digits);
    static const char k_max[] = "18446744073709551615";
    if (nd < sizeof(k_max) - 1) return 1;
    if (nd > sizeof(k_max) - 1) return 0;
    return strcmp(digits, k_max) <= 0;
}

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
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end && *end) return 0;
    if (errno == ERANGE) return 0; /* #1883: 2^63 is outside long */
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
/* tz_required: the dateTimeStamp tail — the timezone is part of
 * the value space, never absent (#2155 §3.3.7). */
static int tz_required(const char** p) {
    if (**p != 'Z' && **p != '+' && **p != '-') return 0;
    if (**p == 'Z') { (*p)++; return **p == 0; }
    (*p)++;
    return two_digits(p) && *(*p) == ':' && (*p)++ && two_digits(p) &&
           **p == 0;
}
static int tz_optional(const char** p) {
    /* #1936: the absent timezone is only absent at END of string —
     * "2000-01-01garbage" used to pass the g-regional family. */
    if (**p != 'Z' && **p != '+' && **p != '-') return **p == 0;
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
/* #1883: calendar-aware day bounds — 1999-02-29 is invalid, the
 * leap years are not. */
static int ymd(const char** p, int need_month, int need_day) {
    if (!four_digits(p)) return 0;
    int month = 1, day = 1;
    if (need_month) {
        if (*(*p)++ != '-') return 0;
        if (!two_digits_bounded(p, 1, 12)) return 0;
        month = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
    }
    if (need_day) {
        if (*(*p)++ != '-') return 0;
        if (!two_digits_bounded(p, 1, 31)) return 0;
        day = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
    }
    if (need_day) {
        static const int k_days[13] = {0, 31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30, 31};
        int y = 0;
        const char* ys = (*p) - (need_month ? 5 : 2) - 4 -
                         (need_month ? 0 : 0);
        /* the year digits start at p - 4 - (month+day consumed) */
        (void)ys;
        /* recompute from the front: four_digits consumed exactly 4 */
        ys = (*p) - 4 - (need_month ? 3 : 0) - (need_day ? 3 : 0);
        for (int k = 0; k < 4; k++)
            y = y * 10 + (ys[k] - '0');
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        int dim = k_days[month] + (month == 2 ? leap : 0);
        if (day > dim) return 0;
    }
    return 1;
}
static int time_tail(const char** p, int need_tz) {
    /* #1883: hour 0-23 (24:00:00 is the end-of-day spelling and
     * must carry 00 minutes and 00 seconds), minute 0-59, second
     * 0-60 (60 = leap second). */
    if (!two_digits_bounded(p, 0, 24)) return 0;
    int hour = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
    if (*(*p)++ != ':') return 0;
    if (!two_digits_bounded(p, 0, 59)) return 0;
    int minute = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
    int second = 0;
    if (**p == ':') {
        (*p)++;
        if (!two_digits_bounded(p, 0, 60)) return 0;
        second = ((*p)[-2] - '0') * 10 + ((*p)[-1] - '0');
        if (**p == '.') {
            (*p)++;
            /* at least one fraction digit, then the timezone —
             * "12:00:00.123Z" (the fraction consumed digits until
             * the offset; the old whole-remainder check rejected
             * every fractional-seconds-with-tz spelling) */
            if (!isdigit((unsigned char)**p)) return 0;
            while (isdigit((unsigned char)**p)) (*p)++;
        }
    }
    if (hour == 24 && (minute != 0 || second != 0)) return 0;
    /* #1883: second 60 is the leap second — only at 23:59 */
    if (second == 60 && (hour != 23 || minute != 59)) return 0;
    return need_tz ? tz_required(p) : tz_optional(p);
}

/* ---- the built-in table ------------------------------------------ */

int xsd_builtin_known(const char* type) {
    /* xsd_builtin_valid returns -1 exactly when the type is not in
     * the tier-1 table; any other verdict proves existence. */
    return type && xsd_builtin_valid(type, "x") != -1;
}

/* #2150: builtin EXISTENCE (not validity) — the tier-1 table plus
 * the names that carry no lexical validator of their own
 * (QName/NOTATION), the list builtins, and the ur-types. */
int xsd_builtin_exists(const char* type) {
    if (!type) return 0;
    if (xsd_builtin_known(type)) return 1;
    return strcmp(type, "xs:QName") == 0 ||
           strcmp(type, "xs:NOTATION") == 0 ||
           strcmp(type, "xs:anySimpleType") == 0 ||
           strcmp(type, "xs:anyType") == 0 ||
           strcmp(type, "xs:anyAtomicType") == 0 ||
           strcmp(type, "xs:IDREFS") == 0 ||
           strcmp(type, "xs:NMTOKENS") == 0 ||
           strcmp(type, "xs:ENTITIES") == 0;
}

int xsd_builtin_valid(const char* type, const char* v) {
    if (!type || !v) return 0;
    if (strcmp(type, "xs:string") == 0) return 1;
    if (strcmp(type, "xs:normalizedString") == 0) return 1;
    if (strcmp(type, "xs:token") == 0) {
        /* #1883: whiteSpace=collapse is TOTAL — every string has a
         * collapsed value, so the lexical space is every string
         * (the old leading/trailing/double-space rejections
         * over-stricted Part 2 3.3.2). */
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
        strcmp(type, "xs:IDREF") == 0 || strcmp(type, "xs:ENTITY") == 0) {
        /* #1883: token-family collapse precedes the NCName check —
         * " id " validates as the NCName "id". */
        while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')
            v++;
        size_t vn = strlen(v);
        while (vn > 0 && (v[vn - 1] == ' ' || v[vn - 1] == '\t' ||
                          v[vn - 1] == '\n' || v[vn - 1] == '\r'))
            vn--;
        char* buf = (char*)malloc(vn + 1);
        if (!buf) return 0;
        memcpy(buf, v, vn);
        buf[vn] = 0;
        int rc = xsd_is_ncname(buf);
        free(buf);
        return rc;
    }
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
        /* XSD Part 2: anyURI's whiteSpace facet is collapse — trim
         * before the lexical check, and the collapsed empty string
         * is a legal anyURI (#1883: Jing accepts space-padded and
         * whitespace-only values; the openmath CD-loading corpus
         * pads CDURL elements). Interior control characters still
         * reject; interior spaces stay permitted. */
        while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r')
            v++;
        size_t n = strlen(v);
        while (n > 0 && (unsigned char)v[n - 1] <= 0x20) n--;
        if (n == 0) return 1;
        for (const char* p = v; p < v + n; p++)
            if ((unsigned char)*p < 0x20) return 0;
        return 1;
    }
    if (strcmp(type, "xs:QName") == 0) {
        const char* colon = strchr(v, ':');
        if (!colon) return xsd_is_ncname(v);
        size_t plen = (size_t)(colon - v);
        /* #1883: "xmlns" is a reserved prefix (Part 2 3.2.18) */
        if (plen == 5 && strncmp(v, "xmlns", 5) == 0) return 0;
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
        if (strcmp(type, t->name) != 0) continue;
        /* #1883: the integer family is arbitrary precision — the
         * sign class applies, the magnitude does not. */
        if (t->bounded == 0)
            return xsd_signed_integer(v) && unbounded_sign_ok(t->name, v);
        if (t->bounded == 2) return ulong_in_range(v);
        return value_in_range(v, t->lo, t->hi);
    }
    if (strcmp(type, "xs:float") == 0 || strcmp(type, "xs:double") == 0 ||
        strcmp(type, "xs:precisionDecimal") == 0) {
        /* #2155: precisionDecimal shares the float lexical shape —
         * decimal literals with the optional exponent plus the IEEE
         * specials INF/+INF/-INF/NaN (the suite's legal schemas
         * enumerate 10.01e2 and NaN on precisionDecimal, d3_3_4v14
         * / pdecimal006). */
        if (strcmp(v, "INF") == 0 || strcmp(v, "-INF") == 0 ||
            strcmp(v, "+INF") == 0 || strcmp(v, "NaN") == 0)
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
        if (!two_digits_bounded(&p, 1, 12)) return 0;
        if (*p == 0) return 1;
        if (*p++ != '-') return 0;
        /* the g-regional types all carry the optional timezone
         * (--02-28Z, --12-31-06:00 — the date_0 corpus family) */
        return two_digits_bounded(&p, 1, 31) && tz_optional(&p);
    }
    if (strcmp(type, "xs:gDay") == 0) {
        const char* p = v;
        /* the lexical is ---DD (THREE leading dashes) */
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        return two_digits_bounded(&p, 1, 31) && tz_optional(&p);
    }
    if (strcmp(type, "xs:gMonth") == 0) {
        const char* p = v;
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        if (!two_digits_bounded(&p, 1, 12)) return 0;
        /* #1936: XSD 1.0 first edition spelled the lexical --MM--
         * (the errata added --MM) — both are valid 1.0 forms, each
         * carrying the optional timezone. */
        if (p[0] == '-' && p[1] == '-') {
            const char* q = p + 2;
            return tz_optional(&q);
        }
        return tz_optional(&p);
    }
    if (strcmp(type, "xs:dateTime") == 0) {
        const char* p = v;
        if (*p == '-') p++;
        if (!ymd(&p, 1, 1)) return 0;
        if (*p != 'T' && *p != ' ') return 0;
        p++;
        return time_tail(&p, 0);
    }
    if (strcmp(type, "xs:dateTimeStamp") == 0) {
        /* #2155: dateTime with the timezone REQUIRED (§3.3.7) —
         * "2005-01-01T00:00:00" is a dateTime but not a stamp. */
        const char* p = v;
        if (*p == '-') p++;
        if (!ymd(&p, 1, 1)) return 0;
        if (*p != 'T' && *p != ' ') return 0;
        p++;
        return time_tail(&p, 1);
    }
    if (strcmp(type, "xs:time") == 0) {
        const char* p = v;
        return time_tail(&p, 0);
    }
    if (strcmp(type, "xs:yearMonthDuration") == 0 ||
        strcmp(type, "xs:dayTimeDuration") == 0) {
        /* #2155: the 1.1 duration family carries its OWN lexical
         * spaces (§3.4.26/§3.4.27) — yearMonthDuration admits only
         * the Y/M components, dayTimeDuration only D/H/M/S, with
         * at least one component and the single leading sign. */
        const char* p = v;
        int months_only = strcmp(type, "xs:yearMonthDuration") == 0;
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
            int saw_fraction = 0;
            if (*p == '.') {
                p++;
                saw_fraction = 1;
                while (isdigit((unsigned char)*p)) p++;
            }
            char unit = *p;
            if (!unit) return 0;
            /* fraction only on seconds (P200.5Y / P30.5D invalid) */
            if (saw_fraction && unit != 'S') return 0;
            if (in_time && unit == 'Y') return 0;
            if (!in_time && (unit == 'H' || unit == 'S')) return 0;
            if (months_only && (in_time || unit == 'D')) return 0;
            if (!months_only && unit == 'Y') return 0;
            /* the M unit is MINUTES inside T, MONTHS outside —
             * dayTimeDuration only forbids the months spelling */
            if (!months_only && !in_time && unit == 'M') return 0;
            p++;
            any = 1;
        }
        return any && *p == 0;
    }
    if (strcmp(type, "xs:duration") == 0) {
        /* PnYnMnDTnHnMnS, at least one component; optional sign. */
        const char* p = v;
        if (*p == '-' || *p == '+') p++;
        if (*p++ != 'P') return 0;
        int any = 0;
        int in_time = 0;
        int t_seen = 0;
        int any_time = 0;
        for (;;) {
            if (*p == 'T') {
                if (in_time) return 0;
                in_time = 1;
                t_seen = 1;
                p++;
                continue;
            }
            if (!isdigit((unsigned char)*p)) break;
            while (isdigit((unsigned char)*p)) p++;
            int saw_fraction = 0;
            if (*p == '.') {
                p++;
                saw_fraction = 1;
                while (isdigit((unsigned char)*p)) p++;
            }
            char unit = *p;
            if (!unit) return 0;
            /* #1883: fraction only on seconds (P200.5Y is invalid) */
            if (saw_fraction && unit != 'S') return 0;
            /* Months (date) and minutes (time) share 'M' — the
             * in_time flag disambiguates, so only the unambiguous
             * units are side-checked. */
            if (in_time && unit == 'Y') return 0;
            if (!in_time && (unit == 'H' || unit == 'S')) return 0;
            if (in_time) any_time = 1;
            p++;
            any = 1;
        }
        /* #1883: a bare T with no time component is invalid
         * (P1Y2MT) */
        if (t_seen && !any_time) return 0;
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
        /* #1883: the padded alphabet — '=' only terminates the
         * final group; a remainder of 1 byte cannot form a group
         * ("abcde"), and nothing may follow padding. #2184
         * (d3_3_17ii01): the RFC 2045 padding is MANDATORY — the
         * quantum must close ("123456" is not a lexical), so every
         * length carries exactly the padding its remainder implies. */
        size_t n = strlen(v);
        if (!n) return 1;
        size_t eq = 0;
        for (size_t i = 0; i < n; i++) {
            char c = v[i];
            if (c == '=') {
                eq++;
                continue;
            }
            if (eq) return 0; /* data after padding */
            if (!isalnum((unsigned char)c) && c != '+' && c != '/')
                return 0;
        }
        if (eq > 2) return 0;
        /* #2184 (d3_3_17ii01): the RFC 2045 quantum always CLOSES —
         * the length including its padding is a multiple of four
         * ("123456" is not a lexical). */
        if (n % 4 != 0) return 0;
        return 1;
    }
    return -1; /* not in the tier-1 table */
}

/* ---- facet checking on a captured simpleType --------------------- */

int xsd_simple_valid(const XsdSimple* simple, const char* v,
                     const char* terminal_base, int chain_is_list);

/* The base chain resolves to a built-in reference: validate the
 * lexical against it first. */
static int base_valid(const char* base, const char* v) {
    if (!base) return xsd_builtin_valid("xs:string", v);
    if (strncmp(base, "xs:", 3) == 0) return xsd_builtin_valid(base, v);
    return -1; /* local base — the resolver walks the chain */
}

static int facet_check_body(const XsdSimple* s, const char* v,
                            const char* terminal_base, int chain_is_list);
static int facet_check(const XsdSimple* s, const char* v,
                       const char* terminal_base, int chain_is_list) {
    /* #1936 (stG010): a LIST value space is whitespace=collapse —
     * element content arrives as "\n\tCA\n  " and the enumeration
     * /pattern facets must see the collapsed form ("CA"), not the
     * raw text. #2155 (d4_3_6v01): a restriction declaring
     * whiteSpace="collapse" collapses its own value space the same
     * way — whitespace-only content reduces to the empty string
     * before the facets run. */
    int list_valued = chain_is_list || s->deriv == XSD_SIMPLE_LIST ||
        (terminal_base &&
         (strcmp(terminal_base, "xs:IDREFS") == 0 ||
          strcmp(terminal_base, "xs:NMTOKENS") == 0 ||
          strcmp(terminal_base, "xs:ENTITIES") == 0));
    int collapse_declared = s->whitespace &&
        strcmp(s->whitespace, "collapse") == 0;
    if (!list_valued && !collapse_declared)
        return facet_check_body(s, v, terminal_base, chain_is_list);
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    size_t n = strlen(v);
    while (n > 0 && (v[n-1] == ' ' || v[n-1] == '\t' ||
                     v[n-1] == '\n' || v[n-1] == '\r'))
        n--;
    char* collapsed = (char*)malloc(n + 1);
    if (!collapsed)
        return facet_check_body(s, v, terminal_base, chain_is_list);
    size_t o = 0;
    int in_ws = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = v[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (!in_ws && o) collapsed[o++] = ' ';
            in_ws = 1;
        } else {
            collapsed[o++] = ch;
            in_ws = 0;
        }
    }
    if (o && collapsed[o-1] == ' ') o--;
    collapsed[o] = 0;
    int rc = facet_check_body(s, collapsed, terminal_base, chain_is_list);
    free(collapsed);
    return rc;
}

/* ---- temporal enumeration equality (leptris-rs#27) ---------------
 * Enumeration (§4.3.5) is an equality test over the VALUE space:
 * dateTime compares by instant (App. E), and the time/date/gMonth
 * family includes the timezone in the value so only the zero
 * spellings (Z / +00:00 / -00:00) normalize to each other. */

/* days from 1970-01-01 (Howard Hinnant's civil algorithm) */
static double xsd_days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (double)era * 146097.0 + (double)doe - 719468.0;
}

/* [-]yyyy-mm-ddThh:mm:ss(.f+)?(Z|(+|-)hh:mm)? -> seconds since the
 * epoch; 0 unless fully parsed (only well-formed lexicals reach
 * the comparison — anything else stays lexical-equal-only). */
static int xsd_parse_datetime_instant(const char* v, double* out) {
    const char* p = v;
    int neg_year = 0;
    if (*p == '-') {
        neg_year = 1;
        p++;
    }
    long y = 0;
    int got = 0;
    while (*p >= '0' && *p <= '9') {
        y = y * 10 + (*p++ - '0');
        got++;
    }
    if (got < 4) return 0;
    if (*p++ != '-') return 0;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    int mo = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (mo < 1 || mo > 12) return 0;
    if (*p++ != '-') return 0;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    int da = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (da < 1 || da > 31) return 0;
    if (*p != 'T' && *p != ' ') return 0;
    p++;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    int hh = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (hh > 24) return 0;
    if (*p++ != ':') return 0;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    int mm = (p[0] - '0') * 10 + (p[1] - '0');
    p += 2;
    if (mm > 59) return 0;
    if (*p++ != ':') return 0;
    double ss = 0;
    got = 0;
    while (*p >= '0' && *p <= '9') {
        ss = ss * 10 + (*p++ - '0');
        got++;
    }
    if (!got) return 0;
    if (*p == '.') {
        p++;
        double f = 0.1;
        while (*p >= '0' && *p <= '9') {
            ss += (*p++ - '0') * f;
            f /= 10.0;
        }
    }
    long tzo = 0;
    if (*p == 'Z') {
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        p++;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
            return 0;
        int zh = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (*p++ != ':') return 0;
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9')
            return 0;
        int zm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (zh > 14 || zm > 59) return 0;
        tzo = sign * (zh * 60 + zm);
    }
    if (*p) return 0;
    if (neg_year) y = -y;
    *out = xsd_days_from_civil(y, (unsigned)mo, (unsigned)da) *
               86400.0 +
           hh * 3600.0 + mm * 60.0 + ss - (double)tzo * 60.0;
    return 1;
}

static int xsd_datetime_instant_equal(const char* a, const char* b) {
    double ia = 0, ib = 0;
    if (!xsd_parse_datetime_instant(a, &ia)) return 0;
    if (!xsd_parse_datetime_instant(b, &ib)) return 0;
    return ia == ib;
}

static void xsd_normalize_zero_tz(const char* s, char* out, size_t cap) {
    size_t n = strlen(s);
    const char* tail = (n >= 6) ? s + n - 6 : NULL;
    if (tail && (strcmp(tail, "+00:00") == 0 ||
                 strcmp(tail, "-00:00") == 0)) {
        if (n - 6 + 1 < cap) {
            memcpy(out, s, n - 6);
            out[n - 6] = 'Z';
            out[n - 5] = 0;
            return;
        }
    }
    snprintf(out, cap, "%s", s);
}

static int xsd_tznormalized_equal(const char* a, const char* b) {
    char na[256], nb[256];
    xsd_normalize_zero_tz(a, na, sizeof na);
    xsd_normalize_zero_tz(b, nb, sizeof nb);
    return strcmp(na, nb) == 0;
}

/* The value carries an explicit timezone — a trailing Z or a
 * [+-]hh:mm tail (fractional seconds allowed before it). */
static int xsd_value_has_tz(const char* v) {
    size_t n = strlen(v);
    if (!n) return 0;
    if (v[n - 1] == 'Z') return 1;
    if (n >= 6 && (v[n - 6] == '+' || v[n - 6] == '-') &&
        v[n - 3] == ':' && isdigit((unsigned char)v[n - 5]) &&
        isdigit((unsigned char)v[n - 4]) &&
        isdigit((unsigned char)v[n - 2]) &&
        isdigit((unsigned char)v[n - 1]))
        return 1;
    return 0;
}
/* precisionDecimal's spelling-claimed scale (mantissa fraction
 * digits minus the exponent; specials have none) — defined below. */
static int xsd_precision_scale(const char* v, long* out);
/* the exact decimal parse (digits/exp/sign/inf) — defined below. */
static int xsd_decimal_parse_exact(const char* v, char* digits,
                                   size_t cap, long* exp, int* sign,
                                   int* inf);
static int facet_check_body(const XsdSimple* s, const char* v,
                            const char* terminal_base,
                            int chain_is_list) {
    /* several patterns are a disjunction (p16: same as [abc]+|[123]+) */
    if (s->patterns) {
        int matched = 0;
        for (XsdFacetValue* p = s->patterns; p; p = p->next)
            if (rng_regex_matches(p->text, v)) {
                matched = 1;
                break;
            }
        if (!matched) return 0;
    }
    if (s->enum_values) {
        int found = 0;
        int is_dt = terminal_base &&
                    (strcmp(terminal_base, "xs:dateTime") == 0 ||
                     strcmp(terminal_base, "xs:dateTimeStamp") == 0);
        int is_tzval = terminal_base &&
                       (strcmp(terminal_base, "xs:time") == 0 ||
                        strcmp(terminal_base, "xs:date") == 0 ||
                        strcmp(terminal_base, "xs:gYearMonth") == 0 ||
                        strcmp(terminal_base, "xs:gYear") == 0 ||
                        strcmp(terminal_base, "xs:gMonthDay") == 0 ||
                        strcmp(terminal_base, "xs:gDay") == 0 ||
                        strcmp(terminal_base, "xs:gMonth") == 0);        int is_dur = terminal_base &&
                     (xsd_terminal_class(terminal_base) ==
                      XSD_TC_DURATION);
        /* precisionDecimal's arithmetic equality: NaN equals
         * NOTHING, not even itself — an enumerated NaN admits no
         * value (pdecimal006.n2); float/double keep the identity
         * reading (simple022). */
        int nan_never_matches =
            terminal_base &&
            strcmp(terminal_base, "xs:precisionDecimal") == 0 &&
            (strcmp(v, "NaN") == 0);
        /* numerics compare in the VALUE space — the enumerated
         * "123456.78" admits "12345678e-2" (d3_3_4v18), "0" admits
         * "-0.0"/"10e-1" (pdecimal006.v1) */
        int is_num =
            terminal_base && xsd_terminal_class(terminal_base) ==
                                 XSD_TC_NUMERIC;
        for (XsdFacetValue* e = s->enum_values; e; e = e->next) {
            if (!nan_never_matches && strcmp(e->text, v) == 0) {
                found = 1;
                break;
            }
            if (!found && is_dt &&
                xsd_datetime_instant_equal(e->text, v)) {
                found = 1;
                break;
            }
            if (!found && is_tzval && xsd_tznormalized_equal(e->text, v)) {
                found = 1;
                break;
            }
            /* the g-family and time enumerations match in the
             * VALUE space too — "-0000-02" equals the enumerated
             * "0000-02" (negative-zero year), "02:00:00-05:00"
             * equals "07:00:00Z" (same instant; zone202/206) */
            if (!found && (is_dt || is_tzval) &&
                xsd_facet_value_compare(terminal_base, e->text, v) ==
                    0) {
                found = 1;
                break;
            }
            if (!found && is_dur &&
                xsd_facet_value_compare(terminal_base, e->text, v) ==
                    0) {
                found = 1;
                break;
            }
            if (!found && is_num &&
                !nan_never_matches &&
                strcmp(e->text, "NaN") != 0 &&
                xsd_facet_value_compare(terminal_base, e->text, v) ==
                    0) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    /* Length family: character count (string-family semantics) —
     * except the binary types, where XML Schema Part 2 counts
     * OCTETS (the wml ST_LongHexNumber shape: hexBinary + length 4
     * accepts 8 hex digits; leptris-ruby#434). #1882: the facets
     * measure the VALUE SPACE — list types count whitespace-
     * separated items, and QName/NOTATION have no length family
     * at all (Part 2 sections 4.3.1-4.3.3 applicability). */
    size_t n = strlen(v);
    if (chain_is_list || s->deriv == XSD_SIMPLE_LIST ||
        (terminal_base &&
         (strcmp(terminal_base, "xs:IDREFS") == 0 ||
          strcmp(terminal_base, "xs:NMTOKENS") == 0 ||
          strcmp(terminal_base, "xs:ENTITIES") == 0))) {
        size_t items = 0;
        int in_item = 0;
        for (const char* q = v; *q; q++) {
            if (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                in_item = 0;
            else if (!in_item) {
                in_item = 1;
                items++;
            }
        }
        n = items;
    }
    if (terminal_base && (strcmp(terminal_base, "xs:hexBinary") == 0 ||
                          strcmp(terminal_base, "xs:base64Binary") == 0)) {
        if (strcmp(terminal_base, "xs:hexBinary") == 0) {
            n = n / 2;
        } else {
            size_t pads = 0;
            while (n > 0 && v[n - 1] == '=') {
                pads++;
                n--;
            }
            size_t octets = (n / 4) * 3;
            if (n % 4 == 2) octets += 1;
            else if (n % 4 == 3) octets += 2;
            n = octets;
        }
    }
    /* #1882: QName/NOTATION have no length family — the facets
     * do not apply (Part 2 sections 4.3.1-4.3.3). */
    if (!(terminal_base &&
          (strcmp(terminal_base, "xs:QName") == 0 ||
           strcmp(terminal_base, "xs:NOTATION") == 0))) {
        if (s->length >= 0 && (long)n != s->length) return 0;
        if (s->min_length >= 0 && (long)n < s->min_length) return 0;
        if (s->max_length >= 0 && (long)n > s->max_length) return 0;
    }

    /* #2155: the explicitTimezone facet's value posture — a
     * required timezone must be present, a prohibited one must be
     * absent ("optional" imposes nothing). Runs BEFORE the bound
     * dispatch's early return. */
    if (s->explicit_timezone) {
        int has_tz = xsd_value_has_tz(v);
        if (strcmp(s->explicit_timezone, "required") == 0 && !has_tz)
            return 0;
        if (strcmp(s->explicit_timezone, "prohibited") == 0 && has_tz)
            return 0;
    }
    /* #2155: the ordered bounds compare in the VALUE space —
     * family keys for date/time (timezone-normalized, year zero
     * included), total months/seconds for the duration family,
     * EXACT arithmetic for the decimal-shaped numerics (strtod
     * rounds 4.999999999999999999999 into 5). NaN (incomparable)
     * satisfies no order constraint — it violates every bound
     * (pdecimal002.n3/003.n1/004.n3/005.n3). */
    int tclass =
        terminal_base ? xsd_terminal_class(terminal_base) : XSD_TC_OTHER;
    int decimal_shaped =
        tclass == XSD_TC_NUMERIC && terminal_base &&
        strcmp(terminal_base, "xs:float") != 0 &&
        strcmp(terminal_base, "xs:double") != 0;
    if (tclass == XSD_TC_DATETIME || tclass == XSD_TC_DURATION) {
        int c;
        /* minInclusive/maxInclusive admit equality; the exclusive
         * pair does not. NaN (incomparable, -2) satisfies no order
         * constraint — it violates every bound. */
        if (s->min_inclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->min_inclusive)) != 1 &&
            c != 0)
            return 0;
        if (s->min_exclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->min_exclusive)) != 1)
            return 0;
        if (s->max_inclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->max_inclusive)) != -1 &&
            c != 0)
            return 0;
        if (s->max_exclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->max_exclusive)) != -1)
            return 0;
        return 1; /* no digit family on the temporal terminals */
    }
    if (decimal_shaped) {
        int c;
        if (s->min_inclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->min_inclusive)) != 1 &&
            c != 0)
            return 0;
        if (s->min_exclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->min_exclusive)) != 1)
            return 0;
        if (s->max_inclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->max_inclusive)) != -1 &&
            c != 0)
            return 0;
        if (s->max_exclusive &&
            (c = xsd_facet_value_compare(terminal_base, v,
                                         s->max_exclusive)) != -1)
            return 0;
        /* the scale window rides the SPELLING ("2.0e-3" claims
         * scale 4, "2e4" claims -4; the specials have none) */
        if (terminal_base &&
            (strcmp(terminal_base, "xs:precisionDecimal") == 0 ||
             strcmp(terminal_base, "xs:decimal") == 0) &&
            (s->min_scale != -1 || s->max_scale != -1)) {
            long scale;
            if (xsd_precision_scale(v, &scale)) {
                if (s->min_scale != -1 && scale < s->min_scale)
                    return 0;
                if (s->max_scale != -1 && scale > s->max_scale) return 0;
            }
        }
        /* the exact path decided; the digit counts still apply */
    } else if (tclass == XSD_TC_NUMERIC) {
        /* float/double: the binary approximate space */
        char* end = NULL;
        double d = strtod(v, &end);
        if (end && *end == 0 && end != v) {
            if (s->min_inclusive &&
                !(d >= strtod(s->min_inclusive, NULL)))
                return 0;
            if (s->min_exclusive &&
                !(d > strtod(s->min_exclusive, NULL)))
                return 0;
            if (s->max_inclusive &&
                !(d <= strtod(s->max_inclusive, NULL)))
                return 0;
            if (s->max_exclusive && !(d < strtod(s->max_exclusive, NULL)))
                return 0;
        }
    }
    if (s->total_digits >= 0) {
        if (terminal_base &&
            strcmp(terminal_base, "xs:precisionDecimal") == 0) {
            /* precisionDecimal's ARITHMETIC value space counts the
             * canonical decimal's digits: the mantissa with leading
             * zeros dropped (zero -> one digit), the "0." prefix
             * kept when |v| < 1, the fraction read off the exponent
             * ("12345678e-2" carries 8 total and 2 fraction digits;
             * "1234.0" carries 5; the specials carry none —
             * pdecimal008 / d3_3_4v18). Plain xs:decimal keeps the
             * lexical count below (the 1.0 oracle reads it). */
            char digits[512];
            long exp10 = 0;
            int sign = 1, inf = 0;
            if (xsd_decimal_parse_exact(v, digits, sizeof(digits),
                                        &exp10, &sign, &inf) &&
                !inf) {
                /* the mantissa's digits carry the precision: leading
                 * zeros drop, trailing zeros stay ("1234.0" claims
                 * 5, "0.000000" claims 1, "1.234e-20" claims 4) */
                long total = (long)strlen(digits);
                long frac = exp10 < 0 ? -exp10 : 0;
                if (total > s->total_digits) return 0;
                if (s->fraction_digits >= 0 &&
                    frac > s->fraction_digits)
                    return 0;
            }
        } else {
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
    }
    return 1;
}

int xsd_simple_valid(const XsdSimple* simple, const char* v,
                     const char* terminal_base, int chain_is_list) {
    if (!simple || !v) return 0;
    int base = base_valid(simple->base, v);
    if (base == 0) return 0;
    /* base == -1 (local base) delegates to the resolver's chain in
     * the caller; facets still apply here. */
    return facet_check(simple, v, terminal_base, chain_is_list);
}

/* ---- #2155: the 1.1 facet-applicability and value-space table -----
 *
 * XSD 1.1 Part 2 registers four new builtins (dateTimeStamp,
 * precisionDecimal, yearMonthDuration, dayTimeDuration) and two new
 * facets (minScale/maxScale, explicitTimezone). The COMPILE-time
 * facet applicability (which facets are legal on which type) and
 * the ordered-facet VALUE space (instants, total durations, exact
 * numerics — year zero included) live here; compile.c orchestrates
 * the walk and the error reporting. */

static int name_in(const char* t, const char* const* list) {
    for (int i = 0; list[i]; i++)
        if (strcmp(t, list[i]) == 0) return 1;
    return 0;
}

/* The integer derivations (xs:decimal and precisionDecimal are NOT
 * integers — their scale is unrestricted/facet-driven). */
static int xsd_is_integer_family(const char* t) {
    static const char* ints[] = {
        "xs:integer", "xs:long", "xs:int", "xs:short", "xs:byte",
        "xs:nonNegativeInteger", "xs:positiveInteger",
        "xs:nonPositiveInteger", "xs:negativeInteger", "xs:unsignedLong",
        "xs:unsignedInt", "xs:unsignedShort", "xs:unsignedByte", NULL};
    return name_in(t, ints);
}

int xsd_terminal_class(const char* t) {
    static const char* strings[] = {
        "xs:string", "xs:normalizedString", "xs:token", "xs:language",
        "xs:Name", "xs:NMTOKEN", "xs:NCName", "xs:ID", "xs:IDREF",
        "xs:ENTITY", "xs:anyURI", NULL};
    static const char* binaries[] = {"xs:hexBinary", "xs:base64Binary",
                                     NULL};
    static const char* numerics[] = {
        "xs:decimal", "xs:integer", "xs:long", "xs:int", "xs:short",
        "xs:byte", "xs:nonNegativeInteger", "xs:positiveInteger",
        "xs:nonPositiveInteger", "xs:negativeInteger", "xs:unsignedLong",
        "xs:unsignedInt", "xs:unsignedShort", "xs:unsignedByte",
        "xs:float", "xs:double", "xs:precisionDecimal", NULL};
    static const char* datetimes[] = {
        "xs:dateTime", "xs:dateTimeStamp", "xs:time", "xs:date",
        "xs:gYearMonth", "xs:gYear", "xs:gMonthDay", "xs:gDay",
        "xs:gMonth", NULL};
    static const char* durations[] = {
        "xs:duration", "xs:yearMonthDuration", "xs:dayTimeDuration",
        NULL};
    if (!t) return XSD_TC_OTHER;
    if (name_in(t, strings)) return XSD_TC_STRING;
    if (name_in(t, binaries)) return XSD_TC_BINARY;
    if (strcmp(t, "xs:QName") == 0 || strcmp(t, "xs:NOTATION") == 0)
        return XSD_TC_NAME;
    if (name_in(t, numerics)) return XSD_TC_NUMERIC;
    if (name_in(t, datetimes)) return XSD_TC_DATETIME;
    if (name_in(t, durations)) return XSD_TC_DURATION;
    if (strcmp(t, "xs:boolean") == 0) return XSD_TC_BOOLEAN;
    return XSD_TC_OTHER;
}

/* The 1.1-only builtins: the facet tables these carry are new in
 * XSD 1.1 — the compile-time applicability/value/narrowing gates
 * key on them (the 1.0 lane keeps its posture). */
int xsd_builtin_is_11(const char* t) {
    return t != NULL &&
           (strcmp(t, "xs:dateTimeStamp") == 0 ||
            strcmp(t, "xs:precisionDecimal") == 0 ||
            strcmp(t, "xs:yearMonthDuration") == 0 ||
            strcmp(t, "xs:dayTimeDuration") == 0);
}

/* Is the facet legal on a restriction resolving to `t`? 0 = schema
 * error (facet-applicability). The universal facets (pattern,
 * enumeration, whiteSpace, assertion) apply everywhere. */
int xsd_facet_applicable(const char* t, const char* facet,
                         int chain_is_list) {
    int length_fam = strcmp(facet, "length") == 0 ||
                     strcmp(facet, "minLength") == 0 ||
                     strcmp(facet, "maxLength") == 0;
    int ordered = strcmp(facet, "minInclusive") == 0 ||
                  strcmp(facet, "minExclusive") == 0 ||
                  strcmp(facet, "maxInclusive") == 0 ||
                  strcmp(facet, "maxExclusive") == 0;
    if (strcmp(facet, "pattern") == 0 ||
        strcmp(facet, "enumeration") == 0 ||
        strcmp(facet, "whiteSpace") == 0 ||
        strcmp(facet, "assertion") == 0)
        return 1;
    if (strcmp(facet, "totalDigits") == 0) {
        /* decimal, the integer derivations and precisionDecimal —
         * not float/double, not the non-numeric families */
        return xsd_terminal_class(t) == XSD_TC_NUMERIC &&
               (strncmp(t, "xs:decimal", 11) == 0 ||
                xsd_is_integer_family(t) ||
                strcmp(t, "xs:precisionDecimal") == 0);
    }
    if (strcmp(facet, "fractionDigits") == 0) {
        /* decimal + the integer family only — precisionDecimal
         * spells precision with minScale/maxScale (d3_3_4si20) */
        return xsd_terminal_class(t) == XSD_TC_NUMERIC &&
               (strncmp(t, "xs:decimal", 11) == 0 ||
                xsd_is_integer_family(t));
    }
    if (strcmp(facet, "minScale") == 0 || strcmp(facet, "maxScale") == 0) {
        /* the scale pair is decimal + precisionDecimal only (the
         * integer scale is fixed at 0) */
        return xsd_terminal_class(t) == XSD_TC_NUMERIC &&
               (strncmp(t, "xs:decimal", 11) == 0 ||
                strcmp(t, "xs:precisionDecimal") == 0);
    }
    if (strcmp(facet, "explicitTimezone") == 0)
        return xsd_terminal_class(t) == XSD_TC_DATETIME;
    if (chain_is_list) {
        /* a LIST value space: the length family counts items; the
         * ordered facets and the digit counts do not apply
         * (d2_4_1_2si01) */
        return length_fam;
    }
    switch (xsd_terminal_class(t)) {
        case XSD_TC_STRING:
        case XSD_TC_BINARY:
            return length_fam;
        case XSD_TC_NAME:
            /* QName/NOTATION carry no length family (#1882) */
            return 0;
        case XSD_TC_NUMERIC:
        case XSD_TC_DATETIME:
            return ordered; /* no length family on values */
        case XSD_TC_DURATION:
            /* the duration family is ordered but carries no length
             * facets (d3_4_26si01 / d3_4_27si01) */
            return ordered;
        case XSD_TC_BOOLEAN:
            return 0;
        default:
            return 1;
    }
}

/* The facet VALUE's own lexical validity: ordered bounds and
 * enumeration entries must be lexicals of the terminal builtin,
 * the digit/length families are nonNegativeInteger, the scale pair
 * is INTEGER (negative scales are legal — precisionDecimal's
 * precision extends past the decimal point, pdecimal016), and the
 * new-facet domains are closed. */
int xsd_facet_value_valid(const char* t, const char* facet,
                          const char* value, int chain_is_list) {
    if (!value) return 0;
    if (strcmp(facet, "length") == 0 || strcmp(facet, "minLength") == 0 ||
        strcmp(facet, "maxLength") == 0 ||
        strcmp(facet, "totalDigits") == 0 ||
        strcmp(facet, "fractionDigits") == 0) {
        if (!*value) return 0;
        for (const char* p = value; *p; p++)
            if (!isdigit((unsigned char)*p)) return 0;
        return 1;
    }
    if (strcmp(facet, "minScale") == 0 || strcmp(facet, "maxScale") == 0)
        return xsd_signed_integer(value);
    if (strcmp(facet, "whiteSpace") == 0)
        return strcmp(value, "preserve") == 0 ||
               strcmp(value, "replace") == 0 ||
               strcmp(value, "collapse") == 0;
    if (strcmp(facet, "explicitTimezone") == 0)
        return strcmp(value, "required") == 0 ||
               strcmp(value, "optional") == 0 ||
               strcmp(value, "prohibited") == 0;
    if (strcmp(facet, "enumeration") == 0) {
        /* a LIST's enumeration entries are whole-list lexicals —
         * their item-level typing rides the item type, skip here */
        if (chain_is_list) return 1;
        if (xsd_terminal_class(t) == XSD_TC_NAME)
            return 1; /* QName enums need the in-scope namespaces */
        return xsd_builtin_valid(t, value);
    }
    if (strcmp(facet, "minInclusive") == 0 ||
        strcmp(facet, "minExclusive") == 0 ||
        strcmp(facet, "maxInclusive") == 0 ||
        strcmp(facet, "maxExclusive") == 0) {
        int cls = xsd_terminal_class(t);
        if (cls == XSD_TC_NUMERIC || cls == XSD_TC_DATETIME ||
            cls == XSD_TC_DURATION)
            return xsd_builtin_valid(t, value);
        return 1;
    }
    return 1;
}

/* A duration-family lexical as (total months, total seconds) —
 * 0 unless the spelling is a fully valid duration. */
static int xsd_duration_value(const char* v, double* months,
                              double* secs) {
    const char* p = v;
    double sign = 1.0;
    if (*p == '-') {
        sign = -1.0;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (*p++ != 'P') return 0;
    double m = 0, s = 0;
    int any = 0, in_time = 0, t_seen = 0, any_time = 0;
    for (;;) {
        if (*p == 'T') {
            if (in_time) return 0;
            in_time = 1;
            t_seen = 1;
            p++;
            continue;
        }
        if (!isdigit((unsigned char)*p)) break;
        double val = 0;
        while (isdigit((unsigned char)*p)) {
            val = val * 10 + (*p++ - '0');
        }
        int frac = 0;
        if (*p == '.') {
            p++;
            frac = 1;
            double f = 0.1;
            while (isdigit((unsigned char)*p)) {
                val += (*p++ - '0') * f;
                f /= 10.0;
            }
        }
        char unit = *p;
        if (!unit) return 0;
        if (frac && unit != 'S') return 0;
        if (in_time && unit == 'Y') return 0;
        if (!in_time && (unit == 'H' || unit == 'S')) return 0;
        switch (unit) {
            case 'Y': m += val * 12.0; break;
            case 'M':
                if (in_time)
                    s += val * 60.0;
                else
                    m += val;
                break;
            case 'D': s += val * 86400.0; break;
            case 'H': s += val * 3600.0; break;
            case 'S': s += val; break;
            default: return 0;
        }
        if (in_time) any_time = 1;
        p++;
        any = 1;
    }
    if (t_seen && !any_time) return 0;
    if (!any || *p) return 0;
    *months = sign * m;
    *secs = sign * s;
    return 1;
}

/* The date/time family's comparable key (#2155): each member's own
 * total order, timezone folded and year zero included —
 * dateTime/date/dateTimeStamp by instant, time by the local
 * seconds-of-day minus the offset, gYear/gYearMonth by era days,
 * gMonthDay/gDay/gMonth by their within-year keys. 0 unless `v` is
 * a fully valid lexical of the family. */
static int xsd_scan_tz(const char** p, long* tzo) {
    *tzo = 0;
    if (**p == 'Z') {
        (*p)++;
        return **p == 0;
    }
    if (**p == '+' || **p == '-') {
        int sign = (**p == '+') ? 1 : -1;
        (*p)++;
        if (p[0][0] < '0' || p[0][0] > '9' || p[0][1] < '0' ||
            p[0][1] > '9')
            return 0;
        int zh = (p[0][0] - '0') * 10 + (p[0][1] - '0');
        *p += 2;
        long zm = 0;
        if (**p == ':') {
            (*p)++;
            if (p[0][0] < '0' || p[0][0] > '9' || p[0][1] < '0' ||
                p[0][1] > '9')
                return 0;
            zm = (p[0][0] - '0') * 10 + (p[0][1] - '0');
            *p += 2;
        }
        if (zh > 14 || zm > 59) return 0;
        *tzo = sign * (zh * 60 + zm);
    }
    return **p == 0;
}

static int xsd_scan_digits(const char** p, int n, long* out) {
    long v = 0;
    for (int i = 0; i < n; i++) {
        if ((*p)[0] < '0' || (*p)[0] > '9') return 0;
        v = v * 10 + ((*p)[0] - '0');
        (*p)++;
    }
    *out = v;
    return 1;
}

static int xsd_temporal_key(const char* t, const char* v, double* out) {
    const char* p = v;
    int neg = 0;
    long tzo = 0;
    if (strcmp(t, "xs:time") == 0) {
        long h, m, s;
        if (!xsd_scan_digits(&p, 2, &h)) return 0;
        if (*p++ != ':') return 0;
        if (!xsd_scan_digits(&p, 2, &m)) return 0;
        if (*p++ != ':') return 0;
        if (!xsd_scan_digits(&p, 2, &s)) return 0;
        double frac = 0;
        if (*p == '.') {
            p++;
            double f = 0.1;
            while (*p >= '0' && *p <= '9') {
                frac += (*p++ - '0') * f;
                f /= 10.0;
            }
        }
        if (h > 24 || m > 59 || s > 60) return 0;
        if (h == 24 && (m != 0 || s != 0)) return 0;
        if (!xsd_scan_tz(&p, &tzo)) return 0;
        *out = h * 3600.0 + m * 60.0 + s + frac - (double)tzo * 60.0;
        return 1;
    }
    if (strcmp(t, "xs:gDay") == 0 || strcmp(t, "xs:gMonth") == 0 ||
        strcmp(t, "xs:gMonthDay") == 0) {
        /* the g-regional family: their own within-year order */
        long mo = 1, da = 1;
        if (*p++ != '-') return 0;
        if (*p++ != '-') return 0;
        if (strcmp(t, "xs:gDay") == 0) {
            if (*p++ != '-') return 0;
            if (!xsd_scan_digits(&p, 2, &da) || da < 1 || da > 31)
                return 0;
        } else {
            if (!xsd_scan_digits(&p, 2, &mo) || mo < 1 || mo > 12)
                return 0;
            if (strcmp(t, "xs:gMonthDay") == 0) {
                if (*p++ != '-') return 0;
                if (!xsd_scan_digits(&p, 2, &da) || da < 1 || da > 31)
                    return 0;
            } else if (p[0] == '-' && p[1] == '-') {
                /* the 1.0 first-edition --MM-- spelling */
                p += 2;
            }
        }
        if (!xsd_scan_tz(&p, &tzo)) return 0;
        *out = (mo * 32.0 + da) * 86400.0 - (double)tzo * 60.0;
        return 1;
    }
    /* the year-leading family: dateTime/dateTimeStamp require the
     * T-tail, date/gYear/gYearMonth end (timezone-optional) */
    if (*p == '-') {
        neg = 1;
        p++;
    }
    long y = 0;
    int got = 0;
    while (*p >= '0' && *p <= '9') {
        y = y * 10 + (*p++ - '0');
        got++;
    }
    if (got < 4) return 0;
    long mo = 1, da = 1;
    int has_month = 0, has_day = 0;
    if (*p == '-') {
        p++;
        if (!xsd_scan_digits(&p, 2, &mo)) return 0;
        if (mo < 1 || mo > 12) return 0;
        has_month = 1;
        if (*p == '-') {
            p++;
            if (!xsd_scan_digits(&p, 2, &da)) return 0;
            if (da < 1 || da > 31) return 0;
            has_day = 1;
        }
    }
    double seconds = 0;
    int full_day = strcmp(t, "xs:dateTime") == 0 ||
                   strcmp(t, "xs:dateTimeStamp") == 0;
    if (full_day || *p == 'T' || *p == ' ') {
        if (*p != 'T' && *p != ' ') return 0;
        p++;
        long h, mi, s = 0;
        if (!xsd_scan_digits(&p, 2, &h)) return 0;
        if (*p++ != ':') return 0;
        if (!xsd_scan_digits(&p, 2, &mi)) return 0;
        if (h > 24 || mi > 59) return 0;
        if (*p == ':') {
            p++;
            if (!xsd_scan_digits(&p, 2, &s)) return 0;
            if (s > 60) return 0;
        }
        double frac = 0;
        if (*p == '.') {
            p++;
            double f = 0.1;
            while (*p >= '0' && *p <= '9') {
                frac += (*p++ - '0') * f;
                f /= 10.0;
            }
        }
        seconds = h * 3600.0 + mi * 60.0 + s + frac;
        if (s == 60 && h != 23) return 0;
        if (h == 24 && (mi != 0 || s != 0)) return 0;
        full_day = 1;
    }
    if (!xsd_scan_tz(&p, &tzo)) return 0;
    if (neg) y = -y;
    double days = xsd_days_from_civil(y, (unsigned)(has_month ? mo : 1),
                                      (unsigned)(has_day ? da : 1));
    *out = days * 86400.0 + seconds - (double)tzo * 60.0;
    return 1;
}

/* Exact decimal comparison for the decimal-shaped terminals (the
 * value space is ARITHMETIC — strtod rounds "4.999999999999999999999"
 * into 5.0, which wrongly collapses strict bounds). Parses into a
 * significant-digit string plus a power-of-ten; INF/+INF/-INF order
 * beyond every finite, NaN is incomparable. -2 = unparseable. */
static int xsd_decimal_parse_exact(const char* v, char* digits,
                                   size_t cap, long* exp, int* sign,
                                   int* inf) {
    const char* p = v;
    *sign = 1;
    *inf = 0;
    if (*p == '+') p++;
    else if (*p == '-') {
        *sign = -1;
        p++;
    }
    if (strcmp(p, "INF") == 0) {
        *inf = *sign;
        return 1;
    }
    if (strcmp(p, "NaN") == 0) return 0;
    size_t n = 0;
    long frac = 0, exp10 = 0;
    int seen_dot = 0;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            if (n + 1 >= cap) return 0;
            digits[n++] = *p;
            if (seen_dot) frac++;
            p++;
        } else if (*p == '.' && !seen_dot) {
            seen_dot = 1;
            p++;
        } else if ((*p == 'e' || *p == 'E') && n) {
            p++;
            int esign = 1;
            if (*p == '+') p++;
            else if (*p == '-') {
                esign = -1;
                p++;
            }
            if (!isdigit((unsigned char)*p)) return 0;
            long ev = 0;
            while (isdigit((unsigned char)*p)) {
                ev = ev * 10 + (*p++ - '0');
                if (ev > 1000000) return 0;
            }
            exp10 = esign * ev;
        } else {
            return 0;
        }
    }
    if (!n) return 0;
    digits[n] = 0;
    char* d = digits;
    while (*d == '0' && d[1]) {
        d++;
        n--;
    }
    if (n == 1 && d[0] == '0') {
        /* arithmetic zero: one spelling, sign dropped */
        digits[0] = '0';
        digits[1] = 0;
        *sign = 1;
        *exp = 0;
        return 1;
    }
    if (d != digits) memmove(digits, d, n + 1);
    *exp = exp10 - frac;
    return 1;
}

static int xsd_decimal_exact_cmp(const char* a, const char* b) {
    char da[512], db[512];
    long ea, eb;
    int sa, sb, infa, infb;
    if (!xsd_decimal_parse_exact(a, da, sizeof(da), &ea, &sa, &infa))
        return -2;
    if (!xsd_decimal_parse_exact(b, db, sizeof(db), &eb, &sb, &infb))
        return -2;
    if (infa || infb) {
        if (infa && infb)
            return infa == infb ? 0 : (infa > infb ? 1 : -1);
        /* one infinity against a finite: the infinity orders beyond
         * every finite magnitude */
        return infa ? infa : -infb;
    }
    int zero_a = strcmp(da, "0") == 0, zero_b = strcmp(db, "0") == 0;
    if (zero_a && zero_b) return 0;
    if (zero_a) return sb > 0 ? -1 : 1;
    if (zero_b) return sa > 0 ? 1 : -1;
    if (sa != sb) return sa < sb ? -1 : 1;
    /* same sign: magnitudes order by (high-digit position, digits) */
    long ha = ea + (long)strlen(da);
    long hb = eb + (long)strlen(db);
    int mag;
    if (ha != hb) {
        mag = ha > hb ? 1 : -1;
    } else {
        size_t la = strlen(da), lb = strlen(db);
        size_t m = la > lb ? la : lb;
        mag = 0;
        for (size_t i = 0; i < m && !mag; i++) {
            char ca = i < la ? da[i] : '0';
            char cb = i < lb ? db[i] : '0';
            if (ca != cb) mag = ca > cb ? 1 : -1;
        }
        if (!mag) return 0;
    }
    return sa > 0 ? mag : -mag;
}

/* precisionDecimal's scale (#2155): the number of fractional
 * digits the SPELLING claims — mantissa fraction digits minus the
 * exponent ("2.0e-3" has scale 4, "2e4" has scale -4; pdecimal010
 * /016/019/020). The specials have no scale. 0 unless v parses. */
static int xsd_precision_scale(const char* v, long* out) {
    const char* p = v;
    if (*p == '+' || *p == '-') p++;
    if (strcmp(p, "INF") == 0 || strcmp(p, "NaN") == 0) return 0;
    long frac = 0, exp10 = 0;
    int seen_dot = 0, seen_digit = 0;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            seen_digit = 1;
            if (seen_dot) frac++;
            p++;
        } else if (*p == '.' && !seen_dot) {
            seen_dot = 1;
            p++;
        } else if ((*p == 'e' || *p == 'E') && seen_digit) {
            p++;
            int esign = 1;
            if (*p == '+') p++;
            else if (*p == '-') {
                esign = -1;
                p++;
            }
            if (!isdigit((unsigned char)*p)) return 0;
            long ev = 0;
            while (isdigit((unsigned char)*p)) {
                ev = ev * 10 + (*p++ - '0');
                if (ev > 1000000) return 0;
            }
            exp10 = esign * ev;
        } else {
            return 0;
        }
    }
    if (!seen_digit) return 0;
    *out = frac - exp10;
    return 1;
}

/* The ordered-facet VALUE-space comparison (#2155 work item 2):
 * durations by total months/seconds, the date/time family by their
 * family keys (timezone-normalized, year zero included), numerics
 * exactly. Returns -1/0/1, or -2 when the pair is incomparable
 * (NaN, unparseable, or a non-ordered terminal) — callers MUST
 * treat -2 as "no violation". */
int xsd_facet_value_compare(const char* t, const char* a, const char* b) {
    int cls = xsd_terminal_class(t);
    if (cls == XSD_TC_DURATION) {
        double ma, sa, mb, sb;
        if (!xsd_duration_value(a, &ma, &sa)) return -2;
        if (!xsd_duration_value(b, &mb, &sb)) return -2;
        /* month first: the (months, seconds) pair orders
         * lexicographically through the fixed months->seconds
         * scaling, monotone over both components */
        double va = ma * 2592000.0 + sa;
        double vb = mb * 2592000.0 + sb;
        return (va < vb) ? -1 : (va > vb) ? 1 : 0;
    }
    if (cls == XSD_TC_DATETIME) {
        double ia, ib;
        if (!xsd_temporal_key(t, a, &ia)) return -2;
        if (!xsd_temporal_key(t, b, &ib)) return -2;
        return (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
    }
    if (cls == XSD_TC_NUMERIC) {
        /* the decimal-shaped terminals are ARITHMETIC — compare
         * exactly; float/double keep the binary approximate space */
        if (strcmp(t, "xs:float") != 0 && strcmp(t, "xs:double") != 0)
            return xsd_decimal_exact_cmp(a, b);
        char* e = NULL;
        double da = strtod(a, &e);
        if (!e || *e || e == a) return -2;
        e = NULL;
        double db = strtod(b, &e);
        if (!e || *e || e == b) return -2;
        if (da != da || db != db) return -2; /* NaN is unordered */
        return (da < db) ? -1 : (da > db) ? 1 : 0;
    }
    return -2;
}

/* Pattern/enumeration defaults for whitespace — slice 3 grows the
 * whitespace facet (collapse/replace) into the value pipeline. */
const char* xsd_simple_whitespace(const XsdSimple* simple) {
    return simple ? simple->whitespace : NULL;
}
