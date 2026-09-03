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

typedef enum { XQ_DECL_VAR, XQ_DECL_NS, XQ_DECL_FN } XqDeclKind;

typedef struct {
    XqDeclKind kind;
    char* name;        /* var name (bare), ns prefix, or fn qname */
    char* uri;         /* XQ_DECL_NS */
    char* params;      /* XQ_DECL_FN: '\x01'-joined param names */
    size_t arity;
    XPathASTNode* ast; /* var initializer / fn body */
} XqDecl;

typedef struct {
    int is_for;        /* 0 = let */
    char* var;
    XPathASTNode* expr;
} XqClause;

typedef struct {
    XPathASTNode* key;
    int descending;
} XqOrderKey;

struct LeptrisXQueryInternal {
    XqDecl* decls;
    size_t ndecls;
    XqClause* clauses;
    size_t nclauses;
    XPathASTNode* where_ast;
    XqOrderKey* keys;
    size_t nkeys;
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
           word_is(w, len, "descending");
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
static void xq_translate_content(const char* s, const char* e, Buf* out);
static int xq_is_name_start(char c);

static XPathASTNode* parse_expr_span(const char* a, const char* b) {
    if (a >= b) return NULL;
    /* Direct element constructors translate to the computed form
     * first (purely textual). */
    int has_ctor = 0;
    for (const char* q = a; q + 1 < b && !has_ctor; q++)
        if (*q == '<' && xq_is_name_start(q[1])) has_ctor = 1;
    char* translated = NULL;
    if (has_ctor) {
        Buf tb = {0};
        xq_translate_content(a, b, &tb);
        translated = tb.s ? tb.s : (char*)calloc(1, 1);
        a = translated;
        b = translated + strlen(translated);
    }
    XPathParser* parser = xpath_parser_new(a, (size_t)(b - a));
    if (!parser) { free(translated); return NULL; }
    XPathASTNode* ast = xpath_parse(parser);
    xpath_parser_free(parser);
    free(translated);
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
        if (q->clauses[i].expr) ast_node_free(q->clauses[i].expr);
    }
    free(q->clauses);
    if (q->where_ast) ast_node_free(q->where_ast);
    for (size_t i = 0; i < q->nkeys; i++)
        if (q->keys[i].key) ast_node_free(q->keys[i].key);
    free(q->keys);
    if (q->return_ast) ast_node_free(q->return_ast);
    free(q);
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

LEPTRIS_API LeptrisXQuery leptris_xquery_parse(const char* query,
                                               size_t len) {
    if (!query || !len) return NULL;
    struct LeptrisXQueryInternal* q =
        (struct LeptrisXQueryInternal*)calloc(1, sizeof(*q));
    if (!q) return NULL;

    Scan s = {query, query + len};

    /* Prolog. */
    scan_ws(&s);
    for (;;) {
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
                    char* var = parse_dollar_name(&s);
                    if (!var) {
                        xq_free(q);
                        return NULL;
                    }
                    scan_ws(&s);
                    if (is_for) {
                        const char* iw;
                        size_t iwl = scan_word(&s, &iw);
                        if (!iwl || !word_is(iw, iwl, "in")) {
                            free(var);
                            xq_free(q);
                            return NULL;
                        }
                        s.p = iw + iwl;
                    } else {
                        if (s.p + 1 >= s.end || s.p[0] != ':' ||
                            s.p[1] != '=') {
                            free(var);
                            xq_free(q);
                            return NULL;
                        }
                        s.p += 2;
                    }
                    Scan e = s;
                    scan_expr_segment(&e, 0);
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
                    q->clauses[q->nclauses].expr = expr;
                    q->nclauses++;
                    s = e;
                } else if (word_is(kw, kwl, "where")) {
                    Scan e = s;
                    scan_expr_segment(&e, 0);
                    q->where_ast = parse_expr_span(s.p, e.p);
                    if (!q->where_ast) {
                        xq_free(q);
                        return NULL;
                    }
                    s = e;
                } else if (word_is(kw, kwl, "order")) {
                    scan_ws(&s);
                    const char* bw;
                    size_t bwl = scan_word(&s, &bw);
                    if (!bwl || !word_is(bw, bwl, "by")) {
                        xq_free(q);
                        return NULL;
                    }
                    s.p = bw + bwl;
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
                        s = e;
                        scan_ws(&s);
                        const char* dw;
                        Scan dt = s;
                        size_t dwl = scan_word(&dt, &dw);
                        if (dwl && (word_is(dw, dwl, "descending"))) {
                            desc = 1;
                            s.p = dw + dwl;
                        } else if (dwl &&
                                   word_is(dw, dwl, "ascending")) {
                            s.p = dw + dwl;
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

/* Tuple snapshot: var bindings as raw member contents. */
typedef struct {
    char** names;
    char** contents;   /* synthetic raw content; NULL entry = node */
    void** nodes;
    size_t n;
    char** keys;       /* order-key strings */
    size_t nkeys;
} XqTuple;

static void xq_tuple_free(XqTuple* t) {
    for (size_t i = 0; i < t->n; i++) {
        free(t->names[i]);
        free(t->contents[i]);
    }
    free(t->names);
    free(t->contents);
    free(t->nodes);
    for (size_t i = 0; i < t->nkeys; i++) free(t->keys[i]);
    free(t->keys);
}

static void xq_unbind_all(XPathContext* ctx, XqClause* clauses,
                          size_t n) {
    for (size_t i = 0; i < n; i++)
        xpath_variable_set_remove((XPathVariableSet*)ctx->variable_set,
                                  clauses[i].var);
}

/* Snapshot the current clause bindings. */
static int xq_snapshot(XPathContext* ctx, XqClause* clauses, size_t n,
                       XqTuple* t) {
    t->names = (char**)calloc(n, sizeof(char*));
    t->contents = (char**)calloc(n, sizeof(char*));
    t->nodes = (void**)calloc(n, sizeof(void*));
    t->n = 0;
    t->keys = NULL;
    t->nkeys = 0;
    if (!t->names || !t->contents || !t->nodes) return 0;
    for (size_t i = 0; i < n; i++) {
        XPathVariable* var = xpath_variable_set_get(
            (XPathVariableSet*)ctx->variable_set, clauses[i].var);
        if (!var) continue;
        XPathNodeSet* ns = var->value.v.nodeset_value;
        void* node = (ns && ns->count) ? ns->nodes[0] : NULL;
        t->names[t->n] = strdup(clauses[i].var);
        if (node && XPATH_NODE_TYPE(node) != LEPTRIS_NODE_TEXT) {
            t->nodes[t->n] = node;
            t->contents[t->n] = NULL;
        } else if (node) {
            const char* c = ((XPathTextNode*)node)->content;
            t->contents[t->n] = strdup(c ? c : "");
            t->nodes[t->n] = NULL;
        }
        t->n++;
    }
    return 1;
}

/* Rebind a snapshot into the context. */
static int xq_rebind(XPathContext* ctx, const XqTuple* t) {
    for (size_t i = 0; i < t->n; i++) {
        XPathNodeSet* one = xpath_nodeset_new();
        if (!one) return 0;
        one->owns_synthetic_text = 1;
        if (t->nodes[i]) {
            xpath_nodeset_add(one, t->nodes[i]);
            one->owns_synthetic_text = 0;   /* document node */
        } else if (t->contents[i]) {
            XPathTextNode* tn = xpath_synth_text(
                t->contents[i], strlen(t->contents[i]));
            if (tn) xpath_nodeset_add(one, tn);
        }
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

static int key_cmp(const char* a, const char* b) {
    char *ea = NULL, *eb = NULL;
    double va = strtod(a, &ea);
    double vb = strtod(b, &eb);
    int na = ea && *ea == '\0' && ea != a;
    int nb = eb && *eb == '\0' && eb != b;
    if (na && nb) return (va < vb) ? -1 : (va > vb) ? 1 : 0;
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
        if (q->nkeys) {
            t->keys = (char**)calloc(q->nkeys, sizeof(char*));
            if (!t->keys) return 0;
            t->nkeys = q->nkeys;
            for (size_t k = 0; k < q->nkeys; k++) {
                struct leptris_xpath_result* r =
                    evaluate_expr(ctx, q->keys[k].key);
                char* s = r ? xpath_to_string(r) : NULL;
                xpath_result_free(r);
                t->keys[k] = s ? s : strdup("");
            }
        }
        return 1;
    }

    XqClause* c = &q->clauses[idx];
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
            ok = xq_enumerate(q, ctx, idx + 1, out, out_n, out_cap);
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

LEPTRIS_API LeptrisXPathResult leptris_xquery_eval(LeptrisXQuery query,
                                                   LeptrisDocument doc,
                                                   LeptrisElement context_node) {
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
                xpath_function_registry_register(reg, d->name,
                                                 xq_fn_thunk,
                                                 (int)d->arity,
                                                 (int)d->arity);
                if (reg->count > 0)
                    reg->functions[reg->count - 1].user_data = cc;
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
        } else if (d->kind == XQ_DECL_VAR) {
            struct leptris_xpath_result* v = evaluate_expr(ctx, d->ast);
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
            } else {
                /* Stable order-by: insertion sort over key lists. */
                for (size_t i = 1; i < n_tuples; i++) {
                    XqTuple tmp = tuples[i];
                    size_t j = i;
                    while (j > 0) {
                        int cmp = 0;
                        for (size_t k = 0; k < q->nkeys && !cmp; k++) {
                            int c = key_cmp(tuples[j - 1].keys[k],
                                            tmp.keys[k]);
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
                    for (size_t ti = 0; ti < n_tuples; ti++) {
                        xq_rebind(ctx, &tuples[ti]);
                        struct leptris_xpath_result* r =
                            evaluate_expr(ctx, q->return_ast);
                        if (r) {
                            char* s = xpath_to_string(r);
                            XPathTextNode* tn = xpath_synth_text(
                                s ? s : "", s ? strlen(s) : 0);
                            free(s);
                            if (tn) xpath_nodeset_add(out, tn);
                            xpath_result_free(r);
                        }
                        xq_unbind_all(ctx, q->clauses, q->nclauses);
                    }
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

static void xq_translate_content(const char* s, const char* e, Buf* out) {
    const char* ts = s, *p = s;
    while (p < e) {
        if (*p == '{') {
            if (p > ts) {
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
                }
                if (depth) j++;
            }
            if (out->len) buf_str(out, ", ");
            buf_put(out, "(", 1);
            buf_put(out, p + 1, j > p + 1 ? (size_t)(j - (p + 1)) : 0);
            buf_put(out, ")", 1);
            p = j + 1;
            ts = p;
        } else if (*p == '<' && p + 1 < e && xq_is_name_start(p[1])) {
            if (p > ts) {
                if (out->len) buf_str(out, ", ");
                buf_str(out, "text { ");
                buf_lit(out, ts, (size_t)(p - ts));
                buf_str(out, " }");
            }
            p = xq_translate_element(p, e, out);
            ts = p;
        } else {
            p++;
        }
    }
    if (e > ts) {
        if (out->len) buf_str(out, ", ");
        buf_str(out, "text { ");
        buf_lit(out, ts, (size_t)(e - ts));
        buf_str(out, " }");
    }
}
