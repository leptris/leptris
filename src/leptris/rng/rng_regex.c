/* rng/rng_regex.c — self-contained regex matcher for RELAX NG
 * <param name="pattern"> facets (#878).
 *
 * XSD pattern semantics: the pattern must match the ENTIRE value.
 * Supported subset (byte-oriented, so UTF-8 text works as long as
 * classes/literals are ASCII or raw byte ranges): literals, '.',
 * [...] classes with ranges and leading '^', the \d \D \w \W \s \S
 * shorthands, escapes, (...) groups, '|' alternation, and the
 * *, +, ?, {m}, {m,}, {m,n} quantifiers. Constructs outside the
 * subset are rejected at SCHEMA PARSE time (rng_regex_supported) —
 * a loud refusal rather than a silently wrong verdict.
 *
 * Matching is greedy-with-backtracking on quantifier repeats;
 * a pattern whose inner group has multiple match lengths that only
 * succeed on a non-first alternative is outside the guaranteed
 * subset (e.g. (a|ab)c on "abc") — real-world schema patterns
 * (classes, bounded repeats, alternation of literals) are covered.
 */
#include "rng_internal.h"

#if defined(LEPTRIS_HAS_UTF8PROC)
#include <utf8proc.h>
#endif
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum {
    RRE_END,   /* sentinel: the entire-value anchor (XSD patterns
                * match the WHOLE value). Threaded as the top-level
                * continuation so ALT/REP backtracking must satisfy
                * it — a partial left alternative no longer wins. */
    RRE_CHAR,
    RRE_CLASS,
    RRE_DOT,
    RRE_CAT,
    RRE_ALT,
    RRE_REP,
};

typedef struct RreNode {
    int kind;
    struct RreNode* l;   /* CAT: first; ALT: left; REP: child */
    struct RreNode* r;   /* CAT: rest; ALT: right */
    unsigned char ch;    /* RRE_CHAR */
    unsigned char cls[32]; /* RRE_CLASS bitmap (bytes 0x00-0xFF) */
    /* RRE_CLASS unicode ranges (blocks above 0xFF, moxml#464):
     * a codepoint matches the class when it is in the bitmap OR in
     * any uni range; neg flips the combined result. */
    unsigned char uni_n;
    unsigned uni_lo[4];
    unsigned uni_hi[4];
    /* XSD category escapes (\p{L}, \p{Nd}, ...): a mask over the
     * utf8proc category codes, consulted per codepoint. Requires
     * LEPTRIS_HAS_UTF8PROC (without it the escapes fail loudly). */
    unsigned cat_set;
    unsigned cat_mask;
    int neg;
    int lo, hi;          /* RRE_REP bounds; hi < 0 = unbounded */
} RreNode;

typedef struct RreParser {
    const char* p;
    int depth;
    int bad;
} RreParser;

static RreNode* parse_alt(RreParser* ps);

static RreNode* node_new(int kind) {
    RreNode* n = (RreNode*)calloc(1, sizeof(*n));
    if (n) n->kind = kind;
    return n;
}

static void node_free(RreNode* n) {
    if (!n) return;
    node_free(n->l);
    node_free(n->r);
    free(n);
}

static void cls_add(RreNode* n, unsigned char c) {
    n->cls[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static void cls_add_builtin(RreNode* n, char c) {
    switch (c) {
        case 'd':
            for (int i = '0'; i <= '9'; i++) cls_add(n, (unsigned char)i);
            break;
        case 'w':
            for (int i = '0'; i <= '9'; i++) cls_add(n, (unsigned char)i);
            for (int i = 'a'; i <= 'z'; i++) cls_add(n, (unsigned char)i);
            for (int i = 'A'; i <= 'Z'; i++) cls_add(n, (unsigned char)i);
            cls_add(n, '_');
            break;
        case 's':
            cls_add(n, ' ');
            cls_add(n, '\t');
            cls_add(n, '\n');
            cls_add(n, '\r');
            cls_add(n, '\f');
            cls_add(n, '\v');
            break;
        case 'i':   /* XML NameStartChar */
            for (int i = 'a'; i <= 'z'; i++) cls_add(n, (unsigned char)i);
            for (int i = 'A'; i <= 'Z'; i++) cls_add(n, (unsigned char)i);
            cls_add(n, '_');
            cls_add(n, ':');
            for (int i = 0x80; i < 0x100; i++) cls_add(n, (unsigned char)i);
            break;
        case 'c':   /* XML NameChar = \i + digits . - */
            for (int i = 'a'; i <= 'z'; i++) cls_add(n, (unsigned char)i);
            for (int i = 'A'; i <= 'Z'; i++) cls_add(n, (unsigned char)i);
            for (int i = '0'; i <= '9'; i++) cls_add(n, (unsigned char)i);
            cls_add(n, '_');
            cls_add(n, ':');
            cls_add(n, '.');
            cls_add(n, '-');
            for (int i = 0x80; i < 0x100; i++) cls_add(n, (unsigned char)i);
            break;
        default:
            break;
    }
}

static int cls_has(const RreNode* n, unsigned char c) {
    int hit = (n->cls[c >> 3] >> (c & 7)) & 1;
    return n->neg ? !hit : hit;
}

/* Codepoint form (moxml#464): bytes 0x00-0xFF check the bitmap,
 * higher codepoints check the unicode ranges; neg flips the
 * combined result. */
static int cls_has_cp(const RreNode* n, unsigned cp) {
    int hit;
    if (cp < 0x100) {
        hit = (n->cls[cp >> 3] >> (cp & 7)) & 1;
    } else {
        hit = 0;
        for (int i = 0; i < n->uni_n; i++) {
            if (cp >= n->uni_lo[i] && cp <= n->uni_hi[i]) {
                hit = 1;
                break;
            }
        }
    }
#if defined(LEPTRIS_HAS_UTF8PROC)
    if (!hit && n->cat_set) {
        hit = (n->cat_mask &
               (1u << utf8proc_get_property((utf8proc_int32_t)cp)
                    ->category)) != 0;
    }
#else
    (void)0;
#endif
    return n->neg ? !hit : hit;
}

/* Strict UTF-8 decode at s (text is well-formed UTF-8; malformed
 * lead/continuation bytes degrade to single-byte handling). */
static unsigned rre_decode_cp(const char* s, size_t avail, int* len) {
    unsigned char c = (unsigned char)s[0];
    int n;
    unsigned cp;
    if (c < 0x80) {
        *len = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        cp = c & 0x07;
    } else {
        *len = 1;
        return c;
    }
    if ((size_t)n > avail) {
        *len = 1;
        return c;
    }
    for (int i = 1; i < n; i++) {
        unsigned char cc = (unsigned char)s[i];
        if ((cc & 0xC0) != 0x80) {
            *len = 1;
            return c;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *len = n;
    return cp;
}

/* Add a block range: byte-fitting blocks join the bitmap, higher
 * blocks join the unicode ranges (cap 4 — one class atom carries at
 * most a handful of block escapes; more fail loudly as before). */
#if defined(LEPTRIS_HAS_UTF8PROC)
/* XSD Part 2 F.1 general-category escapes: exact two-letter tokens
 * and single-letter family masks, over the utf8proc categories. */
static const struct {
    const char* tok;
    unsigned cat;
} rre_cats[] = {
    {"Lu", UTF8PROC_CATEGORY_LU}, {"Ll", UTF8PROC_CATEGORY_LL},
    {"Lt", UTF8PROC_CATEGORY_LT}, {"Lm", UTF8PROC_CATEGORY_LM},
    {"Lo", UTF8PROC_CATEGORY_LO},
    {"Mn", UTF8PROC_CATEGORY_MN}, {"Mc", UTF8PROC_CATEGORY_MC},
    {"Me", UTF8PROC_CATEGORY_ME},
    {"Nd", UTF8PROC_CATEGORY_ND}, {"Nl", UTF8PROC_CATEGORY_NL},
    {"No", UTF8PROC_CATEGORY_NO},
    {"Pc", UTF8PROC_CATEGORY_PC}, {"Pd", UTF8PROC_CATEGORY_PD},
    {"Ps", UTF8PROC_CATEGORY_PS}, {"Pe", UTF8PROC_CATEGORY_PE},
    {"Pi", UTF8PROC_CATEGORY_PI}, {"Pf", UTF8PROC_CATEGORY_PF},
    {"Po", UTF8PROC_CATEGORY_PO},
    {"Zs", UTF8PROC_CATEGORY_ZS}, {"Zl", UTF8PROC_CATEGORY_ZL},
    {"Zp", UTF8PROC_CATEGORY_ZP},
    {"Sm", UTF8PROC_CATEGORY_SM}, {"Sc", UTF8PROC_CATEGORY_SC},
    {"Sk", UTF8PROC_CATEGORY_SK}, {"So", UTF8PROC_CATEGORY_SO},
    {"Cc", UTF8PROC_CATEGORY_CC}, {"Cf", UTF8PROC_CATEGORY_CF},
    {"Co", UTF8PROC_CATEGORY_CO}, {"Cs", UTF8PROC_CATEGORY_CS},
    {"Cn", UTF8PROC_CATEGORY_CN},
};

static int rre_category_mask(const char* tok, size_t len,
                             unsigned* mask) {
    if (len == 2) {
        for (size_t i = 0; i < sizeof(rre_cats) / sizeof(rre_cats[0]);
             i++) {
            if (tok[0] == rre_cats[i].tok[0] &&
                tok[1] == rre_cats[i].tok[1]) {
                *mask = 1u << rre_cats[i].cat;
                return 1;
            }
        }
        return 0;
    }
    if (len != 1) return 0;
    switch (tok[0]) {
        case 'L':
            *mask = (1u << UTF8PROC_CATEGORY_LU) |
                    (1u << UTF8PROC_CATEGORY_LL) |
                    (1u << UTF8PROC_CATEGORY_LT) |
                    (1u << UTF8PROC_CATEGORY_LM) |
                    (1u << UTF8PROC_CATEGORY_LO);
            return 1;
        case 'M':
            *mask = (1u << UTF8PROC_CATEGORY_MN) |
                    (1u << UTF8PROC_CATEGORY_MC) |
                    (1u << UTF8PROC_CATEGORY_ME);
            return 1;
        case 'N':
            *mask = (1u << UTF8PROC_CATEGORY_ND) |
                    (1u << UTF8PROC_CATEGORY_NL) |
                    (1u << UTF8PROC_CATEGORY_NO);
            return 1;
        case 'P':
            *mask = (1u << UTF8PROC_CATEGORY_PC) |
                    (1u << UTF8PROC_CATEGORY_PD) |
                    (1u << UTF8PROC_CATEGORY_PS) |
                    (1u << UTF8PROC_CATEGORY_PE) |
                    (1u << UTF8PROC_CATEGORY_PI) |
                    (1u << UTF8PROC_CATEGORY_PF) |
                    (1u << UTF8PROC_CATEGORY_PO);
            return 1;
        case 'S':
            *mask = (1u << UTF8PROC_CATEGORY_SM) |
                    (1u << UTF8PROC_CATEGORY_SC) |
                    (1u << UTF8PROC_CATEGORY_SK) |
                    (1u << UTF8PROC_CATEGORY_SO);
            return 1;
        case 'Z':
            *mask = (1u << UTF8PROC_CATEGORY_ZS) |
                    (1u << UTF8PROC_CATEGORY_ZL) |
                    (1u << UTF8PROC_CATEGORY_ZP);
            return 1;
        case 'C':
            *mask = (1u << UTF8PROC_CATEGORY_CC) |
                    (1u << UTF8PROC_CATEGORY_CF) |
                    (1u << UTF8PROC_CATEGORY_CS) |
                    (1u << UTF8PROC_CATEGORY_CO) |
                    (1u << UTF8PROC_CATEGORY_CN);
            return 1;
        default:
            return 0;
    }
}
#endif /* LEPTRIS_HAS_UTF8PROC */

static int cls_add_block(RreNode* n, unsigned lo, unsigned hi) {
    if (hi < 0x100) {
        for (unsigned i = lo; i <= hi; i++) cls_add(n, (unsigned char)i);
        return 1;
    }
    if (n->uni_n >= 4) return 0;
    n->uni_lo[n->uni_n] = lo;
    n->uni_hi[n->uni_n] = hi;
    n->uni_n++;
    return 1;
}

/* [...] — after the '['. */
/* XSD Part 2 Appendix F block escapes: name -> inclusive byte
 * range. The byte-class matcher serves blocks that fit below
 * 0x100 exactly; the table extends with the full Unicode block
 * list when the classes go codepoint-wide (moxml#464). */
typedef struct {
    const char* name;
    unsigned lo;
    unsigned hi;
} RreBlock;

/* XSD 1.0 Part 2 Appendix F.1 — the block-escape name list
 * (moxml#464). Byte-fitting blocks serve the byte bitmap; the rest
 * ride the class node's unicode ranges. */
static const RreBlock rre_blocks[] = {
    {"IsBasicLatin", 0x0000, 0x007F},
    {"IsLatin-1Supplement", 0x0080, 0x00FF},
    {"IsLatinExtended-A", 0x0100, 0x017F},
    {"IsLatinExtended-B", 0x0180, 0x024F},
    {"IsIPAExtensions", 0x0250, 0x02AF},
    {"IsSpacingModifierLetters", 0x02B0, 0x02FF},
    {"IsCombiningDiacriticalMarks", 0x0300, 0x036F},
    {"IsGreek", 0x0370, 0x03FF},
    {"IsGreekandCoptic", 0x0370, 0x03FF},
    {"IsCyrillic", 0x0400, 0x04FF},
    {"IsCyrillicSupplement", 0x0500, 0x052F},
    {"IsArmenian", 0x0530, 0x058F},
    {"IsHebrew", 0x0590, 0x05FF},
    {"IsArabic", 0x0600, 0x06FF},
    {"IsSyriac", 0x0700, 0x074F},
    {"IsThaana", 0x0780, 0x07BF},
    {"IsDevanagari", 0x0900, 0x097F},
    {"IsBengali", 0x0980, 0x09FF},
    {"IsGurmukhi", 0x0A00, 0x0A7F},
    {"IsGujarati", 0x0A80, 0x0AFF},
    {"IsOriya", 0x0B00, 0x0B7F},
    {"IsTamil", 0x0B80, 0x0BFF},
    {"IsTelugu", 0x0C00, 0x0C7F},
    {"IsKannada", 0x0C80, 0x0CFF},
    {"IsMalayalam", 0x0D00, 0x0D7F},
    {"IsSinhala", 0x0D80, 0x0DFF},
    {"IsThai", 0x0E00, 0x0E7F},
    {"IsLao", 0x0E80, 0x0EFF},
    {"IsTibetan", 0x0F00, 0x0FFF},
    {"IsMyanmar", 0x1000, 0x109F},
    {"IsGeorgian", 0x10A0, 0x10FF},
    {"IsHangulJamo", 0x1100, 0x11FF},
    {"IsEthiopic", 0x1200, 0x137F},
    {"IsCherokee", 0x13A0, 0x13FF},
    {"IsUnifiedCanadianAboriginalSyllabics", 0x1400, 0x167F},
    {"IsOgham", 0x1680, 0x169F},
    {"IsRunic", 0x16A0, 0x16FF},
    {"IsKhmer", 0x1780, 0x17FF},
    {"IsMongolian", 0x1800, 0x18AF},
    {"IsLatinExtendedAdditional", 0x1E00, 0x1EFF},
    {"IsGreekExtended", 0x1F00, 0x1FFF},
    {"IsGeneralPunctuation", 0x2000, 0x206F},
    {"IsSuperscriptsandSubscripts", 0x2070, 0x209F},
    {"IsCurrencySymbols", 0x20A0, 0x20CF},
    {"IsCombiningMarksforSymbols", 0x20D0, 0x20FF},
    {"IsLetterlikeSymbols", 0x2100, 0x214F},
    {"IsNumberForms", 0x2150, 0x218F},
    {"IsArrows", 0x2190, 0x21FF},
    {"IsMathematicalOperators", 0x2200, 0x22FF},
    {"IsMiscellaneousTechnical", 0x2300, 0x23FF},
    {"IsControlPictures", 0x2400, 0x243F},
    {"IsOpticalCharacterRecognition", 0x2440, 0x245F},
    {"IsEnclosedAlphanumerics", 0x2460, 0x24FF},
    {"IsBoxDrawing", 0x2500, 0x257F},
    {"IsBlockElements", 0x2580, 0x259F},
    {"IsGeometricShapes", 0x25A0, 0x25FF},
    {"IsMiscellaneousSymbols", 0x2600, 0x26FF},
    {"IsDingbats", 0x2700, 0x27BF},
    {"IsBraillePatterns", 0x2800, 0x28FF},
    {"IsCJKRadicalsSupplement", 0x2E80, 0x2EFF},
    {"IsKangxiRadicals", 0x2F00, 0x2FDF},
    {"IsIdeographicDescriptionCharacters", 0x2FF0, 0x2FFF},
    {"IsCJKSymbolsandPunctuation", 0x3000, 0x303F},
    {"IsHiragana", 0x3040, 0x309F},
    {"IsKatakana", 0x30A0, 0x30FF},
    {"IsBopomofo", 0x3100, 0x312F},
    {"IsHangulCompatibilityJamo", 0x3130, 0x318F},
    {"IsKanbun", 0x3190, 0x319F},
    {"IsBopomofoExtended", 0x31A0, 0x31BF},
    {"IsKatakanaPhoneticExtensions", 0x31D0, 0x31FF},
    {"IsEnclosedCJKLettersandMonths", 0x3200, 0x32FF},
    {"IsCJKCompatibility", 0x3300, 0x33FF},
    {"IsCJKUnifiedIdeographsExtensionA", 0x3400, 0x4DBF},
    {"IsYijingHexagramSymbols", 0x4DC0, 0x4DFF},
    {"IsCJKUnifiedIdeographs", 0x4E00, 0x9FFF},
    {"IsYiSyllables", 0xA000, 0xA48F},
    {"IsYiRadicals", 0xA490, 0xA4CF},
    {"IsHangulSyllables", 0xAC00, 0xD7AF},
    {"IsHighSurrogates", 0xD800, 0xDB7F},
    {"IsHighPrivateUseSurrogates", 0xDB80, 0xDBFF},
    {"IsLowSurrogates", 0xDC00, 0xDFFF},
    {"IsPrivateUse", 0xE000, 0xF8FF},
    {"IsCJKCompatibilityIdeographs", 0xF900, 0xFAFF},
    {"IsAlphabeticPresentationForms", 0xFB00, 0xFB4F},
    {"IsArabicPresentationForms-A", 0xFB50, 0xFDFF},
    {"IsCombiningHalfMarks", 0xFE20, 0xFE2F},
    {"IsCJKCompatibilityForms", 0xFE30, 0xFE4F},
    {"IsSmallFormVariants", 0xFE50, 0xFE6F},
    {"IsArabicPresentationForms-B", 0xFE70, 0xFEFF},
    {"IsHalfwidthandFullwidthForms", 0xFF00, 0xFFEF},
    {"IsSpecials", 0xFFF0, 0xFFFF},
};

static int rre_block_range(const char* name, size_t len, unsigned* lo,
                           unsigned* hi) {
    for (size_t i = 0; i < sizeof(rre_blocks) / sizeof(rre_blocks[0]);
         i++) {
        if (strlen(rre_blocks[i].name) == len &&
            memcmp(name, rre_blocks[i].name, len) == 0) {
            *lo = rre_blocks[i].lo;
            *hi = rre_blocks[i].hi;
            return 1;
        }
    }
    return 0;
}

/* The Cc category (control bytes) rides the same table shape. */
static int rre_block_cc(unsigned* lo, unsigned* hi) {
    *lo = 0x00;
    *hi = 0x1F;
    return 1; /* the 0x7F and 0x80-0x9F pieces set by the caller */
}

static RreNode* parse_class(RreParser* ps) {
    RreNode* n = node_new(RRE_CLASS);
    if (!n) return NULL;
    if (*ps->p == '^') {
        n->neg = 1;
        ps->p++;
    }
    int first = 1;
    while (*ps->p && (*ps->p != ']' || first)) {
        first = 0;
        if (*ps->p == '\\' && ps->p[1] == 'p' && ps->p[2] == '{') {
            /* XSD property classes for the Latin-1 byte world —
             * the byte-class matcher covers these exactly
             * (leptris-ruby#434, ST_ContentType). Other blocks
             * fail loudly as before. */
            ps->p += 3;
            const char* close = strchr(ps->p, '}');
            if (!close) {
                node_free(n);
                ps->bad = 1;
                return NULL;
            }
            size_t len = (size_t)(close - ps->p);
            unsigned lo = 0, hi = 0;
            int hit = rre_block_range(ps->p, len, &lo, &hi);
            if (!hit && len == 2 && ps->p[0] == 'C' && ps->p[1] == 'c') {
                rre_block_cc(&lo, &hi);
                hit = 1;
            }
            if (hit) {
                if (!cls_add_block(n, lo, hi)) {
                    node_free(n);
                    ps->bad = 1;
                    return NULL;
                }
            } else {
                /* category escape (\p{L}, \p{Nd}, ...): utf8proc-
                 * gated — without it the escape fails loudly */
#if defined(LEPTRIS_HAS_UTF8PROC)
                unsigned mask = 0;
                if (!rre_category_mask(ps->p, len, &mask)) {
                    node_free(n);
                    ps->bad = 1;
                    return NULL;
                }
                n->cat_set = 1;
                n->cat_mask |= mask;
#else
                node_free(n);
                ps->bad = 1;
                return NULL;
#endif
            }
            if (len == 2 && ps->p[0] == 'C' && ps->p[1] == 'c') {
                cls_add(n, 0x7F);
                for (int i = 0x80; i <= 0x9F; i++)
                    cls_add(n, (unsigned char)i);
            }
            ps->p = close + 1;
            continue;
        }
        if (*ps->p == '\\' && ps->p[1]) {
            ps->p++;
            char e = *ps->p++;
            if (e == 'd' || e == 'D' || e == 'w' || e == 'W' ||
                e == 's' || e == 'S' || e == 'i' || e == 'I' ||
                e == 'c' || e == 'C') {
                RreNode* tmp = node_new(RRE_CLASS);
                if (!tmp) {
                    node_free(n);
                    return NULL;
                }
                cls_add_builtin(tmp, e);
                int neg_b = e == 'D' || e == 'W' || e == 'S' ||
                            e == 'I' || e == 'C';
                for (int i = 0; i < 256; i++)
                    if (((tmp->cls[i >> 3] >> (i & 7)) & 1) ==
                        (neg_b ? 0 : 1))
                        cls_add(n, (unsigned char)i);
                if (neg_b) {
                    /* Negated built-in: everything except the set. */
                    memset(n->cls, 0xff, sizeof(n->cls));
                    for (int i = 0; i < 256; i++)
                        if ((tmp->cls[i >> 3] >> (i & 7)) & 1)
                            n->cls[i >> 3] &=
                                (unsigned char)~(1u << (i & 7));
                }
                node_free(tmp);
                continue;
            }
            /* XSD singleCharEsc covers the control escapes too
             * (the OPC ST_ContentType pattern uses \t) —
             * leptris-ruby#434. */
            if (e == 't') { cls_add(n, '\t'); continue; }
            if (e == 'n') { cls_add(n, '\n'); continue; }
            if (e == 'r') { cls_add(n, '\r'); continue; }
            cls_add(n, (unsigned char)e);
            continue;
        }
        /* XSD charClassSub: `[ A - [ B ] ]` — the '-' followed by
         * '[' subtracts the bracketed class (leptris-ruby#434, the
         * OPC ST_ContentType shape). The subtraction is the last
         * member of the class. */
        if (*ps->p == '-' && ps->p[1] == '[') {
            ps->p++;
            RreNode* sub = parse_class(ps);
            if (!sub) {
                node_free(n);
                return NULL;
            }
            for (int i = 0; i < 256; i++)
                if ((sub->cls[i >> 3] >> (i & 7)) & 1)
                    n->cls[i >> 3] &= (unsigned char)~(1u << (i & 7));
            node_free(sub);
            if (*ps->p != ']') {
                node_free(n);
                ps->bad = 1;
                return NULL;
            }
            break;
        }
        unsigned char c = (unsigned char)*ps->p++;
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            ps->p++;
            unsigned char hi = (unsigned char)*ps->p++;
            for (unsigned int i = c; i <= hi; i++) cls_add(n, (unsigned char)i);
        } else {
            cls_add(n, c);
        }
    }
    if (*ps->p != ']') {
        node_free(n);
        ps->bad = 1;
        return NULL;
    }
    ps->p++;
    return n;
}

static RreNode* parse_atom(RreParser* ps) {
    if (*ps->p == '[') {
        ps->p++;
        return parse_class(ps);
    }
    if (*ps->p == '(') {
        ps->p++;
        if (++ps->depth > 32) {
            ps->bad = 1;
            return NULL;
        }
        RreNode* g = parse_alt(ps);
        ps->depth--;
        if (!g || *ps->p != ')') {
            node_free(g);
            ps->bad = 1;
            return NULL;
        }
        ps->p++;
        return g;
    }
    if (*ps->p == '\\' && ps->p[1]) {
        char e = ps->p[1];
        if (e == 'p' || e == 'P') {
            /* XSD block-escape ATOM (moxml#464): \p{IsBLOCK} /
             * \P{IsBLOCK} — a class node of the block's byte
             * range, complemented for \P. The byte-class matcher
             * serves blocks below 0x100 exactly. */
            if (ps->p[2] != '{') {
                ps->bad = 1;
                return NULL;
            }
            ps->p += 3;
            const char* close = strchr(ps->p, '}');
            if (!close) {
                ps->bad = 1;
                return NULL;
            }
            size_t len = (size_t)(close - ps->p);
            unsigned lo = 0, hi = 0;
            int is_block = rre_block_range(ps->p, len, &lo, &hi);
            unsigned cat_mask = 0;
#if defined(LEPTRIS_HAS_UTF8PROC)
            int is_cat = 0;
            if (!is_block &&
                rre_category_mask(ps->p, len, &cat_mask))
                is_cat = 1;
#endif
            if (!is_block
#if defined(LEPTRIS_HAS_UTF8PROC)
                && !is_cat
#endif
            ) {
                ps->bad = 1;
                return NULL;
            }
            ps->p = close + 1;
            RreNode* n = node_new(RRE_CLASS);
            if (!n) return NULL;
            if (is_block) {
                if (!cls_add_block(n, lo, hi)) {
                    node_free(n);
                    ps->bad = 1;
                    return NULL;
                }
#if defined(LEPTRIS_HAS_UTF8PROC)
            } else {
                n->cat_set = 1;
                n->cat_mask = cat_mask;
#endif
            }
            if (e == 'P') n->neg = 1; /* complement over all codepoints */
            return n;
        }
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' ||
            e == 'S' || e == 'i' || e == 'I' || e == 'c' || e == 'C') {
            ps->p += 2;
            RreNode* n = node_new(RRE_CLASS);
            if (!n) return NULL;
            if (e == 'd' || e == 'w' || e == 's' || e == 'i' ||
                e == 'c') {
                cls_add_builtin(n, e);
            } else {
                memset(n->cls, 0xff, sizeof(n->cls));
                RreNode* tmp = node_new(RRE_CLASS);
                if (!tmp) {
                    node_free(n);
                    return NULL;
                }
                cls_add_builtin(tmp, e == 'D' ? 'd'
                                  : e == 'W' ? 'w'
                                  : e == 'S' ? 's'
                                  : e == 'I' ? 'i' : 'c');
                for (int i = 0; i < 256; i++)
                    if ((tmp->cls[i >> 3] >> (i & 7)) & 1)
                        n->cls[i >> 3] &= (unsigned char)~(1u << (i & 7));
                node_free(tmp);
            }
            return n;
        }
        ps->p += 2;
        RreNode* n = node_new(RRE_CHAR);
        if (!n) return NULL;
        /* Only punctuation may be escaped as a literal, plus the
         * XSD control escapes (the OPC ST_ContentType pattern uses
         * \t) — other escapes (XSD \p{...} outside a class,
         * backrefs) are outside the supported subset and must fail
         * loudly at parse time (leptris-ruby#434). */
        if (e == 't') { n->ch = '\t'; return n; }
        if (e == 'n') { n->ch = '\n'; return n; }
        if (e == 'r') { n->ch = '\r'; return n; }
        if (isalnum((unsigned char)e)) {
            free(n);
            ps->bad = 1;
            return NULL;
        }
        n->ch = (unsigned char)e;
        return n;
    }
    if (*ps->p == '.') {
        ps->p++;
        return node_new(RRE_DOT);
    }
    if (*ps->p && strchr("*+?()[]|", *ps->p) == NULL) {
        RreNode* n = node_new(RRE_CHAR);
        if (!n) return NULL;
        n->ch = (unsigned char)*ps->p++;
        return n;
    }
    ps->bad = 1;
    return NULL;
}

static RreNode* parse_piece(RreParser* ps) {
    RreNode* a = parse_atom(ps);
    if (!a) return NULL;
    int lo = 1, hi = 1;
    if (*ps->p == '*') {
        lo = 0;
        hi = -1;
        ps->p++;
    } else if (*ps->p == '+') {
        lo = 1;
        hi = -1;
        ps->p++;
    } else if (*ps->p == '?') {
        lo = 0;
        hi = 1;
        ps->p++;
    } else if (*ps->p == '{') {
        const char* q = ps->p + 1;
        int l = -1, h = -1;
        char* end = NULL;
        long v = strtol(q, &end, 10);
        if (end > q) {
            l = (int)v;
            q = end;
            if (*q == ',') {
                q++;
                if (*q == '}') {
                    h = -1;
                } else {
                    long v2 = strtol(q, &end, 10);
                    if (end > q) {
                        h = (int)v2;
                        q = end;
                    }
                }
            }
            /* {n} with no comma is EXACT — h must be l, not the
             * -1 "unbounded" sentinel. The old code left h = -1,
             * so [A-Z]{3} matched ABCD (open-ended {3,}). */
            if (*q != ',' && h == -1) h = l;
            if (*q == '}' && l >= 0 && (h == -1 || h >= l)) {
                lo = l;
                hi = h;
                ps->p = q + 1;
            }
        }
    }
    if (lo == 1 && hi == 1) return a;
    RreNode* r = node_new(RRE_REP);
    if (!r) {
        node_free(a);
        return NULL;
    }
    r->l = a;
    r->lo = lo;
    r->hi = hi;
    return r;
}

static RreNode* parse_cat(RreParser* ps) {
    RreNode* first = parse_piece(ps);
    if (!first) return NULL;
    if (!*ps->p || *ps->p == '|' || *ps->p == ')') return first;
    RreNode* rest = parse_cat(ps);
    if (!rest) {
        node_free(first);
        return NULL;
    }
    RreNode* c = node_new(RRE_CAT);
    if (!c) {
        node_free(first);
        node_free(rest);
        return NULL;
    }
    c->l = first;
    c->r = rest;
    return c;
}

static RreNode* parse_alt(RreParser* ps) {
    RreNode* l = parse_cat(ps);
    if (!l) return NULL;
    if (*ps->p != '|') return l;
    ps->p++;
    RreNode* r = parse_alt(ps);
    if (!r) {
        node_free(l);
        return NULL;
    }
    RreNode* a = node_new(RRE_ALT);
    if (!a) {
        node_free(l);
        node_free(r);
        return NULL;
    }
    a->l = l;
    a->r = r;
    return a;
}

typedef struct RreCont {
    RreNode* node;
    const struct RreCont* next;
} RreCont;

static const char* rre_match(RreNode* n, const char* s,
                             const RreCont* k);

static const char* rre_run(const RreCont* k, const char* s) {
    return k ? rre_match(k->node, s, k->next) : s;
}

static const char* rre_match(RreNode* n, const char* s,
                             const RreCont* k) {
    switch (n->kind) {
        case RRE_END:
            return *s == 0 ? rre_run(k, s) : NULL;
        case RRE_CHAR:
            if (*s == (char)n->ch) return rre_run(k, s + 1);
            return NULL;
        case RRE_DOT:
            if (*s) return rre_run(k, s + 1);
            return NULL;
        case RRE_CLASS: {
            if (!*s) return NULL;
            if ((unsigned char)*s < 0x80 && !n->cat_set) {
                if (cls_has(n, (unsigned char)*s))
                    return rre_run(k, s + 1);
                return NULL;
            }
            /* moxml#464: multibyte codepoints check the unicode
             * ranges and advance the full sequence. */
            int len = 0;
            unsigned cp = rre_decode_cp(s, strlen(s), &len);
            if (cls_has_cp(n, cp)) return rre_run(k, s + len);
            return NULL;
        }
        case RRE_ALT: {
            const char* r = rre_match(n->l, s, k);
            return r ? r : rre_match(n->r, s, k);
        }
        case RRE_CAT: {
            RreCont ck = {n->r, k};
            return rre_match(n->l, s, &ck);
        }
        case RRE_REP: {
            /* Two passes: count the greedy maximum, then store the
             * end position of every prefix count in a heap array. */
            size_t cap = n->hi < 0 ? 64 : (size_t)n->hi + 1;
            const char** ends =
                (const char**)malloc((cap + 1) * sizeof(*ends));
            if (!ends) return NULL;
            ends[0] = s;
            size_t cnt = 0;
            while (n->hi < 0 || (int)cnt < n->hi) {
                const char* r = rre_match(n->l, ends[cnt], NULL);
                if (!r || r == ends[cnt]) break;
                if (cnt == cap) {
                    cap *= 2;
                    const char** grown = (const char**)realloc(
                        ends, (cap + 1) * sizeof(*ends));
                    if (!grown) {
                        free(ends);
                        return NULL;
                    }
                    ends = grown;
                }
                ends[++cnt] = r;
            }
            const char* hit = NULL;
            for (int i = (int)cnt; i >= n->lo; i--) {
                hit = rre_run(k, ends[i]);
                if (hit) break;
            }
            free(ends);
            return hit;
        }
    }
    return NULL;
}

int rng_regex_supported(const char* pat) {
    if (!pat || !*pat) return 0;
    RreParser ps;
    ps.p = pat;
    ps.depth = 0;
    ps.bad = 0;
    RreNode* n = parse_alt(&ps);
    int ok = n && !ps.bad && *ps.p == 0;
    node_free(n);
    return ok;
}

int rng_regex_matches(const char* pat, const char* text) {
    if (!pat || !text) return 0;
    RreParser ps;
    ps.p = pat;
    ps.depth = 0;
    ps.bad = 0;
    RreNode* n = parse_alt(&ps);
    if (!n || ps.bad || *ps.p != 0) {
        node_free(n);
        return 0;
    }
    RreNode end;
    memset(&end, 0, sizeof(end));
    end.kind = RRE_END;
    RreCont k = {&end, NULL};
    const char* r = rre_match(n, text, &k);
    node_free(n);
    return r != NULL;
}
