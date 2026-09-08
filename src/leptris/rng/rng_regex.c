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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum {
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
    unsigned char cls[32]; /* RRE_CLASS bitmap */
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
        default:
            break;
    }
}

static int cls_has(const RreNode* n, unsigned char c) {
    int hit = (n->cls[c >> 3] >> (c & 7)) & 1;
    return n->neg ? !hit : hit;
}

/* [...] — after the '['. */
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
        if (*ps->p == '\\' && ps->p[1]) {
            ps->p++;
            char e = *ps->p++;
            if (e == 'd' || e == 'D' || e == 'w' || e == 'W' ||
                e == 's' || e == 'S') {
                RreNode* tmp = node_new(RRE_CLASS);
                if (!tmp) {
                    node_free(n);
                    return NULL;
                }
                cls_add_builtin(tmp, e);
                for (int i = 0; i < 256; i++)
                    if (((tmp->cls[i >> 3] >> (i & 7)) & 1) ==
                        (e == 'D' || e == 'W' || e == 'S' ? 0 : 1))
                        cls_add(n, (unsigned char)i);
                if (e == 'D' || e == 'W' || e == 'S') {
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
            cls_add(n, (unsigned char)e);
            continue;
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
        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' ||
            e == 'S') {
            ps->p += 2;
            RreNode* n = node_new(RRE_CLASS);
            if (!n) return NULL;
            if (e == 'd' || e == 'w' || e == 's') {
                cls_add_builtin(n, e);
            } else {
                memset(n->cls, 0xff, sizeof(n->cls));
                RreNode* tmp = node_new(RRE_CLASS);
                if (!tmp) {
                    node_free(n);
                    return NULL;
                }
                cls_add_builtin(tmp, e == 'D' ? 'd' : e == 'W' ? 'w' : 's');
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
        /* Only punctuation may be escaped as a literal; other
         * escapes (XSD \p{...}, backrefs, \n...) are outside the
         * supported subset and must fail loudly at parse time. */
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
        case RRE_CHAR:
            if (*s == (char)n->ch) return rre_run(k, s + 1);
            return NULL;
        case RRE_DOT:
            if (*s) return rre_run(k, s + 1);
            return NULL;
        case RRE_CLASS:
            if (*s && cls_has(n, (unsigned char)*s))
                return rre_run(k, s + 1);
            return NULL;
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
    const char* r = rre_match(n, text, NULL);
    node_free(n);
    return r && *r == 0;
}
