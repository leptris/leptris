/* xquery.c — XQuery 1.0 core (TODO.xslt-full/11, #684-A).
 *
 * An orchestration layer over the XPath engine (SSOT): the prolog
 * binds into the evaluation context, FLWOR clauses drive the same
 * FOR/LET binding discipline the XPath 2.0+ forms use, and results
 * reuse the XPath result model. No second evaluator.
 *
 * Ground truth: Saxon-HE 12.7 net.sf.saxon.Query (/tmp/probe9/xq).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "../../include/leptris/xquery/xquery.h"
#include "../leptris_internal.h"
#include "../xpath/evaluator.h"
#include "../xpath/evaluator_internal.h"
#include "../xpath/functions.h"
#include "../xpath/parser.h"
#include "../xpath/xpath_variables.h"

extern struct leptris_xpath_result* evaluate_expr(XPathContext*,
                                                  XPathASTNode*);
extern struct leptris_xpath_result* xpath_call_function_item(
    XPathContext* ctx, const char* cc, char** argv, size_t argc);
extern XPathNodeSet* xpath_nodeset_deep_copy(const XPathNodeSet* src);

/* ---- query model ---- */

typedef enum { XQ_DECL_VAR, XQ_DECL_NS, XQ_DECL_FN, XQ_DECL_DEFAULT_NS,
                 XQ_DECL_SKIP } XqDeclKind;

typedef struct {
    XqDeclKind kind;
    char* name;        /* var name (bare), ns prefix, or fn qname */
    char* uri;         /* XQ_DECL_NS */
    char* params;      /* XQ_DECL_FN: '\x01'-joined param names */
    size_t arity;
    XPathASTNode* ast; /* var initializer / fn body */
    int external;      /* XQ_DECL_VAR: `declare variable $x
                        * external [:= default]` — ast is default */
} XqDecl;

typedef struct {
    int sliding;                     /* 0 = tumbling */
    char* win_var;
    char* s_var, *s_pos_var;
    XPathASTNode* s_when;
    char* e_var, *e_pos_var;
    XPathASTNode* e_when;
    XPathASTNode* domain;
} XqWindow;

typedef struct {
    int is_for;        /* 0 = let; 2 = window */
    char* var;
    char* pos_var;     /* XQuery `for $x at $i` — NULL if none */
    XPathASTNode* expr;
    XqWindow win;
} XqClause;

typedef struct {
    XPathASTNode* key;
    int descending;
    int empty_least;   /* `empty least` — the default empty mode is
                        * greatest (XQuery 3.0 impl-defined; Saxon) */
    int strmode;       /* key evaluated to xs:string — codepoint
                        * order, never numerically re-parsed */
    size_t clause;     /* which `order by` clause this key came
                        * from — clauses apply as sequential stable
                        * sorts (XQuery 3.0 allows repeated order by) */
} XqOrderKey;

struct LeptrisXQueryInternal {
    XqDecl* decls;
    size_t ndecls;
    XqClause* clauses;
    size_t nclauses;
    XPathASTNode* where_ast;
    XPathASTNode* post_where_ast;  /* where AFTER group by (12) */
    XqOrderKey* keys;
    size_t nkeys;
    char** group_vars;     /* group by $k [:= Expr], ... (12) */
    XPathASTNode** group_keys;
    size_t ngroup;
    char* wrap_tag;        /* hoisted `<tag>{ FLWOR }</tag>` wrap */
    XPathASTNode* return_ast;
};

/* ---- scanning helpers ---- */

typedef struct {
    const char* p;
    const char* end;
} Scan;

static void scan_ws(Scan* s) {
    for (;;) {
        while (s->p < s->end && isspace((unsigned char)*s->p)) s->p++;
        /* XQuery comments (: ... :) — nestable. */
        if (s->p + 1 < s->end && s->p[0] == ':' && s->p[1] == '(') {
            int depth = 1;
            s->p += 2;
            while (s->p < s->end && depth > 0) {
                if (s->p + 1 < s->end && s->p[0] == ':' &&
                    s->p[1] == '(') {
                    depth++;
                    s->p += 2;
                } else if (s->p + 1 < s->end && s->p[0] == ')' &&
                           s->p[1] == ':') {
                    depth--;
                    s->p += 2;
                } else {
                    s->p++;
                }
            }
            continue;
        }
        return;
    }
}

/* Word at the cursor: [A-Za-z_][A-Za-z0-9.-]* (NCName set). */
static size_t scan_word(Scan* s, const char** out) {
    *out = s->p;
    if (s->p >= s->end ||
        !(isalpha((unsigned char)*s->p) || *s->p == '_'))
        return 0;
    const char* q = s->p;
    while (q < s->end &&
           (isalnum((unsigned char)*q) || *q == '_' || *q == '-' ||
            *q == '.'))
        q++;
    return (size_t)(q - s->p);
}

static int word_is(const char* w, size_t len, const char* kw) {
    return strlen(kw) == len && strncmp(w, kw, len) == 0;
}

/* Skip a quoted string ('...' / "..." with doubled-quote escapes). */
static void scan_string(Scan* s) {
    char q = *s->p;
    s->p++;
    while (s->p < s->end) {
        if (*s->p == q) {
            if (s->p + 1 < s->end && s->p[1] == q) {
                s->p += 2;
                continue;
            }
            s->p++;
            return;
        }
        s->p++;
    }
}

static int is_clause_word(const char* w, size_t len) {
    return word_is(w, len, "for") || word_is(w, len, "let") ||
           word_is(w, len, "where") || word_is(w, len, "order") ||
           word_is(w, len, "by") || word_is(w, len, "return") ||
           word_is(w, len, "stable") || word_is(w, len, "ascending") ||
           word_is(w, len, "descending") || word_is(w, len, "group") ||
           word_is(w, len, "collation") ||
           word_is(w, len, "tumbling") || word_is(w, len, "sliding") ||
           word_is(w, len, "window") || word_is(w, len, "start") ||
           word_is(w, len, "end") || word_is(w, len, "when") ||
           word_is(w, len, "at");
}

/* Advance over one FLWOR expression segment: stops before a clause
 * keyword at nesting depth 0 (a bare word delimited by whitespace
 * or segment end). */
static void scan_expr_segment(Scan* s, int stop_at_comma) {
    int depth = 0;
    while (s->p < s->end) {
        char c = *s->p;
        if (c == '\'' || c == '"') {
            scan_string(s);
            continue;
        }
        if (c == '`') {
            s->p++;
            while (s->p < s->end && *s->p != '`') s->p++;
            if (s->p < s->end) s->p++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') {
            if (depth == 0) return;   /* segment boundary */
            depth--;
        } else if (c == ',' && depth == 0 && stop_at_comma) {
            return;
        } else if (depth == 0 &&
                   (isalpha((unsigned char)c) || c == '_')) {
            const char* w;
            size_t wl = scan_word(s, &w);
            if (wl && is_clause_word(w, wl)) {
                /* A clause keyword only at a word boundary followed
                 * by whitespace/segment end. scan_word does not
                 * advance the cursor — look past the word. */
                const char* after = w + wl;
                if (after >= s->end || isspace((unsigned char)*after))
                    return;
            }
            s->p += wl ? wl : 1;
            continue;
        }
        s->p++;
    }
}

typedef struct { char* s; size_t len, cap; } Buf;
static void buf_put(Buf* b, const char* s, size_t n);
static void xq_expand_span(const char* a, const char* b, Buf* out);
static const char* xq_expr_close(const char* p, const char* cb);
static const char* xq_ctor_end(const char* ca, const char* cb);
static const char* xq_enclosed_end(const char* p, const char* end);
static const char* xq_elem_ctor_end(const char* p, const char* end);
static void buf_str(Buf* b, const char* s);
static const char* xq_translate_element(const char* p, const char* e,
                                        Buf* out);
static void xq_translate_content(const char* s, const char* e, Buf* out);
static int xq_is_name_start(char c);

/* Splice rewriter (#684): an expression span keeps its text
 * verbatim; every DIRECT element constructor inside it is replaced
 * by its computed form. Content-translating the whole span (the old
 * approach) mangled expressions that merely CONTAIN a constructor —
 * `count(document { <a/> }//b)` turned into text items. Adjacent
 * constructors join with ", " (a top-level multi-ctor sequence). */
static char* xq_splice_ctors(const char* a, const char* b) {
    Buf tb = {0};
    const char* p = a;
    int prev_ctor = 0;
    while (p < b) {
        if (*p == '\'' || *p == '"') {
            const char* q = p + 1;
            while (q < b && *q != *p) q++;
            buf_put(&tb, p, (size_t)((q < b ? q + 1 : b) - p));
            p = (q < b) ? q + 1 : b;
            prev_ctor = 0;
            continue;
        }
        if (*p == '<' && p + 1 < b && xq_is_name_start(p[1])) {
            if (prev_ctor && tb.len) buf_str(&tb, ", ");
            /* Parenthesize: the computed form is a primary, and a
             * following path step (`<a/>//x`) is only legal over a
             * parenthesized primary. */
            buf_str(&tb, "(");
            const char* end = xq_translate_element(p, b, &tb);
            buf_str(&tb, ")");
            p = end ? end : p + 1;
            prev_ctor = 1;
            continue;
        }
        if (!isspace((unsigned char)*p)) prev_ctor = 0;
        buf_put(&tb, p, 1);
        p++;
    }
    if (!tb.s) {
        tb.s = (char*)calloc(1, 1);
        return tb.s;
    }
    tb.s[tb.len] = 0;
    return tb.s;
}

/* String-constructor rewriter (#692 tail, XQuery 3.1):
 * `lit {expr} lit` expands to concat("lit", string(EXPR), "lit").
 * Doubled braces are literal braces; enclosed expressions are
 * brace-matched with quote and nested-ctor awareness. Expansion MUST
 * run before the direct-constructor splice — `<a/>` inside backticks
 * is a STRING, and once quoted the splice pass skips it. */
static const char* xq_skip_quoted(const char* q, const char* b) {
    char qt = *q;
    q++;
    while (q < b) {
        if (*q == qt && q + 1 < b && q[1] == qt) { q += 2; continue; }
        if (*q == qt) return q + 1;
        q++;
    }
    return b;
}

/* Closing backtick of a ctor whose content starts at ca, or NULL. */
static const char* xq_ctor_end(const char* ca, const char* cb) {
    const char* p = ca;
    while (p < cb) {
        if (*p == '`') return p;
        if (*p == '{' && p + 1 < cb && p[1] == '{') { p += 2; continue; }
        if (*p == '}' && p + 1 < cb && p[1] == '}') { p += 2; continue; }
        if (*p == '{') {
            const char* e = xq_expr_close(p, cb);
            if (!e) return NULL;
            p = e + 1;
            continue;
        }
        p++;
    }
    return NULL;
}

/* Matching '}' for the '{' at p, or NULL. Skips quoted strings and
 * nested string constructors inside the expression. */
static const char* xq_expr_close(const char* p, const char* cb) {
    int depth = 1;
    const char* q = p + 1;
    while (q < cb && depth > 0) {
        if (*q == '\'' || *q == '"') {
            q = xq_skip_quoted(q, cb);
        } else if (*q == '`') {
            const char* e = xq_ctor_end(q + 1, cb);
            if (!e) return NULL;
            q = e + 1;
        } else {
            if (*q == '{') depth++;
            else if (*q == '}') depth--;
            q++;
        }
    }
    return depth == 0 ? q - 1 : NULL;
}

/* Append a literal chunk as one or more quoted XPath string args.
 * The XPath 1.0 lexer has NO escape: a literal ends at the first
 * matching quote. So runs go out double-quoted (split at "), and a
 * lone " goes out as the one-char literal '"'. Segments rejoin via
 * concat(), which the caller wraps once chunks >= 2. */
static void xq_emit_quoted(Buf* args, const char* s, size_t n, int* nch) {
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        if (s[j] != '"') {
            while (j < n && s[j] != '"') j++;
            if (*nch) buf_str(args, ", ");
            buf_put(args, "\"", 1);
            buf_put(args, s + i, j - i);
            buf_put(args, "\"", 1);
        } else {
            if (*nch) buf_str(args, ", ");
            buf_str(args, "'\"'");
            j++;
        }
        (*nch)++;
        i = j;
    }
}

/* Expand ctor content [ca,cb) into out as ONE expression. */
static void xq_emit_str_ctor(const char* ca, const char* cb, Buf* out) {
    Buf args = {0}, lit = {0};
    int nch = 0;
    const char* p = ca;
    while (p < cb) {
        if (*p == '{' && p + 1 < cb && p[1] == '{') {
            buf_put(&lit, "{", 1);
            p += 2;
            continue;
        }
        if (*p == '}' && p + 1 < cb && p[1] == '}') {
            buf_put(&lit, "}", 1);
            p += 2;
            continue;
        }
        if (*p == '{') {
            const char* e = xq_expr_close(p, cb);
            if (!e) break;
            xq_emit_quoted(&args, lit.s, lit.len, &nch);
            lit.len = 0;
            if (e > p + 1) {  /* empty {} contributes nothing */
                if (nch) buf_str(&args, ", ");
                buf_str(&args, "string(");
                Buf inner = {0};
                xq_expand_span(p + 1, e, &inner);
                buf_put(&args, inner.s ? inner.s : "",
                        inner.s ? inner.len : 0);
                free(inner.s);
                buf_str(&args, ")");
                nch++;
            }
            p = e + 1;
            continue;
        }
        buf_put(&lit, p, 1);
        p++;
    }
    xq_emit_quoted(&args, lit.s, lit.len, &nch);
    free(lit.s);
    if (nch == 0) {
        buf_str(out, "\"\"");
    } else if (nch == 1) {
        buf_put(out, args.s, args.len);
    } else {
        buf_str(out, "concat(");
        buf_put(out, args.s, args.len);
        buf_str(out, ")");
    }
    free(args.s);
}

/* Copy span verbatim, expanding every string constructor in it. */
static void xq_expand_span(const char* a, const char* b, Buf* out) {
    const char* p = a;
    while (p < b) {
        if (*p == '\'' || *p == '"') {
            const char* q = xq_skip_quoted(p, b);
            buf_put(out, p, (size_t)(q - p));
            p = q;
            continue;
        }
        if (*p == '`') {
            const char* e = xq_ctor_end(p + 1, b);
            if (!e) {
                buf_put(out, p, (size_t)(b - p));
                return;
            }
            xq_emit_str_ctor(p + 1, e, out);
            p = e + 1;
            continue;
        }
        buf_put(out, p, 1);
        p++;
    }
}

/* Whole-span entry: NULL when no backtick (caller keeps the text). */
static char* xq_rewrite_str_ctors(const char* a, const char* b) {
    int has = 0;
    for (const char* q = a; q < b && !has; q++)
        if (*q == '`') has = 1;
    if (!has) return NULL;
    Buf tb = {0};
    xq_expand_span(a, b, &tb);
    if (!tb.s) {
        tb.s = (char*)calloc(1, 1);
        return tb.s;
    }
    tb.s[tb.len] = 0;
    return tb.s;
}

static XPathASTNode* parse_expr_span(const char* a, const char* b) {
    if (a >= b) return NULL;
    char* rewritten = xq_rewrite_str_ctors(a, b);
    if (rewritten) {
        a = rewritten;
        b = rewritten + strlen(rewritten);
    }
    int has_ctor = 0;
    for (const char* q = a; q + 1 < b && !has_ctor; q++)
        if (*q == '<' && xq_is_name_start(q[1])) has_ctor = 1;
    char* translated = NULL;
    if (has_ctor) {
        translated = xq_splice_ctors(a, b);
        if (!translated) { free(rewritten); return NULL; }
        a = translated;
        b = translated + strlen(translated);
    }
    XPathParser* parser = xpath_parser_new(a, (size_t)(b - a));
    if (!parser) { free(translated); free(rewritten); return NULL; }
    XPathASTNode* ast = xpath_parse(parser);
    xpath_parser_free(parser);
    free(translated);
    free(rewritten);
    return ast;
}

static char* span_dup(const char* a, const char* b) {
    size_t n = (size_t)(b - a);
    char* s = (char*)malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, a, n);
    s[n] = 0;
    return s;
}

/* ---- parse ---- */

static void xq_free(struct LeptrisXQueryInternal* q) {
    if (!q) return;
    for (size_t i = 0; i < q->ndecls; i++) {
        free(q->decls[i].name);
        free(q->decls[i].uri);
        free(q->decls[i].params);
        if (q->decls[i].ast) ast_node_free(q->decls[i].ast);
    }
    free(q->decls);
    for (size_t i = 0; i < q->nclauses; i++) {
        free(q->clauses[i].var);
        free(q->clauses[i].pos_var);
        if (q->clauses[i].expr) ast_node_free(q->clauses[i].expr);
        if (q->clauses[i].is_for == 2) {
            XqWindow* w = &q->clauses[i].win;
            free(w->win_var);
            free(w->s_var);
            free(w->s_pos_var);
            free(w->e_var);
            free(w->e_pos_var);
            if (w->s_when) ast_node_free(w->s_when);
            if (w->e_when) ast_node_free(w->e_when);
            if (w->domain) ast_node_free(w->domain);
        }
    }
    free(q->clauses);
    if (q->where_ast) ast_node_free(q->where_ast);
    if (q->post_where_ast) ast_node_free(q->post_where_ast);
    for (size_t i = 0; i < q->nkeys; i++)
        if (q->keys[i].key) ast_node_free(q->keys[i].key);
    free(q->keys);
    for (size_t i = 0; i < q->ngroup; i++) {
        free(q->group_vars[i]);
        if (q->group_keys[i]) ast_node_free(q->group_keys[i]);
    }
    free(q->group_vars);
    free(q->group_keys);
    free(q->wrap_tag);
    if (q->return_ast) ast_node_free(q->return_ast);
    free(q);
}

static void xq_clause_partial_free(XqClause* c) {
    if (c->is_for != 2) {
        free(c->var);
        free(c->pos_var);
        if (c->expr) ast_node_free(c->expr);
        return;
    }
    XqWindow* w = &c->win;
    free(w->win_var);
    free(w->s_var);
    free(w->s_pos_var);
    free(w->e_var);
    free(w->e_pos_var);
    if (w->s_when) ast_node_free(w->s_when);
    if (w->e_when) ast_node_free(w->e_when);
    if (w->domain) ast_node_free(w->domain);
}

/* Parse one `$name` reference; returns the bare name (malloc). */
static char* parse_dollar_name(Scan* s) {
    scan_ws(s);
    if (s->p >= s->end || *s->p != '$') return NULL;
    s->p++;
    /* Variable names are QNames — include ':' ($p:weight). */
    const char* w = s->p;
    while (s->p < s->end &&
           (isalnum((unsigned char)*s->p) || *s->p == '_' ||
            *s->p == '-' || *s->p == '.' || *s->p == ':'))
        s->p++;
    if (s->p == w) return NULL;
    return span_dup(w, s->p);
}

static int parse_decl(struct LeptrisXQueryInternal* q, Scan* s,
                      XqDecl* out) {
    memset(out, 0, sizeof(*out));
    scan_ws(s);
    const char* w;
    size_t wl = scan_word(s, &w);
    s->p = w + wl;
    if (word_is(w, wl, "variable")) {
        char* name = parse_dollar_name(s);
        if (!name) return 0;
        scan_ws(s);
        /* Optional `as SequenceType` — skipped textually: walk to
         * `external` or `:=` at depth 0 before the ';'. */
        {
            Scan at = *s;
            const char* aw;
            size_t awl = scan_word(&at, &aw);
            if (awl && word_is(aw, awl, "as")) {
                const char* p = aw + awl;
                int depth = 0;
                const char* stop = NULL;
                while (p < s->end) {
                    char c = *p;
                    if (c == '\'' || c == '"') {
                        const char* q = p + 1;
                        while (q < s->end && *q != c) q++;
                        p = (q < s->end) ? q + 1 : s->end;
                        continue;
                    }
                    if (c == '(' || c == '[' || c == '{') depth++;
                    else if (c == ')' || c == ']' || c == '}') depth--;
                    else if (c == ';' && depth == 0) break;
                    else if (depth == 0 && p + 1 < s->end &&
                             p[0] == ':' && p[1] == '=') {
                        stop = p;
                        break;
                    } else if (depth == 0 &&
                               (isalpha((unsigned char)c) || c == '_')) {
                        const char* ww;
                        size_t wwl = scan_word(&((Scan){p, s->end}), &ww);
                        if (wwl && word_is(ww, wwl, "external")) {
                            stop = ww;
                            break;
                        }
                        p += wwl ? wwl : 1;
                        continue;
                    }
                    p++;
                }
                if (!stop) {
                    free(name);
                    return 0;
                }
                s->p = stop;
                scan_ws(s);
            }
        }
        {
            Scan t = *s;
            const char* ew;
            size_t ewl = scan_word(&t, &ew);
            if (ewl && word_is(ew, ewl, "external")) {
                s->p = ew + ewl;
                scan_ws(s);
                out->kind = XQ_DECL_VAR;
                out->name = name;
                out->external = 1;
                if (s->p + 1 < s->end &&
                    s->p[0] == ':' && s->p[1] == '=') {
                    s->p += 2;
                    Scan e = *s;
                    {
                        int depth = 0;
                        while (e.p < e.end) {
                            char c = *e.p;
                            if (c == '\'' || c == '"') {
                                scan_string(&e);
                                continue;
                            }
                            if (c == '(' || c == '[' || c == '{') depth++;
                            else if (c == ')' || c == ']' || c == '}') depth--;
                            else if (c == ';' && depth == 0) break;
                            e.p++;
                        }
                    }
                    out->ast = parse_expr_span(s->p, e.p);
                    if (!out->ast) {
                        return 0;
                    }
                    s->p = (e.p < e.end) ? e.p + 1 : e.end;
                } else if (s->p < s->end && *s->p == ';') {
                    s->p++;
                }
                return 1;
            }
        }
        if (s->p + 1 >= s->end || s->p[0] != ':' || s->p[1] != '=') {
            free(name);
            return 0;
        }
        s->p += 2;
        /* initializer runs to the terminating ';' at depth 0 */
        Scan e = *s;
        {
            int depth = 0;
            while (e.p < e.end) {
                char c = *e.p;
                if (c == '\'' || c == '"') {
                    scan_string(&e);
                    continue;
                }
                if (c == '(' || c == '[' || c == '{') depth++;
                else if (c == ')' || c == ']' || c == '}') depth--;
                else if (c == ';' && depth == 0) break;
                e.p++;
            }
        }
        out->kind = XQ_DECL_VAR;
        out->name = name;
        out->ast = parse_expr_span(s->p, e.p);
        if (!out->ast) return 0;
        s->p = (e.p < e.end) ? e.p + 1 : e.end;   /* ';' */
        return 1;
    }
    if (word_is(w, wl, "base-uri")) {
        /* `declare base-uri "URI";` — accepted and skipped: it only
         * anchors RELATIVE collation URIs, and codepoint ordering
         * is URI-independent (QT3 orderBy60). */
        scan_ws(s);
        if (s->p >= s->end || (*s->p != '"' && *s->p != '\''))
            return 0;
        scan_string(s);
        scan_ws(s);
        if (s->p < s->end && *s->p == ';') s->p++;
        out->kind = XQ_DECL_SKIP;
        return 1;
    }
    if (word_is(w, wl, "default")) {
        /* `declare default function namespace "URI";` (also
         * element/function collation variants — accepted and
         * skipped: local-name resolution is unchanged, which is
         * what the corpus gates). `default element namespace` is
         * CAPTURED: unprefixed path names and constructed elements
         * resolve in it. */
        scan_ws(s);
        wl = scan_word(s, &w);
        s->p = w + wl;
        if (!wl) return 0;
        if (word_is(w, wl, "function") || word_is(w, wl, "element") ||
            word_is(w, wl, "collation") || word_is(w, wl, "order")) {
            int is_elem = word_is(w, wl, "element");
            /* optional "empty" / "namespace" filler words */
            scan_ws(s);
            Scan t2 = *s;
            size_t w2l = scan_word(&t2, &w);
            if (w2l && (word_is(w, w2l, "namespace") ||
                        word_is(w, w2l, "empty"))) {
                s->p = w + w2l;
                scan_ws(s);
            }
            if (s->p >= s->end || (*s->p != '"' && *s->p != '\'')) return 0;
            if (is_elem && w2l && word_is(w, w2l, "namespace")) {
                const char* lit = s->p;
                scan_string(s);
                size_t ll = (size_t)(s->p - lit);
                if (ll >= 2) {
                    out->uri = span_dup(lit + 1, lit + ll - 1);
                    if (!out->uri) return 0;
                }
            } else {
                scan_string(s);
            }
            scan_ws(s);
            if (s->p < s->end && *s->p == ';') s->p++;
            out->kind = is_elem ? XQ_DECL_DEFAULT_NS : XQ_DECL_SKIP;
            return 1;
        }
        return 0;
    }
    if (word_is(w, wl, "namespace")) {
        scan_ws(s);
        wl = scan_word(s, &w);
        if (!wl) return 0;
        s->p = w + wl;
        char* prefix = span_dup(w, w + wl);
        scan_ws(s);
        if (s->p >= s->end || *s->p != '=') {
            free(prefix);
            return 0;
        }
        s->p++;
        scan_ws(s);
        if (s->p >= s->end || (*s->p != '"' && *s->p != '\'')) {
            free(prefix);
            return 0;
        }
        const char* qs = s->p;
        scan_string(s);
        out->kind = XQ_DECL_NS;
        out->name = prefix;
        out->uri = span_dup(qs + 1, s->p - 1);
        scan_ws(s);
        if (s->p < s->end && *s->p == ';') s->p++;
        return out->uri != NULL;
    }
    if (word_is(w, wl, "function")) {
        scan_ws(s);
        /* function names are QNames — include ':' */
        {
            const char* q = s->p;
            while (q < s->end &&
                   (isalnum((unsigned char)*q) || *q == '_' ||
                    *q == '-' || *q == '.' || *q == ':'))
                q++;
            wl = (size_t)(q - s->p);
            w = s->p;
        }
        if (!wl) return 0;
        s->p = w + wl;
        char* fname = span_dup(w, w + wl);
        scan_ws(s);
        if (s->p >= s->end || *s->p != '(') {
            free(fname);
            return 0;
        }
        s->p++;
        /* parameters */
        char params[256];
        size_t plen = 0;
        params[0] = 0;
        size_t arity = 0;
        for (;;) {
            scan_ws(s);
            if (s->p < s->end && *s->p == ')') {
                s->p++;
                break;
            }
            char* pn = parse_dollar_name(s);
            if (!pn) {
                free(fname);
                return 0;
            }
            arity++;
            size_t pnlen = strlen(pn);
            if (plen && plen + 1 < sizeof(params))
                params[plen++] = '\x01';
            if (plen + pnlen < sizeof(params)) {
                memcpy(params + plen, pn, pnlen);
                plen += pnlen;
            }
            params[plen] = 0;
            free(pn);
            scan_ws(s);
            /* optional `as SequenceType` — skip to , or ) */
            while (s->p < s->end && *s->p != ',' && *s->p != ')') s->p++;
            if (s->p < s->end && *s->p == ',') s->p++;
        }
        /* optional `as SequenceType` — skip to '{' */
        scan_ws(s);
        while (s->p < s->end && *s->p != '{') s->p++;
        if (s->p >= s->end) {
            free(fname);
            return 0;
        }
        s->p++;   /* '{' */
        /* body: to the matching '}' */
        int depth = 1;
        const char* body = s->p;
        while (s->p < s->end && depth > 0) {
            char c = *s->p;
            if (c == '\'' || c == '"') {
                scan_string(s);
                continue;
            }
            if (c == '{') depth++;
            else if (c == '}') depth--;
            if (depth == 0) break;
            s->p++;
        }
        out->kind = XQ_DECL_FN;
        out->name = fname;
        out->params = span_dup(params, params + plen);
        out->arity = arity;
        out->ast = parse_expr_span(body, s->p);
        if (s->p < s->end) s->p++;   /* '}' */
        scan_ws(s);
        if (s->p < s->end && *s->p == ';') s->p++;
        return out->ast != NULL && out->params != NULL;
    }
    return 0;   /* import / option / default / base-uri */
}

/* Matching '}' for the '{' at p — quote, comment and nested
 * element-ctor aware. */
static const char* xq_enclosed_end(const char* p, const char* end) {
    int depth = 1;
    const char* i = p + 1;
    while (i < end && depth > 0) {
        if (*i == '\'' || *i == '"') {
            char qt = *i++;
            while (i < end && *i != qt) i++;
            if (i < end) i++;
            continue;
        }
        if (i + 1 < end && i[0] == ':' && i[1] == '(') {
            i += 2;
            int cd = 1;
            while (i + 1 < end && cd > 0) {
                if (i[0] == ':' && i[1] == '(') { cd++; i += 2; }
                else if (i[0] == ')' && i[1] == ':') { cd--; i += 2; }
                else i++;
            }
            continue;
        }
        if (*i == '<' && i + 1 < end && xq_is_name_start(i[1])) {
            i = xq_elem_ctor_end(i, end);
            if (!i) return NULL;
            continue;
        }
        if (*i == '{') depth++;
        else if (*i == '}') depth--;
        i++;
    }
    return depth == 0 ? i - 1 : NULL;
}

/* End (past '>') of the element constructor starting at p ('<').
 * Attribute quotes, nested ctors and enclosed expressions are
 * skipped structurally. */
static const char* xq_elem_ctor_end(const char* p, const char* end) {
    const char* i = p + 1;
    while (i < end && !isspace((unsigned char)*i) && *i != '>' &&
           *i != '/')
        i++;
    size_t nl = (size_t)(i - (p + 1));
    if (!nl) return NULL;
    while (i < end && *i != '>' && *i != '/') {
        if (*i == '"' || *i == '\'') {
            char qt = *i++;
            while (i < end && *i != qt) i++;
            if (i < end) i++;
        } else {
            i++;
        }
    }
    if (i >= end) return NULL;
    if (*i == '/') {
        if (i + 1 < end && i[1] == '>') return i + 2;
        return NULL;
    }
    i++;   /* past '>' — mixed content until </name> */
    while (i < end) {
        if (*i == '<') {
            if (i + 1 < end && i[1] == '/') {
                const char* j = i + 2;
                while (j < end && !isspace((unsigned char)*j) &&
                       *j != '>')
                    j++;
                if ((size_t)(j - (i + 2)) == nl &&
                    memcmp(i + 2, p + 1, nl) == 0) {
                    while (j < end && *j != '>') j++;
                    return j < end ? j + 1 : NULL;
                }
                return NULL;
            }
            if (i + 1 < end && xq_is_name_start(i[1])) {
                const char* e = xq_elem_ctor_end(i, end);
                if (!e) return NULL;
                i = e;
                continue;
            }
            i++;
            continue;
        }
        if (*i == '{') {
            const char* e = xq_enclosed_end(i, end);
            if (!e) return NULL;
            i = e + 1;
            continue;
        }
        i++;
    }
    return NULL;
}

/* Hoist `<tag>{ FLWOR }</tag>` — a direct ctor whose ENTIRE
 * content is one FLWOR carrying group by / order by. Those clauses
 * only run at the XQuery layer (the XPath-side for has no group
 * by), so the FLWOR is parsed bare and the ctor wraps the result
 * sequence once (tag_out). Returns a malloc'd rewritten query, or
 * NULL when the shape does not apply. */
static char* xq_hoist_ctor_flwor(const char* q, size_t len,
                                 char** tag_out) {
    const char* end = q + len;
    const char* p = q;
    /* prolog: `xquery version ...;` and `declare ...;` statements
     * (quote-aware ';' scan). */
    for (;;) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p + 7 <= end && memcmp(p, "xquery", 6) == 0 &&
            isspace((unsigned char)p[6])) {
            /* skip to ';' */
            while (p < end && *p != ';') {
                if (*p == '\'' || *p == '"') {
                    char qt = *p++;
                    while (p < end && *p != qt) p++;
                }
                p++;
            }
            if (p < end) p++;
            continue;
        }
        if (p + 8 <= end && memcmp(p, "declare", 7) == 0 &&
            isspace((unsigned char)p[7])) {
            while (p < end && *p != ';') {
                if (*p == '\'' || *p == '"') {
                    char qt = *p++;
                    while (p < end && *p != qt) p++;
                }
                p++;
            }
            if (p < end) p++;
            continue;
        }
        break;
    }
    if (p >= end || *p != '<' || p + 1 >= end ||
        !xq_is_name_start(p[1]))
        return NULL;
    const char* tag = p + 1;
    const char* tp = tag;
    while (tp < end && !isspace((unsigned char)*tp) && *tp != '>')
        tp++;
    size_t taglen = (size_t)(tp - tag);
    if (!taglen || tp >= end || *tp != '>') return NULL;  /* attrs */
    p = tp + 1;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p != '{') return NULL;
    const char* inner = p + 1;
    /* matching close brace (ctor/quote/comment aware) */
    const char* ib = xq_enclosed_end(p, end);
    if (!ib) return NULL;
    const char* inner_end = ib;
    /* tail: `</tag>` then end */
    p = ib + 1;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p + 2 + taglen + 1 > end || p[0] != '<' || p[1] != '/' ||
        memcmp(p + 2, tag, taglen) != 0)
        return NULL;
    p += 2 + taglen;
    if (p >= end || *p != '>') return NULL;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p != end) return NULL;
    /* inner must be a FLWOR with group by / order by */
    {
        const char* s2 = inner;
        while (s2 < inner_end && isspace((unsigned char)*s2)) s2++;
        if (s2 + 3 <= inner_end && memcmp(s2, "for", 3) == 0 &&
            (s2 + 3 == inner_end ||
             isspace((unsigned char)s2[3]))) {
            /* ok */
        } else if (s2 + 3 <= inner_end &&
                   memcmp(s2, "let", 3) == 0 &&
                   (s2 + 3 == inner_end ||
                    isspace((unsigned char)s2[3]))) {
            /* ok */
        } else {
            return NULL;
        }
    }
    Buf sb = {0};
    buf_put(&sb, inner, (size_t)(inner_end - inner));
    if (!sb.s) return NULL;
    sb.s[sb.len] = 0;
    int has_clause = 0;
    for (const char* c = sb.s; c + 5 <= sb.s + sb.len; c++) {
        if ((c == sb.s || isspace((unsigned char)c[-1])) &&
            memcmp(c, "group", 5) == 0 &&
            (c + 5 == sb.s + sb.len ||
             isspace((unsigned char)c[5])))
            has_clause = 1;
        if ((c == sb.s || isspace((unsigned char)c[-1])) &&
            memcmp(c, "order", 5) == 0 &&
            (c + 5 == sb.s + sb.len ||
             isspace((unsigned char)c[5])))
            has_clause = 1;
    }
    if (!has_clause) {
        free(sb.s);
        return NULL;
    }
    /* split at the FIRST depth-0 `return` */
    const char* split = NULL;
    {
        int d2 = 0;
        const char* c = sb.s;
        const char* se = sb.s + sb.len;
        while (c < se) {
            if (*c == '\'' || *c == '"') {
                char qt = *c++;
                while (c < se && *c != qt) c++;
                if (c < se) c++;
                continue;
            }
            if (c + 1 < se && c[0] == ':' && c[1] == '(') {
                c += 2;
                int cd = 1;
                while (c + 1 < se && cd > 0) {
                    if (c[0] == ':' && c[1] == '(') { cd++; c += 2; }
                    else if (c[0] == ')' && c[1] == ':') { cd--; c += 2; }
                    else c++;
                }
                continue;
            }
            if (*c == '<' && c + 1 < se && xq_is_name_start(c[1])) {
                c = xq_elem_ctor_end(c, se);
                if (!c) break;
                continue;
            }
            if (*c == '(' || *c == '[' || *c == '{') d2++;
            else if (*c == ')' || *c == ']' || *c == '}') {
                if (d2 > 0) d2--;
            } else if (d2 == 0 &&
                       (c == sb.s || !isalnum((unsigned char)c[-1])) &&
                       memcmp(c, "return", 6) == 0 &&
                       (c + 6 == se ||
                        isspace((unsigned char)c[6]))) {
                split = c;
                break;
            }
            c++;
        }
    }
    if (!split) {
        free(sb.s);
        return NULL;
    }
    size_t ret_off = (size_t)(split - sb.s) + 6;
    Buf out = {0};
    /* prefix: the PROLOG only — the ctor wraps the RESULT once at
     * eval (wrap_tag), not per tuple. */
    buf_put(&out, q, (size_t)(tag - 1 - q));
    buf_put(&out, sb.s, (size_t)(split - sb.s));
    buf_str(&out, " return ");
    buf_put(&out, sb.s + ret_off, sb.len - ret_off);
    free(sb.s);
    if (!out.s) return NULL;
    out.s[out.len] = 0;
    if (tag_out) *tag_out = span_dup(tag, tag + taglen);
    return out.s;
}

LEPTRIS_API LeptrisXQuery leptris_xquery_parse(const char* query,
                                               size_t len) {
    if (!query || !len) return NULL;
    /* ctor-enclosed group/order FLWOR: hoist the ctor into a
     * single result wrap (single level — the hoisted body starts
     * with for/let and does not rematch). */
    {
        char* wrap_tag = NULL;
        char* hoisted = xq_hoist_ctor_flwor(query, len, &wrap_tag);
        if (hoisted) {
            LeptrisXQuery h =
                leptris_xquery_parse(hoisted, strlen(hoisted));
            free(hoisted);
            if (h) {
                ((struct LeptrisXQueryInternal*)h)->wrap_tag =
                    wrap_tag;
                return h;
            }
            free(wrap_tag);
            /* fall through: parse the original */
        }
    }
    struct LeptrisXQueryInternal* q =
        (struct LeptrisXQueryInternal*)calloc(1, sizeof(*q));
    if (!q) return NULL;

    Scan s = {query, query + len};

    /* Prolog. */
    scan_ws(&s);
    for (;;) {
        /* XQuery version declaration: `xquery version 'X';`
         * (+ optional `encoding "enc";`) — accepted and skipped
         * (the engine's semantics are version-independent; #684). */
        {
            Scan t = s;
            const char* w;
            size_t wl = scan_word(&t, &w);
            if (wl && word_is(w, wl, "xquery")) {
                t.p = w + wl;
                scan_ws(&t);
                wl = scan_word(&t, &w);
                if (wl && word_is(w, wl, "version")) {
                    t.p = w + wl;
                    scan_ws(&t);
                    if (t.p < t.end &&
                        (*t.p == '"' || *t.p == '\'')) {
                        scan_string(&t);
                        scan_ws(&t);
                        /* optional encoding decl */
                        {
                            Scan e = t;
                            const char* ew;
                            size_t ewl = scan_word(&e, &ew);
                            if (ewl && word_is(ew, ewl, "encoding")) {
                                e.p = ew + ewl;
                                scan_ws(&e);
                                if (e.p < e.end &&
                                    (*e.p == '"' || *e.p == '\'')) {
                                    scan_string(&e);
                                    t = e;
                                    scan_ws(&t);
                                }
                            }
                        }
                        if (t.p < t.end && *t.p == ';') {
                            t.p++;
                            s = t;
                            scan_ws(&s);
                            continue;
                        }
                    }
                }
            }
        }
        const char* w;
        Scan t = s;
        size_t wl = scan_word(&t, &w);
        if (!wl || !word_is(w, wl, "declare")) break;
        t.p = w + wl;
        XqDecl d;
        if (!parse_decl(q, &t, &d)) {
            xq_free(q);
            return NULL;
        }
        XqDecl* grown = (XqDecl*)realloc(q->decls,
                                         (q->ndecls + 1) * sizeof(XqDecl));
        if (!grown) {
            xq_free(q);
            return NULL;
        }
        q->decls = grown;
        q->decls[q->ndecls++] = d;
        s = t;
        scan_ws(&s);
    }

    /* Body. */
    scan_ws(&s);
    {
        const char* w;
        Scan t = s;
        size_t wl = scan_word(&t, &w);
        if (wl && (word_is(w, wl, "for") || word_is(w, wl, "let"))) {
            /* FLWOR */
            for (;;) {
                scan_ws(&s);
                const char* kw;
                size_t kwl = scan_word(&s, &kw);
                if (!kwl) {
                    xq_free(q);
                    return NULL;
                }
                s.p = kw + kwl;
                if (word_is(kw, kwl, "stable")) continue;
                if (word_is(kw, kwl, "for") || word_is(kw, kwl, "let")) {
                    int is_for = word_is(kw, kwl, "for");
                    /* window clause: for tumbling|sliding window
                     * $w in D start ... end ... (TODO 12) */
                    {
                        Scan wk = s;
                        scan_ws(&wk);
                        const char* wword;
                        size_t wwl = scan_word(&wk, &wword);
                        if (is_for && wwl &&
                            (word_is(wword, wwl, "tumbling") ||
                             word_is(wword, wwl, "sliding"))) {
                            int sliding =
                                word_is(wword, wwl, "sliding");
                            s.p = wword + wwl;
                            scan_ws(&s);
                            const char* kw2;
                            size_t k2 = scan_word(&s, &kw2);
                            if (!k2 || !word_is(kw2, k2, "window")) {
                                xq_free(q);
                                return NULL;
                            }
                            s.p = kw2 + k2;
                            XqClause wc;
                            memset(&wc, 0, sizeof(wc));
                            wc.is_for = 2;
                            wc.win.sliding = sliding;
                            wc.win.win_var = parse_dollar_name(&s);
                            if (!wc.win.win_var) {
                                xq_free(q);
                                return NULL;
                            }
                            scan_ws(&s);
                            const char* iw;
                            size_t iwl = scan_word(&s, &iw);
                            if (!iwl || !word_is(iw, iwl, "in")) {
                                xq_clause_partial_free(&wc);
                                xq_free(q);
                                return NULL;
                            }
                            s.p = iw + iwl;
                            Scan e = s;
                            scan_expr_segment(&e, 0);
                            wc.win.domain = parse_expr_span(s.p, e.p);
                            if (!wc.win.domain) {
                                xq_clause_partial_free(&wc);
                                xq_free(q);
                                return NULL;
                            }
                            s = e;
                            /* start clause */
                            scan_ws(&s);
                            const char* sw;
                            size_t swl = scan_word(&s, &sw);
                            if (!swl || !word_is(sw, swl, "start")) {
                                xq_clause_partial_free(&wc);
                                xq_free(q);
                                return NULL;
                            }
                            s.p = sw + swl;
                            scan_ws(&s);
                            if (s.p < s.end && *s.p == '$') {
                                wc.win.s_var = parse_dollar_name(&s);
                                if (!wc.win.s_var) {
                                    xq_clause_partial_free(&wc);
                                    xq_free(q);
                                    return NULL;
                                }
                                scan_ws(&s);
                                const char* aw;
                                size_t awl = scan_word(&s, &aw);
                                if (awl && word_is(aw, awl, "at")) {
                                    s.p = aw + awl;
                                    wc.win.s_pos_var =
                                        parse_dollar_name(&s);
                                    if (!wc.win.s_pos_var) {
                                        xq_clause_partial_free(&wc);
                                        xq_free(q);
                                        return NULL;
                                    }
                                    scan_ws(&s);
                                }
                            }
                            {
                                const char* ww;
                                size_t wwl2 = scan_word(&s, &ww);
                                if (wwl2 && word_is(ww, wwl2, "when")) {
                                    s.p = ww + wwl2;
                                    Scan se = s;
                                    scan_expr_segment(&se, 0);
                                    wc.win.s_when =
                                        parse_expr_span(s.p, se.p);
                                    if (!wc.win.s_when) {
                                        xq_clause_partial_free(&wc);
                                        xq_free(q);
                                        return NULL;
                                    }
                                    s = se;
                                }
                            }
                            /* optional end clause */
                            scan_ws(&s);
                            {
                                const char* ew;
                                size_t ewl = scan_word(&s, &ew);
                                if (ewl && word_is(ew, ewl, "end")) {
                                    s.p = ew + ewl;
                                    scan_ws(&s);
                                    if (s.p < s.end && *s.p == '$') {
                                        wc.win.e_var =
                                            parse_dollar_name(&s);
                                        if (!wc.win.e_var) {
                                            xq_clause_partial_free(&wc);
                                            xq_free(q);
                                            return NULL;
                                        }
                                        scan_ws(&s);
                                        const char* aw;
                                        size_t awl = scan_word(&s, &aw);
                                        if (awl &&
                                            word_is(aw, awl, "at")) {
                                            s.p = aw + awl;
                                            wc.win.e_pos_var =
                                                parse_dollar_name(&s);
                                            if (!wc.win.e_pos_var) {
                                                xq_clause_partial_free(
                                                    &wc);
                                                xq_free(q);
                                                return NULL;
                                            }
                                            scan_ws(&s);
                                        }
                                    }
                                    const char* ww;
                                    size_t wwl2 = scan_word(&s, &ww);
                                    if (wwl2 && word_is(ww, wwl2, "when")) {
                                        s.p = ww + wwl2;
                                        Scan ee = s;
                                        scan_expr_segment(&ee, 0);
                                        wc.win.e_when =
                                            parse_expr_span(s.p, ee.p);
                                        if (!wc.win.e_when) {
                                            xq_clause_partial_free(&wc);
                                            xq_free(q);
                                            return NULL;
                                        }
                                        s = ee;
                                    }
                                }
                            }
                            XqClause* grown =
                                (XqClause*)realloc(
                                    q->clauses, (q->nclauses + 1) *
                                                    sizeof(XqClause));
                            if (!grown) {
                                xq_clause_partial_free(&wc);
                                xq_free(q);
                                return NULL;
                            }
                            q->clauses = grown;
                            q->clauses[q->nclauses++] = wc;
                            continue;
                        }
                    }
                    /* Binding list: one clause binds
                     * VAR (:=|in) ExprSingle ("," VAR ...)*. */
                    for (;;) {
                        char* var = parse_dollar_name(&s);
                        if (!var) {
                            xq_free(q);
                            return NULL;
                        }
                        char* pos_var = NULL;
                        scan_ws(&s);
                        if (is_for) {
                            /* positional: for $x at $i in ... */
                            Scan at = s;
                            const char* aw;
                            size_t awl = scan_word(&at, &aw);
                            if (awl && word_is(aw, awl, "at") &&
                                aw + awl < s.end) {
                                s.p = aw + awl;
                                pos_var = parse_dollar_name(&s);
                                if (!pos_var) {
                                    free(var);
                                    xq_free(q);
                                    return NULL;
                                }
                                scan_ws(&s);
                            }
                            const char* iw;
                            size_t iwl = scan_word(&s, &iw);
                            if (!iwl || !word_is(iw, iwl, "in")) {
                                free(var);
                                xq_free(q);
                                return NULL;
                            }
                            s.p = iw + iwl;
                        } else {
                            /* optional `as SequenceType` — accepted
                             * and ignored (value model is dynamic) */
                            {
                                Scan at2 = s;
                                const char* aw2;
                                size_t aw2l = scan_word(&at2, &aw2);
                                if (aw2l && word_is(aw2, aw2l, "as")) {
                                    at2.p = aw2 + aw2l;
                                    scan_ws(&at2);
                                    const char* tw3 = at2.p;
                                    while (at2.p < at2.end &&
                                           (isalnum((unsigned char)*at2.p) ||
                                            *at2.p == ':' || *at2.p == '_' ||
                                            *at2.p == '.' || *at2.p == '-'))
                                        at2.p++;
                                    if (at2.p > tw3) {
                                        scan_ws(&at2);
                                        if (at2.p < at2.end &&
                                            (*at2.p == '?' || *at2.p == '*' ||
                                             *at2.p == '+'))
                                            at2.p++;
                                        s = at2;
                                        scan_ws(&s);
                                    }
                                }
                            }
                            if (s.p + 1 >= s.end || s.p[0] != ':' ||
                                s.p[1] != '=') {
                                free(var);
                                xq_free(q);
                                return NULL;
                            }
                            s.p += 2;
                        }
                        Scan e = s;
                        scan_expr_segment(&e, 1);
                        XPathASTNode* expr = parse_expr_span(s.p, e.p);
                        if (!expr) {
                            free(var);
                            xq_free(q);
                            return NULL;
                        }
                        XqClause* grown = (XqClause*)realloc(
                            q->clauses,
                            (q->nclauses + 1) * sizeof(XqClause));
                        if (!grown) {
                            free(var);
                            ast_node_free(expr);
                            xq_free(q);
                            return NULL;
                        }
                        q->clauses = grown;
                        q->clauses[q->nclauses].is_for = is_for;
                        q->clauses[q->nclauses].var = var;
                        q->clauses[q->nclauses].pos_var = pos_var;
                        q->clauses[q->nclauses].expr = expr;
                        q->nclauses++;
                        s = e;
                        scan_ws(&s);
                        if (s.p < s.end && *s.p == ',') {
                            s.p++;
                            scan_ws(&s);
                            continue;
                        }
                        break;
                    }
                } else if (word_is(kw, kwl, "where")) {
                    Scan e = s;
                    scan_expr_segment(&e, 0);
                    /* WHERE after GROUP BY filters the GROUPED
                     * tuples (aggregates see the whole group) */
                    XPathASTNode* w_ast = parse_expr_span(s.p, e.p);
                    if (!w_ast) {
                        xq_free(q);
                        return NULL;
                    }
                    if (q->ngroup) {
                        if (q->post_where_ast)
                            ast_node_free(w_ast);
                        else
                            q->post_where_ast = w_ast;
                    } else {
                        if (q->where_ast)
                            ast_node_free(w_ast);
                        else
                            q->where_ast = w_ast;
                    }
                    s = e;
                } else if (word_is(kw, kwl, "group")) {
                    scan_ws(&s);
                    const char* bw;
                    size_t bwl = scan_word(&s, &bw);
                    if (!bwl || !word_is(bw, bwl, "by")) {
                        xq_free(q);
                        return NULL;
                    }
                    s.p = bw + bwl;
                    /* GroupingSpec list: `$k := E` or bare `$k`
                     * (key = the variable's own value), comma
                     * separated (XQuery 3.0 §3.8.1). */
                    for (;;) {
                        scan_ws(&s);
                        const char* vstart = s.p;
                        char* gvar = parse_dollar_name(&s);
                        if (!gvar) {
                            xq_free(q);
                            return NULL;
                        }
                        const char* vend = s.p;
                        scan_ws(&s);
                        /* optional `as SequenceType` — accepted and
                         * ignored (the value model is dynamic) */
                        {
                            Scan at = s;
                            const char* aw;
                            size_t awl = scan_word(&at, &aw);
                            if (awl && word_is(aw, awl, "as")) {
                                at.p = aw + awl;
                                scan_ws(&at);
                                const char* tw2 = at.p;
                                while (at.p < at.end &&
                                       (isalnum((unsigned char)*at.p) ||
                                        *at.p == ':' || *at.p == '_' ||
                                        *at.p == '.' || *at.p == '-'))
                                    at.p++;
                                if (at.p > tw2) {
                                    scan_ws(&at);
                                    if (at.p < at.end &&
                                        (*at.p == '?' || *at.p == '*' ||
                                         *at.p == '+'))
                                        at.p++;
                                    s = at;
                                    scan_ws(&s);
                                }
                            }
                        }
                        XPathASTNode* gk;
                        if (s.p + 1 < s.end && s.p[0] == ':' &&
                            s.p[1] == '=') {
                            s.p += 2;
                            Scan e = s;
                            scan_expr_segment(&e, 0);
                            gk = parse_expr_span(s.p, e.p);
                            if (!gk) {
                                free(gvar);
                                xq_free(q);
                                return NULL;
                            }
                            s = e;
                        } else {
                            gk = parse_expr_span(vstart, vend);
                            if (!gk) {
                                free(gvar);
                                xq_free(q);
                                return NULL;
                            }
                        }
                        char** gv = (char**)realloc(
                            q->group_vars,
                            (q->ngroup + 1) * sizeof(char*));
                        if (!gv) {
                            free(gvar);
                            ast_node_free(gk);
                            xq_free(q);
                            return NULL;
                        }
                        q->group_vars = gv;
                        XPathASTNode** gks = (XPathASTNode**)realloc(
                            q->group_keys,
                            (q->ngroup + 1) * sizeof(XPathASTNode*));
                        if (!gks) {
                            free(gvar);
                            ast_node_free(gk);
                            xq_free(q);
                            return NULL;
                        }
                        q->group_keys = gks;
                        q->group_vars[q->ngroup] = gvar;
                        q->group_keys[q->ngroup] = gk;
                        q->ngroup++;
                        scan_ws(&s);
                        /* optional CollationSpec: accepted and
                         * ignored (codepoint is the engine's
                         * ordering) */
                        {
                            Scan ct = s;
                            const char* cw;
                            size_t cwl = scan_word(&ct, &cw);
                            if (cwl && word_is(cw, cwl, "collation")) {
                                ct.p = cw + cwl;
                                scan_ws(&ct);
                                if (ct.p < ct.end &&
                                    (*ct.p == '"' || *ct.p == '\'')) {
                                    scan_string(&ct);
                                    s = ct;
                                    scan_ws(&s);
                                }
                            }
                        }
                        if (s.p < s.end && *s.p == ',') {
                            s.p++;
                            continue;
                        }
                        break;
                    }
                } else if (word_is(kw, kwl, "order")) {
                    scan_ws(&s);
                    const char* bw;
                    size_t bwl = scan_word(&s, &bw);
                    if (!bwl || !word_is(bw, bwl, "by")) {
                        xq_free(q);
                        return NULL;
                    }
                    s.p = bw + bwl;
                    size_t kclause =
                        q->nkeys ? q->keys[q->nkeys - 1].clause + 1 : 0;
                    for (;;) {
                        scan_ws(&s);
                        Scan e = s;
                        scan_expr_segment(&e, 1);   /* stop at ',' */
                        XPathASTNode* key = parse_expr_span(s.p, e.p);
                        if (!key) {
                            xq_free(q);
                            return NULL;
                        }
                        int desc = 0;
                        int eleast = 0;
                        s = e;
                        scan_ws(&s);
                        const char* dw;
                        Scan dt = s;
                        size_t dwl = scan_word(&dt, &dw);
                        if (dwl && (word_is(dw, dwl, "descending"))) {
                            desc = 1;
                            s.p = dw + dwl;
                            scan_ws(&s);
                            dt = s;
                            dwl = scan_word(&dt, &dw);
                        } else if (dwl &&
                                   word_is(dw, dwl, "ascending")) {
                            s.p = dw + dwl;
                            scan_ws(&s);
                            dt = s;
                            dwl = scan_word(&dt, &dw);
                        }
                        /* `empty greatest|least` — the empty-sequence
                         * ordering mode; optional `collation "URI"`
                         * is accepted (codepoint ordering). */
                        if (dwl && word_is(dw, dwl, "empty")) {
                            Scan et = dt;
                            const char* ew;
                            size_t ewl = scan_word(&et, &ew);
                            if (ewl &&
                                (word_is(ew, ewl, "least") ||
                                 word_is(ew, ewl, "greatest"))) {
                                eleast = word_is(ew, ewl, "least");
                                s.p = ew + ewl;
                                scan_ws(&s);
                                dt = s;
                                dwl = scan_word(&dt, &dw);
                            }
                        }
                        if (dwl && word_is(dw, dwl, "collation")) {
                            Scan ct2 = dt;
                            const char* cw2;
                            size_t c2l = scan_word(&ct2, &cw2);
                            (void)c2l;
                            (void)cw2;
                            s.p = dw + dwl;
                            scan_ws(&s);
                            if (s.p < s.end &&
                                (*s.p == '"' || *s.p == '\'')) {
                                scan_string(&s);
                                scan_ws(&s);
                            }
                        }
                        XqOrderKey* grown = (XqOrderKey*)realloc(
                            q->keys, (q->nkeys + 1) * sizeof(XqOrderKey));
                        if (!grown) {
                            ast_node_free(key);
                            xq_free(q);
                            return NULL;
                        }
                        q->keys = grown;
                        q->keys[q->nkeys].key = key;
                        q->keys[q->nkeys].descending = desc;
                        q->keys[q->nkeys].empty_least = eleast;
                        q->keys[q->nkeys].clause = kclause;
                        q->nkeys++;
                        scan_ws(&s);
                        if (s.p < s.end && *s.p == ',') {
                            s.p++;
                            continue;
                        }
                        break;
                    }
                } else if (word_is(kw, kwl, "return")) {
                    q->return_ast = parse_expr_span(s.p, s.end);
                    if (!q->return_ast) {
                        xq_free(q);
                        return NULL;
                    }
                    return q;
                } else {
                    xq_free(q);
                    return NULL;
                }
            }
        }
    }

    /* Plain XPath expression body. */
    q->return_ast = parse_expr_span(s.p, s.end);
    if (!q->return_ast) {
        xq_free(q);
        return NULL;
    }
    return q;
}

static LeptrisXPathResult xq_eval_impl(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* param_names, const char* const* param_selects,
    size_t nparams);

LEPTRIS_API LeptrisXPathResult leptris_xquery_eval(LeptrisXQuery query,
                                                   LeptrisDocument doc,
                                                   LeptrisElement context_node) {
    return xq_eval_impl(query, doc, context_node, NULL, NULL, 0);
}

LEPTRIS_API LeptrisXPathResult leptris_xquery_eval_params(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* names, const char* const* selects, size_t count) {
    if (count && (!names || !selects)) return NULL;
    return xq_eval_impl(query, doc, context_node, names, selects, count);
}

LEPTRIS_API void leptris_xquery_free(LeptrisXQuery query) {
    xq_free((struct LeptrisXQueryInternal*)query);
}

/* ---- evaluation ---- */

/* local:function thunk: user_data = "\x03FN..." closure content. */
static struct leptris_xpath_result* xq_fn_thunk(XPathContext* ctx,
                                                XPathASTNode** args,
                                                size_t arg_count) {
    const char* cc = (const char*)ctx->current_fn_user_data;
    if (!cc) return NULL;
    char** argv = NULL;
    if (arg_count) {
        argv = (char**)calloc(arg_count, sizeof(char*));
        if (!argv) return NULL;
    }
    struct leptris_xpath_result* out = NULL;
    int ok = 1;
    for (size_t i = 0; i < arg_count; i++) {
        struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
        char* v = r ? xpath_to_string(r) : NULL;
        if (r) xpath_result_free(r);
        argv[i] = v ? v : strdup("");
        if (!argv[i]) ok = 0;
    }
    if (ok) out = xpath_call_function_item(ctx, cc, argv, arg_count);
    for (size_t i = 0; i < arg_count; i++) free(argv[i]);
    free(argv);
    return out;
}

/* Bind a variable to an evaluated result (FOR/LET/prolog): nodeset
 * results deep-copy synthetic members; scalars ride one synthetic
 * member (numeric-marker discipline). */
static int xq_bind(XPathContext* ctx, const char* name,
                   struct leptris_xpath_result* v) {
    XPathNodeSet* one;
    if (v->type == XPATH_RESULT_NODESET && v->value.nodeset_value) {
        one = xpath_nodeset_deep_copy(v->value.nodeset_value);
    } else {
        one = xpath_nodeset_new();
        if (one) {
            one->owns_synthetic_text = 1;
            if (v->type == XPATH_RESULT_NUMBER) {
                char* s = xpath_to_string(v);
                size_t sl = s ? strlen(s) : 0;
                char* marked = (char*)malloc(sl + 3);
                if (marked) {
                    marked[0] = '\x03';
                    marked[1] = 'N';
                    if (sl) memcpy(marked + 2, s, sl);
                    marked[2 + sl] = 0;
                    XPathTextNode* tn =
                        xpath_synth_text(marked, sl + 2);
                    free(marked);
                    if (tn) xpath_nodeset_add(one, tn);
                }
                free(s);
            } else {
                char* s = xpath_to_string(v);
                XPathTextNode* tn =
                    xpath_synth_text(s ? s : "", s ? strlen(s) : 0);
                free(s);
                if (tn) xpath_nodeset_add(one, tn);
            }
        }
    }
    if (!one) return 0;
    XPathVariable* var = xpath_variable_set_add(
        (XPathVariableSet*)ctx->variable_set, name,
        XPATH_VAR_TYPE_NODE_SET);
    if (!var) {
        xpath_nodeset_free(one);
        return 0;
    }
    xpath_variable_set_nodeset(var, one);   /* var owns it */
    return 1;
}

/* Tuple snapshot: per var, a LIST of members (nodes or raw
 * synthetic contents) — group by rebinds vars to whole-group
 * sequences. */
typedef struct {
    char** names;
    char*** contents;  /* contents[i][j]; NULL member = node */
    void*** nodes;
    unsigned char** owns_node;  /* per member: tuple owns (disposable) */
    size_t* counts;
    size_t n;
    char** keys;       /* order-key strings */
    size_t nkeys;
} XqTuple;

/* Clone a varset-owned synthetic member so the TUPLE outlives the
 * variable binding it was captured from (attribute/namespace nodes
 * die at unbind — dangling keys were the QT3 group-by reds). */
static void* xq_clone_synth(void* nd) {
    int ty = XPATH_NODE_TYPE(nd);
    if (ty == LEPTRIS_NODE_ATTRIBUTE) {
        LeptrisAttributeNode* a = (LeptrisAttributeNode*)nd;
        LeptrisAttributeNode* c = LEPTRIS_ALLOC(LeptrisAttributeNode);
        if (!c) return NULL;
        memset(c, 0, sizeof(*c));
        c->node_type = LEPTRIS_NODE_ATTRIBUTE;
        c->name = a->name ? leptris_strdup(a->name) : NULL;
        c->value = a->value ? leptris_strdup(a->value) : NULL;
        c->namespace_uri = a->namespace_uri
                               ? leptris_strdup(a->namespace_uri) : NULL;
        c->owner = a->owner;
        return c;
    }
    if (ty == LEPTRIS_NODE_NAMESPACE) {
        LeptrisNamespaceNode* ns = (LeptrisNamespaceNode*)nd;
        LeptrisNamespaceNode* c = LEPTRIS_ALLOC(LeptrisNamespaceNode);
        if (!c) return NULL;
        memset(c, 0, sizeof(*c));
        c->node_type = LEPTRIS_NODE_NAMESPACE;
        c->prefix = ns->prefix ? leptris_strdup(ns->prefix) : NULL;
        c->uri = ns->uri ? leptris_strdup(ns->uri) : NULL;
        c->owner = ns->owner;
        return c;
    }
    return NULL;
}

/* Capture one nodeset member into tuple storage. Synthetic
 * attribute/namespace members are CLONED (tuple-owned); text goes
 * to contents; document nodes are shared. */
static void xq_capture_member(void* nd, char** content_out,
                              void** node_out,
                              unsigned char* owns_out) {
    *content_out = NULL;
    *node_out = NULL;
    *owns_out = 0;
    if (!nd) return;
    int ty = XPATH_NODE_TYPE(nd);
    if (ty == LEPTRIS_NODE_ATTRIBUTE || ty == LEPTRIS_NODE_NAMESPACE) {
        void* c = xq_clone_synth(nd);
        if (c) {
            *node_out = c;
            *owns_out = 1;
            return;
        }
        /* OOM fallback: keep the string value at least */
        char* s = get_node_text(nd);
        *content_out = s ? s : strdup("");
        return;
    }
    if (ty == LEPTRIS_NODE_TEXT) {
        const char* c = ((XPathTextNode*)nd)->content;
        *content_out = strdup(c ? c : "");
        return;
    }
    *node_out = nd;
}

static void xq_tuple_free(XqTuple* t) {
    for (size_t i = 0; i < t->n; i++) {
        free(t->names[i]);
        for (size_t j = 0; j < t->counts[i]; j++) {
            if (t->contents[i]) free(t->contents[i][j]);
            if (t->owns_node && t->owns_node[i] && t->nodes[i] &&
                t->owns_node[i][j])
                xpath_nodeset_dispose_node(t->nodes[i][j]);
        }
        free(t->contents[i]);
        free(t->nodes[i]);
        if (t->owns_node) free(t->owns_node[i]);
    }
    free(t->names);
    free(t->contents);
    free(t->nodes);
    free(t->owns_node);
    free(t->counts);
    for (size_t i = 0; i < t->nkeys; i++) free(t->keys[i]);
    free(t->keys);
}

static void xq_unbind_all(XPathContext* ctx, XqClause* clauses,
                          size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (clauses[i].is_for == 2) {
            XqWindow* w = &clauses[i].win;
            if (w->win_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set,
                    w->win_var);
            if (w->s_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set, w->s_var);
            if (w->s_pos_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set,
                    w->s_pos_var);
            if (w->e_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set, w->e_var);
            if (w->e_pos_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set,
                    w->e_pos_var);
            continue;
        }
        xpath_variable_set_remove((XPathVariableSet*)ctx->variable_set,
                                  clauses[i].var);
        if (clauses[i].pos_var)
            xpath_variable_set_remove(
                (XPathVariableSet*)ctx->variable_set,
                clauses[i].pos_var);
    }
}

/* Snapshot the current clause bindings. */
static int xq_snapshot(XPathContext* ctx, XqClause* clauses, size_t n,
                       XqTuple* t) {
    /* var + optional pos var per clause; windows carry up to 5 */
    t->names = (char**)calloc(6 * n + 1, sizeof(char*));
    t->contents = (char***)calloc(6 * n + 1, sizeof(char**));
    t->nodes = (void***)calloc(6 * n + 1, sizeof(void**));
    t->owns_node =
        (unsigned char**)calloc(6 * n + 1, sizeof(unsigned char*));
    t->counts = (size_t*)calloc(6 * n + 1, sizeof(size_t));
    t->n = 0;
    t->keys = NULL;
    t->nkeys = 0;
    if (!t->names || !t->contents || !t->nodes || !t->owns_node ||
        !t->counts)
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (clauses[i].is_for == 2) {
            /* window: $w is the whole member list; the boundary
             * vars are single members. */
            XqWindow* w = &clauses[i].win;
            const char* singles[4];
            singles[0] = w->s_var;
            singles[1] = w->s_pos_var;
            singles[2] = w->e_var;
            singles[3] = w->e_pos_var;
            if (w->win_var) {
                XPathVariable* var = xpath_variable_set_get(
                    (XPathVariableSet*)ctx->variable_set, w->win_var);
                if (var && var->value.v.nodeset_value) {
                    XPathNodeSet* ns = var->value.v.nodeset_value;
                    t->names[t->n] = strdup(w->win_var);
                    t->contents[t->n] = (char**)calloc(
                        ns->count ? ns->count : 1, sizeof(char*));
                    t->nodes[t->n] = (void**)calloc(
                        ns->count ? ns->count : 1, sizeof(void*));
                    t->owns_node[t->n] = (unsigned char*)calloc(
                        ns->count ? ns->count : 1, 1);
                    t->counts[t->n] = ns->count;
                    if (!t->contents[t->n] || !t->nodes[t->n] ||
                        !t->owns_node[t->n])
                        return 0;
                    for (size_t m = 0; m < ns->count; m++)
                        xq_capture_member(
                            ns->nodes[m], &t->contents[t->n][m],
                            &t->nodes[t->n][m], &t->owns_node[t->n][m]);
                    t->n++;
                }
            }
            for (int k = 0; k < 4; k++) {
                if (!singles[k]) continue;
                XPathVariable* var = xpath_variable_set_get(
                    (XPathVariableSet*)ctx->variable_set, singles[k]);
                if (!var) continue;
                XPathNodeSet* ns = var->value.v.nodeset_value;
                void* node = (ns && ns->count) ? ns->nodes[0] : NULL;
                t->names[t->n] = strdup(singles[k]);
                t->contents[t->n] = (char**)calloc(1, sizeof(char*));
                t->nodes[t->n] = (void**)calloc(1, sizeof(void*));
                t->owns_node[t->n] = (unsigned char*)calloc(1, 1);
                t->counts[t->n] = 1;
                if (!t->contents[t->n] || !t->nodes[t->n] ||
                    !t->owns_node[t->n])
                    return 0;
                xq_capture_member(node, &t->contents[t->n][0],
                                  &t->nodes[t->n][0],
                                  &t->owns_node[t->n][0]);
                t->n++;
            }
            continue;
        }
        const char* names[2];
        names[0] = clauses[i].var;
        names[1] = clauses[i].pos_var;
        for (int k = 0; k < 2; k++) {
            if (!names[k]) continue;
            if (t->n >= 6 * n) break;
            XPathVariable* var = xpath_variable_set_get(
                (XPathVariableSet*)ctx->variable_set, names[k]);
            if (!var) continue;
            XPathNodeSet* ns = var->value.v.nodeset_value;
            if (clauses[i].is_for == 0 && k == 0 && ns && ns->count) {
                /* LET: the binding is the whole sequence, not the
                 * first member (group semantics reuse this list
                 * form). */
                size_t cnt = ns->count;
                t->names[t->n] = strdup(names[k]);
                t->contents[t->n] = (char**)calloc(cnt, sizeof(char*));
                t->nodes[t->n] = (void**)calloc(cnt, sizeof(void*));
                t->owns_node[t->n] = (unsigned char*)calloc(cnt, 1);
                t->counts[t->n] = cnt;
                if (!t->contents[t->n] || !t->nodes[t->n] ||
                    !t->owns_node[t->n])
                    return 0;
                for (size_t m = 0; m < cnt; m++)
                    xq_capture_member(ns->nodes[m],
                                      &t->contents[t->n][m],
                                      &t->nodes[t->n][m],
                                      &t->owns_node[t->n][m]);
                t->n++;
                continue;
            }
            void* node = (ns && ns->count) ? ns->nodes[0] : NULL;
            t->names[t->n] = strdup(names[k]);
            t->contents[t->n] = (char**)calloc(1, sizeof(char*));
            t->nodes[t->n] = (void**)calloc(1, sizeof(void*));
            t->owns_node[t->n] = (unsigned char*)calloc(1, 1);
            t->counts[t->n] = 1;
            if (!t->contents[t->n] || !t->nodes[t->n] ||
                !t->owns_node[t->n])
                return 0;
            xq_capture_member(node, &t->contents[t->n][0],
                              &t->nodes[t->n][0], &t->owns_node[t->n][0]);
            t->n++;
        }
    }
    return 1;
}

/* Rebind a snapshot into the context (member lists join as one
 * nodeset — group sequences included). */
static int xq_rebind(XPathContext* ctx, const XqTuple* t) {
    for (size_t i = 0; i < t->n; i++) {
        XPathNodeSet* one = xpath_nodeset_new();
        if (!one) return 0;
        for (size_t j = 0; j < t->counts[i]; j++) {
            if (t->nodes[i][j]) {
                void* nd = t->nodes[i][j];
                int ty = XPATH_NODE_TYPE(nd);
                /* Attribute/namespace members are CLONED into the
                 * varset so the binding is freed independently of
                 * the tuple — sharing left a UAF when group-by
                 * freed the source tuples while the last rebind's
                 * vars still held the pointers (ASAN on
                 * OrderByClause). Document elements stay shared. */
                if (ty == LEPTRIS_NODE_ATTRIBUTE ||
                    ty == LEPTRIS_NODE_NAMESPACE) {
                    void* c = xq_clone_synth(nd);
                    if (!c) {
                        xpath_nodeset_free(one);
                        return 0;
                    }
                    xpath_nodeset_add(one, c);
                    if (ty == LEPTRIS_NODE_ATTRIBUTE)
                        one->owns_attributes = 1;
                    else
                        one->owns_namespaces = 1;
                } else {
                    xpath_nodeset_add(one, nd);
                }
            } else if (t->contents[i][j]) {
                XPathTextNode* tn = xpath_synth_text(
                    t->contents[i][j], strlen(t->contents[i][j]));
                if (tn) xpath_nodeset_add(one, tn);
            }
        }
        one->owns_synthetic_text = 1;
        XPathVariable* var = xpath_variable_set_add(
            (XPathVariableSet*)ctx->variable_set, t->names[i],
            XPATH_VAR_TYPE_NODE_SET);
        if (!var) {
            xpath_nodeset_free(one);
            return 0;
        }
        xpath_variable_set_nodeset(var, one);
    }
    return 1;
}

static void xq_window_bind(XPathContext* ctx, XqWindow* w,
                           XPathNodeSet* ns, size_t start,
                           size_t n) {
    (void)n;
    if (w->s_var) {
        XPathNodeSet* one = xpath_nodeset_new();
        if (one) {
            xpath_nodeset_add(one, ns->nodes[start]);
            XPathVariable* var = xpath_variable_set_add(
                (XPathVariableSet*)ctx->variable_set, w->s_var,
                XPATH_VAR_TYPE_NODE_SET);
            if (var) xpath_variable_set_nodeset(var, one);
            else xpath_nodeset_free(one);
        }
    }
    if (w->s_pos_var) {
        char nb[24];
        int nl = snprintf(nb, sizeof(nb), "\x03N%zu", start + 1);
        XPathNodeSet* pone = xpath_nodeset_new();
        if (pone) {
            pone->owns_synthetic_text = 1;
            XPathTextNode* ptn = xpath_synth_text(nb, (size_t)nl);
            if (ptn) xpath_nodeset_add(pone, ptn);
            XPathVariable* var = xpath_variable_set_add(
                (XPathVariableSet*)ctx->variable_set,
                w->s_pos_var, XPATH_VAR_TYPE_NODE_SET);
            if (var) xpath_variable_set_nodeset(var, pone);
            else xpath_nodeset_free(pone);
        }
    }
}

static void xq_window_unbind(XPathContext* ctx, XqWindow* w) {
    if (w->s_var)
        xpath_variable_set_remove(
            (XPathVariableSet*)ctx->variable_set, w->s_var);
    if (w->s_pos_var)
        xpath_variable_set_remove(
            (XPathVariableSet*)ctx->variable_set, w->s_pos_var);
    if (w->e_var)
        xpath_variable_set_remove(
            (XPathVariableSet*)ctx->variable_set, w->e_var);
    if (w->e_pos_var)
        xpath_variable_set_remove(
            (XPathVariableSet*)ctx->variable_set, w->e_pos_var);
}

static int key_cmp(const char* a, const char* b) {
    char *ea = NULL, *eb = NULL;
    double va = strtod(a, &ea);
    double vb = strtod(b, &eb);
    int na = ea && *ea == '\0' && ea != a;
    int nb = eb && *eb == '\0' && eb != b;
    if (na && nb) {
        /* XQuery order by (Saxon/QT3): NaN sorts as the LEAST
         * numeric value — first ascending, last descending; INF
         * sorts above every finite value. */
        int ana = (va != va);
        int bnb = (vb != vb);
        if (ana || bnb) {
            if (ana && bnb) return 0;
            return ana ? -1 : 1;
        }
        return (va < vb) ? -1 : (va > vb) ? 1 : 0;
    }
    return strcmp(a, b);
}

/* Enumerate tuples (phase 1): recursive clause walker. */
static int xq_enumerate(struct LeptrisXQueryInternal* q, XPathContext* ctx,
                        size_t idx, XqTuple** out, size_t* out_n,
                        size_t* out_cap) {
    if (idx == q->nclauses) {
        if (q->where_ast) {
            struct leptris_xpath_result* w =
                evaluate_expr(ctx, q->where_ast);
            int truth = w ? xpath_to_boolean(w) : 0;
            if (w) xpath_result_free(w);
            if (!truth) return 1;   /* filtered out, not an error */
        }
        if (*out_n == *out_cap) {
            *out_cap = *out_cap ? *out_cap * 2 : 8;
            *out = (XqTuple*)realloc(*out, *out_cap * sizeof(XqTuple));
            if (!*out) return 0;
        }
        XqTuple* t = &(*out)[(*out_n)++];
        memset(t, 0, sizeof(*t));
        if (!xq_snapshot(ctx, q->clauses, q->nclauses, t)) return 0;
        /* Order keys are evaluated at the EVAL phase — after
         * group by, they see the grouped bindings (Saxon). */
        return 1;
    }

    XqClause* c = &q->clauses[idx];
    if (c->is_for == 2) {
        /* Window clause (TODO 12): enumerate windows, each binding
         * $w to the member list and the boundary vars. */
        XqWindow* w = &c->win;
        struct leptris_xpath_result* domain =
            evaluate_expr(ctx, w->domain);
        if (!domain) return 0;
        int ok = 1;
        if (domain->type != XPATH_RESULT_NODESET ||
            !domain->value.nodeset_value) {
            xpath_result_free(domain);
            return 1;   /* empty/atomic domain: no windows */
        }
        XPathNodeSet* ns = domain->value.nodeset_value;
        size_t n = ns->count;
        size_t start = 0;
        while (start < n && ok) {
            /* start condition */
            int starts = 1;
            if (w->s_when) {
                xq_window_unbind(ctx, w);
                xq_window_bind(ctx, w, ns, start, n);
                struct leptris_xpath_result* r =
                    evaluate_expr(ctx, w->s_when);
                starts = r ? xpath_to_boolean(r) : 0;
                if (r) xpath_result_free(r);
            }
            if (!starts) {
                start++;
                continue;
            }
            /* end: first position >= start whose condition holds
             * (default: the last item). */
            size_t end = n - 1;
            if (w->e_when) {
                end = 0;
                for (size_t p = start + 1; p < n; p++) {
                    xq_window_unbind(ctx, w);
                    xq_window_bind(ctx, w, ns, start, n);
                    /* candidate end at p */
                    if (w->e_var) {
                        XPathNodeSet* one = xpath_nodeset_new();
                        if (one) {
                            xpath_nodeset_add(one, ns->nodes[p]);
                            XPathVariable* var = xpath_variable_set_add(
                                (XPathVariableSet*)ctx->variable_set,
                                w->e_var, XPATH_VAR_TYPE_NODE_SET);
                            if (var)
                                xpath_variable_set_nodeset(var, one);
                            else
                                xpath_nodeset_free(one);
                        }
                    }
                    if (w->e_pos_var) {
                        char nb[24];
                        int nl2 = snprintf(nb, sizeof(nb),
                                           "\x03N%zu", p + 1);
                        XPathNodeSet* pone = xpath_nodeset_new();
                        if (pone) {
                            pone->owns_synthetic_text = 1;
                            XPathTextNode* ptn =
                                xpath_synth_text(nb, (size_t)nl2);
                            if (ptn) xpath_nodeset_add(pone, ptn);
                            XPathVariable* var =
                                xpath_variable_set_add(
                                    (XPathVariableSet*)
                                        ctx->variable_set,
                                    w->e_pos_var,
                                    XPATH_VAR_TYPE_NODE_SET);
                            if (var)
                                xpath_variable_set_nodeset(var, pone);
                            else
                                xpath_nodeset_free(pone);
                        }
                    }
                    struct leptris_xpath_result* r =
                        evaluate_expr(ctx, w->e_when);
                    int ends = r ? xpath_to_boolean(r) : 0;
                    if (r) xpath_result_free(r);
                    if (ends) {
                        end = p;
                        break;
                    }
                }
                if (end == 0 && start + 1 <= n - 1 && n > 0) {
                    /* no end matched: window runs to the end */
                    end = n - 1;
                } else if (end == 0) {
                    end = n - 1;
                }
            }
            /* bind the window vars and recurse */
            xq_window_unbind(ctx, w);
            {
                /* $w = member list */
                XPathNodeSet* all = xpath_nodeset_new();
                if (!all) { ok = 0; break; }
                for (size_t k = start; k <= end; k++)
                    xpath_nodeset_add(all, ns->nodes[k]);
                /* members are BORROWED from the domain nodeset —
                 * claiming ownership would free them on unbind
                 * while the domain still uses them. */
                all->owns_synthetic_text = 0;
                XPathVariable* var = xpath_variable_set_add(
                    (XPathVariableSet*)ctx->variable_set,
                    w->win_var, XPATH_VAR_TYPE_NODE_SET);
                if (var) xpath_variable_set_nodeset(var, all);
                else { xpath_nodeset_free(all); ok = 0; break; }
            }
            xq_window_bind(ctx, w, ns, start, n);
            if (w->e_var || w->e_pos_var) {
                if (w->e_var) {
                    XPathNodeSet* one = xpath_nodeset_new();
                    if (one) {
                        xpath_nodeset_add(one, ns->nodes[end]);
                        XPathVariable* var = xpath_variable_set_add(
                            (XPathVariableSet*)ctx->variable_set,
                            w->e_var, XPATH_VAR_TYPE_NODE_SET);
                        if (var)
                            xpath_variable_set_nodeset(var, one);
                        else xpath_nodeset_free(one);
                    }
                }
                if (w->e_pos_var) {
                    char nb[24];
                    int nl3 = snprintf(nb, sizeof(nb),
                                       "\x03N%zu", end + 1);
                    XPathNodeSet* pone = xpath_nodeset_new();
                    if (pone) {
                        pone->owns_synthetic_text = 1;
                        XPathTextNode* ptn =
                            xpath_synth_text(nb, (size_t)nl3);
                        if (ptn) xpath_nodeset_add(pone, ptn);
                        XPathVariable* var = xpath_variable_set_add(
                            (XPathVariableSet*)ctx->variable_set,
                            w->e_pos_var,
                            XPATH_VAR_TYPE_NODE_SET);
                        if (var)
                            xpath_variable_set_nodeset(var, pone);
                        else xpath_nodeset_free(pone);
                    }
                }
            }
            ok = xq_enumerate(q, ctx, idx + 1, out, out_n, out_cap);
            xq_window_unbind(ctx, w);
            xpath_variable_set_remove(
                (XPathVariableSet*)ctx->variable_set, w->win_var);
            if (!w->sliding)
                start = end + 1;   /* tumbling: next after the end */
            else
                start++;
        }
        xpath_result_free(domain);
        return ok;
    }
    if (!c->is_for) {
        /* LET: evaluated per enclosing-FOR tuple — later clause
         * domains may reference it. */
        struct leptris_xpath_result* v = evaluate_expr(ctx, c->expr);
        if (!v) return 0;
        int ok = xq_bind(ctx, c->var, v);
        xpath_result_free(v);
        if (!ok) return 0;
        int r = xq_enumerate(q, ctx, idx + 1, out, out_n, out_cap);
        xpath_variable_set_remove((XPathVariableSet*)ctx->variable_set,
                                  c->var);
        return r;
    }

    struct leptris_xpath_result* domain = evaluate_expr(ctx, c->expr);
    if (!domain) return 0;
    int ok = 1;
    if (domain->type == XPATH_RESULT_NODESET &&
        domain->value.nodeset_value) {
        XPathNodeSet* ns = domain->value.nodeset_value;
        for (size_t i = 0; i < ns->count && ok; i++) {
            XPathNodeSet* one = xpath_nodeset_new();
            if (!one) {
                ok = 0;
                break;
            }
            xpath_nodeset_add(one, ns->nodes[i]);
            XPathVariable* var = xpath_variable_set_add(
                (XPathVariableSet*)ctx->variable_set, c->var,
                XPATH_VAR_TYPE_NODE_SET);
            if (!var) {
                xpath_nodeset_free(one);
                ok = 0;
                break;
            }
            xpath_variable_set_nodeset(var, one);
            /* positional `at $i` — 1-based, numeric-marker member */
            if (c->pos_var) {
                char nb[24];
                int nl = snprintf(nb, sizeof(nb), "\x03N%zu", i + 1);
                XPathNodeSet* pone = xpath_nodeset_new();
                if (pone) {
                    pone->owns_synthetic_text = 1;
                    XPathTextNode* ptn = xpath_synth_text(nb, (size_t)nl);
                    if (ptn) xpath_nodeset_add(pone, ptn);
                    XPathVariable* pvar = xpath_variable_set_add(
                        (XPathVariableSet*)ctx->variable_set,
                        c->pos_var, XPATH_VAR_TYPE_NODE_SET);
                    if (pvar) xpath_variable_set_nodeset(pvar, pone);
                    else xpath_nodeset_free(pone);
                }
            }
            ok = xq_enumerate(q, ctx, idx + 1, out, out_n, out_cap);
            if (c->pos_var)
                xpath_variable_set_remove(
                    (XPathVariableSet*)ctx->variable_set, c->pos_var);
            xpath_variable_set_remove(
                (XPathVariableSet*)ctx->variable_set, c->var);
        }
    } else {
        ok = xq_bind(ctx, c->var, domain) &&
             xq_enumerate(q, ctx, idx + 1, out, out_n, out_cap);
        xpath_variable_set_remove((XPathVariableSet*)ctx->variable_set,
                                  c->var);
    }
    xpath_result_free(domain);
    return ok;
}

/* A param select evaluates in its own context over the document
 * (QT3: external-variable values are computed independently of
 * the query's own bindings). */
/* A param select evaluates on the QUERY's context (QT3: external
 * values are computed independently of the query's own local
 * bindings — none are bound at prolog time). Using the live ctx
 * keeps parse-xml()-owned documents anchored for the whole eval;
 * a throwaway context freed them under the variable. */
static struct leptris_xpath_result* xq_eval_param(
    XPathContext* ctx, const char* sel) {
    XPathASTNode* ast = parse_expr_span(sel, sel + strlen(sel));
    if (!ast) return NULL;
    struct leptris_xpath_result* v = evaluate_expr(ctx, ast);
    ast_node_free(ast);
    return v;
}

static LeptrisXPathResult xq_eval_impl(
    LeptrisXQuery query, LeptrisDocument doc, LeptrisElement context_node,
    const char* const* param_names, const char* const* param_selects,
    size_t nparams) {
    struct LeptrisXQueryInternal* q = (struct LeptrisXQueryInternal*)query;
    if (!q || !doc) return NULL;

    LeptrisElement ctx_elem =
        context_node ? context_node : leptris_document_root(doc);
    if (!ctx_elem) return NULL;

    XPathContext ctx_storage;
    XPathContext* ctx = &ctx_storage;
    xpath_context_init(ctx, (struct leptris_document*)doc, ctx_elem);

    /* local: functions: merged registry over the standard library.
     * Closure contents live for the eval; the registry itself is
     * freed by context cleanup. */
    char** fn_contents = NULL;
    size_t n_fn_contents = 0;
    int has_fn = 0;
    for (size_t i = 0; i < q->ndecls; i++)
        if (q->decls[i].kind == XQ_DECL_FN) {
            has_fn = 1;
            break;
        }
    if (has_fn) {
        XPathFunctionRegistry* reg = xpath_function_registry_new();
        if (reg) {
            xpath_function_registry_init_standard(reg);
            fn_contents = (char**)calloc(q->ndecls, sizeof(char*));
            for (size_t i = 0; i < q->ndecls; i++) {
                XqDecl* d = &q->decls[i];
                if (d->kind != XQ_DECL_FN) continue;
                size_t plen = d->params ? strlen(d->params) : 0;
                char* cc = (char*)malloc(plen + 24);
                if (!cc) continue;
                memcpy(cc, "\x03" "FN\x02", 4);
                size_t len = 4;
                if (plen) {
                    memcpy(cc + len, d->params, plen);
                    len += plen;
                }
                cc[len++] = '\x02';
                len += (size_t)snprintf(cc + len, 17, "%016llx",
                                        (unsigned long long)(uintptr_t)d->ast);
                fn_contents[n_fn_contents++] = cc;
                /* register_ud: a user fn SHADOWING a builtin is
                 * replaced IN PLACE — the old count-1 user_data
                 * write hit the wrong slot and the thunk lost its
                 * body (declare function unordered() over the
                 * builtin fn:unordered). */
                xpath_function_registry_register_ud(reg, d->name,
                                                    xq_fn_thunk,
                                                    (int)d->arity,
                                                    (int)d->arity,
                                                    cc);
            }
            ctx->function_registry = reg;
        }
    }

    /* xpath_context_cleanup treats variable_set as caller-borrowed
     * — the scratch set is ours to free (with every binding still
     * in it; set_free releases the owned nodesets). */
    XPathVariableSet* scratch_vs = NULL;
    if (!ctx->variable_set) {
        scratch_vs = xpath_variable_set_new();
        if (!scratch_vs) {
            xpath_context_cleanup(ctx);
            return NULL;
        }
        ctx->variable_set = scratch_vs;
    }

    /* Prolog in order: namespaces, then variables/functions as
     * declared (each sees the earlier ones). */
    int err = 0;
    ctx->xquery_spelling = 1;
    for (size_t i = 0; i < q->ndecls && !err; i++) {
        XqDecl* d = &q->decls[i];
        if (d->kind == XQ_DECL_NS) {
            if (ctx->namespace_count == ctx->namespace_capacity) {
                ctx->namespace_capacity =
                    ctx->namespace_capacity ? ctx->namespace_capacity * 2 : 8;
                ctx->namespace_mappings = (XPathNamespaceMapping*)realloc(
                    ctx->namespace_mappings,
                    ctx->namespace_capacity * sizeof(XPathNamespaceMapping));
            }
            if (!ctx->namespace_mappings) {
                err = 1;
                break;
            }
            /* cleanup frees these with LEPTRIS_FREE — allocate
             * through the matching channel. */
            {
                size_t pn = strlen(d->name), un = strlen(d->uri);
                char* pfx = (char*)LEPTRIS_ALLOC_N(char, pn + 1);
                char* uri = (char*)LEPTRIS_ALLOC_N(char, un + 1);
                if (pfx) { memcpy(pfx, d->name, pn); pfx[pn] = 0; }
                if (uri) { memcpy(uri, d->uri, un); uri[un] = 0; }
                ctx->namespace_mappings[ctx->namespace_count].prefix = pfx;
                ctx->namespace_mappings[ctx->namespace_count].uri = uri;
            }
            ctx->namespace_count++;
            ctx->namespaces_collected = 1;   /* keep ours */
        } else if (d->kind == XQ_DECL_DEFAULT_NS) {
            /* unprefixed names resolve in the default element
             * namespace (XQuery §4.2.3): path tests and
             * constructed elements */
            ctx->xquery_default_ns = d->uri;
        } else if (d->kind == XQ_DECL_VAR) {
            struct leptris_xpath_result* v = NULL;
            if (d->external) {
                size_t pi = nparams;
                for (size_t i = 0; i < nparams; i++) {
                    if (param_names[i] &&
                        strcmp(param_names[i], d->name) == 0) {
                        pi = i;
                        break;
                    }
                }
                if (pi < nparams) {
                    v = xq_eval_param(ctx, param_selects[pi]);
                } else if (d->ast) {
                    v = evaluate_expr(ctx, d->ast);
                } else {
                    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                            "unbound external variable $%s", d->name);
                }
            } else {
                v = evaluate_expr(ctx, d->ast);
            }
            if (!v) {
                err = 1;
                break;
            }
            int ok = xq_bind(ctx, d->name, v);
            xpath_result_free(v);
            if (!ok) {
                err = 1;
                break;
            }
        }
    }

    struct leptris_xpath_result* result = NULL;
    if (!err) {
        if (q->nclauses == 0) {
            result = evaluate_expr(ctx, q->return_ast);
        } else {
            /* Phase 1: tuples. */
            XqTuple* tuples = NULL;
            size_t n_tuples = 0, cap = 0;
            if (!xq_enumerate(q, ctx, 0, &tuples, &n_tuples, &cap)) {
                err = 1;
            } else if (q->ngroup) {
                /* group by (12): partition on the key-value tuple
                 * (components joined with \x01) in first-appearance
                 * order; every clause var is rebound to the group's
                 * member list, and each group var carries its key
                 * component. */
                char** gkeys = NULL;
                char*** gparts = NULL;  /* per group: key components */
                size_t** gmembers = NULL;   /* per group: tuple indices */
                size_t* gcounts = NULL;
                size_t n_groups = 0;
                XqTuple* grouped = NULL;
                size_t n_grouped = 0;
                int gerr = 0;
                gkeys = (char**)calloc(n_tuples ? n_tuples : 1,
                                       sizeof(char*));
                gparts = (char***)calloc(n_tuples ? n_tuples : 1,
                                         sizeof(char**));
                gmembers = (size_t**)calloc(n_tuples ? n_tuples : 1,
                                            sizeof(size_t*));
                gcounts = (size_t*)calloc(n_tuples ? n_tuples : 1,
                                          sizeof(size_t));
                grouped = (XqTuple*)calloc(n_tuples ? n_tuples : 1,
                                           sizeof(XqTuple));
                if (!gkeys || !gparts || !gmembers || !gcounts ||
                    !grouped)
                    gerr = 1;
                for (size_t ti = 0; ti < n_tuples && !gerr; ti++) {
                    xq_unbind_all(ctx, q->clauses, q->nclauses);
                    if (!xq_rebind(ctx, &tuples[ti])) {
                        gerr = 1;
                        break;
                    }
                    char** parts =
                        (char**)calloc(q->ngroup, sizeof(char*));
                    size_t klen = 0;
                    if (!parts) {
                        gerr = 1;
                        break;
                    }
                    for (size_t gi = 0; gi < q->ngroup; gi++) {
                        struct leptris_xpath_result* kr =
                            evaluate_expr(ctx, q->group_keys[gi]);
                        parts[gi] = kr ? xpath_to_string(kr) : NULL;
                        if (kr) xpath_result_free(kr);
                        if (!parts[gi]) parts[gi] = strdup("");
                        if (!parts[gi]) {
                            gerr = 1;
                            break;
                        }
                        klen += strlen(parts[gi]) + 1;
                    }
                    char* key = NULL;
                    if (!gerr) {
                        key = (char*)malloc(klen + 1);
                        if (key) {
                            key[0] = 0;
                            for (size_t gi = 0; gi < q->ngroup; gi++) {
                                strcat(key, parts[gi]);
                                strcat(key, "\x01");
                            }
                        } else {
                            gerr = 1;
                        }
                    }
                    if (gerr) {
                        for (size_t gi = 0; gi < q->ngroup; gi++)
                            free(parts[gi]);
                        free(parts);
                        break;
                    }
                    size_t g = n_groups;
                    for (size_t x = 0; x < n_groups; x++) {
                        if (strcmp(gkeys[x], key) == 0) {
                            g = x;
                            break;
                        }
                    }
                    if (g == n_groups) {
                        gkeys[g] = key;
                        gparts[g] = parts;
                        gcounts[g] = 0;
                        gmembers[g] = (size_t*)calloc(
                            n_tuples, sizeof(size_t));
                        if (!gmembers[g]) {
                            gerr = 1;
                            free(key);
                            gkeys[g] = NULL;
                            gparts[g] = NULL;
                            for (size_t gi = 0; gi < q->ngroup; gi++)
                                free(parts[gi]);
                            free(parts);
                            break;
                        }
                        n_groups++;
                    } else {
                        free(key);
                        for (size_t gi = 0; gi < q->ngroup; gi++)
                            free(parts[gi]);
                        free(parts);
                    }
                    gmembers[g][gcounts[g]++] = ti;
                }
                for (size_t g = 0; g < n_groups && !gerr; g++) {
                    /* Build the group tuple from the first member's
                     * shape, aggregating every member's values. */
                    XqTuple* first = &tuples[gmembers[g][0]];
                    XqTuple* gt = &grouped[n_grouped];
                    memset(gt, 0, sizeof(*gt));
                    gt->names = (char**)calloc(first->n + q->ngroup,
                                               sizeof(char*));
                    gt->contents = (char***)calloc(
                        first->n + q->ngroup, sizeof(char**));
                    gt->nodes = (void***)calloc(first->n + q->ngroup,
                                                sizeof(void**));
                    gt->owns_node = (unsigned char**)calloc(
                        first->n + q->ngroup, sizeof(unsigned char*));
                    gt->counts = (size_t*)calloc(first->n + q->ngroup,
                                                 sizeof(size_t));
                    if (!gt->names || !gt->contents || !gt->nodes ||
                        !gt->owns_node || !gt->counts) {
                        gerr = 1;
                        break;
                    }
                    gt->n = 0;
                    for (size_t v = 0; v < first->n; v++) {
                        gt->names[gt->n] = strdup(first->names[v]);
                        size_t total = 0;
                        for (size_t m = 0; m < gcounts[g]; m++)
                            total += tuples[gmembers[g][m]].counts[v];
                        gt->contents[gt->n] = (char**)calloc(
                            total ? total : 1, sizeof(char*));
                        gt->nodes[gt->n] = (void**)calloc(
                            total ? total : 1, sizeof(void*));
                        gt->owns_node[gt->n] = (unsigned char*)calloc(
                            total ? total : 1, 1);
                        if (!gt->contents[gt->n] || !gt->nodes[gt->n] ||
                            !gt->owns_node[gt->n]) {
                            gerr = 1;
                            break;
                        }
                        size_t w = 0;
                        for (size_t m = 0; m < gcounts[g]; m++) {
                            XqTuple* mt = &tuples[gmembers[g][m]];
                            for (size_t j = 0; j < mt->counts[v];
                                 j++) {
                                if (mt->nodes[v][j]) {
                                    /* member tuples are freed after
                                     * grouping — owned synthetic
                                     * members are cloned into the
                                     * group tuple */
                                    xq_capture_member(
                                        mt->nodes[v][j],
                                        &gt->contents[gt->n][w],
                                        &gt->nodes[gt->n][w],
                                        &gt->owns_node[gt->n][w]);
                                } else {
                                    gt->contents[gt->n][w] = strdup(
                                        mt->contents[v][j]);
                                }
                                w++;
                            }
                        }
                        gt->counts[gt->n] = total;
                        gt->n++;
                    }
                    if (gerr) break;
                    /* each group variable carries its key
                     * component */
                    for (size_t gi = 0; gi < q->ngroup; gi++) {
                        gt->names[gt->n] = strdup(q->group_vars[gi]);
                        gt->contents[gt->n] = (char**)calloc(
                            1, sizeof(char*));
                        gt->nodes[gt->n] =
                            (void**)calloc(1, sizeof(void*));
                        gt->owns_node[gt->n] =
                            (unsigned char*)calloc(1, 1);
                        if (!gt->names[gt->n] ||
                            !gt->contents[gt->n] || !gt->nodes[gt->n] ||
                            !gt->owns_node[gt->n]) {
                            gerr = 1;
                            break;
                        }
                        gt->contents[gt->n][0] = strdup(gparts[g][gi]);
                        gt->counts[gt->n] = 1;
                        gt->n++;
                    }
                    if (gerr) break;
                    n_grouped++;
                }
                for (size_t g = 0; g < n_groups; g++) {
                    free(gkeys[g]);
                    for (size_t gi = 0; gparts[g] && gi < q->ngroup;
                         gi++)
                        free(gparts[g][gi]);
                    free(gparts[g]);
                    free(gmembers[g]);
                }
                free(gkeys);
                free(gparts);
                free(gmembers);
                free(gcounts);
                /* Drop the last group-key rebind before disposing
                 * the source tuples (xq_rebind clones attrs, but
                 * clear vars so nothing can still observe them). */
                xq_unbind_all(ctx, q->clauses, q->nclauses);
                for (size_t ti = 0; ti < n_tuples; ti++)
                    xq_tuple_free(&tuples[ti]);
                free(tuples);
                if (gerr) {
                    for (size_t gi = 0; gi < n_grouped; gi++)
                        xq_tuple_free(&grouped[gi]);
                    free(grouped);
                    err = 1;
                } else {
                    tuples = grouped;
                    n_tuples = n_grouped;
                }
            }
            if (!err && q->post_where_ast) {
                /* WHERE after GROUP BY: filter the grouped tuples
                 * (the aggregates see the whole group). */
                size_t w = 0;
                for (size_t ti = 0; ti < n_tuples && !err; ti++) {
                    xq_unbind_all(ctx, q->clauses, q->nclauses);
                    for (size_t gi = 0; gi < q->ngroup; gi++)
                        xpath_variable_set_remove(
                            (XPathVariableSet*)ctx->variable_set,
                            q->group_vars[gi]);
                    if (!xq_rebind(ctx, &tuples[ti])) {
                        err = 1;
                        break;
                    }
                    struct leptris_xpath_result* pv =
                        evaluate_expr(ctx, q->post_where_ast);
                    int truth = pv ? xpath_to_boolean(pv) : 0;
                    if (pv) xpath_result_free(pv);
                    if (truth) tuples[w++] = tuples[ti];
                    else xq_tuple_free(&tuples[ti]);
                }
                n_tuples = w;
            }
            if (!err) {
                /* Order keys: evaluated against the (possibly
                 * grouped) bindings. */
                for (size_t ti = 0; ti < n_tuples && !err; ti++) {
                    xq_unbind_all(ctx, q->clauses, q->nclauses);
                    for (size_t gi = 0; gi < q->ngroup; gi++)
                        xpath_variable_set_remove(
                            (XPathVariableSet*)ctx->variable_set,
                            q->group_vars[gi]);
                    if (!xq_rebind(ctx, &tuples[ti])) {
                        err = 1;
                        break;
                    }
                    if (q->nkeys) {
                        tuples[ti].keys = (char**)calloc(
                            q->nkeys, sizeof(char*));
                        if (!tuples[ti].keys) {
                            err = 1;
                            break;
                        }
                        tuples[ti].nkeys = q->nkeys;
                        for (size_t k = 0; k < q->nkeys; k++) {
                            struct leptris_xpath_result* r =
                                evaluate_expr(ctx, q->keys[k].key);
                            /* Only a genuinely EMPTY SEQUENCE key
                             * takes the empty-mode; an empty STRING
                             * key is a real key and sorts by value
                             * ("" < "a" — cbcl-distinct-values-010).
                             * NULL slot = empty sequence. */
                            int empty_seq =
                                !r || (r->type == XPATH_RESULT_NODESET &&
                                       (!r->value.nodeset_value ||
                                        r->value.nodeset_value->count ==
                                            0));
                            char* ks =
                                empty_seq ? NULL
                                          : (r ? xpath_to_string(r)
                                               : NULL);
                            if (ti == 0)
                                q->keys[k].strmode =
                                    (r &&
                                     r->type == XPATH_RESULT_STRING);
                            xpath_result_free(r);
                            tuples[ti].keys[k] = ks;
                        }
                    }
                }
                /* Drop the last order-key rebind before the sort /
                 * phase-2 rebinds. */
                xq_unbind_all(ctx, q->clauses, q->nclauses);
                for (size_t gi = 0; gi < q->ngroup; gi++)
                    xpath_variable_set_remove(
                        (XPathVariableSet*)ctx->variable_set,
                        q->group_vars[gi]);
            }
            if (!err) {
                /* Stable order-by: insertion sort over key lists,
                 * ONE SORT PER `order by` CLAUSE (repeated clauses
                 * are legal XQuery 3.0; stability makes later
                 * clauses refine earlier orderings). */
                size_t nkey_clauses = 0;
                for (size_t k = 0; k < q->nkeys; k++)
                    if (q->keys[k].clause + 1 > nkey_clauses)
                        nkey_clauses = q->keys[k].clause + 1;
                for (size_t c = 0; c < nkey_clauses; c++) {
                for (size_t i = 1; i < n_tuples; i++) {
                    XqTuple tmp = tuples[i];
                    size_t j = i;
                    while (j > 0) {
                        int cmp = 0;
                        for (size_t k = 0; k < q->nkeys && !cmp; k++) {
                            if (q->keys[k].clause != c) continue;
                            const char* ka = tuples[j - 1].keys[k];
                            const char* kb = tmp.keys[k];
                            int c;
                            int ea = !ka;
                            int eb = !kb;
                            if (ea || eb) {
                                /* empty-sequence keys order by the
                                 * key's empty mode (greatest default) */
                                if (ea && eb)
                                    c = 0;
                                else if (q->keys[k].empty_least)
                                    c = ea ? -1 : 1;
                                else
                                    c = ea ? 1 : -1;
                            } else if (q->keys[k].strmode) {
                                c = strcmp(ka, kb);
                            } else {
                                c = key_cmp(ka, kb);
                            }
                            cmp = q->keys[k].descending ? -c : c;
                        }
                        if (cmp > 0) {
                            tuples[j] = tuples[j - 1];
                            j--;
                        } else {
                            break;
                        }
                    }
                    tuples[j] = tmp;
                }
                }

                /* Phase 2: rebind in output order, evaluate return. */
                XPathNodeSet* out = xpath_nodeset_new();
                result = xpath_result_new(XPATH_RESULT_NODESET);
                if (!out || !result) {
                    xpath_nodeset_free(out);
                    if (result) {
                        xpath_result_free(result);
                        result = NULL;
                    }
                    err = 1;
                } else {
                    out->owns_synthetic_text = 1;
                    out->is_sequence = 1;
                    int adopted = 0;
                    for (size_t ti = 0; ti < n_tuples; ti++) {
                        /* xpath_variable_set_nodeset overwrites
                         * without freeing — clear the key-loop
                         * bindings before rebinding. */
                        xq_unbind_all(ctx, q->clauses, q->nclauses);
                        for (size_t gi = 0; gi < q->ngroup; gi++)
                            xpath_variable_set_remove(
                                (XPathVariableSet*)ctx->variable_set,
                                q->group_vars[gi]);
                        xq_rebind(ctx, &tuples[ti]);
                        struct leptris_xpath_result* r =
                            evaluate_expr(ctx, q->return_ast);
                        if (r) {
                            /* A single tuple's atomic return IS
                             * the value: a false boolean must stay
                             * falsy at the boundary (a one-member
                             * text nodeset reads true). */
                            if (!adopted && n_tuples == 1 &&
                                r->type != XPATH_RESULT_NODESET) {
                                xpath_result_free(result);
                                result = r;
                                adopted = 1;
                            } else {
                                /* element results keep their NODE
                                 * identity — the ctor wrap and
                                 * assert-xml serialize them; text
                                 * stays a synthetic member */
                                int kept = 0;
                                if (r->type == XPATH_RESULT_NODESET &&
                                    r->value.nodeset_value) {
                                    XPathNodeSet* rn =
                                        r->value.nodeset_value;
                                    for (size_t mi = 0; mi < rn->count;
                                         mi++) {
                                        void* nd = rn->nodes[mi];
                                        if (nd && XPATH_NODE_TYPE(nd) ==
                                                     LEPTRIS_NODE_ELEMENT) {
                                            xpath_nodeset_add(out, nd);
                                            kept = 1;
                                        }
                                    }
                                }
                                if (!kept) {
                                    /* a multi-member sequence result
                                     * spreads one member per item
                                     * (K2: `return ($i, 2)` is TWO
                                     * members, not their first) */
                                    int spread = 0;
                                    if (r->type ==
                                            XPATH_RESULT_NODESET &&
                                        r->value.nodeset_value &&
                                        r->value.nodeset_value
                                                ->count > 1) {
                                        spread = 1;
                                        for (size_t mi = 0;
                                             mi <
                                             r->value.nodeset_value
                                                     ->count;
                                             mi++) {
                                            char* s2 = get_node_text(
                                                r->value
                                                    .nodeset_value
                                                    ->nodes[mi]);
                                            XPathTextNode* tn =
                                                xpath_synth_text(
                                                    s2 ? s2 : "",
                                                    s2 ? strlen(s2)
                                                       : 0);
                                            free(s2);
                                            if (tn)
                                                xpath_nodeset_add(out,
                                                                  tn);
                                        }
                                    }
                                    if (!spread) {
                                        /* Atomic returns render with
                                         * the XQuery number spelling
                                         * (xq_number_to_string), not
                                         * the libxml2-parity XPath
                                         * printer. */
                                        char* s =
                                            (r->type ==
                                                 XPATH_RESULT_NUMBER &&
                                             !r->is_int)
                                                ? xpath_number_to_string_xq(
                                                      r->value
                                                          .number_value)
                                                : xpath_to_string(r);
                                        XPathTextNode* tn =
                                            xpath_synth_text(
                                                s ? s : "",
                                                s ? strlen(s) : 0);
                                        free(s);
                                        if (tn)
                                            xpath_nodeset_add(out, tn);
                                    }
                                }
                                xpath_result_free(r);
                            }
                        }
                        xq_unbind_all(ctx, q->clauses, q->nclauses);
                    }
                    if (adopted)
                        xpath_nodeset_free(out);
                    else
                        result->value.nodeset_value = out;
                }
            }
            for (size_t ti = 0; ti < n_tuples; ti++)
                xq_tuple_free(&tuples[ti]);
            free(tuples);
        }
    }

    for (size_t i = 0; i < n_fn_contents; i++) free(fn_contents[i]);
    free(fn_contents);
    if (scratch_vs) {
        ctx->variable_set = NULL;
        xpath_variable_set_free(scratch_vs);
    }
    xpath_context_cleanup(ctx);
    /* Hoisted ctor wrapper (`<tag>{ FLWOR }</tag>`): the element
     * wraps the WHOLE FLWOR result sequence once (the hoist moved
     * it out of the per-tuple return). Element members serialize;
     * under a default element namespace the wrapper carries it. */
    if (result && q->wrap_tag &&
        leptris_xpath_result_type(result) == LEPTRIS_XPATH_NODESET) {
        size_t n = leptris_xpath_result_count(result);
        size_t tl = strlen(q->wrap_tag);
        size_t dnl = (ctx->xquery_default_ns &&
                      !strchr(q->wrap_tag, ':'))
                         ? strlen(ctx->xquery_default_ns)
                         : 0;
        size_t len = tl * 2 + 5 + n + (dnl ? dnl + 10 : 0);
        char** ser = (char**)calloc(n ? n : 1, sizeof(char*));
        if (!ser) return result;
        for (size_t i = 0; i < n; i++) {
            LeptrisElement el = leptris_xpath_result_get(result, i);
            if (el)
                ser[i] = leptris_element_serialize(el, NULL);
            if (!ser[i]) {
                const char* v =
                    leptris_xpath_result_node_value(result, i);
                ser[i] = leptris_strdup(v ? v : "");
            }
            len += strlen(ser[i]);
        }
        char* s = (char*)malloc(len + 1);
        if (!s) {
            for (size_t i = 0; i < n; i++) leptris_free_string(ser[i]);
            free(ser);
            return result;
        }
        size_t w2 = 0;
        memcpy(s + w2, "<", 1);
        w2 += 1;
        memcpy(s + w2, q->wrap_tag, tl);
        w2 += tl;
        if (dnl) {
            w2 += (size_t)snprintf(s + w2, len + 1 - w2,
                                   " xmlns=\"%s\"",
                                   ctx->xquery_default_ns);
        }
        s[w2++] = '>';
        for (size_t i = 0; i < n; i++) {
            LeptrisElement el = leptris_xpath_result_get(result, i);
            if (!el && i) s[w2++] = ' ';   /* text members join with
                                             * spaces (XQuery sequence
                                             * string form); elements
                                             * sit adjacent */
            size_t vl = strlen(ser[i]);
            memcpy(s + w2, ser[i], vl);
            w2 += vl;
            leptris_free_string(ser[i]);
        }
        free(ser);
        s[w2++] = '<';
        s[w2++] = '/';
        memcpy(s + w2, q->wrap_tag, tl);
        w2 += tl;
        s[w2++] = '>';
        s[w2] = 0;
        leptris_xpath_result_free(result);
        result = xpath_result_new(XPATH_RESULT_STRING);
        if (result)
            result->value.string_value = s;
        else
            free(s);
    }
    /* XQuery number spelling rides the result: NUMBER returns render
     * via the shortest round-trip E form in result_string (the
     * libxml2-parity XPath printer stays for the XPath surface). */
    if (result && result->type == XPATH_RESULT_NUMBER && !result->is_int)
        result->xq_spelling = 1;
    return result;
}

/* ---- direct element constructors (TODO 11 slice B) ----
 *
 * Translated to the computed form before the XPath parser sees a
 * span: <n a="{E}">{C}</n> becomes
 * element n { attribute a { ...AVT... }, text { "..." }, (C) }.
 * Purely textual — balanced-tag scans that respect quotes. */

static void buf_put(Buf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->s = (char*)realloc(b->s, b->cap);
    }
    if (!b->s) return;
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

static void buf_str(Buf* b, const char* s) {
    buf_put(b, s, strlen(s));
}

/* Emit a text run as an XPath string literal (choosing the quote
 * character that does not occur in the run; both occurring is
 * split across a concat). */
static void buf_lit(Buf* b, const char* s, size_t n) {
    int sq = 0, dq = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\'') sq = 1;
        else if (s[i] == '"') dq = 1;
    }
    if (!sq) {
        buf_put(b, "'", 1);
        buf_put(b, s, n);
        buf_put(b, "'", 1);
    } else if (!dq) {
        buf_put(b, "\"", 1);
        buf_put(b, s, n);
        buf_put(b, "\"", 1);
    } else {
        buf_str(b, "concat(");
        for (size_t i = 0; i < n; i++) {
            if (i) buf_str(b, ", ");
            buf_put(b, s[i] == '\'' ? "\"" : "'", 1);
            buf_put(b, s + i, 1);
            buf_put(b, s[i] == '\'' ? "\"" : "'", 1);
        }
        buf_str(b, ")");
    }
}

static int xq_is_name_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}
static int xq_is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' ||
           c == '.' || c == ':';
}

/* Emit an attribute value template: pre{E}post —> concat pieces. */
static void buf_avt(Buf* b, const char* s, size_t n) {
    int any = 0;
    int n_pieces = 0;
    Buf pieces = {0};
    size_t i = 0, lit = 0;
    while (i < n) {
        if (s[i] == '{') {
            if (i > lit) { buf_str(&pieces, any ? ", " : ""); buf_lit(&pieces, s + lit, i - lit); any = 1; }
            size_t j = i + 1;
            int depth = 1;
            while (j < n && depth) {
                if (s[j] == '{') depth++;
                else if (s[j] == '}') depth--;
                else if (s[j] == '\'' || s[j] == '"') {
                    char q = s[j++];
                    while (j < n && s[j] != q) j++;
                }
                if (depth) j++;
            }
            buf_str(&pieces, any ? ", " : "");
            buf_put(&pieces, "(", 1);
            buf_put(&pieces, s + i + 1, j > i + 1 ? j - (i + 1) : 0);
            buf_put(&pieces, ")", 1);
            any = 1;
            n_pieces++;
            i = j + 1;
            lit = i;
        } else i++;
    }
    if (lit < n) { buf_str(&pieces, any ? ", " : ""); buf_lit(&pieces, s + lit, n - lit); any = 1; n_pieces++; }
    if (!any) buf_str(b, "''");
    else if (n_pieces == 1) {
        buf_put(b, pieces.s, pieces.len);
    } else {
        /* concat needs >= 2 args (arity 2..n) */
        buf_str(b, "concat(");
        buf_put(b, pieces.s, pieces.len);
        buf_str(b, ", '')");
    }
    free(pieces.s);
}

static const char* xq_translate_element(const char* p, const char* e,
                                        Buf* out);

static void xq_translate_content(const char* s, const char* e, Buf* out);

static const char* xq_translate_element(const char* p, const char* e,
                                        Buf* out) {
    /* p at '<', tag name follows. */
    const char* tag = p + 1;
    const char* q = tag;
    while (q < e && xq_is_name_char(*q)) q++;
    size_t tnlen = (size_t)(q - tag);
    if (!tnlen) { buf_put(out, p, 1); return p + 1; }
    const char* gt = q;
    while (gt < e && *gt != '>') {
        if (*gt == '"' || *gt == '\'') {
            char qc = *gt++;
            while (gt < e && *gt != qc) gt++;
        }
        if (gt < e && *gt != '>') gt++;
    }
    if (gt >= e) { buf_put(out, p, (size_t)(e - p)); return e; }
    int self_closing = gt[-1] == '/';
    const char* attr_end = self_closing ? gt - 1 : gt;

    Buf ab = {0};
    const char* ap = q;
    while (ap < attr_end) {
        while (ap < attr_end && isspace((unsigned char)*ap)) ap++;
        if (ap >= attr_end) break;
        const char* an = ap;
        while (ap < attr_end && xq_is_name_char(*ap)) ap++;
        size_t anlen = (size_t)(ap - an);
        while (ap < attr_end && isspace((unsigned char)*ap)) ap++;
        if (ap >= attr_end || *ap != '=' || anlen == 0) continue;
        ap++;
        while (ap < attr_end && isspace((unsigned char)*ap)) ap++;
        if (ap >= attr_end || (*ap != '"' && *ap != '\'')) continue;
        char qc = *ap++;
        const char* av = ap;
        while (ap < attr_end && *ap != qc) ap++;
        if (ab.len) buf_str(&ab, ", ");
        buf_str(&ab, "attribute ");
        buf_put(&ab, an, anlen);
        buf_str(&ab, " { ");
        buf_avt(&ab, av, (size_t)(ap - av));
        buf_str(&ab, " }");
        ap++;
    }

    Buf cb = {0};
    const char* end;
    if (self_closing) {
        end = gt + 1;
    } else {
        const char* body = gt + 1;
        const char* close = body;
        int depth = 1;
        while (close < e) {
            if (*close == '<') {
                if (close + 1 < e && close[1] == '/' &&
                    (size_t)(e - close) > tnlen + 2 &&
                    strncmp(close + 2, tag, tnlen) == 0 &&
                    close[2 + tnlen] == '>') {
                    depth--;
                    if (!depth) break;
                } else if (close + 1 < e && xq_is_name_start(close[1])) {
                    const char* nq = close + 1;
                    while (nq < e && xq_is_name_char(*nq)) nq++;
                    if ((size_t)(nq - close - 1) == tnlen &&
                        strncmp(close + 1, tag, tnlen) == 0)
                        depth++;
                }
            } else if (*close == '"' || *close == '\'') {
                char qc = *close++;
                while (close < e && *close != qc) close++;
            }
            close++;
        }
        if (close >= e) {
            buf_put(out, p, (size_t)(e - p));
            free(ab.s);
            free(cb.s);
            return e;
        }
        xq_translate_content(body, close, &cb);
        end = close + 2 + tnlen + 1;   /* past </name> */
    }

    buf_str(out, "element ");
    buf_put(out, tag, tnlen);
    buf_str(out, " { ");
    if (ab.len) {
        buf_put(out, ab.s, ab.len);
        if (cb.len) buf_str(out, ", ");
    }
    if (cb.len) buf_put(out, cb.s, cb.len);
    buf_str(out, " }");
    free(ab.s);
    free(cb.s);
    return end;
}

/* Whitespace-only text strips in direct constructors (XQuery
 * default boundary-space handling). */
static int xq_ws_only(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)s[i])) return 0;
    return 1;
}

static void xq_translate_content(const char* s, const char* e, Buf* out) {
    const char* ts = s, *p = s;
    while (p < e) {
        if (*p == '{') {
            if (p > ts && !xq_ws_only(ts, (size_t)(p - ts))) {
                if (out->len) buf_str(out, ", ");
                buf_str(out, "text { ");
                buf_lit(out, ts, (size_t)(p - ts));
                buf_str(out, " }");
            }
            const char* j = p + 1;
            int depth = 1;
            while (j < e && depth) {
                if (*j == '{') depth++;
                else if (*j == '}') depth--;
                else if (*j == '\'' || *j == '"') {
                    char qc = *j++;
                    while (j < e && *j != qc) j++;
                } else if (*j == '<' && j + 1 < e &&
                           xq_is_name_start(j[1])) {
                    /* nested ctor: skip whole element so ctor-inner
                     * braces don't close the enclosing expr */
                    const char* ne = xq_elem_ctor_end(j, e);
                    if (!ne) break;
                    j = ne;
                    continue;
                }
                if (depth) j++;
            }
            if (out->len) buf_str(out, ", ");
            buf_put(out, "(", 1);
            {
                /* the enclosed expression may itself hold direct
                 * ctors (`<out>{ <g/> }</out>`) — splice recursively */
                char* spliced = xq_splice_ctors(p + 1, j);
                if (spliced) {
                    buf_str(out, spliced);
                    free(spliced);
                } else if (j > p + 1) {
                    buf_put(out, p + 1, (size_t)(j - (p + 1)));
                }
            }
            buf_put(out, ")", 1);
            p = j + 1;
            ts = p;
        } else if (*p == '<' && p + 1 < e && xq_is_name_start(p[1])) {
            if (p > ts && !xq_ws_only(ts, (size_t)(p - ts))) {
                if (out->len) buf_str(out, ", ");
                buf_str(out, "text { ");
                buf_lit(out, ts, (size_t)(p - ts));
                buf_str(out, " }");
            }
            if (out->len) buf_str(out, ", ");
            p = xq_translate_element(p, e, out);
            ts = p;
        } else if (xq_is_name_start(*p)) {
            /* Computed-ctor keyword followed by '{' (document {
             * ... }, text { ... }) or a name+'{' (element n { ... },
             * attribute n { ... }): pass the keyword verbatim and
             * recurse into the brace content so DIRECT constructors
             * inside it translate too (#684 — `document { <a/> }`
             * used to mangle into text). */
            const char* w = p;
            while (w < e && (xq_is_name_start(*w) ||
                             isalnum((unsigned char)*w) || *w == '-' ||
                             *w == '_' || *w == '.' || *w == ':'))
                w++;
            const char* gap = w;
            while (gap < e && isspace((unsigned char)*gap)) gap++;
            if (w < e && gap < e && *gap == '{' &&
                (((size_t)(w - p) == 8 &&
                  strncmp(p, "document", 8) == 0) ||
                 ((size_t)(w - p) == 4 &&
                  strncmp(p, "text", 4) == 0))) {
                if (p > ts && !xq_ws_only(ts, (size_t)(p - ts))) {
                    if (out->len) buf_str(out, ", ");
                    buf_str(out, "text { ");
                    buf_lit(out, ts, (size_t)(p - ts));
                    buf_str(out, " }");
                }
                if (out->len) buf_str(out, ", ");
                buf_put(out, p, (size_t)(w - p));
                buf_str(out, " { ");
                /* matching close brace, respecting nesting+quotes */
                const char* j = gap + 1;
                int depth = 1;
                while (j < e && depth) {
                    if (*j == '{') depth++;
                    else if (*j == '}') depth--;
                    else if (*j == '\'' || *j == '"') {
                        char qc = *j++;
                        while (j < e && *j != qc) j++;
                    }
                    if (depth) j++;
                }
                /* Fresh sub-buffer: the recursive translator emits
                 * its own leading separators against out->len. */
                Buf sub = {0};
                xq_translate_content(gap + 1, j, &sub);
                if (sub.len) buf_put(out, sub.s, sub.len);
                free(sub.s);
                buf_str(out, " }");
                p = (j < e) ? j + 1 : e;
                ts = p;
            } else if (w < e && gap < e && *gap == '{' &&
                       (((size_t)(w - p) == 7 &&
                             strncmp(p, "element", 7) == 0) ||
                        ((size_t)(w - p) == 9 &&
                         strncmp(p, "attribute", 9) == 0)) &&
                       gap != w /* a NAME must sit between */) {
                /* element NAME { ... } / attribute NAME { ... }:
                 * copy keyword+name, recurse the braces. */
                const char* nm = w;
                while (nm < e && (isalnum((unsigned char)*nm) ||
                                  *nm == '_' || *nm == '-' ||
                                  *nm == '.' || *nm == ':'))
                    nm++;
                const char* gap2 = nm;
                while (gap2 < e && isspace((unsigned char)*gap2)) gap2++;
                if (gap2 < e && *gap2 == '{' && nm > w) {
                    if (p > ts && !xq_ws_only(ts, (size_t)(p - ts))) {
                        if (out->len) buf_str(out, ", ");
                        buf_str(out, "text { ");
                        buf_lit(out, ts, (size_t)(p - ts));
                        buf_str(out, " }");
                    }
                    if (out->len) buf_str(out, ", ");
                    buf_put(out, p, (size_t)(nm - p));
                    buf_str(out, " { ");
                    const char* j = gap2 + 1;
                    int depth = 1;
                    while (j < e && depth) {
                        if (*j == '{') depth++;
                        else if (*j == '}') depth--;
                        else if (*j == '\'' || *j == '"') {
                            char qc = *j++;
                            while (j < e && *j != qc) j++;
                        }
                        if (depth) j++;
                    }
                    Buf sub = {0};
                    xq_translate_content(gap2 + 1, j, &sub);
                    if (sub.len) buf_put(out, sub.s, sub.len);
                    free(sub.s);
                    buf_str(out, " }");
                    p = (j < e) ? j + 1 : e;
                    ts = p;
                } else {
                    p++;
                }
            } else {
                p++;
            }
        } else {
            p++;
        }
    }
    if (e > ts && !xq_ws_only(ts, (size_t)(e - ts))) {
        if (out->len) buf_str(out, ", ");
        buf_str(out, "text { ");
        buf_lit(out, ts, (size_t)(e - ts));
        buf_str(out, " }");
    }
}
