/* lib/src/xpath/collation.c - collation URI parsing and UCA compare
 * Copyright (c) 2024, Ribose Inc.
 *
 * Codepoint and html-ascii-case-insensitive collations are plain
 * byte/ASCII compares. UCA collations (LEPTRIS_ENABLE_DUCET) build
 * multi-level sort keys from the vendored DUCET table: NFD the input
 * (utf8proc), look up collation elements (single codepoints, plus
 * DUCET's 2/3-codepoint contractions), then emit primary, secondary
 * and tertiary levels separated by 0x00. Every weight is encoded in
 * a 0x01-range byte form so no level byte can be zero — the
 * separator stays unique and plain memcmp orders the keys.
 */

#include "collation.h"
#include "ducet_table.h"
#include "../common/port.h"
#if LEPTRIS_HAS_UTF8PROC
#include "../unicode/unicode.h"
#endif
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define UCA_URI "http://www.w3.org/2013/collation/UCA"
#define CODEPOINT_URI \
    "http://www.w3.org/2005/xpath-functions/collation/codepoint"
#define ASCII_CI_URI \
    "http://www.w3.org/2005/xpath-functions/collation/" \
    "html-ascii-case-insensitive"

int leptris_collation_from_uri(const char* uri, leptris_collation* out) {
    if (!uri || !out) return -1;
    leptris_collation c;
    memset(&c, 0, sizeof(c));

    if (strcmp(uri, CODEPOINT_URI) == 0) {
        c.kind = LEPTRIS_COLL_CODEPOINT;
        c.strength = LEPTRIS_COLL_STRENGTH_IDENTICAL;
        *out = c;
        return 0;
    }
    if (strcmp(uri, ASCII_CI_URI) == 0) {
        c.kind = LEPTRIS_COLL_ASCII_CI;
        c.strength = LEPTRIS_COLL_STRENGTH_IDENTICAL;
        *out = c;
        return 0;
    }

    size_t base_len = strlen(UCA_URI);
    size_t uri_len = strlen(uri);
    if (uri_len < base_len || strncmp(uri, UCA_URI, base_len) != 0 ||
        (uri_len > base_len && uri[base_len] != '?')) {
        return -1;
    }

    c.kind = LEPTRIS_COLL_UCA;
    c.strength = LEPTRIS_COLL_STRENGTH_TERTIARY;
    c.alternate = LEPTRIS_COLL_ALTERNATE_NON_IGNORABLE;

    /* F&O 3.x parameter defaults, parsed from the query string:
     * strength, alternate. Unknown values reject the URI. */
    /* The W3C UCA collation URI separates parameters with ';'
     * (optionally ';'), NOT '&'. */
    const char* q = uri + base_len; /* NUL, or "?..." */
    if (*q == '?') q++;
    while (*q) {
        const char* eq = strchr(q, '=');
        const char* semi = strchr(q, ';');
        const char* amp = strchr(q, '&');
        const char* sep = semi && (!amp || semi < amp) ? semi : amp;
        if (!eq || (sep && eq > sep)) return -1;
        const char* vend = sep ? sep : q + strlen(q);
        size_t vlen = (size_t)(vend - (eq + 1));

        if ((size_t)(eq - q) == 4 && strncmp(q, "lang", 4) == 0) {
            /* DUCET is language-neutral (the root locale): accept a
             * well-formed language tag and use the root weights. */
            int ok = vlen > 0;
            for (size_t i = 0; i < vlen && ok; i++)
                if (!isalnum((unsigned char)eq[1 + i]) &&
                    eq[1 + i] != '-' && eq[1 + i] != '_')
                    ok = 0;
            if (!ok) return -1;
        } else if ((size_t)(eq - q) == 9 &&
                   strncmp(q, "caseFirst", 9) == 0) {
            if (vlen == 5 && strncmp(eq + 1, "upper", 5) == 0)
                c.case_first = 1;
            else if (vlen == 5 && strncmp(eq + 1, "lower", 5) == 0)
                c.case_first = 2;
            else
                return -1;
        } else if ((size_t)(eq - q) == 8 &&
                   strncmp(q, "strength", 8) == 0) {
            if (vlen == 7 && strncmp(eq + 1, "primary", 7) == 0)
                c.strength = LEPTRIS_COLL_STRENGTH_PRIMARY;
            else if (vlen == 9 && strncmp(eq + 1, "secondary", 9) == 0)
                c.strength = LEPTRIS_COLL_STRENGTH_SECONDARY;
            else if (vlen == 8 && strncmp(eq + 1, "tertiary", 8) == 0)
                c.strength = LEPTRIS_COLL_STRENGTH_TERTIARY;
            else if (vlen == 10 && strncmp(eq + 1, "quaternary", 10) == 0)
                c.strength = LEPTRIS_COLL_STRENGTH_QUATERNARY;
            else if (vlen == 9 && strncmp(eq + 1, "identical", 9) == 0)
                c.strength = LEPTRIS_COLL_STRENGTH_IDENTICAL;
            else
                return -1;
        } else if ((size_t)(eq - q) == 9 && strncmp(q, "alternate", 9) == 0) {
            if (vlen == 14 && strncmp(eq + 1, "non-ignorable", 14) == 0)
                c.alternate = LEPTRIS_COLL_ALTERNATE_NON_IGNORABLE;
            else if (vlen == 7 && strncmp(eq + 1, "shifted", 7) == 0)
                c.alternate = LEPTRIS_COLL_ALTERNATE_SHIFTED;
            else if (vlen == 7 && strncmp(eq + 1, "blanked", 7) == 0)
                c.alternate = LEPTRIS_COLL_ALTERNATE_BLANKED;
            else
                return -1;
        } else {
            return -1; /* unknown parameter */
        }
        q = sep ? sep + 1 : vend;
    }

    *out = c;
    return 0;
}

/* ---- codepoint / ascii-ci ------------------------------------- */

static int ascii_ci_compare(const char* a, size_t alen, const char* b,
                            size_t blen) {
    size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (alen == blen) return 0;
    return alen < blen ? -1 : 1;
}

/* ---- UCA ------------------------------------------------------- */

/* One collation element: DUCET weights plus the variable flag. */
struct ce {
    uint16_t p, s, t;
    uint8_t var;
};

/* Binary search in ducet_cp; returns the entry index or (size_t)-1. */
static size_t ducet_lookup_single(uint32_t cp) {
    size_t lo = 0, hi = sizeof(ducet_cp) / sizeof(ducet_cp[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (ducet_cp[mid] == cp)
            return mid;
        if (ducet_cp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (size_t)-1;
}

/* Contraction table lookup: entries sorted by (a, b, c); c == 0
 * marks a 2-cp mapping. Returns the index or (size_t)-1. */
static size_t ducet_lookup_contraction(uint32_t a, uint32_t b, uint32_t c) {
    size_t n = sizeof(ducet_ct_a) / sizeof(ducet_ct_a[0]);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        uint32_t ka = ducet_ct_a[mid], kb = ducet_ct_b[mid],
                 kc = ducet_ct_c[mid];
        if (ka < a || (ka == a && (kb < b || (kb == b && kc < c))))
            lo = mid + 1;
        else if (ka == a && kb == b && kc == c)
            return mid;
        else
            hi = mid;
    }
    return (size_t)-1;
}

static void ce_from_weights(struct ce* out, size_t w_idx, uint8_t var) {
    out->p = ducet_w[w_idx * 3];
    out->s = ducet_w[w_idx * 3 + 1];
    out->t = ducet_w[w_idx * 3 + 2];
    out->var = var;
}

/* UCA 7.1.1 implicit weights for codepoints with no DUCET
 * mapping. The @implicitweights scripts (allkeys.txt) take their
 * registered FBxx base; everything else (unassigned) takes
 * 0xFB80 + 2*(cp >> 15) — coarse groups that order after all
 * assigned scripts. Per codepoint the algorithm emits two CEs:
 *   [base + 2*offset     .0020.0002]
 *   [base + 2*offset + 1 .0020.0004]
 * The even/odd stride keeps neighbors from colliding and orders
 * within a group by codepoint. Returns 2, or 1 when the trail CE
 * would overflow the 16-bit weight space. */
struct implicit_range {
    uint32_t lo, hi, base;
};
static const struct implicit_range implicit_ranges[] = {
    {0x17000, 0x18AFF, 0xFB00}, /* Tangut + Tangut Supplement */
    {0x18B00, 0x18CFF, 0xFB02}, /* Khitan Small Script */
    {0x1B170, 0x1B2FF, 0xFB01}, /* Nushu */
};

static int ce_implicit(struct ce* out, uint32_t cp) {
    uint32_t base = 0, offset = 0;
    int hit = 0;
    for (size_t i = 0; i < sizeof(implicit_ranges) /
                              sizeof(implicit_ranges[0]); i++) {
        if (cp >= implicit_ranges[i].lo && cp <= implicit_ranges[i].hi) {
            base = implicit_ranges[i].base;
            offset = cp - implicit_ranges[i].lo;
            hit = 1;
            break;
        }
    }
    if (!hit) {
        /* Unassigned: each 32k-codepoint group gets its own base
         * pair; the intra-group offset keeps codepoint order. */
        base = 0xFB80u + (cp >> 15) * 2u;
        offset = cp & 0x7FFFu;
    }
    uint32_t p1 = base + offset * 2u;
    if (p1 > 0xFFFF) p1 = 0xFFFF;
    out[0].p = (uint16_t)p1;
    out[0].s = 0x20;
    out[0].t = 0x2;
    out[0].var = 0;
    if (p1 >= 0xFFFF) return 1;
    out[1].p = (uint16_t)(p1 + 1);
    out[1].s = 0x20;
    out[1].t = 0x4;
    out[1].var = 0;
    return 2;
}

/* Decode one UTF-8 sequence; advances *s. Invalid bytes decode as
 * U+FFFD, one byte consumed. */
static uint32_t utf8_next(const unsigned char** s, const unsigned char* end) {
    const unsigned char* p = *s;
    if (p >= end) return (uint32_t)-1;
    unsigned char b = *p++;
    if (b < 0x80) {
        *s = p;
        return b;
    }
    size_t n;
    uint32_t cp;
    if ((b & 0xE0) == 0xC0) { n = 1; cp = b & 0x1Fu; }
    else if ((b & 0xF0) == 0xE0) { n = 2; cp = b & 0x0Fu; }
    else if ((b & 0xF8) == 0xF0) { n = 3; cp = b & 0x07u; }
    else { *s = p; return 0xFFFD; }
    if ((size_t)(end - p) < n) { *s = p; return 0xFFFD; }
    for (size_t i = 0; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s = p; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    *s = p + n;
    return cp;
}

/* Grow-only CE buffer. */
struct ce_buf {
    struct ce* v;
    size_t n, cap;
};

static int ce_buf_push(struct ce_buf* buf, const struct ce* ce) {
    if (buf->n == buf->cap) {
        size_t cap = buf->cap ? buf->cap * 2 : 64;
        struct ce* v = (struct ce*)realloc(buf->v, cap * sizeof(*v));
        if (!v) return -1;
        buf->v = v;
        buf->cap = cap;
    }
    buf->v[buf->n++] = *ce;
    return 0;
}

static int ces_from_string(const char* str, size_t len, struct ce_buf* buf) {
    /* DUCET lookups assume NFD; F&O default normalization=yes. */
    const char* nfd = str; /* borrow when no normalization possible */
    size_t nfd_len = len;
    char* owned = NULL;
#if LEPTRIS_HAS_UTF8PROC
    owned = leptris_unicode_normalize(str, len, LEPTRIS_UNICODE_NFD,
                                       &nfd_len);
    if (owned) nfd = owned;
#else
    /* A DUCET build without utf8proc cannot normalize; the CMake
     * gate makes this combination impossible, so this is only a
     * defensive fallback to the raw input. */
#endif

    const unsigned char* p = (const unsigned char*)nfd;
    const unsigned char* end = p + nfd_len;
    while (p < end) {
        uint32_t cp = utf8_next(&p, end);
        if (cp == (uint32_t)-1) break;
        const unsigned char* nxt = p;

        size_t matched = 0;      /* contraction index */
        size_t consumed = 0;     /* cps consumed */
        uint32_t c2 = 0, c3 = 0;
        if (nxt < end) {
            c2 = utf8_next(&nxt, end);
            if (c2 != (uint32_t)-1 && nxt < end) {
                c3 = utf8_next(&nxt, end);
                if (c3 == (uint32_t)-1) c3 = 0;
            } else {
                c2 = 0;
            }
        }

        if (c2 && c3) {
            size_t idx = ducet_lookup_contraction(cp, c2, c3);
            if (idx != (size_t)-1) {
                matched = idx;
                consumed = 3;
            }
        }
        if (!consumed && c2) {
            size_t idx = ducet_lookup_contraction(cp, c2, 0);
            if (idx != (size_t)-1) {
                matched = idx;
                consumed = 2;
            }
        }

        int oom = 0;
        if (consumed) {
            size_t off = ducet_ct_off[matched];
            for (uint8_t i = 0; i < ducet_ct_nce[matched]; i++) {
                struct ce ce;
                ce_from_weights(&ce, off + i, ducet_var[off + i]);
                oom |= ce_buf_push(buf, &ce);
            }
            /* advance past the consumed codepoints */
            p = (consumed == 3 && c3) ? nxt : (consumed == 2 ? nxt : p);
        } else {
            size_t idx = ducet_lookup_single(cp);
            if (idx != (size_t)-1) {
                size_t off = ducet_ce_off[idx];
                size_t off_end = ducet_ce_off[idx + 1]; /* sequential */
                for (size_t w = off; w < off_end; w++) {
                    struct ce ce;
                    ce_from_weights(&ce, w, ducet_var[w]);
                    oom |= ce_buf_push(buf, &ce);
                }
            } else {
                struct ce ces[2];
                int n_implicit = ce_implicit(ces, cp);
                for (int q = 0; q < n_implicit; q++)
                    oom |= ce_buf_push(buf, &ces[q]);
            }
        }
        if (oom) {
            free(owned);
            free(buf->v);
            buf->v = NULL;
            return -1;
        }
    }
    free(owned);
    return 0;
}

/* Order-preserving 0x01-range encoding of a 16-bit weight: three
 * bytes, each 0x01..0x80, so no key byte is 0x00. */
static void emit_weight(unsigned char** k, uint16_t w) {
    (*k)[0] = (unsigned char)(0x01 + ((w >> 14) & 0x03));
    (*k)[1] = (unsigned char)(0x01 + ((w >> 7) & 0x7F));
    (*k)[2] = (unsigned char)(0x01 + (w & 0x7F));
    *k += 3;
}

int leptris_collation_sortkey(const char* s, size_t len,
                              const leptris_collation* c,
                              unsigned char** out, size_t* out_len) {
    if (!s || !c || !out || !out_len) return -1;
    if (c->kind == LEPTRIS_COLL_ASCII_CI) {
        unsigned char* key =
            (unsigned char*)malloc(len ? len : 1);
        if (!key) return -1;
        for (size_t i = 0; i < len; i++) {
            unsigned char b = (unsigned char)s[i];
            if (b >= 'A' && b <= 'Z') b += 32;
            key[i] = b;
        }
        *out = key;
        *out_len = len;
        return 0;
    }
    if (c->kind != LEPTRIS_COLL_UCA) return -1;

    struct ce_buf buf = {NULL, 0, 0};
    if (ces_from_string(s, len, &buf) != 0) {
        free(buf.v);
        return -1;
    }

    /* Worst case per CE: primary + secondary + tertiary + (shifted)
     * quaternary weight = 4 weights x 3 bytes, plus separators. */
    size_t max = buf.n * 12 + 8;
    unsigned char* key = (unsigned char*)malloc(max);
    if (!key) {
        free(buf.v);
        return -1;
    }
    unsigned char* k = key;

    /* Primary. Under shifted/blanked variable weighting, variable
     * CEs leave every ordinary level (shifted re-adds the primary
     * at the quaternary level below; blanked drops them wholly). */
    int var_off = c->alternate != LEPTRIS_COLL_ALTERNATE_NON_IGNORABLE;
    for (size_t i = 0; i < buf.n; i++) {
        const struct ce* ce = &buf.v[i];
        if (ce->p == 0) continue; /* primary-ignorable */
        if (var_off && ce->var)
            continue;
        emit_weight(&k, ce->p);
    }
    *k++ = 0x00;

    if (c->strength >= LEPTRIS_COLL_STRENGTH_SECONDARY) {
        for (size_t i = 0; i < buf.n; i++) {
            if (var_off && buf.v[i].var) continue;
            if (buf.v[i].s != 0) emit_weight(&k, buf.v[i].s);
        }
        *k++ = 0x00;
    }

    if (c->strength >= LEPTRIS_COLL_STRENGTH_TERTIARY) {
        for (size_t i = 0; i < buf.n; i++) {
            if (var_off && buf.v[i].var) continue;
            uint16_t t = buf.v[i].t;
            /* caseFirst: DUCET tertiaries order lowercase (0x0002)
             * before uppercase (0x0008); upper-first swaps the two
             * case weights. */
            if (c->case_first == 1) {
                if (t == 0x0002) t = 0x0008;
                else if (t == 0x0008) t = 0x0002;
            }
            if (t != 0) emit_weight(&k, t);
        }
        *k++ = 0x00;
    }

    if (c->strength >= LEPTRIS_COLL_STRENGTH_QUATERNARY &&
        c->alternate == LEPTRIS_COLL_ALTERNATE_SHIFTED) {
        for (size_t i = 0; i < buf.n; i++) {
            const struct ce* ce = &buf.v[i];
            if (ce->var && ce->p != 0) emit_weight(&k, ce->p);
        }
        *k++ = 0x00;
    }

    free(buf.v);
    *out = key;
    *out_len = (size_t)(k - key);
    return 0;
}

int leptris_collation_compare(const char* a, size_t alen, const char* b,
                              size_t blen, const leptris_collation* c) {
    if (!a || !b || !c) return 0;
    if (c->kind == LEPTRIS_COLL_ASCII_CI)
        return ascii_ci_compare(a, alen, b, blen);
    if (c->kind == LEPTRIS_COLL_CODEPOINT) {
        size_t n = alen < blen ? alen : blen;
        int cmp = memcmp(a, b, n);
        if (cmp) return cmp < 0 ? -1 : 1;
        if (alen == blen) return 0;
        return alen < blen ? -1 : 1;
    }

    unsigned char *ka = NULL, *kb = NULL;
    size_t la = 0, lb = 0;
    if (leptris_collation_sortkey(a, alen, c, &ka, &la) != 0 ||
        leptris_collation_sortkey(b, blen, c, &kb, &lb) != 0) {
        free(ka);
        free(kb);
        return 0;
    }
    size_t n = la < lb ? la : lb;
    int cmp = memcmp(ka, kb, n);
    if (!cmp) cmp = (la < lb) ? -1 : (la > lb ? 1 : 0);
    free(ka);
    free(kb);
    return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}
