/* xslt/xslt_parse.c — stylesheet compilation (TODO.transform 02/03).
 *
 * Parses an XSLT 1.0 stylesheet (an XML document) into the
 * instruction forest: every select/test/name is compiled ONCE via
 * leptris_xpath_compile; literal result elements carry their
 * qualified name + namespace. xsl:include is inlined; xsl:import
 * becomes a higher import_rank (lower precedence). Whitespace-only
 * text in the STYLESHEET is dropped (§4.8 stripping) outside
 * xsl:text. */
#include "xslt_internal.h"
#include "../dom/text.h"
#include <stdlib.h>

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
} SheetParser;

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
        /* Literal result element. */
        XsltInstr* in = instr_new(XSLT_INSTR_RESULT_ELEM);
        if (!in) return NULL;
        in->name = leptris_strdup(name);
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
            XsltLAttr* la = (XsltLAttr*)calloc(1, sizeof(*la));
            if (!la) continue;
            la->name = leptris_strdup(an);
            la->value = leptris_strdup(av);
            *atail = la;
            atail = &la->next;
        }
        in->child = parse_content(sp, e);
        return in;
    }

    if (strcmp(local, "text") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_TEXT);
        if (!in) return NULL;
        const char* t = leptris_element_child_value(e);
        in->text = leptris_strdup(t ? t : "");
        return in;
    }
    if (strcmp(local, "value-of") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_VALUE_OF);
        if (!in) return NULL;
        in->select = compile_attr_sp(sp, e, "select");
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
        in->child = parse_content(sp, e);
        return in;
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
        in->child = parse_content(sp, e);
        return in;
    }
    if (strcmp(local, "element") == 0) {
        XsltInstr* in = instr_new(XSLT_INSTR_ELEMENT);
        if (!in) return NULL;
        in->name = leptris_strdup(leptris_element_attribute(e, "name"));
        in->ns_uri = leptris_strdup(leptris_element_attribute(e, "namespace")
                                        ? leptris_element_attribute(e, "namespace") : "");
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
        return in;
    }
    /* Unknown xsl: instruction — treated as no-op (fallback
     * semantics v1; xsl:fallback content executes per spec only
     * when the element is unsupported, which v1 approximates). */
    return NULL;
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

/* Default pattern priority (§5.5, simplified default rules). */
static double default_priority(const char* pattern) {
    if (!pattern) return -0.5;
    /* "child::*[predicate]" and other complex forms: 0.5. Named
     * steps: 0; prefixed names: 0.25? — XSLT's real table: NCName
     * 0, QName 0 (prefix only matters for namespace), * -0.25,
     * step-with-predicate 0.5, ... v1 approximates the common rows. */
    if (strcmp(pattern, "*") == 0) return -0.25;
    if (strchr(pattern, '[')) return 0.5;
    if (strchr(pattern, '@')) return -0.25;
    if (strchr(pattern, '/')) return 0.0;
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
            /* v1: import rank only — href loading lands with the
             * resolver board (TODO.assemble 04); single-file
             * stylesheets compile fully today. */
            continue;
        }
        if (node_is_xsl(e, "include")) {
            continue;   /* same: multi-file landing with resolver */
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
            const char* ind = leptris_element_attribute(e, "indent");
            sp->sheet->out_indent = ind && strcmp(ind, "yes") == 0;
            const char* od = leptris_element_attribute(e, "omit-xml-declaration");
            sp->sheet->out_omit_decl = od && strcmp(od, "yes") == 0;
            const char* enc = leptris_element_attribute(e, "encoding");
            sp->sheet->out_encoding = enc ? leptris_strdup(enc) : NULL;
            const char* ver = leptris_element_attribute(e, "version");
            sp->sheet->out_version = ver ? leptris_strdup(ver) : NULL;
            continue;
        }
        if (node_is_xsl(e, "strip-space") ||
            node_is_xsl(e, "preserve-space") ||
            node_is_xsl(e, "decimal-format") ||
            node_is_xsl(e, "attribute-set") ||
            node_is_xsl(e, "namespace-alias") ||
            node_is_xsl(e, "fallback")) {
            continue;   /* registered; bodies land with their phases */
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
    free(sheet);
}

XsltStylesheet* xslt_stylesheet_parse(LeptrisDocument doc) {
    if (!doc) return NULL;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return NULL;
    if (!node_is_xsl(root, "stylesheet") && !node_is_xsl(root, "transform")) {
        return NULL;
    }

    XsltStylesheet* sheet = (XsltStylesheet*)calloc(1, sizeof(*sheet));
    if (!sheet) return NULL;
    SheetParser sp = { doc, sheet, 0, 0 };
    parse_top_level(&sp, root);
    if (sp.errors) {
        xslt_stylesheet_free(sheet);
        return NULL;
    }
    return sheet;
}
