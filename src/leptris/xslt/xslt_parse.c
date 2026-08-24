/* xslt/xslt_parse.c — stylesheet compilation (TODO.transform 02/03).
 *
 * Parses an XSLT 1.0 stylesheet (an XML document) into the
 * instruction forest: every select/test/name is compiled ONCE via
 * leptris_xpath_compile; literal result elements carry their
 * qualified name + namespace. xsl:include is inlined; xsl:import
 * becomes a higher import_rank (lower precedence). Whitespace-only
 * text in the STYLESHEET is dropped (§4.8 stripping) outside
 * xsl:text.
 *
 * Multi-file loading: import/include resolve `href` via
 * leptris_parse_file (v1 base = CWD); a depth guard prevents
 * cycles. Includes keep the same import_rank; imports increment
 * it. Variables have a per-parse name table for duplicate marking.
 */
#include "xslt_internal.h"
#include "../dom/text.h"
#include <stdlib.h>
#include <stdio.h>

#define XSLT_INCLUDE_DEPTH_MAX 64

typedef struct {
    char** paths;
    size_t len, cap;
} XsltImportChain;

static int chain_has(XsltImportChain* c, const char* p);
static int chain_push(XsltImportChain* c, const char* p);
static void chain_pop(XsltImportChain* c);

/* Text content of a node child (TEXT/CDATA). */
static const char* leptris_node_text(LeptrisNodeRef n) {
    if (!n) return NULL;
    if (leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_TEXT ||
        leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_CDATA) {
        return leptris_text_get_content((LeptrisTextNode*)n);
    }
    return NULL;
}

#define XSLT_NS "http://www.w3.org/1999/XSL/Transform"

typedef struct {
    LeptrisDocument doc;
    XsltStylesheet* sheet;
    int import_rank;
    int errors;             /* any compile failure fails the sheet */
    XsltImportChain chain;  /* paths seen so far for cycle detection */
} SheetParser;

static int chain_has(XsltImportChain* c, const char* p) {
    for (size_t i = 0; i < c->len; i++)
        if (strcmp(c->paths[i], p) == 0) return 1;
    return 0;
}
static int chain_push(XsltImportChain* c, const char* p) {
    if (c->len == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 8;
        char** np = (char**)realloc(c->paths, nc * sizeof(char*));
        if (!np) return 0;
        c->paths = np; c->cap = nc;
    }
    c->paths[c->len] = leptris_strdup(p);
    if (!c->paths[c->len]) return 0;
    c->len++;
    return 1;
}
static void chain_pop(XsltImportChain* c) {
    if (c->len) { free(c->paths[--c->len]); c->paths[c->len] = NULL; }
}

static int node_is_xsl(LeptrisElement e, const char* local) {
    if (!e) return 0;
    const char* pfx = leptris_element_prefix(e);
    if (!pfx || strcmp(pfx, "xsl") != 0) {
        /* Also accept default-namespaced stylesheets. */
        const char* uri = leptris_element_get_namespace_uri(e);
        if (!uri || strcmp(uri, XSLT_NS) != 0) return 0;
    } else {
        const char* uri = leptris_element_get_namespace_uri(e);
        if (uri && strcmp(uri, XSLT_NS) != 0) return 0;
    }
    const char* n = leptris_element_get_name(e);
    if (!n) return 0;
    const char* colon = strchr(n, ':');
    const char* local_n = colon ? colon + 1 : n;
    return strcmp(local_n, local) == 0;
}

static LeptrisXPathCompiled compile_attr_sp(SheetParser* sp,
                                             LeptrisElement e,
                                             const char* attr) {
    const char* v = leptris_element_attribute(e, attr);
    if (!v || !v[0]) return NULL;
    LeptrisXPathCompiled c = leptris_xpath_compile(v);
    if (!c && sp) sp->errors = 1;
    return c;
}


/* Portable, reentrant tokenizer (strtok_r is POSIX-only; the
 * import/include recursion also makes plain strtok unsafe). */
static char* xslt_strtok(char* s, const char* delims, char** save) {
    char* p = s ? s : *save;
    if (!p) return NULL;
    while (*p) {
        const char* d = delims;
        int hit = 0;
        while (*d) { if (*p == *d) { hit = 1; break; } d++; }
        if (!hit) break;
        p++;
    }
    if (!*p) { *save = p; return NULL; }
    char* tok = p;
    while (*p) {
        const char* d = delims;
        int hit = 0;
        while (*d) { if (*p == *d) { hit = 1; break; } d++; }
        if (hit) break;
        p++;
    }
    if (*p) { *p = 0; *save = p + 1; }
    else *save = p;
    return tok;
}

static XsltInstr* instr_new(XsltInstrKind k) {
    XsltInstr* in = (XsltInstr*)calloc(1, sizeof(*in));
    if (in) in->kind = k;
    return in;
}

static void instr_append(XsltInstr** list, XsltInstr* in) {
    if (!in) return;
    if (!*list) { *list = in; return; }
    XsltInstr* t = *list;
    while (t->next) t = t->next;
    t->next = in;
}

/* Collect use-attribute-sets into name list (comma-separated,
 * whitespace ignored). Stores copy of each name on the
 * instruction. Returns count. */
static size_t collect_attr_sets(XsltInstr* in, LeptrisElement e) {
    const char* v = leptris_element_attribute(e, "use-attribute-sets");
    if (!v || !*v) return 0;
    char* dup = leptris_strdup(v);
    if (!dup) return 0;
    size_t cap = 0, cnt = 0;
    char** arr = NULL;
    char* save = NULL;
    for (char* tok = xslt_strtok(dup, " \t\n,", &save); tok;
         tok = xslt_strtok(NULL, " \t\n,", &save)) {
        if (cnt == cap) {
            cap = cap ? cap * 2 : 4;
            char** na = (char**)realloc(arr, cap * sizeof(char*));
            if (!na) break;
            arr = na;
        }
        arr[cnt++] = leptris_strdup(tok);
    }
    free(dup);
    in->attr_set_names = arr;
    in->attr_set_count = cnt;
    return cnt;
}

static XsltSort* parse_sorts(SheetParser* sp, LeptrisElement parent) {
    XsltSort* head = NULL;
    XsltSort** tail = &head;
    for (LeptrisElement c = leptris_element_first_child_any(parent); c;
         c = leptris_element_next_sibling_any(c)) {
        if (!node_is_xsl(c, "sort")) continue;
        XsltSort* s = (XsltSort*)calloc(1, sizeof(*s));
        if (!s) continue;
        s->select = compile_attr_sp(sp, c, "select");
        const char* dt = leptris_element_attribute(c, "data-type");
        s->numeric = dt && strcmp(dt, "number") == 0;
        const char* od = leptris_element_attribute(c, "order");
        s->descending = od && strcmp(od, "descending") == 0;
        const char* co = leptris_element_attribute(c, "case-order");
        s->case_upper_first = co ? strcmp(co, "upper-first") == 0 : -1;
        *tail = s;
        tail = &s->next;
    }
    return head;
}

/* Forward: the content compiler (element bodies). */
static XsltInstr* parse_content(SheetParser* sp, LeptrisElement list);

static XsltInstr* parse_instruction(SheetParser* sp, LeptrisElement e) {
    const char* name = leptris_element_get_name(e);
    const char* colon = name ? strchr(name, ':') : NULL;
    const char* local = colon ? colon + 1 : name;
    int is_xsl = node_is_xsl(e, local ? local : "");

    if (!is_xsl) {
        /* Literal result element. The stored name is the FULL QName
         * (prefix:local) — element_name() yields only the local
         * part, and xsl:namespace-alias + namespace output need the
         * prefix. */
        XsltInstr* in = instr_new(XSLT_INSTR_RESULT_ELEM);
        if (!in) return NULL;
        const char* pfx = leptris_element_prefix(e);
        char qname[256];
        if (pfx && pfx[0])
            snprintf(qname, sizeof(qname), "%s:%s", pfx,
                     name ? name : "");
        else
            snprintf(qname, sizeof(qname), "%s", name ? name : "");
        in->name = leptris_strdup(qname);
        in->ns_uri = leptris_strdup(
            leptris_element_get_namespace_uri(e) ? leptris_element_get_namespace_uri(e) : "");
        XsltLAttr** atail = &in->attrs;
        size_t na = leptris_element_attribute_count(e);
        for (size_t i = 0; i < na; i++) {
            const char* an = leptris_element_attribute_name_at(e, i);
            const char* av = leptris_element_attribute_value_at(e, i);
            if (!an || !av) continue;
            if (strcmp(an, "xmlns") == 0 ||
                strncmp(an, "xmlns:", 6) == 0) continue;
            /* use-attribute-sets is a directive, not an output
             * attribute — captured separately by collect_attr_sets. */
            if (strcmp(an, "use-attribute-sets") == 0) continue;
            XsltLAttr* la = (XsltLAttr*)calloc(1, sizeof(*la));
            if (!la) continue;
            la->name = leptris_strdup(an);
            la->value = leptris_strdup(av);
            *atail = la;
            atail = &la->next;
        }
        collect_attr_sets(in, e);
        in->child = parse_content(sp, e);
        return in;
    }

    if (strcmp(local, "text") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_TEXT);
        if (!in) return NULL;
        const char* t = leptris_element_child_value(e);
        in->text = leptris_strdup(t ? t : "");
        const char* doe = leptris_element_attribute(e, "disable-output-escaping");
        in->doe = doe && strcmp(doe, "yes") == 0;
        return in;
    }
    if (strcmp(local, "value-of") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_VALUE_OF);
        if (!in) return NULL;
        in->select = compile_attr_sp(sp, e, "select");
        const char* doe = leptris_element_attribute(e, "disable-output-escaping");
        in->doe = doe && strcmp(doe, "yes") == 0;
        return in;
    }
    if (strcmp(local, "for-each") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_FOR_EACH);
        if (!in) return NULL;
        in->select = compile_attr_sp(sp, e, "select");
        in->sorts = parse_sorts(sp, e);
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "if") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_IF);
        if (!in) return NULL;
        in->test = compile_attr_sp(sp, e, "test");
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "choose") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_CHOOSE);
        if (!in) return NULL;
        in->child = parse_content(sp, e);   /* WHEN/OTHERWISE arms */
        return in;
    }
    if (strcmp(local, "when") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_WHEN);
        if (!in) return NULL;
        in->test = compile_attr_sp(sp, e, "test");
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "otherwise") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_OTHERWISE);
        if (!in) return NULL;
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "variable") == 0 || strcmp(local, "param") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_VARIABLE);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->select = compile_attr_sp(sp, e, "select");
        in->is_param = (local[0] == 'p');   /* xsl:param (§11.6) */
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "apply-imports") == 0) {
        return instr_new(XSLT_INSTR_APPLY_IMPORTS);
    }
    if (strcmp(local, "with-param") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_WITH_PARAM);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->select = compile_attr_sp(sp, e, "select");
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "call-template") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_CALL_TEMPLATE);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->child = parse_content(sp, e);   /* with-params */
        return in;
    }
    if (strcmp(local, "apply-templates") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_APPLY_TEMPLATES);
        if (!in) return NULL;
        in->select = compile_attr_sp(sp, e, "select");
        in->name = leptris_strdup(leptris_element_attribute(e, "mode"));
        in->sorts = parse_sorts(sp, e);
        in->child = parse_content(sp, e);   /* with-params */
        return in;
    }
    if (strcmp(local, "copy-of") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_COPY_OF);
        if (!in) return NULL;
        in->select = compile_attr_sp(sp, e, "select");
        return in;
    }
    if (strcmp(local, "copy") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_COPY);
        if (!in) return NULL;
        collect_attr_sets(in, e);
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "element") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_ELEMENT);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->ns_uri = leptris_strdup(leptris_element_attribute(e, "namespace")
                                        ? leptris_element_attribute(e, "namespace") : "");
        collect_attr_sets(in, e);
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "attribute") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_ATTRIBUTE);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "comment") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_COMMENT);
        if (!in) return NULL;
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "processing-instruction") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_PI);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "message") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_MESSAGE);
        if (!in) return NULL;
        const char* term = leptris_element_attribute(e, "terminate");
        in->terminate = term && strcmp(term, "yes") == 0;
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "number") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_NUMBER);
        if (!in) return NULL;
        const char* lvl = leptris_element_attribute(e, "level");
        in->num_level = (lvl && strcmp(lvl, "multiple") == 0) ? 1
                       : (lvl && strcmp(lvl, "any") == 0) ? 2 : 0;
        in->num_value = compile_attr_sp(sp, e, "value");
        in->num_count = compile_attr_sp(sp, e, "count");
        in->num_from = compile_attr_sp(sp, e, "from");
        in->num_format = leptris_strdup(
            leptris_element_attribute(e, "format")
                ? leptris_element_attribute(e, "format") : "1");
        const char* gs = leptris_element_attribute(e, "grouping-size");
        in->num_group_size = gs ? atoi(gs) : 0;
        const char* gp = leptris_element_attribute(e, "grouping-separator");
        in->num_group_sep = gp ? gp[0] : 0;
        const char* lv = leptris_element_attribute(e, "letter-value");
        in->letter_value = lv ? leptris_strdup(lv) : NULL;
        return in;
    }
    /* Unknown xsl: instruction (§2.5/§15): a fallback container.
     * Any xsl:fallback children execute when the element is
     * instantiated; without them it is a no-op (forward-compatible
     * processing — an error only when the element is actually
     * selected, which the container materializes). */
    XsltInstr* in = instr_new(XSLT_INSTR_UNKNOWN_XSL);
    if (!in) return NULL;
    in->name = leptris_strdup(name);
    /* Merge every xsl:fallback child's content into one sequence;
     * op_unknown_xsl executes it directly. */
    XsltInstr** tail = &in->child;
    for (LeptrisElement c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        if (!node_is_xsl(c, "fallback")) continue;
        XsltInstr* content = parse_content(sp, c);
        if (!content) continue;
        *tail = content;
        while (*tail) tail = &(*tail)->next;
    }
    return in;
}

static XsltInstr* parse_content(SheetParser* sp, LeptrisElement list) {
    XsltInstr* out = NULL;
    for (LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(list));
         n; n = leptris_node_next_sibling(n)) {
        int type = leptris_node_get_type(n);
        if (type == LEPTRIS_NODE_TYPE_TEXT) {
            /* §4.8: stylesheet whitespace-only text is stripped. */
            const char* t = leptris_node_text(n);
            if (!t) continue;
            int ws_only = 1;
            for (const char* p = t; *p; p++) {
                if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                    ws_only = 0;
                    break;
                }
            }
            if (ws_only) continue;
            XsltInstr* in = instr_new(XSLT_INSTR_TEXT);
            if (in) in->text = leptris_strdup(t);
            instr_append(&out, in);
        } else if (type == LEPTRIS_NODE_TYPE_ELEMENT) {
            instr_append(&out, parse_instruction(sp, (LeptrisElement)n));
        }
    }
    return out;
}

/* Default pattern priority — the exact §5.5 table:
 *   QName (or processing-instruction(Literal)) preceded by an axis
 *       specifier .......................................... 0
 *   NCName:* preceded by an axis specifier ............. -0.25
 *   otherwise, a bare NodeTest (* / node() /
 *       processing-instruction()) with an axis ........... -0.5
 *   otherwise (paths, predicates, ...) .................... 0.5
 * The caller computes per-alternative values (split on '|'). */
static double default_priority(const char* pattern) {
    if (!pattern || !*pattern) return 0.5;
    /* Multi-step or any predicate → 0.5. */
    if (strchr(pattern, '/') || strchr(pattern, '[')) return 0.5;

    /* Single step: strip leading axis specifiers. */
    const char* p = pattern;
    while (*p == ' ') p++;
    if (strncmp(p, "child::", 7) == 0) p += 7;
    else if (strncmp(p, "attribute::", 11) == 0) p += 11;
    else if (*p == '@') p++;
    while (*p == ' ') p++;

    if (strncmp(p, "processing-instruction(", 23) == 0) {
        const char* arg = p + 23;
        /* literal present? ' " ... */
        return (*arg == '\'' || *arg == '"') ? 0.0 : -0.5;
    }
    size_t len = strlen(p);
    while (len > 0 && p[len-1] == ' ') len--;
    char leaf[128];
    if (len >= sizeof(leaf)) return 0.5;
    memcpy(leaf, p, len); leaf[len] = 0;

    /* NCName:* → -0.25. */
    char* colon = strchr(leaf, ':');
    if (colon && strcmp(colon, ":*") == 0) return -0.25;
    /* Bare NodeTests. */
    if (strcmp(leaf, "*") == 0 || strcmp(leaf, "node()") == 0 ||
        strcmp(leaf, "comment()") == 0 || strcmp(leaf, "text()") == 0)
        return -0.5;
    if (strncmp(leaf, "processing-instruction", 22) == 0) return -0.5;
    /* QName (possibly prefixed). */
    return 0.0;
}

/* Split "a|b|c" into alternatives (respecting brackets — v1 splits
 * on top-level '|' outside [] and ()). */
static char** split_alternatives(const char* pattern, size_t* count) {
    size_t n = 1;
    int depth = 0;
    for (const char* p = pattern; *p; p++) {
        if (*p == '[' || *p == '(') depth++;
        else if (*p == ']' || *p == ')') depth--;
        else if (*p == '|' && depth == 0) n++;
    }
    char** parts = (char**)calloc(n, sizeof(char*));
    if (!parts) return NULL;
    const char* start = pattern;
    size_t i = 0;
    depth = 0;
    for (const char* p = pattern;; p++) {
        if (*p == '[' || *p == '(') depth++;
        else if (*p == ']' || *p == ')') depth--;
        if ((*p == '|' && depth == 0) || *p == '\0') {
            size_t len = (size_t)(p - start);
            while (len > 0 && (start[len-1] == ' ')) len--;
            char* part = (char*)malloc(len + 1);
            if (part) {
                memcpy(part, start, len);
                part[len] = '\0';
                parts[i++] = part;
            }
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    *count = i;
    return parts;
}

static void add_template(SheetParser* sp, LeptrisElement e) {
    const char* match = leptris_element_attribute(e, "match");
    const char* name = leptris_element_attribute(e, "name");
    const char* mode = leptris_element_attribute(e, "mode");
    if (!match && !name) return;

    XsltTemplate* t = (XsltTemplate*)calloc(1, sizeof(*t));
    if (!t) return;
    t->name = name ? leptris_strdup(name) : NULL;
    t->mode = mode ? leptris_strdup(mode) : NULL;
    t->import_rank = sp->import_rank;
    t->body = parse_content(sp, e);

    if (match) {
        if (!*match) { sp->errors = 1; free(t); return; }
        size_t nalt = 0;
        char** alts = split_alternatives(match, &nalt);
        XsltPattern** tail = &t->matches;
        for (size_t i = 0; i < nalt; i++) {
            char* trimmed = alts[i];
            while (*trimmed == ' ') trimmed++;
            if (!*trimmed) continue;
            XsltPattern* p = (XsltPattern*)calloc(1, sizeof(*p));
            if (!p) continue;
            p->expr = leptris_xpath_compile(trimmed);
            if (!p->expr) {
                /* Bad pattern: fail the whole stylesheet. */
                sp->errors = 1;
                free(p);
                free(alts[i]);
                continue;
            }
            p->priority = default_priority(trimmed);
            /* Bare-name fast path for the root element. */
            const char* last = strrchr(trimmed, '/');
            const char* leaf = last ? last + 1 : trimmed;
            int simple = 1;
            for (const char* q = leaf; *q; q++) {
                if (*q == '[' || *q == '(' || *q == '@' || *q == ':') simple = 0;
            }
            if (simple && strlen(leaf) < sizeof(p->expr_name)) {
                strcpy(p->expr_name, leaf);
                p->expr_name_only = 1;
            }
            *tail = p;
            tail = &p->next;
            free(alts[i]);
        }
        free(alts);
    }

    /* Append (imported templates were added FIRST by construction —
     * xsl:import is processed before the body). */
    if (!sp->sheet->templates) {
        sp->sheet->templates = t;
    } else {
        XsltTemplate* x = sp->sheet->templates;
        while (x->next) x = x->next;
        x->next = t;
    }
    sp->sheet->template_count++;
}

static void parse_top_level(SheetParser* sp, LeptrisElement root) {
    for (LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT) continue;
        LeptrisElement e = (LeptrisElement)n;
        if (node_is_xsl(e, "import")) {
            const char* href = leptris_element_attribute(e, "href");
            if (href && *href && !chain_has(&sp->chain, href) &&
                sp->chain.len < XSLT_INCLUDE_DEPTH_MAX) {
                LeptrisDocument d =
                    leptris_parse_file(href, NULL);
                if (!d) { sp->errors = 1; continue; }
                sp->import_rank++;
                chain_push(&sp->chain, href);
                LeptrisElement r2 = leptris_document_root(d);
                if (r2) parse_top_level(sp, r2);
                chain_pop(&sp->chain);
                sp->import_rank--;
                leptris_document_free(d);
            }
            continue;
        }
        if (node_is_xsl(e, "include")) {
            const char* href = leptris_element_attribute(e, "href");
            if (href && *href && !chain_has(&sp->chain, href) &&
                sp->chain.len < XSLT_INCLUDE_DEPTH_MAX) {
                LeptrisDocument d =
                    leptris_parse_file(href, NULL);
                if (!d) { sp->errors = 1; continue; }
                chain_push(&sp->chain, href);
                /* Include keeps the same import_rank — declared in
                 * the including sheet's position. */
                LeptrisElement r2 = leptris_document_root(d);
                if (r2) parse_top_level(sp, r2);
                chain_pop(&sp->chain);
                leptris_document_free(d);
            }
            continue;
        }
        if (node_is_xsl(e, "template")) {
            add_template(sp, e);
            continue;
        }
        if (node_is_xsl(e, "variable") || node_is_xsl(e, "param")) {
            XsltInstr* in = parse_instruction(sp, e);
            if (in) instr_append(&sp->sheet->globals, in);
            continue;
        }
        if (node_is_xsl(e, "key")) {
            XsltKeyDef* k = (XsltKeyDef*)calloc(1, sizeof(*k));
            if (!k) continue;
            k->name = leptris_strdup(leptris_element_attribute(e, "name"));
            k->match = compile_attr_sp(sp, e, "match");
            k->use = compile_attr_sp(sp, e, "use");
            XsltKeyDef** tail = &sp->sheet->keys;
            while (*tail) tail = &(*tail)->next;
            *tail = k;
            continue;
        }
        if (node_is_xsl(e, "output")) {
            const char* m = leptris_element_attribute(e, "method");
            sp->sheet->out_method_text = m && strcmp(m, "text") == 0;
            sp->sheet->out_method_html = m && strcmp(m, "html") == 0;
            const char* ind = leptris_element_attribute(e, "indent");
            sp->sheet->out_indent = ind && strcmp(ind, "yes") == 0;
            const char* od = leptris_element_attribute(e, "omit-xml-declaration");
            sp->sheet->out_omit_decl = od && strcmp(od, "yes") == 0;
            const char* enc = leptris_element_attribute(e, "encoding");
            sp->sheet->out_encoding = enc ? leptris_strdup(enc) : NULL;
            const char* ver = leptris_element_attribute(e, "version");
            sp->sheet->out_version = ver ? leptris_strdup(ver) : NULL;
            const char* sa = leptris_element_attribute(e, "standalone");
            if (sa && *sa)
                sp->sheet->out_standalone =
                    strcmp(sa, "yes") == 0 ? 1 : 0;
            const char* mt = leptris_element_attribute(e, "media-type");
            if (mt && *mt) {
                free((void*)sp->sheet->out_media_type);
                sp->sheet->out_media_type = leptris_strdup(mt);
            }
            const char* ds = leptris_element_attribute(e, "doctype-system");
            if (ds) { free((void*)sp->sheet->out_doctype_system);
                      sp->sheet->out_doctype_system = leptris_strdup(ds); }
            const char* dp = leptris_element_attribute(e, "doctype-public");
            if (dp) { free((void*)sp->sheet->out_doctype_public);
                      sp->sheet->out_doctype_public = leptris_strdup(dp); }
            const char* cs = leptris_element_attribute(e, "cdata-section-elements");
            if (cs) {
                /* Trim + split on whitespace. */
                char* tmp = leptris_strdup(cs);
                if (tmp) {
                    if (sp->sheet->out_cdata_elems) {
                        for (size_t i = 0; sp->sheet->out_cdata_elems[i]; i++)
                            free(sp->sheet->out_cdata_elems[i]);
                        free(sp->sheet->out_cdata_elems);
                    }
                    size_t cap = 4, cnt = 0;
                    char** arr = (char**)malloc(cap * sizeof(char*));
                    char* save = NULL;
                    char* tok = xslt_strtok(tmp, " \t\n,", &save);
                    while (tok) {
                        if (cnt + 1 >= cap) { cap *= 2;
                            arr = (char**)realloc(arr, cap * sizeof(char*)); }
                        arr[cnt++] = leptris_strdup(tok);
                        arr[cnt] = NULL;
                        tok = xslt_strtok(NULL, " \t\n,", &save);
                    }
                    free(tmp);
                    sp->sheet->out_cdata_elems = arr;
                }
            }
            continue;
        }
        if (node_is_xsl(e, "strip-space") ||
            node_is_xsl(e, "preserve-space")) {
            /* §3.4 source whitespace handling: both lists are
             * whitespace-split name lists. */
            const char* els = leptris_element_attribute(e, "elements");
            if (els && *els) {
                char* tmp = leptris_strdup(els);
                if (tmp) {
                    char** arr = NULL; size_t cnt = 0, cap = 0;
                    char* save = NULL;
                    for (char* tok = strtok_r(tmp, " \t\n", &save); tok;
                         tok = strtok_r(NULL, " \t\n", &save)) {
                        if (cnt + 1 >= cap) {
                            cap = cap ? cap * 2 : 4;
                            arr = (char**)realloc(arr, cap * sizeof(char*));
                        }
                        arr[cnt++] = leptris_strdup(tok);
                    }
                    free(tmp);
                    arr = (char**)realloc(arr, (cnt + 1) * sizeof(char*));
                    arr[cnt] = NULL;
                    if (node_is_xsl(e, "strip-space")) {
                        sp->sheet->ws_strip = arr;
                    } else {
                        sp->sheet->ws_preserve = arr;
                    }
                }
            }
            continue;
        }
        if (node_is_xsl(e, "namespace-alias")) {
            const char* spx = leptris_element_attribute(e, "stylesheet-prefix");
            const char* rpx = leptris_element_attribute(e, "result-prefix");
            XsltNsAlias* na = (XsltNsAlias*)calloc(1, sizeof(*na));
            if (na) {
                na->stylesheet_prefix =
                    (spx && strcmp(spx, "#default") != 0)
                        ? leptris_strdup(spx) : NULL;
                na->result_prefix =
                    (rpx && strcmp(rpx, "#default") != 0)
                        ? leptris_strdup(rpx) : NULL;
                na->next = sp->sheet->ns_alias;
                sp->sheet->ns_alias = na;
            }
            continue;
        }
        if (node_is_xsl(e, "fallback")) {
            continue;   /* only meaningful inside unknown elements */
        }
        if (node_is_xsl(e, "decimal-format")) {
            /* Find/create by name (NULL = default). */
            const char* name = leptris_element_attribute(e, "name");
            XsltDecimalFormat* df = NULL;
            for (XsltDecimalFormat* d = sp->sheet->decformats; d; d = d->next) {
                if ((!name && !d->name) ||
                    (name && d->name && strcmp(name, d->name) == 0)) {
                    df = d; break;
                }
            }
            if (!df) {
                df = (XsltDecimalFormat*)calloc(1, sizeof(*df));
                if (!df) continue;
                df->decimal_sep = '.';
                df->grouping_sep = ',';
                df->minus_sign = '-';
                df->percent = '%';
                df->per_mille = '%';
                df->zero_digit = '0';
                df->infinity = leptris_strdup("Infinity");
                df->nan = leptris_strdup("NaN");
                df->name = name ? leptris_strdup(name) : NULL;
                /* Defaults slot at the head; named at the tail. */
                if (!name) {
                    df->next = sp->sheet->decformats;
                    sp->sheet->decformats = df;
                } else {
                    XsltDecimalFormat** t = &sp->sheet->decformats;
                    while (*t) t = &(*t)->next;
                    *t = df;
                }
            }
            const char* av;
            if ((av = leptris_element_attribute(e, "decimal-separator")) && *av)
                df->decimal_sep = av[0];
            if ((av = leptris_element_attribute(e, "grouping-separator")) && *av)
                df->grouping_sep = av[0];
            if ((av = leptris_element_attribute(e, "minus-sign")) && *av)
                df->minus_sign = av[0];
            if ((av = leptris_element_attribute(e, "percent")) && *av)
                df->percent = av[0];
            if ((av = leptris_element_attribute(e, "per-mille")) && *av)
                df->per_mille = av[0];
            if ((av = leptris_element_attribute(e, "zero-digit")) && *av)
                df->zero_digit = av[0];
            if ((av = leptris_element_attribute(e, "infinity")) && *av) {
                free((void*)df->infinity);
                df->infinity = leptris_strdup(av);
            }
            if ((av = leptris_element_attribute(e, "NaN")) && *av) {
                free((void*)df->nan);
                df->nan = leptris_strdup(av);
            }
            continue;
        }
        if (node_is_xsl(e, "attribute-set")) {
            const char* nm = leptris_element_attribute(e, "name");
            if (!nm || !*nm) continue;
            XsltAttrSet* s = (XsltAttrSet*)calloc(1, sizeof(*s));
            if (!s) continue;
            s->name = leptris_strdup(nm);
            /* Prepend to the set list so later use-attribute-sets
             * applies later sets FIRST (winning conflicts). */
            s->next = sp->sheet->attrsets;
            sp->sheet->attrsets = s;
            for (LeptrisElement c =
                     leptris_element_first_child_any(e); c;
                 c = leptris_element_next_sibling_any(c)) {
                if (!node_is_xsl(c, "attribute")) continue;
                XsltLAttr* la = (XsltLAttr*)calloc(1, sizeof(*la));
                if (!la) continue;
                la->name = leptris_strdup(
                    leptris_element_attribute(c, "name"));
                const char* v = leptris_element_child_value(c);
                la->value = leptris_strdup(v ? v : "");
                la->next = s->attrs;
                s->attrs = la;   /* newer first, but apply later-staged
                                    sets first because head-of-list is
                                    the most-recently-declared set */
            }
            /* The set itself can have use-attribute-sets (chained
             * sets) — concatenate referenced set attrs onto s->attrs
             * before s was prepended (so they're applied first). */
            const char* uas = leptris_element_attribute(e, "use-attribute-sets");
            if (uas && *uas) {
                /* For v1, we only support single-name chains — full
                 * comma-split lives in collect_attr_sets above; this
                 * is a one-level recursion kept simple. */
                for (XsltAttrSet* other = sp->sheet->attrsets;
                     other && other != s; other = other->next) {
                    if (!other->name || strcmp(other->name, uas) != 0)
                        continue;
                    for (XsltLAttr* a = other->attrs; a; a = a->next) {
                        XsltLAttr* cp = (XsltLAttr*)calloc(1, sizeof(*cp));
                        if (!cp) continue;
                        cp->name = leptris_strdup(a->name);
                        cp->value = leptris_strdup(a->value);
                        cp->next = s->attrs;
                        s->attrs = cp;
                    }
                }
            }
            continue;
        }
    }
}

/* ---- Public compilation ---- */

static void free_instr_list(XsltInstr* list);

static void free_instr(XsltInstr* in) {
    if (!in) return;
    free_instr_list(in->child);
    if (in->test) leptris_xpath_compiled_free(in->test);
    if (in->select) leptris_xpath_compiled_free(in->select);
    free((void*)in->name);
    free((void*)in->ns_uri);
    free((void*)in->text);
    free((void*)in->num_format);
    free((void*)in->letter_value);
    while (in->sorts) {
        XsltSort* s = in->sorts;
        in->sorts = s->next;
        if (s->select) leptris_xpath_compiled_free(s->select);
        free(s);
    }
    while (in->attrs) {
        XsltLAttr* a = in->attrs;
        in->attrs = a->next;
        free((void*)a->name);
        free((void*)a->value);
        free(a);
    }
    if (in->attr_set_names) {
        for (size_t i = 0; i < in->attr_set_count; i++)
            free(in->attr_set_names[i]);
        free(in->attr_set_names);
    }
    free(in);
}

static void free_instr_list(XsltInstr* list) {
    while (list) {
        XsltInstr* n = list->next;
        free_instr(list);
        list = n;
    }
}

void xslt_stylesheet_free(XsltStylesheet* sheet) {
    if (!sheet) return;
    XsltTemplate* t = sheet->templates;
    while (t) {
        XsltTemplate* n = t->next;
        XsltPattern* p = t->matches;
        while (p) {
            XsltPattern* pn = p->next;
            if (p->expr) leptris_xpath_compiled_free(p->expr);
            free(p);
            p = pn;
        }
        free_instr_list(t->body);
        free((void*)t->name);
        free((void*)t->mode);
        free(t);
        t = n;
    }
    while (sheet->keys) {
        XsltKeyDef* k = sheet->keys;
        sheet->keys = k->next;
        if (k->match) leptris_xpath_compiled_free(k->match);
        if (k->use) leptris_xpath_compiled_free(k->use);
        free((void*)k->name);
        free(k);
    }
    free_instr_list(sheet->globals);
    free((void*)sheet->out_encoding);
    free((void*)sheet->out_version);
    free((void*)sheet->out_doctype_system);
    free((void*)sheet->out_doctype_public);
    if (sheet->out_cdata_elems) {
        for (size_t i = 0; sheet->out_cdata_elems[i]; i++)
            free(sheet->out_cdata_elems[i]);
        free(sheet->out_cdata_elems);
    }
    while (sheet->attrsets) {
        XsltAttrSet* s = sheet->attrsets;
        sheet->attrsets = s->next;
        free((void*)s->name);
        while (s->attrs) {
            XsltLAttr* a = s->attrs; s->attrs = a->next;
            free((void*)a->name); free((void*)a->value); free(a);
        }
        free(s);
    }
    if (sheet->ws_strip) {
        for (size_t i = 0; sheet->ws_strip[i]; i++) free(sheet->ws_strip[i]);
        free(sheet->ws_strip);
    }
    if (sheet->ws_preserve) {
        for (size_t i = 0; sheet->ws_preserve[i]; i++)
            free(sheet->ws_preserve[i]);
        free(sheet->ws_preserve);
    }
    while (sheet->ns_alias) {
        XsltNsAlias* na = sheet->ns_alias;
        sheet->ns_alias = na->next;
        free((void*)na->stylesheet_prefix);
        free((void*)na->result_prefix);
        free(na);
    }
    free((void*)sheet->out_media_type);
    while (sheet->decformats) {
        XsltDecimalFormat* d = sheet->decformats;
        sheet->decformats = d->next;
        free((void*)d->name);
        free((void*)d->infinity);
        free((void*)d->nan);
        free(d);
    }
    free(sheet);
}

XsltStylesheet* xslt_stylesheet_parse(LeptrisDocument doc) {
    if (!doc) return NULL;
    return xslt_stylesheet_parse_root(doc, leptris_document_root(doc));
}

XsltStylesheet* xslt_stylesheet_parse_root(LeptrisDocument doc,
                                           LeptrisElement root) {
    if (!doc || !root) return NULL;
    if (!node_is_xsl(root, "stylesheet") && !node_is_xsl(root, "transform")) {
        return NULL;
    }

    XsltStylesheet* sheet = (XsltStylesheet*)calloc(1, sizeof(*sheet));
    if (!sheet) return NULL;
    sheet->out_standalone = -1;
    SheetParser sp;
    sp.doc = doc; sp.sheet = sheet; sp.import_rank = 0; sp.errors = 0;
    sp.chain.paths = NULL; sp.chain.len = 0; sp.chain.cap = 0;
    /* §2.5: a version other than 1.0 enables forwards-compatible
     * processing (unknown top-level elements ignored; unknown
     * instructions fall back per §15). */
    const char* ver = leptris_element_attribute(root, "version");
    if (ver && strcmp(ver, "1.0") != 0) sheet->forwards_compat = 1;
    /* Default xsl:decimal-format (always present; named formats
     * append later). */
    XsltDecimalFormat* df = (XsltDecimalFormat*)calloc(1, sizeof(*df));
    if (df) {
        df->decimal_sep = '.'; df->grouping_sep = ',';
        df->minus_sign = '-'; df->percent = '%'; df->per_mille = '%';
        df->zero_digit = '0';
        df->infinity = leptris_strdup("Infinity");
        df->nan = leptris_strdup("NaN");
        df->next = sheet->decformats;
        sheet->decformats = df;
    }
    parse_top_level(&sp, root);
    while (sp.chain.len) chain_pop(&sp.chain);
    free(sp.chain.paths);
    if (sp.errors) {
        xslt_stylesheet_free(sheet);
        return NULL;
    }
    return sheet;
}
