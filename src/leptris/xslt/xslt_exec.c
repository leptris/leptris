/* xslt/xslt_exec.c — the template engine (TODO.transform 03/04).
 *
 * Dispatch-table execution: each instruction kind maps to a handler
 * registered in a static table — adding instructions (XSLT or
 * EXSLT) never touches the walker (OCP). Template selection per
 * §5.4/5.5: highest priority wins; among equal priorities, the LAST
 * in document order (list append order) wins — imports prepend
 * lower precedence by construction.
 *
 * Output grows in exec->result through the public mutation API;
 * text-method output accumulates string-values only. */
#include "xslt_internal.h"
#include "../dtd/model.h"   /* leptris_dtd_apply_attribute_defaults (#606) */
#include "../dom/text.h"
#include "../dom/cdata.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>   /* NAN — MSVC rejects constant NAN (C2124) */
#if defined(_WIN32)
#  include <direct.h>   /* _mkdir (xsl:result-document) */
#else
#  include <sys/stat.h> /* mkdir (xsl:result-document) */
#endif

#define XSLT_MAX_DEPTH 512

/* xpath/xpath_public.c — drops the doc's cached merged registry
 * (xslt_state is one of its three inputs). */
void leptris_xpath_invalidate_fn_registry(struct leptris_document* doc);

/* functions_ext31.c — the shared value-level map builder (08). */
void* xpath_map_builder_new(void);
void xpath_map_builder_add(void* b, const char* k, const char* v);
struct leptris_xpath_result* xpath_map_builder_finish(void* b);

static char* xslt_capture_children_text(XsltExec* ex,
                                        const XsltInstr* child,
                                        LeptrisElement node);

static struct leptris_xpath_result* xslt_capture_content(
        XsltExec* ex, const XsltInstr* child, LeptrisElement node);

/* Portable ASCII case-insensitive comparison (strcasecmp is
 * POSIX-only; MSVC has _stricmp). */
static int xslt_ci_eq(const char* a, const char* b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static XsltInstrFn g_ops[XSLT_INSTR_UNKNOWN_XSL + 1];

void xslt_register_op(XsltInstrKind kind, XsltInstrFn fn) {
    if ((int)kind >= 0 && kind <= XSLT_INSTR_UNKNOWN_XSL) {
        g_ops[kind] = fn;
    }
}

/* Forward decl (xslt_exec.c) — result-tree-fragment ownership
 * chain. The node pointers in a variable's nodeset-reference RTF
 * live for the binding's lifetime; the exec owns the chain so
 * nodes stay alive until xslt_exec_free. */
struct xslt_rtf_entry {
    LeptrisDocument doc;
    struct xslt_rtf_entry* next;
};

/* ---- Evaluation with the variable frame chain ----
 *
 * Variable-aware path uses leptris_xpath_compiled_eval_vars (the
 * AST interpreter via compiled_eval_context) because the VM's
 * namespace-collection path has tripped a context-cleanup fault in
 * this configuration — the interpreter's path is the proven route.
 * current() (§12.4) is tracked via saved/set/restore on
 * ex->current_node. Node-set vars transport through
 * xpath_variable_set_nodeset (the evaluator returns a leased
 * nodeset; we copy so re-evals are independent). */

struct leptris_xpath_result* xslt_eval(XsltExec* ex,
                                       LeptrisXPathCompiled c,
                                       LeptrisElement node) {
    if (!c || !ex) return NULL;

    /* current() (§12.4): save and set on EVERY path — the var-less
     * fast route previously skipped this and current() resolved to
     * an empty nodeset in plain for-each bodies. Restore on exit so
     * nested evals don't clobber outer scopes. */
    LeptrisElement saved_cur = ex->current_node;
    ex->current_node = node;

    /* No frame chain — fast VM path (ns contexts need the
     * interpreter's prefixed-test resolution). current_pos rides
     * along so position() sees the in-flight iteration (§12.4). */
    if (!ex->vars) {
        struct leptris_xpath_result* r = leptris_xpath_compiled_eval_ctx(
            c, ex->source, node,
            (struct leptris_xpath_ns_map*)ex->current_ns,
            NULL, ex->current_pos, ex->current_size);
        if (!r && !ex->eval_error) {
            ex->eval_error = 1;
            const char* expr = leptris_xpath_compiled_text(c);
            snprintf(ex->error, sizeof(ex->error),
                     "XPath evaluation returned no result%s%s",
                     expr ? ": " : "", expr ? expr : "");
        }
        ex->current_node = saved_cur;
        return r;
    }

    if (!ex->varset) {
        ex->varset = xpath_variable_set_new();
        if (!ex->varset) { ex->current_node = saved_cur; return NULL; }
    }
    /* The scratch varset mirrors the frame chain. Frames only change
     * through push/pop — both mark vars_dirty — so a clean varset is
     * a faithful mirror and the (frame-count × type-switch) rebuild
     * is skipped entirely: eval-heavy loops with stable bindings pay
     * the materialization once per scope, not per expression. */
    if (ex->vars_dirty) {
    ex->vars_dirty = 0;
    while (ex->varset->count > 0) {
        xpath_variable_set_remove(ex->varset,
                                  ex->varset->variables[0]->name);
    }
    /* Shadowing (§11): the scratch set keeps the FIRST binding of a
     * name (xpath_variable_set_add returns existing), so materialize
     * OLDEST→NEWEST — later writes win and inner scopes shadow. */
    size_t nframes = 0;
    for (XsltVar* v = ex->vars; v; v = v->prev) nframes++;
    XsltVar** frames = (XsltVar**)malloc(
        (nframes ? nframes : 1) * sizeof(XsltVar*));
    if (frames) {
        size_t i = nframes;
        for (XsltVar* v = ex->vars; v; v = v->prev) frames[--i] = v;
    }
    for (size_t fi = 0; frames && fi < nframes; fi++) {
        XsltVar* v = frames[fi];
        if (!v->name) continue;
        switch (v->value ? v->value->type : XPATH_RESULT_STRING) {
            case XPATH_RESULT_NUMBER: {
                XPathVariable* xv = xpath_variable_set_add(
                    ex->varset, v->name, XPATH_VAR_TYPE_NUMBER);
                if (xv && v->value)
                    xpath_variable_set_number(
                        xv, v->value->value.number_value);
                break;
            }
            case XPATH_RESULT_BOOLEAN: {
                XPathVariable* xv = xpath_variable_set_add(
                    ex->varset, v->name, XPATH_VAR_TYPE_BOOLEAN);
                if (xv && v->value)
                    xpath_variable_set_boolean(
                        xv, v->value->value.boolean_value);
                break;
            }
            case XPATH_RESULT_STRING: {
                XPathVariable* xv = xpath_variable_set_add(
                    ex->varset, v->name, XPATH_VAR_TYPE_STRING);
                if (xv && v->value && v->value->value.string_value)
                    xpath_variable_set_string(
                        xv, v->value->value.string_value);
                break;
            }
            case XPATH_RESULT_NODESET: {
                /* Node-set variables transport as a fresh XPathNodeSet
                 * so each eval sees a stable snapshot — RTFs may be
                 * mutated by xsl:variable bodies after the first eval,
                 * and the evaluator borrows the nodeset until the
                 * result is read. */
                XPathVariable* xv = xpath_variable_set_add(
                    ex->varset, v->name, XPATH_VAR_TYPE_NODE_SET);
                if (!xv) {
                    /* Type mismatch from a previous eval — clear and retry. */
                    xpath_variable_set_remove(ex->varset, v->name);
                    xv = xpath_variable_set_add(
                        ex->varset, v->name, XPATH_VAR_TYPE_NODE_SET);
                }
                if (xv) {
                    /* Avoid leaking the previous var's nodeset —
                     * xpath_variable_set_nodeset replaces without
                     * freeing. */
                    if (xv->value.v.nodeset_value)
                        xpath_nodeset_free(xv->value.v.nodeset_value);
                    xv->value.v.nodeset_value = NULL;
                    if (v->value && v->value->value.nodeset_value) {
                        XPathNodeSet* copy =
                            xpath_nodeset_new_with_capacity(
                                v->value->value.nodeset_value->count);
                        if (copy) {
                            for (size_t i = 0;
                                 i < v->value->value.nodeset_value->count; i++)
                                xpath_nodeset_add(copy,
                                    v->value->value.nodeset_value->nodes[i]);
                            xpath_variable_set_nodeset(xv, copy);
                        }
                    }
                }
                break;
            }
            default: break;
        }
    }
    free(frames);
    }   /* vars_dirty rebuild */

    struct leptris_xpath_result* r = leptris_xpath_compiled_eval_ctx(
        c, ex->source, node,
        (struct leptris_xpath_ns_map*)ex->current_ns, ex->varset,
        ex->current_pos, ex->current_size);

    /* Issue 627: NULL means the evaluation FAILED (unknown function,
     * unbound variable, arity mismatch) — record it; the dispatcher
     * unwinds and the public entry returns no result, matching
     * libxslt's runtime abort instead of a silent empty value. */
    if (!r && !ex->eval_error) {
        ex->eval_error = 1;
        const char* expr = leptris_xpath_compiled_text(c);
        snprintf(ex->error, sizeof(ex->error),
                 "XPath evaluation returned no result%s%s",
                 expr ? ": " : "", expr ? expr : "");
    }

    ex->current_node = saved_cur;
    return r;
}

void xslt_push_var(XsltExec* ex, const char* name,
                   struct leptris_xpath_result* v) {
    if (!ex || !name) return;
    XsltVar* fv = (XsltVar*)calloc(1, sizeof(*fv));
    if (!fv) return;
    fv->name = name;
    fv->value = v;
    fv->prev = ex->vars;
    ex->vars = fv;
    ex->vars_dirty = 1;   /* the scratch varset is stale */
}

void xslt_pop_var(XsltExec* ex, const char* name) {
    if (!ex || !ex->vars) return;
    XsltVar* top = ex->vars;
    ex->vars = top->prev;
    ex->vars_dirty = 1;
    if (top->value) leptris_xpath_result_free(top->value);
    free(top);
    (void)name;
}

/* Block scope (§11.1/§11.5): pop the frame chain back to `mark`.
 * The instruction walker snapshots ex->vars at sequence entry and
 * restores on exit, so a variable declared among following
 * siblings persists for those siblings and their descendants but
 * is gone when the containing sequence (element body, template
 * body, for-each iteration) completes. */
void xslt_pop_vars_to(XsltExec* ex, XsltVar* mark) {
    while (ex->vars && ex->vars != mark) xslt_pop_var(ex, NULL);
}

/* ---- Shared small helpers ---- */

/* Place a created element into the result (root chain or parent),
 * preserving a source element's PREFIX and namespace URI. */
static LeptrisElement out_place_elem(XsltExec* ex, LeptrisElement parent,
                                     LeptrisElement e) {
    if (!e) return NULL;
    if (!parent) {
        LeptrisElement root = leptris_document_root(ex->result);
        if (!root) {
            leptris_document_set_root(ex->result, e);
            ex->root_sib_tail = NULL;   /* new chain: root is the head */
        } else {
            /* Cached tail (append-only chain; NULL-next validates);
             * stale entries rewalk from the root. */
            LeptrisElement last = (LeptrisElement)ex->root_sib_tail;
            if (!last ||
                leptris_node_get_next_sibling(
                    leptris_element_as_node(last)))
                for (last = root; leptris_node_get_next_sibling(
                          leptris_element_as_node(last));
                     last = (LeptrisElement)leptris_node_get_next_sibling(
                         leptris_element_as_node(last))) {}
            leptris_node_set_next_sibling(
                leptris_element_as_node(last), leptris_element_as_node(e));
            ex->root_sib_tail = (LeptrisNodeRef)e;
        }
    } else {
        leptris_element_append_child(parent, e);
    }
    return e;
}

static LeptrisElement out_append_elem(XsltExec* ex, LeptrisElement parent,
                                      const char* name, const char* ns) {
    LeptrisElement e = leptris_element_create(ex->result, name);
    if (!e) return NULL;
    if (ns && *ns) leptris_element_set_namespace_uri(e, ns);
    return out_place_elem(ex, parent, e);
}

/* Is `prefix` already bound to `uri` on a RESULT ancestor? Then a
 * copy's declaration would be redundant (libxslt omits it). */
static int result_ns_in_scope(LeptrisElement e, const char* prefix,
                              const char* uri) {
    const char* pf = prefix ? prefix : "";
    for (LeptrisElement a = leptris_node_parent((LeptrisNodeRef)e);
         a; a = leptris_node_parent((LeptrisNodeRef)a)) {
        for (struct leptris_namespace* ns = leptris_elem_namespaces(a);
             ns; ns = ns->next) {
            const char* npf = ns->prefix ? ns->prefix : "";
            if (strcmp(npf, pf) == 0 && ns->uri && uri &&
                strcmp(ns->uri, uri) == 0)
                return 1;
        }
    }
    return 0;
}

/* Copy of a source ELEMENT: same local name, prefix and namespace
 * URI as the source (§7.5). */
static LeptrisElement out_copy_elem(XsltExec* ex, LeptrisElement parent,
                                    LeptrisElement src) {
    LeptrisElement e = leptris_element_create(
        ex->result, leptris_element_get_name(src));
    if (!e) return NULL;
    const char* pfx = leptris_element_get_prefix(src);
    if (pfx && *pfx) leptris_element_set_prefix(e, pfx);
    const char* uri = leptris_element_get_namespace_uri(src);
    if (uri && *uri) leptris_element_set_namespace_uri(e, uri);
    return out_place_elem(ex, parent, e);
}

static void xslt_append_fragment_node(XsltExec* ex, LeptrisNodeRef n);

static void out_append_text(XsltExec* ex, LeptrisElement parent,
                            const char* text) {
    if (!text || !*text) return;
    if (parent) {
        LeptrisNodeRef t = leptris_text_node_create(ex->result, text);
        if (t) leptris_element_append_child(parent, (LeptrisElement)t);
        return;
    }
    /* No insertion point yet: fragment-level text. BEFORE the first
     * element it is top_text (prepended); once elements exist the
     * text belongs AFTER them — tail_text, appended post-serialization
     * (§1: the result is a fragment; order matters). */
    size_t tl = strlen(text);
    /* RTF capture: pure-text variable bodies route here instead of
     * leaking into the fragment output. */
    if (ex->rtf_capturing) {
        if (ex->rtf_text_len + tl + 1 > ex->rtf_text_cap) {
            size_t nc = ex->rtf_text_cap ? ex->rtf_text_cap * 2 : 64;
            while (nc < ex->rtf_text_len + tl + 1) nc *= 2;
            char* grown = (char*)realloc(ex->rtf_text, nc);
            if (!grown) return;
            ex->rtf_text = grown;
            ex->rtf_text_cap = nc;
        }
        memcpy(ex->rtf_text + ex->rtf_text_len, text, tl);
        ex->rtf_text_len += tl;
        ex->rtf_text[ex->rtf_text_len] = 0;
        return;
    }
    /* Fragment text = an ordered node like any other (§1: the
     * result is a fragment; text keeps its position among the
     * elements/comments/PIs). */
    {
        LeptrisNodeRef t = leptris_text_node_create(ex->result, text);
        if (t) xslt_append_fragment_node(ex, t);
    }
}

/* String-value of an element subtree (XPath string-value: all
 * descendant text concatenated). */
static char* string_value_deep(LeptrisElement e) {
    /* Serialize + strip tags is wrong for CDATA-marked content;
     * walk the tree collecting TEXT/CDATA contents. */
    size_t cap = 64, len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    /* Iterative DFS via our own small stack. */
    LeptrisNodeRef stack[256];
    int sp = 0;
    stack[sp++] = leptris_element_as_node(e);
    while (sp > 0) {
        LeptrisNodeRef n = stack[--sp];
        int ty = leptris_node_get_type(n);
        if (ty == LEPTRIS_NODE_TYPE_TEXT || ty == LEPTRIS_NODE_TYPE_CDATA) {
            const char* t = leptris_text_get_content((LeptrisTextNode*)n);
            if (t) {
                size_t tl = strlen(t);
                if (len + tl + 1 > cap) {
                    while (len + tl + 1 > cap) cap *= 2;
                    char* grown = (char*)realloc(buf, cap);
                    if (!grown) { free(buf); return NULL; }
                    buf = grown;
                }
                memcpy(buf + len, t, tl);
                len += tl;
            }
        } else if (ty == LEPTRIS_NODE_TYPE_ELEMENT) {
            /* Push children (order: append then reverse siblings by
             * pushing last-first is unnecessary for concatenation). */
            for (LeptrisNodeRef c = leptris_node_first_child(n); c;
                 c = leptris_node_next_sibling(c)) {
                if (sp < 256) stack[sp++] = c;
            }
        }
    }
    buf[len] = '\0';
    return buf;
}

/* ---- Instruction handlers (registered below) ---- */

/* AVT compiled-expression cache (#682): a literal result element's
 * "book-{@id}" attribute otherwise recompiles its {expr} part for
 * every node it is evaluated against. Distinct expressions per
 * transform are few, so a keyed chain compiles each once. */
typedef struct XsltAvtEntry {
    struct XsltAvtEntry* next;
    LeptrisXPathCompiled c;
    char expr[];
} XsltAvtEntry;

static LeptrisXPathCompiled avt_compiled(XsltExec* ex, const char* s,
                                         size_t n) {
    for (XsltAvtEntry* e = (XsltAvtEntry*)ex->avt_cache; e; e = e->next)
        if (strlen(e->expr) == n && memcmp(e->expr, s, n) == 0)
            return e->c;
    XsltAvtEntry* e = (XsltAvtEntry*)malloc(sizeof(*e) + n + 1);
    if (!e) return NULL;
    memcpy(e->expr, s, n);
    e->expr[n] = 0;
    e->c = leptris_xpath_compile(e->expr);
    if (!e->c) { free(e); return NULL; }
    e->next = (XsltAvtEntry*)ex->avt_cache;
    ex->avt_cache = e;
    return e->c;
}

static void xslt_avt_free(XsltExec* ex) {
    while (ex->avt_cache) {
        XsltAvtEntry* e = (XsltAvtEntry*)ex->avt_cache;
        ex->avt_cache = e->next;
        leptris_xpath_compiled_free(e->c);
        free(e);
    }
}

/* Evaluate an attribute-value template: "a{expr}b" pieces are
 * concatenated; {{ and }} are literal braces (§7.1.1). */
static char* eval_avt(XsltExec* ex, const char* tmpl, LeptrisElement node) {
    if (!tmpl) return leptris_strdup("");
    size_t cap = strlen(tmpl) + 32, len = 0;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    const char* p = tmpl;
    while (*p) {
        if (*p == '{' && p[1] == '{') { p += 2;
            if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '{';
        } else if (*p == '}' && p[1] == '}') { p += 2;
            if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = '}';
        } else if (*p == '{') {
            /* Find the close brace OUTSIDE string literals — a
             * brace inside '...'/"..." belongs to the expression
             * (bug-168: concat('{',...)). No nesting in AVTs. */
            const char* close = NULL;
            {
                const char* q = p + 1;
                char lit = 0;
                while (*q) {
                    if (lit) {
                        if (*q == lit) lit = 0;
                    } else if (*q == '\'' || *q == '"') {
                        lit = *q;
                    } else if (*q == '}') {
                        close = q;
                        break;
                    }
                    q++;
                }
            }
            if (!close) {
                if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
                continue;
            }
            size_t elen = (size_t)(close - p - 1);
            LeptrisXPathCompiled c = avt_compiled(ex, p + 1, elen);
            if (c) {
                struct leptris_xpath_result* r =
                    xslt_eval(ex, c, node);
                char* sv = r ? leptris_xpath_result_string(r) : NULL;
                if (r) leptris_xpath_result_free(r);
                if (sv) {
                    size_t sl = strlen(sv);
                    while (len + sl + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                    memcpy(out + len, sv, sl);
                    len += sl;
                    leptris_free_string(sv);
                }
            }
            p = close + 1;
        } else {
            if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = *p++;
        }
    }
    if (!out) return NULL;
    out[len] = 0;
    return out;
}

/* §7.1.4 QName match: unprefixed names compare literally; prefixed
 * names compare (URI, local) so a declaration and a reference may
 * spell different prefixes for the same namespace (bug-190). */
static int set_name_matches(const XsltAttrSet* s, const char* uname,
                            const char* uuri) {
    if (!s->name || !uname) return 0;
    const char* sc = strchr(s->name, ':');
    const char* uc = strchr(uname, ':');
    if (!sc && !uc) return strcmp(s->name, uname) == 0;
    const char* sl = sc ? sc + 1 : s->name;
    const char* ul = uc ? uc + 1 : uname;
    const char* su = sc ? s->name_uri : NULL;
    const char* uu = uc ? uuri : NULL;
    if (su && uu) return strcmp(su, uu) == 0 && strcmp(sl, ul) == 0;
    if (!su && !uu) return strcmp(sl, ul) == 0;   /* unbound prefixes */
    return 0;
}

/* Precedence semantics (§12.1.4, libxslt bug-80/102/188/189/217):
 * declarations of `name` resolve HIGHEST precedence first —
 * higher-precedence declarations seed attribute positions and LOCK
 * their names; lower-precedence declarations only fill gaps. Within
 * one precedence level, document order applies and later
 * declarations update values in place. A declaration's OWN attrs
 * precede (position) and beat (value) the sets it references. The
 * use-list at the instruction applies with update-in-place: later
 * entries override (§7.1.4), then literal attrs, then
 * xsl:attribute children. */
typedef struct {
    const XsltAttrSet* s;
    int rank;
    size_t seq;         /* position in the prepend list: higher = earlier in doc */
} AttrSetDecl;

static int attrset_decl_cmp(const void* pa, const void* pb) {
    const AttrSetDecl* a = (const AttrSetDecl*)pa;
    const AttrSetDecl* b = (const AttrSetDecl*)pb;
    if (a->rank != b->rank) return a->rank < b->rank ? -1 : 1;
    return a->seq > b->seq ? -1 : 1;   /* doc order within a rank */
}

typedef struct {
    const char** names;
    size_t n, cap;
} NameBag;

/* Resolved attribute vector: the use-list's merged names/values.
 * Values update in place (later entries override — §7.1.4);
 * positions follow first insertion. Applied to the target with
 * skip-if-exists so explicit attributes win. */
typedef struct {
    char** names;
    char** values;
    size_t n, cap;
} AttrVec;

static size_t attrvec_find(const AttrVec* v, const char* nm) {
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->names[i], nm) == 0) return i;
    return (size_t)-1;
}

static void attrvec_set(AttrVec* v, const char* nm, char* val) {
    size_t i = attrvec_find(v, nm);
    if (i != (size_t)-1) {
        free(v->values[i]);
        v->values[i] = val;
        return;
    }
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        char** gn = (char**)realloc(v->names, cap * sizeof(*gn));
        char** gv = (char**)realloc(v->values, cap * sizeof(*gv));
        if (!gn || !gv) {
            free(gn);
            free(gv);
            free(val);
            return;
        }
        v->names = gn;
        v->values = gv;
        v->cap = cap;
    }
    v->names[v->n] = (char*)nm;   /* borrowed: set-decl lifetime */
    v->values[v->n] = val;
    v->n++;
}

static int name_bag_has(const NameBag* b, const char* nm) {
    for (size_t i = 0; i < b->n; i++)
        if (strcmp(b->names[i], nm) == 0) return 1;
    return 0;
}

static void name_bag_add(NameBag* b, const char* nm) {
    if (!nm || name_bag_has(b, nm)) return;
    if (b->n == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8;
        const char** grown =
            (const char**)realloc(b->names, cap * sizeof(*grown));
        if (!grown) return;
        b->names = grown;
        b->cap = cap;
    }
    b->names[b->n++] = nm;
}

static void apply_named_set(XsltExec* ex, const char* name,
                            const char* uri, AttrVec* vec,
                            LeptrisElement node, int depth,
                            const NameBag* pre_locked,
                            NameBag* written) {
    if (!ex || !ex->sheet || !name || !vec || depth > 8) return;
    size_t n = 0;
    for (XsltAttrSet* s = ex->sheet->attrsets; s; s = s->next)
        if (set_name_matches(s, name, uri)) n++;
    if (!n) return;
    AttrSetDecl* matches = (AttrSetDecl*)malloc(n * sizeof(*matches));
    if (!matches) return;
    size_t k = 0, seq = 0;
    for (XsltAttrSet* s = ex->sheet->attrsets; s; s = s->next, seq++)
        if (set_name_matches(s, name, uri)) {
            matches[k].s = s;
            matches[k].rank = s->import_rank;
            matches[k].seq = seq;
            k++;
        }
    qsort(matches, n, sizeof(*matches), attrset_decl_cmp);
    NameBag locked = {NULL, 0, 0};
    if (pre_locked)
        for (size_t i = 0; i < pre_locked->n; i++)
            name_bag_add(&locked, pre_locked->names[i]);
    /* `written` is CALLER-OWNED: names a declaration's reference
     * expansions write count as that declaration's contributions
     * when the rank boundary locks (bug-188). */
    int cur_rank = matches[0].rank;

    for (size_t i = 0; i < n; i++) {
        const XsltAttrSet* s = matches[i].s;
        if (s->import_rank != cur_rank) {
            /* Rank boundary: everything written so far belongs to a
             * higher-precedence level and now locks its names. */
            for (size_t w = 0; w < written->n; w++)
                name_bag_add(&locked, written->names[w]);
            cur_rank = s->import_rank;
        }
        /* Own attributes, document order (stored newest-first). */
        NameBag own = {NULL, 0, 0};
        size_t na = 0;
        for (XsltLAttr* a = s->attrs; a; a = a->next) na++;
        const XsltLAttr** rev =
            (const XsltLAttr**)malloc((na ? na : 1) * sizeof(*rev));
        if (rev) {
            size_t j = 0;
            for (XsltLAttr* a = s->attrs; a; a = a->next) rev[j++] = a;
            for (j = na; j-- > 0;) {
                const XsltLAttr* a = rev[j];
                if (!a->name) continue;
                name_bag_add(&own, a->name);
                if (name_bag_has(&locked, a->name)) continue;
                char* v = eval_avt(ex, a->value, node);
                if (v) attrvec_set(vec, a->name, v);
                else free(v);
                name_bag_add(written, a->name);
            }
            free(rev);
        }
        /* Referenced sets: part of THIS declaration's vector — they
         * must not override its own attributes. */
        for (size_t u = 0; u < s->use_count; u++)
            if (s->use_names[u])
                apply_named_set(ex, s->use_names[u],
                                s->use_uris ? s->use_uris[u] : NULL,
                                vec, node, depth + 1, &own, written);
        free(own.names);
    }
    free(locked.names);
    free(matches);
}

void xslt_apply_attr_sets(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement target, LeptrisElement node) {
    if (!ex || !ex->sheet || !target || !in || in->attr_set_count == 0) return;
    AttrVec vec = {NULL, NULL, 0, 0};
    NameBag written = {NULL, 0, 0};
    for (size_t i = 0; i < in->attr_set_count; i++) {
        if (!in->attr_set_names[i]) continue;
        apply_named_set(ex, in->attr_set_names[i],
                        in->attr_set_uris ? in->attr_set_uris[i] : NULL,
                        &vec, node, 0, NULL, &written);
    }
    /* Skip-if-exists: explicit attributes on the target (literal
     * result attrs, xsl:copy's source attrs) win over set values. */
    for (size_t i = 0; i < vec.n; i++) {
        if (leptris_element_attribute(target, vec.names[i])) continue;
        leptris_element_set_attribute(target, vec.names[i], vec.values[i]);
    }
    for (size_t i = 0; i < vec.n; i++) free(vec.values[i]);
    free(vec.names);
    free(vec.values);
    free(written.names);
}

static int op_attr_set_ref(XsltExec* ex, const XsltInstr* in,
                           LeptrisElement node) {
    /* Placeholder for a runtime attr-set reference from nested
     * instructions. Attribute-set application happens implicitly
     * during literal-result-element/op_element/op_copy creation;
     * a stand-alone ATTR_SET_REF instruction appears when an
     * xsl:attribute-set nests a use-attribute-sets chain — for v1
     * the compile step flattens those into attr_set_names on the
     * parent instruction, so this branch is reserved for future
     * hookups. */
    (void)ex; (void)in; (void)node;
    return 0;
}

/* §7.1.1 xsl:namespace-alias: rewrite the literal result prefix
 * from the stylesheet prefix to the result prefix. */
static const char* apply_ns_alias(const XsltStylesheet* sheet,
                                  const char* qname) {
    if (!sheet->ns_alias || !qname) return qname;
    const char* colon = strchr(qname, ':');
    if (!colon) return qname;   /* default ns aliases need decl maps */
    size_t pl = (size_t)(colon - qname);
    for (const XsltNsAlias* na = sheet->ns_alias; na; na = na->next) {
        if (!na->stylesheet_prefix ||
            strlen(na->stylesheet_prefix) != pl ||
            strncmp(na->stylesheet_prefix, qname, pl) != 0) continue;
        if (!na->result_prefix) return colon + 1;   /* → default ns */
        static char buf[128];   /* single-threaded per transform */
        snprintf(buf, sizeof(buf), "%s%s", na->result_prefix, colon);
        return buf;
    }
    return qname;
}

static int op_result_elem(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement node) {
    /* libxslt's own test extension element (bug-100): <test/> in
     * http://xmlsoft.org/XSLT/ is registered by libxslt's engine —
     * it never serializes; it emits its marker comment. */
    if (in->ns_uri && strcmp(in->ns_uri, "http://xmlsoft.org/XSLT/") == 0) {
        const char* nm = in->name ? in->name : "";
        const char* loc = strchr(nm, ':');
        loc = loc ? loc + 1 : nm;
        if (strcmp(loc, "test") == 0) {
            LeptrisNodeRef cm = leptris_comment_node_create(
                ex->result, "libxslt:test element test worked");
            if (cm) {
                if (ex->pending_parent)
                    leptris_element_append_child_internal(
                        ex->pending_parent, (LeptrisNode*)cm);
                else
                    xslt_append_fragment_node(ex, cm);
            }
            return 0;
        }
    }
    LeptrisElement parent = ex->pending_parent;
    const char* out_name = apply_ns_alias(ex->sheet, in->name);
    LeptrisElement e = out_append_elem(ex, parent, out_name, in->ns_uri);
    if (!e) return -1;
    /* libxslt namespace fixup: an unprefixed literal with NO
     * namespace of its own, constructed under a result ancestor
     * binding the default prefix non-empty, must UNBIND it — the
     * nearest ancestor declaration decides (bug-130's
     * imported-module <div> under a default-namespaced <html>). */
    if (!strchr(out_name ? out_name : "", ':') &&
        (!in->ns_uri || !in->ns_uri[0])) {
        for (LeptrisElement a = leptris_node_parent((LeptrisNodeRef)e);
             a; a = leptris_node_parent((LeptrisNodeRef)a)) {
            int decided = 0;
            for (struct leptris_namespace* ns = leptris_elem_namespaces(a);
                 ns; ns = ns->next) {
                if (!ns->prefix || !ns->prefix[0]) {
                    if (ns->uri && ns->uri[0])
                        leptris_element_add_namespace_definition(e, "", "");
                    decided = 1;
                    break;
                }
            }
            if (decided) break;
        }
    }
    /* §7.1.1: copy in-scope namespace declarations. Skip any
     * binding already present on a RESULT ancestor (full chain,
     * not just the immediate parent — a literal-result-element
     * may appear deeply nested without redeclaring its ancestors'
     * ns). The element's OWN prefix binding leads the chain —
     * libxslt creates the element with its namespace, then copies
     * the remaining in-scope declarations (bug-71). */
    const char* oname = out_name ? out_name : "";
    const char* ocolon = strchr(oname, ':');
    const char* own_pfx = NULL;
    char pbuf[96];
    if (ocolon && in->ns_uri) {
        size_t pl = (size_t)(ocolon - oname);
        if (pl < sizeof(pbuf)) {
            memcpy(pbuf, oname, pl);
            pbuf[pl] = 0;
            own_pfx = pbuf;
            if (!result_ns_in_scope(e, own_pfx, in->ns_uri))
                leptris_element_add_namespace_definition(e, own_pfx,
                                                         in->ns_uri);
        }
    }
    /* The default namespace emits at its SOURCE position among the
     * prefixed declarations (bug-150), not pinned last. */
    size_t dpos = in->ns_out_count;
    if (in->ns_out_default &&
        in->ns_out_default_pos != (size_t)-1 &&
        in->ns_out_default_pos <= in->ns_out_count)
        dpos = in->ns_out_default_pos;
    for (size_t i = 0; i <= in->ns_out_count; i++) {
        if (in->ns_out_default && i == dpos &&
            !result_ns_in_scope(e, "", in->ns_out_default)) {
            leptris_element_add_namespace_definition(
                e, "", in->ns_out_default);
        }
        if (i == in->ns_out_count) break;
        if (result_ns_in_scope(e, in->ns_out_pfx[i],
                               in->ns_out_uri[i])) continue;
        /* The hoisted own-prefix binding (same URI) is covered. */
        if (own_pfx && in->ns_out_pfx[i] &&
            strcmp(own_pfx, in->ns_out_pfx[i]) == 0 &&
            in->ns_out_uri[i] && in->ns_uri &&
            strcmp(in->ns_out_uri[i], in->ns_uri) == 0)
            continue;
        leptris_element_add_namespace_definition(e, in->ns_out_pfx[i],
                                                 in->ns_out_uri[i]);
    }
    /* §7.1.4: literal attrs first (they win positions and values);
     * the resolved attribute-set vector then fills missing names. */
    for (XsltLAttr* a = in->attrs; a; a = a->next) {
        char* v = eval_avt(ex, a->value, node);
        if (v) {
            leptris_element_set_attribute(e, a->name, v);
            free(v);
        }
    }
    xslt_apply_attr_sets(ex, in, e, node);
    ex->pending_parent = e;
    int rc = xslt_exec_instrs(ex, in->child, node);
    /* 3.0 §26.4 xsl:on-empty: content came back empty (no child
     * nodes built) — run the on-empty child's content into the
     * element. The walker skips ON_EMPTY (no registered op). Raw
     * first-child: text nodes count as content (the *_any accessor
     * skips them). */
    if (rc == 0 && !leptris_elem_first_child(e)) {
        for (const XsltInstr* c = in->child; c; c = c->next) {
            if (c->kind != XSLT_INSTR_ON_EMPTY) continue;
            ex->pending_parent = e;
            rc = xslt_exec_instrs(ex, c->child, node);
            break;
        }
    }
    ex->pending_parent = parent;
    return rc;
}

/* Escape & < > for fragment-level accumulation under the xml/html
 * methods (the accumulators concatenate verbatim; §16.3 text method
 * never escapes). DOE text stays raw. */
static char* escape_fragment_text(const char* t, int doe) {
    if (!t || doe) return (char*)t;
    size_t n = 0;
    for (const char* p = t; *p; p++) {
        if (*p == '&') n += 5;       /* &amp; */
        else if (*p == '<' || *p == '>') n += 4;   /* &lt; &gt; */
        else n++;
    }
    if (n == strlen(t)) return (char*)t;   /* nothing to escape */
    char* out = (char*)malloc(n + 1);
    if (!out) return (char*)t;
    char* o = out;
    for (const char* p = t; *p; p++) {
        if (*p == '&') { memcpy(o, "&amp;", 5); o += 5; }
        else if (*p == '<') { memcpy(o, "&lt;", 4); o += 4; }
        else if (*p == '>') { memcpy(o, "&gt;", 4); o += 4; }
        else *o++ = *p;
    }
    *o = 0;
    return out;
}


/* Fragment-level node (comment/PI with no pending parent): chain as
 * a child of the current result root when one exists, else hold on
 * the frag list until an element anchors the chain. Both chains
 * append at the CACHED tail — the per-append tail walk was O(N)
 * (#682). */
static void xslt_append_fragment_node(XsltExec* ex, LeptrisNodeRef n) {
    LeptrisElement root = leptris_document_root(ex->result);
    if (root) {
        /* Chain AFTER the root's sibling tail — the same layout
         * out_append_elem uses for multiple top-level elements, so
         * serialization walks one chain for every node kind. The
         * cached tail is valid while its next is NULL (result
         * chains are append-only); a stale entry falls back to the
         * walk. */
        LeptrisNodeRef last = ex->root_sib_tail;
        if (!last || leptris_node_get_next_sibling(last))
            for (last = leptris_element_as_node(root);
                 leptris_node_get_next_sibling(last);
                 last = leptris_node_get_next_sibling(last)) {}
        leptris_node_set_next_sibling(last, n);
        ex->root_sib_tail = n;
        return;
    }
    XsltFragNode* fn = (XsltFragNode*)calloc(1, sizeof(*fn));
    if (!fn) return;
    fn->node = n;
    if (!ex->frag_nodes) {
        ex->frag_nodes = fn;
    } else {
        XsltFragNode* t = (XsltFragNode*)ex->frag_tail;
        if (!t)
            for (t = (XsltFragNode*)ex->frag_nodes; t->next; t = t->next) {}
        t->next = fn;
    }
    ex->frag_tail = fn;
}

static int op_text(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
    /* 3.0 §10.4.2: expand {expr} value templates first (xsl:text
     * content included — Saxon-verified). */
    char* tvt = in->tvt && in->text ? eval_avt(ex, in->text, node) : NULL;
    const char* text = tvt ? tvt : in->text;
    LeptrisElement parent = ex->pending_parent;
    if (parent) {
        if (in->doe && text) {
            /* §16.4 disable-output-escaping: raw flag — the
             * serializer emits the string verbatim. */
            LeptrisNodeRef t = leptris_text_node_create(ex->result, text);
            if (t) {
                ((LeptrisTextNode*)t)->base.raw = 1;
                leptris_element_append_child(parent, (LeptrisElement)t);
            }
            free(tvt);
            return 0;
        }
        /* Non-DOE: out_append_text (carries the cdata-section-
         * elements conversion for listed parents). */
        out_append_text(ex, parent, text);
        free(tvt);
        return 0;
    }
    /* Fragment-level: accumulate verbatim-safe — escape unless DOE
     * or method=text (§16.3 never escapes). RTF capture stores the
     * LOGICAL text: a pure-text fragment binds as a string, and
     * string($rtf) is unescaped — pre-escaping here leaked &amp;
     * into param values (bug-90). */
    char* v = text ? escape_fragment_text(
                   text,
                   in->doe || ex->sheet->out_method_text ||
                       ex->rtf_capturing) : NULL;
    if (v) {
        out_append_text(ex, NULL, v);
        if (v != text) free(v);
    }
    free(tvt);
    return 0;
}

/* value-of display semantics: an item sequence (for/range/sequence)
 * joins members with the default separator " "; a 2.0+ stylesheet
 * selects a sequence from ANY expression — every item prints. A 1.0
 * stylesheet's plain nodeset keeps the 1.0 first-member rule. */
static char* value_of_string(XsltExec* ex,
                             const struct leptris_xpath_result* r) {
    if (r->type == XPATH_RESULT_NODESET && r->value.nodeset_value &&
        (r->value.nodeset_value->is_sequence ||
         (ex->sheet && ex->sheet->version_major >= 2))) {
        XPathNodeSet* ns = r->value.nodeset_value;
        size_t total = 1;   /* NUL; + separators below */
        for (size_t i = 0; i < ns->count; i++) {
            extern char* get_node_text(void* n);
            char* t = get_node_text(ns->nodes[i]);
            if (t) { total += strlen(t); free(t); }
            if (i + 1 < ns->count) total += 1;   /* " " */
        }
        char* sv = (char*)malloc(total);
        if (sv) {
            char* w = sv;
            for (size_t i = 0; i < ns->count; i++) {
                char* t = get_node_text(ns->nodes[i]);
                if (t) { size_t l = strlen(t); memcpy(w, t, l); w += l; free(t); }
                if (i + 1 < ns->count) *w++ = ' ';
            }
            *w = '\0';
        }
        return sv;
    }
    return leptris_xpath_result_string(
        (struct leptris_xpath_result*)r);
}

static void xslt_sort_items(XsltExec* ex, const XsltInstr* in,
                            LeptrisElement* items, size_t n);
static int copy_node_deep(XsltExec* ex, LeptrisElement node,
                          LeptrisElement parent);

/* xsl:sequence / xsl:perform-sort (3.0): atomic items serialize
 * space-joined into the output; node items are copied in order
 * (Saxon ground truth). perform-sort applies the xsl:sort
 * children to the item list first. */
static int op_sequence(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    if (!in->select) return 0;
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    /* A single ATOMIC item is a scalar result — count() is 0 for
     * those, so string-join it directly (#685 remainder: single-
     * item sequences silently dropped their content). */
    if (r->type != XPATH_RESULT_NODESET) {
        char* p = leptris_xpath_result_string(r);
        /* Sequence items serialize space-separated — including
         * across consecutive sequence instructions (fork arms). */
        if (p && p[0] && ex->pending_parent &&
            leptris_elem_first_child(ex->pending_parent))
            out_append_text(ex, ex->pending_parent, " ");
        out_append_text(ex, ex->pending_parent, p ? p : "");
        free(p);
        leptris_xpath_result_free(r);
        return 0;
    }
    size_t n = leptris_xpath_result_count(r);
    LeptrisElement* items = NULL;
    if (n) {
        items = (LeptrisElement*)calloc(n, sizeof(LeptrisElement));
        if (!items) { leptris_xpath_result_free(r); return -1; }
        for (size_t i = 0; i < n; i++)
            items[i] = (LeptrisElement)leptris_xpath_result_get_node(r, i);
        if (in->sorts) xslt_sort_items(ex, in, items, n);
    }
    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++) {
        LeptrisNodeRef it = (LeptrisNodeRef)items[i];
        int ty = it ? leptris_node_get_type(it) : 0;
        if (ty == LEPTRIS_NODE_TYPE_ELEMENT ||
            ty == LEPTRIS_NODE_TYPE_COMMENT ||
            ty == LEPTRIS_NODE_TYPE_PI ||
            ty == LEPTRIS_NODE_TYPE_CDATA) {
            copy_node_deep(ex, (LeptrisElement)it, ex->pending_parent);
            (void)0;
        } else {
            /* Atomic/textual item: space-joined text. */
            extern char* get_node_text(void* n);
            char* piece = get_node_text(it);
            if (i && piece && piece[0]) out_append_text(ex, ex->pending_parent, " ");
            if (piece) {
                out_append_text(ex, ex->pending_parent, piece);
                free(piece);
            }
        }
    }
    free(items);
    leptris_xpath_result_free(r);
    return rc;
}

static int op_value_of(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    /* select="." (parse-time flag): the string-value of the context
     * node, directly — the eval machinery would build a single-node
     * nodeset, a result wrapper and a conversion string per call
     * (#682; get_node_text IS the string-value for every node
     * kind, markers stripped). */
    char* sv = NULL;
    if (in->select_is_dot) {
        extern char* get_node_text(void* n);
        sv = get_node_text(node);
    } else {
        struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
        if (!r) return 0;
        sv = value_of_string(ex, r);
        leptris_xpath_result_free(r);
    }
    if (sv) {
        op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT, .text = sv,
                                  .doe = in->doe },
                node);
        leptris_free_string(sv);
    }
    return 0;
}

/* xsl:evaluate (3.0 §26): @xpath evaluates to a STRING — the XPath
 * to compile and run now; @context-item picks its context node
 * (omitted or empty = absent context: the source document node
 * anchors absolute paths, Saxon-verified default). */
static int op_evaluate(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    struct leptris_xpath_result* xr = xslt_eval(ex, in->select, node);
    if (!xr) return 0;
    char* expr = leptris_xpath_result_string(xr);
    leptris_xpath_result_free(xr);
    if (!expr) return 0;
    LeptrisXPathCompiled c = leptris_xpath_compile(expr);
    free(expr);
    if (!c) return 0;
    LeptrisElement ctx = NULL;
    if (in->context_item) {
        struct leptris_xpath_result* cr =
            xslt_eval(ex, in->context_item, node);
        if (cr && cr->type == XPATH_RESULT_NODESET &&
            cr->value.nodeset_value && cr->value.nodeset_value->count)
            ctx = (LeptrisElement)cr->value.nodeset_value->nodes[0];
        if (cr) leptris_xpath_result_free(cr);
    }
    if (!ctx) {
        LeptrisNodeRef dn =
            (LeptrisNodeRef)leptris_document_get_node(ex->source);
        ctx = (LeptrisElement)dn;
    }
    /* Child xsl:with-param bindings are visible to the DYNAMIC
     * evaluation only (Saxon-HE 12.7 verified: @xpath's own
     * evaluation resolves the outer bindings). xslt_push_var (not a
     * raw XsltVar link) — the eval varset must be marked dirty or
     * the new binding never reaches variable lookup. */
    XsltVar* scope_mark = ex->vars;
    for (const XsltInstr* w = in->child; w; w = w->next) {
        if (w->kind != XSLT_INSTR_WITH_PARAM || !w->name) continue;
        struct leptris_xpath_result* v = NULL;
        if (w->select) v = xslt_eval(ex, w->select, node);
        else if (w->child) v = xslt_capture_content(ex, w->child, node);
        xslt_push_var(ex, w->name, v);
    }
    struct leptris_xpath_result* r = ctx ? xslt_eval(ex, c, ctx) : NULL;
    xslt_pop_vars_to(ex, scope_mark);
    leptris_xpath_compiled_free(c);
    if (!r) return 0;
    char* sv = value_of_string(ex, r);
    leptris_xpath_result_free(r);
    if (sv) {
        op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT, .text = sv },
                node);
        leptris_free_string(sv);
    }
    return 0;
}

/* xsl:analyze-string (3.0 §18): regex-scan the selected string;
 * matching/non-matching segments run their sub-instruction bodies
 * with a synthetic text node carrying the segment (string(.) reads
 * it). POSIX ERE on POSIX platforms; MSVC builds no-op (same
 * limitation as the EXSLT regexp handlers). */
#ifndef _WIN32
#include <regex.h>
#endif

static XPathTextNode* as_segment_node(const char* s, size_t len) {
    XPathTextNode* tn = (XPathTextNode*)calloc(1, sizeof(*tn));
    if (!tn) return NULL;
    char* copy = (char*)malloc(len + 1);
    if (!copy) { free(tn); return NULL; }
    memcpy(copy, s, len);
    copy[len] = '\0';
    tn->node_type = LEPTRIS_NODE_TEXT;
    tn->content = copy;
    return tn;
}

static void as_segment_free(XPathTextNode* tn) {
    if (!tn) return;
    free(tn->content);
    free(tn);
}

static int op_analyze_string(XsltExec* ex, const XsltInstr* in,
                             LeptrisElement node) {
    if (!in->select || !in->regex) return 0;
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    char* src = leptris_xpath_result_string(r);
    leptris_xpath_result_free(r);
    if (!src) return 0;
    int rc = 0;
#ifndef _WIN32
    regex_t rx;
    int cflags = REG_EXTENDED;
    if (in->regex_flags && strchr(in->regex_flags, 'i'))
        cflags |= REG_ICASE;
    if (regcomp(&rx, in->regex, cflags) != 0) { free(src); return 0; }
    size_t nmatch = rx.re_nsub + 1;
    regmatch_t* pm = (regmatch_t*)calloc(nmatch, sizeof(*pm));
    if (!pm) { regfree(&rx); free(src); return 0; }

    const XsltInstr* match_body = NULL;
    const XsltInstr* nonmatch_body = NULL;
    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind == XSLT_INSTR_MATCHING_SUBSTRING)
            match_body = c->child;
        else if (c->kind == XSLT_INSTR_NONMATCHING_SUBSTRING)
            nonmatch_body = c->child;
    }

    char* saved_src = ex->as_src;
    void* saved_pm = ex->as_pmatch;
    size_t saved_nmatch = ex->as_nmatch;
    size_t saved_pos = ex->as_pos;

    size_t pos = 0;
    while (src[pos] && rc == 0) {
        if (regexec(&rx, src + pos, nmatch, pm, 0) != 0) break;
        size_t so = pos + (size_t)(pm[0].rm_so > 0 ? pm[0].rm_so : 0);
        size_t eo = pos + (size_t)pm[0].rm_eo;
        if (nonmatch_body && so > pos) {
            XPathTextNode* tn = as_segment_node(src + pos, so - pos);
            if (tn) {
                rc = xslt_exec_instrs(ex, nonmatch_body,
                                      (LeptrisElement)tn);
                as_segment_free(tn);
            }
        }
        if (match_body && rc == 0) {
            ex->as_src = src;
            ex->as_pmatch = pm;
            ex->as_nmatch = nmatch;
            ex->as_pos = pos;
            XPathTextNode* tn = as_segment_node(src + so, eo - so);
            if (tn) {
                rc = xslt_exec_instrs(ex, match_body,
                                      (LeptrisElement)tn);
                as_segment_free(tn);
            }
            ex->as_src = saved_src;
            ex->as_pmatch = saved_pm;
            ex->as_nmatch = saved_nmatch;
            ex->as_pos = saved_pos;
        }
        /* Zero-length match: advance one char so the scan terminates. */
        pos = (eo == so) ? so + 1 : eo;
    }
    if (rc == 0 && nonmatch_body && src[pos]) {
        XPathTextNode* tn = as_segment_node(src + pos, strlen(src) - pos);
        if (tn) {
            rc = xslt_exec_instrs(ex, nonmatch_body, (LeptrisElement)tn);
            as_segment_free(tn);
        }
    }
    regfree(&rx);
    free(pm);
#else
    /* MSVC has no POSIX <regex.h>: raise a loud, CATCHABLE dynamic
     * error (xsl:try) instead of silently emitting empty output
     * (issue #686). A portable regex engine is the full fix. */
    (void)rc;
    ex->eval_error = 1;
    snprintf(ex->error, sizeof(ex->error),
             "xsl:analyze-string unavailable in this platform build "
             "(no regex engine) — issue #686");
    free(src);
    return 1;
#endif
#ifndef _WIN32
    free(src);
    return rc;
#endif
}

/* xsl:try/xsl:catch (3.0 §17): the body is the children before the
 * first xsl:catch. A dynamic error (eval_error channel — raised by
 * failing evaluations or the error() bridge) runs the catch content
 * with $err:description bound to the message; the channel clears so
 * the transform continues. */
/* 3.0 §26.4: Saxon-HE 12.7 evaluates on-non-empty content
 * unconditionally (verified against the oracle; the spec allows
 * buffering, parity follows observed behavior). */
/* 3.0 §14 fork: non-streaming = the arms run sequentially into
 * the same destination (Saxon-HE ground truth). */
static int op_fork(XsltExec* ex, const XsltInstr* in,
                   LeptrisElement node) {
    if (!in->child) return 0;
    return xslt_exec_instrs(ex, in->child, node);
}

/* 2.0 §11.7 xsl:namespace: bind prefix->URI on the pending
 * parent (a namespace node in the result). The URI is the string
 * value of the content, captured off-tree (a scratch rtf buffer —
 * emitting into the live result leaks text into the output). */
static int op_namespace(XsltExec* ex, const XsltInstr* in,
                        LeptrisElement node) {
    if (!in->name || !ex->pending_parent) return 0;
    char* uri = xslt_capture_children_text(ex, in->child, node);
    if (uri) {
        leptris_element_add_namespace_definition(ex->pending_parent,
                                                 in->name, uri);
        free(uri);
    }
    return 0;
}

/* 2.0 §11.8 xsl:document: content flows to the pending parent. */
static int op_document(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    if (!in->child) return 0;
    return xslt_exec_instrs(ex, in->child, node);
}

/* §16.1 character-map substitution on a SERIALIZED string: mapped
 * characters are replaced in text spans and inside attribute-value
 * quotes — never elsewhere in markup (comments/PIs count as markup).
 * Returns a fresh string, or NULL when no active map matches. */
char* xslt_apply_output_charmaps(const XsltStylesheet* sheet,
                                 const char* s) {
    if (!sheet || !s || !sheet->out_charmap_name_count) return NULL;
    size_t active = 0;
    for (size_t i = 0; i < sheet->out_charmap_name_count; i++)
        for (size_t m = 0; m < sheet->charmap_count; m++)
            if (sheet->charmaps[m].name &&
                strcmp(sheet->out_charmap_names[i],
                       sheet->charmaps[m].name) == 0)
                active += sheet->charmaps[m].count;
    if (!active) return NULL;
    size_t cap = strlen(s) + 64, len = 0;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    int in_tag = 0, quote = 0, changed = 0;
    for (size_t i = 0; s[i];) {
        const char* repl = NULL;
        size_t clen = 1;
        if (!in_tag || quote) {
            for (size_t n = 0; n < sheet->out_charmap_name_count && !repl;
                 n++)
                for (size_t m = 0; m < sheet->charmap_count && !repl; m++) {
                    if (!sheet->charmaps[m].name ||
                        strcmp(sheet->out_charmap_names[n],
                               sheet->charmaps[m].name) != 0)
                        continue;
                    for (size_t k = 0; k < sheet->charmaps[m].count; k++) {
                        const char* c = sheet->charmaps[m].chars[k];
                        size_t cl = strlen(c);
                        if (cl && strncmp(s + i, c, cl) == 0) {
                            repl = sheet->charmaps[m].repls[k];
                            clen = cl;
                            break;
                        }
                    }
                }
        }
        const char* emit;
        size_t elen;
        if (repl) {
            emit = repl;
            elen = strlen(repl);
            changed = 1;
        } else {
            emit = s + i;
            elen = 1;
            if (!in_tag) {
                if (s[i] == '<') in_tag = 1;
            } else if (quote) {
                if (s[i] == quote) quote = 0;
            } else {
                if (s[i] == '"' || s[i] == '\'') quote = s[i];
                else if (s[i] == '>') in_tag = 0;
            }
        }
        while (len + elen + 1 > cap) {
            cap *= 2;
            char* ng = (char*)realloc(out, cap);
            if (!ng) { free(out); return NULL; }
            out = ng;
        }
        memcpy(out + len, emit, elen);
        len += elen;
        i += clen;
    }
    if (!changed) { free(out); return NULL; }
    out[len] = '\0';
    return out;
}

/* Create every directory component of a file path (best effort). */
static void mkpath_for_file(const char* file) {
    char* dup = leptris_strdup(file);
    if (!dup) return;
    for (char* p = dup + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
#if defined(_WIN32)
        _mkdir(dup);
#else
        mkdir(dup, 0755);
#endif
        *p = '/';
    }
    free(dup);
}

/* 2.0/3.0 §11.8 xsl:result-document: build the content in a fresh
 * scratch document, serialize it (with the sheet's character maps),
 * and write it to the href file. The principal result is unchanged. */
static int op_result_document(XsltExec* ex, const XsltInstr* in,
                              LeptrisElement node) {
    if (!in->name || !in->child) return 0;
    LeptrisDocument saved_res = ex->result;
    LeptrisElement saved_pp = ex->pending_parent;
    ex->result = leptris_document_create();
    ex->pending_parent = NULL;
    xslt_exec_instrs(ex, in->child, node);
    LeptrisDocument rd = ex->result;
    ex->result = saved_res;
    ex->pending_parent = saved_pp;
    LeptrisSerializeOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.xml_declaration = 1;
    opts.encoding = "UTF-8";
    char* out = leptris_document_serialize(rd, &opts);
    leptris_document_free(rd);
    if (!out) return 0;
    char* mapped = xslt_apply_output_charmaps(ex->sheet, out);
    if (mapped) { free(out); out = mapped; }
    mkpath_for_file(in->name);
    FILE* f = fopen(in->name, "wb");
    if (f) {
        fwrite(out, 1, strlen(out), f);
        fclose(f);
    }
    free(out);
    return 0;
}

static int op_on_non_empty(XsltExec* ex, const XsltInstr* in,
                           LeptrisElement node) {
    if (!in->child) return 0;
    return xslt_exec_instrs(ex, in->child, node);
}

/* 3.0 §26.2 where-populated: content runs into a scratch element;
 * nodes built there splice into the real parent, an empty build
 * vanishes. */
static int op_where_populated(XsltExec* ex, const XsltInstr* in,
                              LeptrisElement node) {
    if (!in->child) return 0;
    LeptrisElement saved = ex->pending_parent;
    /* A detached scratch element on the result document — never in
     * the fragment chain, so an empty build cannot leak a wrapper
     * into the output. */
    LeptrisElement scratch = leptris_element_create(ex->result, "wp");
    if (!scratch) {
        ex->pending_parent = saved;
        return xslt_exec_instrs(ex, in->child, node);
    }
    ex->pending_parent = scratch;
    int rc = xslt_exec_instrs(ex, in->child, node);
    ex->pending_parent = saved;
    if (rc) return rc;
    if (!leptris_elem_first_child(scratch)) return 0;   /* empty: drop */
    /* Splice scratch children into the real parent. */
    LeptrisNodeRef c = leptris_node_first_child(leptris_element_as_node(scratch));
    while (c) {
        LeptrisNodeRef nx = leptris_node_next_sibling(c);
        leptris_element_append_child_internal(
            ex->pending_parent ? ex->pending_parent
                               : (LeptrisElement)leptris_document_root(
                                     ex->result),
            (LeptrisNode*)c);
        c = nx;
    }
    return 0;
}

/* 3.0 §6.6 next-match: among templates matching the node, invoke
 * the best one strictly WORSE than the current rule (import rank,
 * then priority, then declaration order). */
static int op_apply_templates(XsltExec* ex, const XsltInstr* in,
                              LeptrisElement node);
static int xslt_invoke_template(XsltExec* ex, const XsltTemplate* t,
                                LeptrisElement node,
                                const XsltInstr* with_params);

static double cur_pri_of(const XsltTemplate* t) {
    double pri = 0;
    for (const XsltPattern* pa = t->matches; pa; pa = pa->next)
        if (pa->priority > pri) pri = pa->priority;
    return pri;
}

static size_t cur_order_of(const XsltExec* ex, const XsltTemplate* cur) {
    size_t order = 0;
    for (const XsltTemplate* t = ex->sheet->templates; t;
         t = t->next, order++)
        if (t == cur) return order;
    return 0;
}

static const XsltTemplate* xslt_select_next_match(
        const XsltExec* ex, LeptrisElement node, const char* mode) {
    const XsltTemplate* cur = ex->current_template;
    if (!cur) return NULL;
    const XsltTemplate* best = NULL;
    double best_pri = 0;
    size_t best_order = 0, order = 0;
    for (const XsltTemplate* t = ex->sheet->templates; t;
         t = t->next, order++) {
        if (!t->matches || t == cur) continue;
        if ((mode && !t->mode) || (!mode && t->mode)) continue;
        if (mode && t->mode && strcmp(mode, t->mode) != 0) continue;
        /* ONE pass over the alternatives (see xslt_select_template). */
        double pri = 0; int have = 0;
        for (const XsltPattern* pa = t->matches; pa; pa = pa->next) {
            if (!xslt_pattern_matches(pa, node, ex->source, t->ns))
                continue;
            if (!have || pa->priority > pri) { pri = pa->priority; have = 1; }
        }
        if (!have) continue;
        /* t must be strictly worse than cur. */
        int worse;
        if (t->import_rank != cur->import_rank)
            worse = t->import_rank > cur->import_rank;
        else if (pri != cur_pri_of(cur))
            worse = pri < cur_pri_of(cur);
        else worse = order < cur_order_of(ex, cur);
        if (!worse) continue;
        int wins = 0;
        if (!best) wins = 1;
        else if (t->import_rank != best->import_rank)
            wins = t->import_rank < best->import_rank;
        else if (pri != best_pri) wins = pri > best_pri;
        else wins = order >= best_order;
        if (wins) { best = t; best_pri = pri; best_order = order; }
    }
    return best;
}

static int op_next_match(XsltExec* ex, const XsltInstr* in,
                         LeptrisElement node) {
    const XsltTemplate* t = xslt_select_next_match(
        ex, node, ex->current_template ? ex->current_template->mode
                                       : NULL);
    if (!t) {
        /* No lower rule: the built-in applies (§6.6). */
        return op_apply_templates(
            ex,
            &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES,
                          .name = ex->current_template
                              ? ex->current_template->mode : NULL },
            node);
    }
    (void)in;
    return xslt_invoke_template(ex, t, node, NULL);
}


static int op_try(XsltExec* ex, const XsltInstr* in,
                  LeptrisElement node) {
    XsltInstr* catch_at = NULL;
    XsltInstr** link = NULL;
    for (XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind == XSLT_INSTR_CATCH) {
            catch_at = c;
            for (link = (XsltInstr**)&in->child;
                 link && *link != catch_at; link = &(*link)->next) {}
            break;
        }
    }
    if (link) *link = NULL;   /* body ends before the catch */

    int saved_err = ex->eval_error;
    char saved_msg[sizeof(ex->error)];
    memcpy(saved_msg, ex->error, sizeof(saved_msg));
    ex->eval_error = 0;

    int rc = xslt_exec_instrs(ex, in->child, node);

    if (ex->eval_error) {
        char msg[sizeof(ex->error)];
        memcpy(msg, ex->error, sizeof(msg));
        if (catch_at) {
            ex->eval_error = 0;
            ex->error[0] = '\0';
            XsltVar* scope = ex->vars;
            struct leptris_xpath_result* dv =
                xpath_result_new(XPATH_RESULT_STRING);
            if (dv) {
                dv->value.string_value = leptris_strdup(msg);
                xslt_push_var(ex, "err:description", dv);
            }
            rc = xslt_exec_instrs(ex, catch_at->child, node);
            xslt_pop_vars_to(ex, scope);
        }
    } else {
        ex->eval_error = saved_err;
        memcpy(ex->error, saved_msg, sizeof(saved_msg));
    }

    if (link) *link = catch_at;
    return rc;
}

/* §10 stable multi-key sort — caches each (item,sort-key) string,
 * compares by walking all sort keys in order (single comparator). */
static void xslt_sort_items(XsltExec* ex, const XsltInstr* in,
                            LeptrisElement* items, size_t n) {
    if (n < 2 || !in->sorts) return;
    size_t nsorts = 0;
    for (const XsltSort* s = in->sorts; s; s = s->next) nsorts++;
    char** keys = (char**)calloc(nsorts * n, sizeof(char*));
    for (size_t i = 0; i < n; i++) {
        const XsltSort* s = in->sorts;
        for (size_t k = 0; k < nsorts && s; k++, s = s->next) {
            char* kp = NULL;
            if (s->select) {
                struct leptris_xpath_result* r =
                    xslt_eval(ex, s->select, items[i]);
                if (r) {
                    kp = leptris_xpath_result_string(r);
                    leptris_xpath_result_free(r);
                }
            } else {
                /* Synthetic sequence items carry no deep value —
                 * their text IS the sort key (perform-sort on
                 * atomic sequences). */
                extern char* get_node_text(void* n);
                kp = get_node_text(items[i]);
                if (!kp) kp = string_value_deep(items[i]);
            }
            keys[k * n + i] = kp;
        }
    }
    for (size_t i = 1; i < n; i++) {
        for (size_t j = i; j > 0; j--) {
            int cmp = 0;
            for (size_t k = 0; k < nsorts && !cmp; k++) {
                const char* a = keys[k * n + (j-1)];
                const char* b = keys[k * n + j];
                const XsltSort* s = in->sorts;
                for (size_t ki = 0; ki < k; ki++) s = s->next;
                const char* a0 = a ? a : "";
                const char* b0 = b ? b : "";
                if (s->numeric) {
                    char *ea = NULL, *eb = NULL;
                    double av = a0[0] ? strtod(a0, &ea) : (NAN);
                    if (a0[0] && ea == a0) av = NAN;
                    double bv = b0[0] ? strtod(b0, &eb) : (NAN);
                    if (b0[0] && eb == b0) bv = NAN;
                    int an = (av != av), bn = (bv != bv);
                    if (an && bn) cmp = 0;
                    else if (an) cmp = -1;
                    else if (bn) cmp = 1;
                    else cmp = (av < bv) ? -1 : (av > bv) ? 1 : 0;
                } else {
                    cmp = (xslt_ci_eq(a0, b0) && !s->case_upper_first)
                              ? -strcmp(a0, b0) : strcmp(a0, b0);
                }
                if (s->descending) cmp = -cmp;
            }
            if (cmp <= 0) break;
            LeptrisElement te = items[j-1]; items[j-1] = items[j]; items[j] = te;
            for (size_t k = 0; k < nsorts; k++) {
                char* tk = keys[k * n + (j-1)];
                keys[k * n + (j-1)] = keys[k * n + j];
                keys[k * n + j] = tk;
            }
        }
    }
    for (size_t i = 0; i < nsorts * n; i++) free(keys[i]);
    free(keys);
}

static int op_for_each(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    size_t n = leptris_xpath_result_count(r);
    /* Collect the element handles first: sort swaps them, and the
     * result may be freed mid-iteration otherwise. */
    LeptrisElement* items = (n > 0)
        ? (LeptrisElement*)calloc(n, sizeof(LeptrisElement)) : NULL;
    if (items) {
        /* Node-typed: for-each bodies see text/comment/PI/namespace
         * nodes (the element-typed accessor nulls them). */
        for (size_t i = 0; i < n; i++) {
            items[i] = (LeptrisElement)leptris_xpath_result_get_node(r, i);
        }
    }
    /* r stays alive across the loop: synthetic namespace/attribute
     * result nodes are RESULT-OWNED — freeing early dangles them. */
    if (!items) { leptris_xpath_result_free(r); return n ? -1 : 0; }

    if (in->sorts) xslt_sort_items(ex, in, items, n);
    int rc = 0;
    size_t saved_pos = ex->current_pos;
    size_t saved_size = ex->current_size;
    ex->current_size = n;
    for (size_t i = 0; i < n && rc == 0; i++) {
        ex->current_pos = i + 1;
        rc = xslt_exec_instrs(ex, in->child, items[i]);
    }
    ex->current_pos = saved_pos;
    ex->current_size = saved_size;
    free(items);
    leptris_xpath_result_free(r);
    return rc;
}

/* xsl:iterate (3.0 §12.5): params chain free helper. */
static struct leptris_xpath_result* xslt_capture_content(
        XsltExec* ex, const XsltInstr* child, LeptrisElement node);

static void iter_params_free(XsltVar* p) {
    while (p) {
        XsltVar* prev = p->prev;
        if (p->value) leptris_xpath_result_free(p->value);
        free(p);
        p = prev;
    }
}

/* Per-iteration param binding: a clone whose nodeset BORROWS the
 * source's node pointers (the initial values outlive every pass —
 * they are freed only after the loop ends). */
static struct leptris_xpath_result* result_clone_borrowed(
        const struct leptris_xpath_result* src) {
    if (!src) return NULL;
    struct leptris_xpath_result* out =
        xpath_result_new((XPathResultType)src->type);
    if (!out) return NULL;
    switch (src->type) {
        case XPATH_RESULT_STRING:
            out->value.string_value =
                leptris_strdup(src->value.string_value
                                   ? src->value.string_value : "");
            break;
        case XPATH_RESULT_NUMBER:
            out->value.number_value = src->value.number_value;
            break;
        case XPATH_RESULT_BOOLEAN:
            out->value.boolean_value = src->value.boolean_value;
            break;
        case XPATH_RESULT_NODESET: {
            XPathNodeSet* sn = src->value.nodeset_value;
            XPathNodeSet* dn = xpath_nodeset_new();
            if (!dn) break;
            for (size_t i = 0; sn && i < sn->count; i++)
                xpath_nodeset_add(dn, sn->nodes[i]);
            out->value.nodeset_value = dn;
            break;
        }
        default: break;
    }
    return out;
}

static int op_iterate(XsltExec* ex, const XsltInstr* in,
                      LeptrisElement node) {
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    size_t n = leptris_xpath_result_count(r);
    LeptrisElement* items = (n > 0)
        ? (LeptrisElement*)calloc(n, sizeof(LeptrisElement)) : NULL;
    if (items) {
        for (size_t i = 0; i < n; i++)
            items[i] = (LeptrisElement)leptris_xpath_result_get_node(r, i);
    }
    if (!items) { leptris_xpath_result_free(r); return n ? -1 : 0; }

    /* Initial param values evaluate ONCE, in the caller's scope
     * (§12.5: outside the iteration). */
    XsltVar* params = NULL;
    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind != XSLT_INSTR_VARIABLE || !c->is_param) break;
        struct leptris_xpath_result* v = NULL;
        if (c->select) v = xslt_eval(ex, c->select, node);
        else if (c->child) v = xslt_capture_content(ex, c->child, node);
        XsltVar* pv = (XsltVar*)calloc(1, sizeof(*pv));
        if (!pv) { if (v) leptris_xpath_result_free(v); break; }
        pv->name = c->name;
        pv->value = v;
        pv->prev = params;
        params = pv;
    }

    int rc = 0;
    size_t saved_pos = ex->current_pos;
    size_t saved_size = ex->current_size;
    ex->current_size = n;
    ex->iterate_depth++;
    for (size_t i = 0; i < n; i++) {
        ex->current_pos = i + 1;
        XsltVar* scope_mark = ex->vars;
        size_t nparams = 0;
        for (XsltVar* pv = params; pv; pv = pv->prev) {
            xslt_push_var(ex, pv->name, result_clone_borrowed(pv->value));
            nparams++;
        }
        ex->iterate_signal = 0;
        rc = xslt_exec_instrs(ex, in->child, items[i]);
        int sig = ex->iterate_signal;
        ex->iterate_signal = 0;

        if (sig == 1) {
            /* Merge the with-param rebindings over the current
             * values; names not supplied keep their value. */
            XsltVar* np = ex->iter_params;
            while (np) {
                XsltVar* nx = np->prev;
                XsltVar* found = NULL;
                for (XsltVar* pv = params; pv && !found; pv = pv->prev)
                    if (strcmp(pv->name, np->name) == 0) found = pv;
                if (found) {
                    if (found->value)
                        leptris_xpath_result_free(found->value);
                    found->value = np->value;
                    free(np);
                } else {
                    np->prev = params;
                    params = np;
                }
                np = nx;
            }
            ex->iter_params = NULL;
        }
        xslt_pop_vars_to(ex, scope_mark);
        if (nparams && ex->vars == scope_mark) { /* balanced */ }
        if (sig == 2) break;
        if (rc) break;
    }
    /* 3.0 §12.5: on-completion runs when the loop ENDED without
     * xsl:break (sig != 2 on the final pass), with the FINAL
     * iteration's parameter values in scope (#729). Its instruction
     * is skipped during the per-item walk (no registered op). */
    if (ex->iterate_signal != 2) {
        for (const XsltInstr* c = in->child; c; c = c->next) {
            if (c->kind != XSLT_INSTR_ON_COMPLETION) continue;
            XsltVar* oc_mark = ex->vars;
            for (XsltVar* pv = params; pv; pv = pv->prev)
                xslt_push_var(ex, pv->name,
                              result_clone_borrowed(pv->value));
            rc = xslt_exec_instrs(ex, c->child, node);
            xslt_pop_vars_to(ex, oc_mark);
            break;
        }
    }
    ex->iterate_depth--;
    ex->iterate_signal = 0;
    iter_params_free(ex->iter_params);
    ex->iter_params = NULL;
    iter_params_free(params);
    ex->current_pos = saved_pos;
    ex->current_size = saved_size;
    free(items);
    leptris_xpath_result_free(r);
    return rc;
}

static int op_next_iteration(XsltExec* ex, const XsltInstr* in,
                             LeptrisElement node) {
    if (ex->iterate_depth <= 0) return 0;   /* no enclosing iterate */
    iter_params_free(ex->iter_params);
    ex->iter_params = NULL;
    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind != XSLT_INSTR_WITH_PARAM || !c->name) continue;
        struct leptris_xpath_result* v = NULL;
        if (c->select) v = xslt_eval(ex, c->select, node);
        else if (c->child) v = xslt_capture_content(ex, c->child, node);
        XsltVar* pv = (XsltVar*)calloc(1, sizeof(*pv));
        if (!pv) { if (v) leptris_xpath_result_free(v); continue; }
        pv->name = c->name;
        pv->value = v;
        pv->prev = ex->iter_params;
        ex->iter_params = pv;
    }
    ex->iterate_signal = 1;
    return 0;
}

static int op_break(XsltExec* ex, const XsltInstr* in,
                    LeptrisElement node) {
    if (ex->iterate_depth <= 0) return 0;
    if (in->child) xslt_exec_instrs(ex, in->child, node);
    ex->iterate_signal = 2;
    return 0;
}

/* xsl:merge (3.0 §14.3): full outer join of every merge-source on
 * the composite merge-key. v1: @select sources (for-each-source is
 * TODO), string keys, and the FIRST key's @order governs the whole
 * composite. Sources' select results stay alive for the whole merge
 * (the entry list borrows their nodes). */
typedef struct xslt_merge_entry {
    char* key;                  /* owned composite key */
    LeptrisNodeRef node;        /* borrowed from the source result */
    size_t src;                 /* source index */
    size_t ord;                 /* selection order (stable tiebreak) */
} XsltMergeEntry;

/* Merge sort direction flag for qsort (no context arg on MSVC). */
#if defined(_MSC_VER)
#  define LEPTRIS_TLS __declspec(thread)
#else
#  define LEPTRIS_TLS __thread
#endif
static LEPTRIS_TLS int g_merge_desc;

static int merge_entry_cmp(const void* a, const void* b) {
    const XsltMergeEntry* x = (const XsltMergeEntry*)a;
    const XsltMergeEntry* y = (const XsltMergeEntry*)b;
    int c = strcmp(x->key, y->key);
    if (g_merge_desc) c = -c;
    if (c == 0) c = (x->ord > y->ord) - (x->ord < y->ord);
    return c;
}

static int op_merge(XsltExec* ex, const XsltInstr* in,
                    LeptrisElement node) {
    const XsltInstr* action = NULL;
    const XsltInstr** srcs = NULL;
    struct leptris_xpath_result** sres = NULL;
    XsltMergeEntry* ents = NULL;
    XsltMergeSide* sides = NULL;
    size_t n_src = 0, n_ents = 0, ent_cap = 0;
    int rc = 0;

    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind == XSLT_INSTR_MERGE_SOURCE) n_src++;
        else if (c->kind == XSLT_INSTR_MERGE_ACTION) action = c;
    }
    if (!action || !n_src) return 0;
    srcs = (const XsltInstr**)calloc(n_src, sizeof(*srcs));
    sres = (struct leptris_xpath_result**)
        calloc(n_src, sizeof(*sres));
    if (!srcs || !sres) { free(srcs); free(sres); return 0; }
    size_t si = 0;
    for (const XsltInstr* c = in->child; c; c = c->next)
        if (c->kind == XSLT_INSTR_MERGE_SOURCE) srcs[si++] = c;

    g_merge_desc = 0;
    size_t ord = 0;
    for (si = 0; si < n_src; si++) {
        const XsltInstr* s = srcs[si];
        if (!s->select) continue;
        struct leptris_xpath_result* r = xslt_eval(ex, s->select, node);
        if (!r) continue;
        sres[si] = r;   /* stays alive: entries borrow its nodes */
        size_t cnt = leptris_xpath_result_count(r);
        for (size_t i = 0; i < cnt; i++) {
            LeptrisNodeRef nd = leptris_xpath_result_get_node(r, i);
            if (!nd) continue;
            /* Composite key: every merge-key child's string value,
             * '\x01'-joined (not in normal text, unlike '|'). Keys
             * live INSIDE the merge-source (REC grammar) or as direct
             * children of xsl:merge (accepted by Saxon-HE — #731:
             * sources without their own keys fell through with an
             * empty composite, collapsing every group). */
            const XsltInstr* skeys = NULL;
            for (const XsltInstr* k = s->child; k; k = k->next)
                if (k->kind == XSLT_INSTR_MERGE_KEY) { skeys = s->child; break; }
            if (!skeys) skeys = in->child;   /* merge-level shared keys */
            size_t klen = 0, kcap = 16;
            char* key = (char*)malloc(kcap);
            if (!key) continue;
            for (const XsltInstr* k = skeys; k; k = k->next) {
                if (k->kind != XSLT_INSTR_MERGE_KEY || !k->select)
                    continue;
                struct leptris_xpath_result* kr =
                    xslt_eval(ex, k->select, (LeptrisElement)nd);
                char* ks = kr ? leptris_xpath_result_string(kr) : NULL;
                if (kr) leptris_xpath_result_free(kr);
                size_t add = (ks ? strlen(ks) : 0) + 1;
                if (klen + add + 1 > kcap) {
                    kcap = (klen + add + 1) * 2;
                    char* nk = (char*)realloc(key, kcap);
                    if (!nk) { free(key); free(ks); key = NULL; break; }
                    key = nk;
                }
                if (klen) key[klen++] = '\x01';
                if (ks) { memcpy(key + klen, ks, strlen(ks));
                          klen += strlen(ks); }
                free(ks);
                if (skeys == k && !g_merge_desc && k->num_start_at)
                    g_merge_desc = 1;
            }
            if (!key) continue;
            key[klen] = '\0';
            if (n_ents == ent_cap) {
                ent_cap = ent_cap ? ent_cap * 2 : 16;
                XsltMergeEntry* ne =
                    (XsltMergeEntry*)realloc(ents, ent_cap * sizeof(*ne));
                if (!ne) { free(key); continue; }
                ents = ne;
            }
            ents[n_ents].key = key;
            ents[n_ents].node = nd;
            ents[n_ents].src = si;
            ents[n_ents].ord = ord++;
            n_ents++;
        }
    }

    qsort(ents, n_ents, sizeof(*ents), merge_entry_cmp);

    sides = (XsltMergeSide*)calloc(n_src, sizeof(*sides));
    if (sides)
        for (si = 0; si < n_src; si++) sides[si].name = srcs[si]->name;

    char* saved_key = ex->merge_key;
    XsltMergeSide* saved_sides = ex->merge_sides;
    size_t saved_count = ex->merge_side_count;

    size_t i = 0;
    while (i < n_ents && !rc) {
        size_t j = i;
        while (j < n_ents && strcmp(ents[j].key, ents[i].key) == 0) j++;
        for (si = 0; si < n_src; si++) sides[si].n = 0;
        for (size_t k = i; k < j; k++) {
            XsltMergeSide* sd = &sides[ents[k].src];
            if (sd->n == sd->cap) {
                sd->cap = sd->cap ? sd->cap * 2 : 4;
                LeptrisNodeRef* ni = (LeptrisNodeRef*)
                    realloc(sd->items, sd->cap * sizeof(*ni));
                if (!ni) { sd->cap /= 2; continue; }
                sd->items = ni;
            }
            sd->items[sd->n++] = ents[k].node;
        }
        ex->merge_key = ents[i].key;
        ex->merge_sides = sides;
        ex->merge_side_count = n_src;
        LeptrisElement ctx = NULL;
        for (si = 0; si < n_src && !ctx; si++)
            if (sides[si].n) ctx = (LeptrisElement)sides[si].items[0];
        rc = xslt_exec_instrs(ex, action->child, ctx ? ctx : node);
        i = j;
    }

    ex->merge_key = saved_key;
    ex->merge_sides = saved_sides;
    ex->merge_side_count = saved_count;

    if (sides)
        for (si = 0; si < n_src; si++) free(sides[si].items);
    free(sides);
    for (i = 0; i < n_ents; i++) free(ents[i].key);
    free(ents);
    for (si = 0; si < n_src; si++)
        if (sres[si]) leptris_xpath_result_free(sres[si]);
    free(sres);
    free(srcs);
    return rc;
}

/* xsl:for-each-group (3.0 §14). Groups in first-appearance order;
 * each group owns its member array (borrowed pointers out of the
 * select's result, which stays alive for the whole loop). */
typedef struct xslt_feg_group {
    char* key;                  /* owned; NULL for pattern groups */
    LeptrisElement* items;
    size_t n, cap;
} XsltFegGroup;

static int feg_group_push(XsltFegGroup* g, LeptrisElement it) {
    if (g->n == g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 4;
        LeptrisElement* grown =
            (LeptrisElement*)realloc(g->items, cap * sizeof(*grown));
        if (!grown) return -1;
        g->items = grown;
        g->cap = cap;
    }
    g->items[g->n++] = it;
    return 0;
}

static int op_for_each_group(XsltExec* ex, const XsltInstr* in,
                             LeptrisElement node) {
    if (!in->group_by && !in->group_starting && !in->group_ending)
        return 0;
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    size_t n = leptris_xpath_result_count(r);
    LeptrisElement* items = (n > 0)
        ? (LeptrisElement*)calloc(n, sizeof(LeptrisElement)) : NULL;
    if (items) {
        for (size_t i = 0; i < n; i++)
            items[i] = (LeptrisElement)leptris_xpath_result_get_node(r, i);
    }
    if (!items) { leptris_xpath_result_free(r); return n ? -1 : 0; }

    XsltFegGroup* groups = (n > 0)
        ? (XsltFegGroup*)calloc(n, sizeof(XsltFegGroup)) : NULL;
    size_t ngroups = 0;
    int cur_group_closed = 0;   /* group-ending-with state */
    int rc = 0;
    if (!groups) { rc = -1; goto done; }

    for (size_t i = 0; i < n; i++) {
        XsltFegGroup* g = NULL;
        if (in->group_by) {
            char* key = NULL;
            struct leptris_xpath_result* kv =
                xslt_eval(ex, in->group_by, items[i]);
            if (kv) {
                key = leptris_xpath_result_string(kv);
                leptris_xpath_result_free(kv);
            }
            if (in->group_adjacent) {
                /* group-adjacent: only the LAST group can absorb the
                 * item; an equal earlier key does not re-open. */
                if (ngroups) {
                    const char* a =
                        groups[ngroups - 1].key ? groups[ngroups - 1].key : "";
                    const char* b = key ? key : "";
                    if (strcmp(a, b) == 0) g = &groups[ngroups - 1];
                }
            } else {
                for (size_t j = 0; j < ngroups; j++) {
                    const char* a = groups[j].key ? groups[j].key : "";
                    const char* b = key ? key : "";
                    if (strcmp(a, b) == 0) { g = &groups[j]; break; }
                }
            }
            if (!g) {
                g = &groups[ngroups++];
                g->key = key;
            } else {
                free(key);
            }
        } else if (in->group_ending) {
            /* group-ending-with: items accumulate into the open
             * group; a match CLOSES it (the next item opens a fresh
             * one). Trailing non-matches form the final group. */
            if (cur_group_closed || ngroups == 0)
                g = &groups[ngroups++];
            else
                g = &groups[ngroups - 1];
            cur_group_closed = 0;
        } else {
            /* group-starting-with: a match opens a new group; the
             * implicit first group collects leading non-matches. */
            if (ngroups == 0 || xslt_pattern_matches(
                    in->group_starting, items[i], ex->source, in->ns))
                g = &groups[ngroups++];
            else
                g = &groups[ngroups - 1];
        }
        if (feg_group_push(g, items[i])) { rc = -1; break; }
        if (in->group_ending && xslt_pattern_matches(
                in->group_ending, items[i], ex->source, in->ns))
            cur_group_closed = 1;
    }

    {
        size_t saved_pos = ex->current_pos;
        size_t saved_size = ex->current_size;
        XPathNodeSet* saved_group = ex->cur_group;
        char* saved_key = ex->cur_group_key;
        ex->current_size = ngroups;
        for (size_t gi = 0; gi < ngroups && rc == 0; gi++) {
            if (groups[gi].n == 0) continue;
            ex->cur_group = xpath_nodeset_new();
            if (!ex->cur_group) { rc = -1; break; }
            for (size_t m = 0; m < groups[gi].n; m++)
                xpath_nodeset_add(ex->cur_group, groups[gi].items[m]);
            ex->cur_group_key =
                groups[gi].key ? leptris_strdup(groups[gi].key) : NULL;
            ex->current_pos = gi + 1;
            rc = xslt_exec_instrs(ex, in->child, groups[gi].items[0]);
            xpath_nodeset_free(ex->cur_group);
            free(ex->cur_group_key);
            ex->cur_group = NULL;
            ex->cur_group_key = NULL;
        }
        ex->cur_group = saved_group;
        ex->cur_group_key = saved_key;
        ex->current_pos = saved_pos;
        ex->current_size = saved_size;
    }

done:
    if (groups) {
        for (size_t j = 0; j < ngroups; j++) {
            free(groups[j].key);
            free(groups[j].items);
        }
        free(groups);
    }
    free(items);
    leptris_xpath_result_free(r);
    return rc;
}

static int op_if(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
    struct leptris_xpath_result* r = xslt_eval(ex, in->test, node);
    int truth = r ? leptris_xpath_result_boolean(r) : 0;
    if (r) leptris_xpath_result_free(r);
    if (!truth) return 0;
    return xslt_exec_instrs(ex, in->child, node);
}

static int copy_node_deep(XsltExec* ex, LeptrisElement node,
                          LeptrisElement parent);

static int op_copy_of(XsltExec* ex, const XsltInstr* in,
                      LeptrisElement node) {
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    if (leptris_xpath_result_count(r) > 0) {
        size_t n = leptris_xpath_result_count(r);
        for (size_t i = 0; i < n; i++) {
            LeptrisNodeRef cn = leptris_xpath_result_get_node(r, i);
            if (cn && leptris_node_get_type(cn) ==
                         LEPTRIS_NODE_TYPE_DOCUMENT) {
                /* An RTF fragment root: copy its children (the
                 * top-level nodes of the fragment). */
                struct leptris_document* fd =
                    ((LeptrisDocumentNode*)cn)->doc;
                for (LeptrisNodeRef c =
                         (LeptrisNodeRef)(fd->new_dom_root
                              ? fd->new_dom_root : fd->root);
                     c; c = leptris_node_next_sibling(c)) {
                    if (leptris_node_get_type(c) ==
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        copy_node_deep(ex, (LeptrisElement)c,
                                       ex->pending_parent);
                }
            } else if (cn && leptris_node_get_type(cn) ==
                                 LEPTRIS_NODE_TYPE_ELEMENT) {
                copy_node_deep(ex, (LeptrisElement)cn, ex->pending_parent);
            } else if (cn) {
                int cty = leptris_node_get_type(cn);
                if (cty == LEPTRIS_NODE_TYPE_COMMENT) {
                    LeptrisNodeRef cm = leptris_comment_node_create(
                        ex->result,
                        leptris_comment_node_get_content(cn));
                    if (cm) {
                        if (ex->pending_parent)
                            leptris_element_append_child_internal(
                                ex->pending_parent, (LeptrisNode*)cm);
                        else
                            xslt_append_fragment_node(ex, cm);
                    }
                } else if (cty == LEPTRIS_NODE_TYPE_PI) {
                    LeptrisNodeRef pi = leptris_pi_node_create(
                        ex->result,
                        leptris_pi_node_get_target(cn),
                        leptris_pi_node_get_data(cn));
                    if (pi) {
                        if (ex->pending_parent)
                            leptris_element_append_child_internal(
                                ex->pending_parent, (LeptrisNode*)pi);
                        else
                            xslt_append_fragment_node(ex, pi);
                    }
                } else if (cty == LEPTRIS_NODE_TYPE_CDATA) {
                    const char* ccd =
                        leptris_text_get_content((LeptrisTextNode*)cn);
                    LeptrisNodeRef cc = (LeptrisNodeRef)leptris_cdata_create(
                        ccd, ccd ? strlen(ccd) : 0,
                        ((struct leptris_document*)ex->result)->pool);
                    if (cc) {
                        if (ex->pending_parent)
                            leptris_element_append_child_internal(
                                ex->pending_parent, (LeptrisNode*)cc);
                        else
                            xslt_append_fragment_node(ex, cc);
                    }
                } else if (cty == LEPTRIS_NODE_TYPE_TEXT) {
                    const char* tc =
                        leptris_text_get_content((LeptrisTextNode*)cn);
                    if (tc) out_append_text(ex, ex->pending_parent, tc);
                } else if (cty == LEPTRIS_NODE_ATTRIBUTE) {
                    /* §11.3: copy-of an attribute node adds the
                     * name/value onto the pending parent (bug-3-:
                     * previously masked by the copy auto-adding
                     * attributes). */
                    LeptrisAttributeNode* an = (LeptrisAttributeNode*)cn;
                    if (ex->pending_parent && an->name)
                        leptris_element_set_attribute(
                            ex->pending_parent, an->name,
                            an->value ? an->value : "");
                } else if (cty == LEPTRIS_NODE_NAMESPACE) {
                    /* §7.5/§12.1: copying a namespace node adds the
                     * declaration onto the pending parent element
                     * (libxslt bug-38- — the op dropped ns nodes).
                     * libxslt's copy: prefixed bindings only — the
                     * implicit xml namespace and duplicates of
                     * already-bound prefixes are skipped (bug-54). */
                    LeptrisNamespaceNode* nn = (LeptrisNamespaceNode*)cn;
                    LeptrisElement pp = ex->pending_parent;
                    if (pp && nn->prefix && nn->prefix[0] && nn->uri) {
                        if (strcmp(nn->prefix, "xml") == 0 &&
                            strcmp(nn->uri,
                                   "http://www.w3.org/XML/1998/namespace")
                                == 0)
                            continue;
                        const char* cur =
                            leptris_element_namespace_for_prefix(
                                pp, nn->prefix);
                        if (cur && strcmp(cur, nn->uri) == 0) continue;
                        leptris_element_add_namespace_definition(
                            pp, nn->prefix, nn->uri);
                    }
                }
            }
        }
    } else {
        char* sv = leptris_xpath_result_string(r);
        if (sv) { out_append_text(ex, ex->pending_parent, sv); free(sv); }
    }
    leptris_xpath_result_free(r);
    return 0;
}

static int op_copy(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
    /* 3.0 §9.9.2: with @select the sequence constructor is ignored
     * and each selected item is copied — the same per-item rules as
     * copy-of (Saxon-HE 12.7 verified: attribute items rebind on
     * the pending parent, element items deep-copy). */
    if (in->select) return op_copy_of(ex, in, node);
    /* §7.5: copying an ATTRIBUTE (re)binds it on the pending
     * parent — later copies override earlier xsl:attribute values
     * (execution order wins, §7.1.3). */
    if (node && ((LeptrisNode*)node)->type == LEPTRIS_NODE_ATTRIBUTE) {
        LeptrisAttributeNode* a = (LeptrisAttributeNode*)node;
        if (ex->pending_parent && a->name)
            leptris_element_set_attribute(ex->pending_parent,
                                          a->name,
                                          a->value ? a->value : "");
        return 0;
    }
    /* §7.5: copy of a NAMESPACE node declares it on the pending
     * parent (the classic copy-the-namespaces idiom). */
    if (node && ((LeptrisNode*)node)->type == LEPTRIS_NODE_NAMESPACE) {
        LeptrisNamespaceNode* ns = (LeptrisNamespaceNode*)node;
        /* The implicit xml prefix is always in scope — copying it
         * would emit a redundant xmlns:xml (libxslt omits it). */
        if (ex->pending_parent && ns->uri && ns->prefix &&
            strcmp(ns->prefix, "xml") == 0)
            return 0;
        if (ex->pending_parent && ns->uri)
            leptris_element_add_namespace_definition(
                ex->pending_parent,
                ns->prefix ? ns->prefix : "", ns->uri);
        return 0;
    }
    /* §7.5: copy of a comment/PI node copies the node itself. */
    {
        int nty = leptris_node_get_type((LeptrisNodeRef)node);
        if (nty == LEPTRIS_NODE_TYPE_COMMENT) {
            LeptrisNodeRef cm = leptris_comment_node_create(
                ex->result, leptris_comment_node_get_content(
                                (LeptrisNodeRef)node));
            if (cm && ex->pending_parent)
                leptris_element_append_child_internal(
                    ex->pending_parent, (LeptrisNode*)cm);
            else if (cm)
                xslt_append_fragment_node(ex, cm);
            return 0;
        }
        if (nty == LEPTRIS_NODE_TYPE_PI) {
            LeptrisNodeRef pi = leptris_pi_node_create(
                ex->result,
                leptris_pi_node_get_target((LeptrisNodeRef)node),
                leptris_pi_node_get_data((LeptrisNodeRef)node));
            if (pi && ex->pending_parent)
                leptris_element_append_child_internal(
                    ex->pending_parent, (LeptrisNode*)pi);
            else if (pi)
                xslt_append_fragment_node(ex, pi);
            return 0;
        }
        if (nty == LEPTRIS_NODE_TYPE_CDATA) {
            /* A copied CDATA section stays CDATA (§7.5). */
            const char* cc = leptris_text_node_get_content(
                (LeptrisNodeRef)node);
            LeptrisNodeRef cn = (LeptrisNodeRef)leptris_cdata_create(
                cc, cc ? strlen(cc) : 0,
                ((struct leptris_document*)ex->result)->pool);
            if (cn && ex->pending_parent)
                leptris_element_append_child_internal(
                    ex->pending_parent, (LeptrisNode*)cn);
            else if (cn)
                xslt_append_fragment_node(ex, cn);
            return 0;
        }
        if (nty == LEPTRIS_NODE_TYPE_TEXT) {
            out_append_text(ex, ex->pending_parent,
                            leptris_text_node_get_content(
                                (LeptrisNodeRef)node));
            return 0;
        }
    }
    if (!leptris_element_get_name(node)) return 0;
    LeptrisElement e = out_copy_elem(ex, ex->pending_parent, node);
    if (!e) return -1;
    /* §7.5: copying an element copies its namespace nodes too —
     * the in-scope declarations travel with the copy (bug-122/124:
     * xmlns:* and default declarations on identity copies). */
    {
        /* §7.5: in-scope namespaces (innermost binding per prefix). */
        char seen_pfx[32][96];
        size_t seen = 0;
        for (LeptrisElement anc = node; anc && seen < 32;
             anc = leptris_node_parent((LeptrisNodeRef)anc)) {
            for (struct leptris_namespace* ns =
                     leptris_elem_namespaces(anc);
                 ns; ns = ns->next) {
                const char* pf = ns->prefix ? ns->prefix : "";
                int dup = 0;
                for (size_t k = 0; k < seen; k++)
                    if (strcmp(seen_pfx[k], pf) == 0) { dup = 1; break; }
                if (dup) continue;
                if (seen < 32)
                    snprintf(seen_pfx[seen++], 96, "%s", pf);
                if (ns->uri &&
                    !result_ns_in_scope(e, pf, ns->uri))
                    leptris_element_add_namespace_definition(e, pf, ns->uri);
            }
        }
    }
    /* §7.5: xsl:copy copies the element and its namespace nodes,
     * NOT its attributes — they reach the result only through
     * apply-templates/@* (libxslt bug-32-: match="@p:*" drop
     * templates never fired because the copy re-added them). */
    xslt_apply_attr_sets(ex, in, e, node);
    LeptrisElement saved = ex->pending_parent;
    ex->pending_parent = e;
    int rc = xslt_exec_instrs(ex, in->child, node);
    ex->pending_parent = saved;
    return rc;
}

static int copy_node_deep(XsltExec* ex, LeptrisElement node,
                          LeptrisElement parent) {
    const char* name = leptris_element_get_name(node);
    if (!name) return -1;
    LeptrisElement e = out_copy_elem(ex, parent, node);
    if (!e) return -1;
    /* Namespace fidelity (§7.5 + Names Rec): copy the element's
     * in-scope DECLARATIONS verbatim — a copy of <foo xmlns="u">
     * keeps xmlns="u" — and when the copy lands under a parent with
     * a default namespace the element does not have, emit xmlns=""
     * so it stays no-namespace. */
    {
        int copied_default = 0;
        /* §7.5: the copy carries the IN-SCOPE namespaces (ancestor
         * declarations, innermost binding per prefix wins). */
        char seen_pfx[32][96];
        size_t seen = 0;
        for (LeptrisElement anc = node; anc && seen < 32;
             anc = leptris_node_parent((LeptrisNodeRef)anc)) {
            for (struct leptris_namespace* ns =
                     leptris_elem_namespaces(anc);
                 ns; ns = ns->next) {
                const char* pf = ns->prefix ? ns->prefix : "";
                int dup = 0;
                for (size_t k = 0; k < seen; k++)
                    if (strcmp(seen_pfx[k], pf) == 0) { dup = 1; break; }
                if (dup) continue;
                if (seen < 32)
                    snprintf(seen_pfx[seen++], 96, "%s", pf);
                if (ns->uri &&
                    !result_ns_in_scope(e, pf, ns->uri))
                    leptris_element_add_namespace_definition(
                        e, pf, ns->uri);
                if (!ns->prefix) copied_default = 1;
            }
        }
        if (!copied_default && parent) {
            const char* pdef = leptris_element_namespace_for_prefix(
                parent, NULL);   /* NULL = the default namespace */
            if (pdef && *pdef && !leptris_element_get_namespace_uri(node))
                leptris_element_add_namespace_definition(e, "", "");
        }
    }
    size_t na = leptris_element_attribute_count(node);
    for (size_t i = 0; i < na; i++) {
        const char* an = leptris_element_attribute_name_at(node, i);
        const char* av = leptris_element_attribute_value_at(node, i);
        if (an && av) leptris_element_set_attribute(e, an, av);
    }
    /* §11.3 verbatim deep copy: every child kind in document order —
     * text/CDATA/comment/PI/element — not the concatenated
     * string-value ahead of the child elements (that duplicated
     * mixed content and dropped the tail whitespace). */
    for (LeptrisNodeRef c = leptris_node_first_child(
             leptris_element_as_node(node));
         c; c = leptris_node_next_sibling(c)) {
        int ty = leptris_node_get_type(c);
        if (ty == LEPTRIS_NODE_TYPE_ELEMENT) {
            copy_node_deep(ex, (LeptrisElement)c, e);
        } else if (ty == LEPTRIS_NODE_TYPE_CDATA) {
            const char* t =
                leptris_text_get_content((LeptrisTextNode*)c);
            LeptrisNodeRef cc = (LeptrisNodeRef)leptris_cdata_create(
                t, t ? strlen(t) : 0,
                ((struct leptris_document*)ex->result)->pool);
            if (cc) leptris_element_append_child_internal(e, (LeptrisNode*)cc);
        } else if (ty == LEPTRIS_NODE_TYPE_COMMENT) {
            LeptrisNodeRef cm = leptris_comment_node_create(
                ex->result, leptris_comment_node_get_content(c));
            if (cm) leptris_element_append_child_internal(e, (LeptrisNode*)cm);
        } else if (ty == LEPTRIS_NODE_TYPE_PI) {
            LeptrisNodeRef pi = leptris_pi_node_create(
                ex->result, leptris_pi_node_get_target(c),
                leptris_pi_node_get_data(c));
            if (pi) leptris_element_append_child_internal(e, (LeptrisNode*)pi);
        } else if (ty == LEPTRIS_NODE_TYPE_TEXT) {
            const char* t = leptris_text_get_content((LeptrisTextNode*)c);
            if (t && t[0]) out_append_text(ex, e, t);
        }
    }
    return 0;
}

static struct leptris_xpath_result* xslt_capture_content(
    XsltExec* ex, const XsltInstr* child, LeptrisElement node);

/* 3.0 §18 xsl:map: build the value-level map (shared representation
 * through the functions_ext31 builder) from xsl:map-entry children. */
static struct leptris_xpath_result* eval_xsl_map(XsltExec* ex,
                                                 const XsltInstr* mi,
                                                 LeptrisElement node) {
    void* b = xpath_map_builder_new();
    if (!b) return NULL;
    for (const XsltInstr* c = mi->child; c; c = c->next) {
        if (c->kind != XSLT_INSTR_MAP_ENTRY) continue;
        char* k = NULL;
        if (c->test) {
            struct leptris_xpath_result* kr =
                xslt_eval(ex, c->test, node);
            if (kr) {
                k = leptris_xpath_result_string(kr);
                leptris_xpath_result_free(kr);
            }
        }
        char* v = NULL;
        if (c->select) {
            struct leptris_xpath_result* vr =
                xslt_eval(ex, c->select, node);
            if (vr) {
                v = leptris_xpath_result_string(vr);
                leptris_xpath_result_free(vr);
            }
        } else if (c->child) {
            v = xslt_capture_children_text(ex, c->child, node);
        }
        if (k) xpath_map_builder_add(b, k, v ? v : "");
        free(k);
        free(v);
    }
    return xpath_map_builder_finish(b);
}

static int op_variable(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    if (!in->name) return 0;
    struct leptris_xpath_result* v = NULL;
    if (in->select) {
        v = xslt_eval(ex, in->select, node);
    } else if (in->child && in->child->kind == XSLT_INSTR_MAP &&
               !in->child->next) {
        /* §18: a variable whose content is a lone xsl:map binds the
         * map VALUE (the Saxon as="map(*)" shape). */
        v = eval_xsl_map(ex, in->child, node);
    } else if (in->child) {
        v = xslt_capture_content(ex, in->child, node);
    }
    xslt_push_var(ex, in->name, v);
    return 0;
}

/* §11.1 RTF capture: build a with-param/variable CONTENT subtree
 * into a FRESH scratch document — never into the result tree, which
 * may carry a partially built element the instruction sits inside.
 * Returns the fragment as a nodeset (its document node) or, for
 * pure-text fragments, a string. */
static struct leptris_xpath_result* xslt_capture_content(
        XsltExec* ex, const XsltInstr* child, LeptrisElement node) {
    struct leptris_xpath_result* v = NULL;
    LeptrisDocument main_result = ex->result;
    LeptrisElement saved = ex->pending_parent;
    ex->result = leptris_document_create();
    ex->pending_parent = NULL;
    /* Fresh buffer per capture (the xslt_capture_children_text
     * discipline): nested captures save/restore the outer length —
     * resetting only the length left stale bytes that later appends
     * resurrected (bug-72's second variable picked up the first's
     * fragment text). */
    size_t o_len = ex->rtf_text_len;
    char* o_buf = ex->rtf_text;
    size_t o_cap = ex->rtf_text_cap;
    int o_capt = ex->rtf_capturing;
    ex->rtf_capturing = 1;
    ex->rtf_text = (char*)calloc(1, 1);
    ex->rtf_text_len = 0;
    ex->rtf_text_cap = 1;
    xslt_exec_instrs(ex, child, node);
    ex->rtf_capturing = 0;
    ex->pending_parent = saved;
    char* frag_text = ex->rtf_text;   /* owned by this block */
    size_t frag_len = ex->rtf_text_len;
    ex->rtf_text = o_buf;
    ex->rtf_text_len = o_len;
    ex->rtf_text_cap = o_cap;
    ex->rtf_capturing = o_capt;
    LeptrisDocument frag_doc = ex->result;
    ex->result = main_result;
    LeptrisElement rr = leptris_document_root(frag_doc);
    v = xpath_result_new(XPATH_RESULT_NODESET);
    if (v && rr) {
        v->value.nodeset_value = xpath_nodeset_new();
        if (v->value.nodeset_value) {
            /* §11.1: the value is the result tree FRAGMENT — bound
             * as its root (document) node. Relative paths
             * ($v/row/cell) and exsl:node-set() then see the
             * fragment as a tree. */
            LeptrisNodeRef dn = (LeptrisNodeRef)
                leptris_document_get_node(frag_doc);
            if (dn)
                xpath_nodeset_add(v->value.nodeset_value, dn);
        }
    } else if (v && !rr) {
        /* Pure-text RTF (bug-56): bind as the fragment's document
         * node — an EMPTY one too, which libxslt counts as 1 (the
         * node exists; its string-value is just ""). Non-empty
         * fragments get ONE text child spliced into the document's
         * chain so string($rtf) is the text. The earlier
         * string-binding regressed bug-72 via stale capture state;
         * the fresh-buffer capture discipline above is what makes
         * the nodeset binding safe now. */
        if (frag_text && frag_len) {
            LeptrisNodeRef tn =
                leptris_text_node_create(frag_doc, frag_text);
            if (tn) {
                struct leptris_document* fd =
                    (struct leptris_document*)frag_doc;
                if (!fd->doc_children_head) {
                    fd->doc_children_head = tn;
                    fd->doc_children_tail = tn;
                } else {
                    leptris_node_set_next_sibling(
                        (LeptrisNodeRef)fd->doc_children_tail, tn);
                    fd->doc_children_tail = tn;
                }
            }
        }
        /* Bind the document node — same shape as the element
         * fragment above. */
        v->value.nodeset_value = xpath_nodeset_new();
        if (v->value.nodeset_value) {
            LeptrisNodeRef dn = (LeptrisNodeRef)
                leptris_document_get_node(frag_doc);
            if (dn)
                xpath_nodeset_add(v->value.nodeset_value, dn);
        }
    }
    free(frag_text);
    /* The nodeset's node pointers live in the fragment document.
     * Move ownership into the exec's RTF chain so the nodes outlive
     * the variable's frame — for BOTH shapes: element-rooted
     * fragments and pure-text ones (their nodeset points at the
     * document node whose child is the text). */
    if (rr || (v && v->type == XPATH_RESULT_NODESET)) {
        struct xslt_rtf_entry* ent =
            (struct xslt_rtf_entry*)malloc(sizeof(*ent));
        if (ent) {
            ent->doc = frag_doc;
            ent->next = (struct xslt_rtf_entry*)ex->rtf_chain;
            ex->rtf_chain = ent;
        } else {
            leptris_document_free(frag_doc);
        }
    } else {
        leptris_document_free(frag_doc);
    }
    return v;
}

static int xslt_invoke_template(XsltExec* ex, const XsltTemplate* t,
                                LeptrisElement node,
                                const XsltInstr* with_params);

static int op_call_template(XsltExec* ex, const XsltInstr* in,
                            LeptrisElement node) {
    if (!in->name) return 0;
    const XsltTemplate* t = NULL;
    for (const XsltTemplate* ct = ex->sheet->templates; ct; ct = ct->next) {
        if (ct->name && strcmp(ct->name, in->name) == 0) t = ct;
    }
    if (!t) return 0;
    /* §11 scoping lives in xslt_invoke_template (shared with
     * apply-templates): globals + own locals, never caller locals. */
    return xslt_invoke_template(ex, t, node, in->child);
}



/* ---- §3.4 whitespace stripping on the SOURCE tree ----
 *
 * Runs once per transform, before globals. A whitespace-only text
 * node survives when: its parent's name is in the preserve list
 * and NOT in the strip list (strip wins on overlap), OR any
 * ancestor carries xml:space="preserve". With no declarations the
 * preserve set is empty and every ws-only text node is stripped —
 * the XSLT 1.0 default. */
static int ws_only(const char* t) {
    if (!t) return 1;
    for (const char* p = t; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') return 0;
    }
    return 1;
}

static int ancestor_xml_space_preserve(LeptrisElement e) {
    for (LeptrisElement a = e; a;
         a = leptris_node_parent((LeptrisNodeRef)a)) {
        const char* xs = leptris_element_attribute(a, "xml:space");
        if (!xs) xs = leptris_element_attribute(a, "space");
        if (xs && strcmp(xs, "preserve") == 0) return 1;
        if (xs && strcmp(xs, "default") == 0) return 0;
    }
    return 0;
}

/* §3.4 NameTest against a list entry: exact QName, "*", "p:*"
 * (any element in p's namespace), "p:name". The stylesheet root's
 * declarations resolve the prefixes. */
static int strip_entry_matches(char** list, const char* entry,
                               const char* local, const char* elem_uri,
                               LeptrisElement sheet_root) {
    (void)list;
    if (!entry || !local) return 0;
    const char* colon = strchr(entry, ':');
    if (!colon) {
        /* Unprefixed: "*" matches every element; a plain name
         * matches NO-NAMESPACE elements only (bug-82: a same-local
         * element in a namespace is not stripped). */
        if (strcmp(entry, "*") == 0) return 1;
        if (elem_uri && *elem_uri) return 0;
        return strcmp(entry, local) == 0;
    }
    if (!sheet_root) return 0;
    size_t plen = (size_t)(colon - entry);
    const char* rest = colon + 1;
    /* Resolve the prefix through the stylesheet root's declarations. */
    const char* uri = NULL;
    for (int i = 0;; i++) {
        const char* p = leptris_element_namespace_decl_prefix(
            sheet_root, i);
        const char* u = leptris_element_namespace_decl_uri(
            sheet_root, i);
        if (!p || !u) break;
        if (strlen(p) == plen && strncmp(p, entry, plen) == 0) {
            uri = u;
            break;
        }
    }
    if (!uri) return 0;
    if (!elem_uri || strcmp(uri, elem_uri) != 0) return 0;
    return strcmp(rest, "*") == 0 || strcmp(rest, local) == 0;
}

static void strip_source_whitespace(XsltExec* ex) {
    if (!ex || !ex->source || !ex->sheet->ws_strip) return;
    /* libxslt reference semantics: source whitespace is PRESERVED
     * by default; only names listed in xsl:strip-space (minus
     * preserve-space) strip, with xml:space="preserve" winning. */
    LeptrisElement sheet_root =
        ex->sheet_doc ? leptris_document_root(ex->sheet_doc) : NULL;
    for (LeptrisElement e = leptris_document_root(ex->source); e;
         e = xslt_next_doc_order(e)) {
        const char* name = leptris_element_name(e);
        if (!name) continue;
        const char* euri = leptris_element_get_namespace_uri(e);
        /* §3.4: the LAST matching declaration wins (bug-82:
         * preserve * then strip child strips child). */
        int strip = 0;
        for (size_t i = 0; i < ex->sheet->ws_rule_count; i++) {
            if (strip_entry_matches(ex->sheet->ws_rules,
                                    ex->sheet->ws_rules[i], name,
                                    euri, sheet_root)) {
                strip = !ex->sheet->ws_rule_preserve[i];
            }
        }
        if (!strip || ancestor_xml_space_preserve(e)) continue;
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_TEXT) continue;
            const char* t = leptris_text_get_content((LeptrisTextNode*)c);
            if (!ws_only(t)) continue;
            /* Empty in place — never unlink (compact-parse text
             * nodes link via int32 offsets that unlinking can
             * orphan for following siblings) and never call
             * leptris_text_set_content: get_content may have
             * materialized the content into POOL memory which
             * set_content would free(). Direct field writes with
             * borrowed=1 + pool=NULL leave the node readable ("")
             * and safe from every free path. */
            LeptrisTextNode* tn = (LeptrisTextNode*)c;
            tn->content = (char*)"";
            tn->content_len = 0;
            tn->borrowed = 1;
            tn->pool = NULL;
        }
    }
}

/* ---- §5.4/§5.5 template selection + invocation ----
 *
 * Selection resolves (import_rank, priority, declaration order):
 * lower rank wins; equal rank → higher priority; equal both → the
 * LAST declared (list append order). Per §5.5 the priority of a
 * union pattern is per-ALTERNATIVE — we take the max priority
 * among the alternatives that actually MATCH. min_rank excludes
 * the importing sheets' own rules for xsl:apply-imports (§5.6). */

/* Comment/PI nodes match only node()-kind tests; the element-cast
 * ladder is unsafe for them. Text-level alternative scan. */
static int pattern_matches_nodekind(const XsltPattern* p,
                                    int node_type,
                                    const char* node_name) {
    for (const XsltPattern* alt = p; alt; alt = alt->next) {
        const char* e = leptris_xpath_compiled_text(alt->expr);
        if (!e) continue;
        int is_comment = node_type == LEPTRIS_NODE_TYPE_COMMENT;
        int is_pi = node_type == LEPTRIS_NODE_TYPE_PI;
        int is_text = node_type == LEPTRIS_NODE_TYPE_TEXT ||
                      node_type == LEPTRIS_NODE_TYPE_CDATA;
        if (strstr(e, "node()")) return 1;
        if (is_comment && strstr(e, "comment()")) return 1;
        if (is_pi && strstr(e, "processing-instruction()")) return 1;
        if (is_text && strstr(e, "text()")) return 1;
        /* Attribute patterns: @* / @name (the only axis reaching
         * attribute nodes — a text-level scan suffices). */
        if (node_type == LEPTRIS_NODE_ATTRIBUTE) {
            const char* at = strchr(e, '@');
            if (at) {
                const char* nm = at + 1;
                if (nm[0] == '*' || !nm[0]) return 1;
                if (node_name && strncmp(nm, node_name,
                                         strlen(node_name)) == 0)
                    return 1;
            }
        }
    }
    return 0;
}

/* §5.2: every node kind matches through the FULL pattern ladder —
 * position predicates included. The old kind-only shortcut made
 * match="text()[2]" fire for every text node (libxslt bug-182). */
static const XsltTemplate* xslt_select_template(
        const XsltExec* ex, LeptrisElement node, const char* mode,
        int min_rank) {
    const XsltTemplate* best = NULL;
    double best_pri = 0;
    size_t best_order = 0, order = 0;
    for (const XsltTemplate* t = ex->sheet->templates; t; t = t->next, order++) {
        if (!t->matches) continue;
        if (t->import_rank < min_rank) continue;
        if ((mode && !t->mode) || (!mode && t->mode)) continue;
        if (mode && t->mode && strcmp(mode, t->mode) != 0) continue;
        /* ONE pass over the alternatives: each is matched exactly
         * once and the max priority rides the same walk (the old
         * shape matched the whole list first, then re-evaluated
         * every alternative for priority — 2x the pattern cost per
         * dispatch candidate). */
        double pri = 0; int have = 0;
        for (const XsltPattern* pa = t->matches; pa; pa = pa->next) {
            if (!xslt_pattern_matches(pa, node, ex->source, t->ns))
                continue;
            if (!have || pa->priority > pri) { pri = pa->priority; have = 1; }
        }
        if (!have) continue;
        int wins = 0;
        if (!best) wins = 1;
        else if (t->import_rank != best->import_rank)
            wins = t->import_rank < best->import_rank;
        else if (pri != best_pri) wins = pri > best_pri;
        else wins = order >= best_order;   /* tie: last declared */
        if (wins) { best = t; best_pri = pri; best_order = order; }
    }
    return best;
}

/* §11.6: evaluate xsl:param defaults for names the caller did not
 * bind (leading is_param children of the template body). Returns
 * the number of frames pushed. */
/* Deep result copy (frame-bindable): scalars duplicate; nodesets
 * copy the array (synthetic members are duplicated so the copy
 * outlives the source's storage). */
static struct leptris_xpath_result* tunnel_result_copy(
        const struct leptris_xpath_result* r) {
    if (!r) return NULL;
    struct leptris_xpath_result* c = xpath_result_new(r->type);
    if (!c) return NULL;
    switch (r->type) {
        case XPATH_RESULT_BOOLEAN:
            c->value.boolean_value = r->value.boolean_value; break;
        case XPATH_RESULT_NUMBER:
            c->value.number_value = r->value.number_value; break;
        case XPATH_RESULT_STRING:
            c->value.string_value = leptris_strdup(
                r->value.string_value ? r->value.string_value : "");
            if (!c->value.string_value) {
                leptris_xpath_result_free(c); return NULL; }
            break;
        case XPATH_RESULT_NODESET: {
            XPathNodeSet* src = r->value.nodeset_value;
            c->value.nodeset_value = xpath_nodeset_new();
            if (!c->value.nodeset_value) {
                leptris_xpath_result_free(c); return NULL; }
            if (src) {
                if (src->owns_synthetic_text)
                    c->value.nodeset_value->owns_synthetic_text = 1;
                c->value.nodeset_value->is_sequence = src->is_sequence;
                for (size_t i = 0; i < src->count; i++) {
                    void* n = src->nodes[i];
                    if (src->owns_synthetic_text && n &&
                        leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_TEXT) {
                        char* t = get_node_text(n);
                        size_t tl = t ? strlen(t) : 0;
                        XPathNodeSet* one = xpath_nodeset_new();
                        if (one) {
                            xpath_nodeset_add(one, n);
                            /* synth via evaluator export */
                            extern XPathTextNode* xpath_synth_text(
                                const char*, size_t);
                            XPathTextNode* tn =
                                xpath_synth_text(t ? t : "", tl);
                            free(t);
                            xpath_nodeset_free(one);
                            if (tn) xpath_nodeset_add(
                                c->value.nodeset_value, tn);
                        }
                    } else {
                        xpath_nodeset_add(c->value.nodeset_value, n);
                    }
                }
            }
            break;
        }
        default: break;
    }
    return c;
}

static int xslt_bind_param_defaults(XsltExec* ex, const XsltInstr* body,
                                    LeptrisElement node) {
    int pushed = 0;
    for (const XsltInstr* p = body; p; p = p->next) {
        if (p->kind != XSLT_INSTR_VARIABLE || !p->is_param) break;
        if (!p->name) continue;
        int bound = 0;
        for (XsltVar* v = ex->vars; v; v = v->prev) {
            if (v->name && strcmp(v->name, p->name) == 0) { bound = 1; break; }
        }
        if (bound) continue;
        if (p->tunnel) {
            /* §11.7: bind from the tunnel chain (innermost wins);
             * absent names fall through to the declared default. */
            int hit = 0;
            for (XsltVar* tv = ex->tunnel_vars; tv; tv = tv->prev) {
                if (tv->name && strcmp(tv->name, p->name) == 0) {
                    struct leptris_xpath_result* v =
                        tunnel_result_copy(tv->value);
                    if (v) {
                        xslt_push_var(ex, p->name, v);
                        pushed++;
                    }
                    hit = 1;
                    break;
                }
            }
            if (hit) continue;
        }
        struct leptris_xpath_result* v = NULL;
        if (p->num_count) {
            /* 4.0 @default expression takes precedence. */
            v = xslt_eval(ex, p->num_count, node);
        } else if (p->select) {
            v = xslt_eval(ex, p->select, node);
        } else if (p->child) {
            /* Content default: build the RTF like op_variable. */
            LeptrisElement saved = ex->pending_parent;
            ex->pending_parent = NULL;
            xslt_exec_instrs(ex, p->child, node);
            ex->pending_parent = saved;
            LeptrisElement rr = leptris_document_root(ex->result);
            v = xpath_result_new(XPATH_RESULT_NODESET);
            if (v && rr) {
                v->value.nodeset_value = xpath_nodeset_new();
                if (v->value.nodeset_value)
                    for (LeptrisElement c = rr; c;
                         c = leptris_element_next_sibling_any(c))
                        xpath_nodeset_add(v->value.nodeset_value,
                                          (LeptrisNodeRef)c);
            }
            if (rr) {
                struct xslt_rtf_entry* ent =
                    (struct xslt_rtf_entry*)malloc(sizeof(*ent));
                if (ent) {
                    ent->doc = ex->result;
                    ent->next = (struct xslt_rtf_entry*)ex->rtf_chain;
                    ex->rtf_chain = ent;
                } else {
                    leptris_document_free(ex->result);
                }
                ex->result = leptris_document_create();
            }
        }
        xslt_push_var(ex, p->name, v);
        pushed++;
    }
    return pushed;
}

/* Execute a template rule body: current-template tracking (§5.6
 * apply-imports), with-param scope, param defaults. */
static int xslt_invoke_template(XsltExec* ex, const XsltTemplate* t,
                                LeptrisElement node,
                                const XsltInstr* with_params) {
    const XsltTemplate* saved_t = ex->current_template;
    ex->current_template = t;
    /* §11: with-param expressions evaluate in the CALLER's scope;
     * the body runs on globals + its own params/locals — the
     * caller's local frames are not visible (bug-40-/42-). */
    struct leptris_xpath_result* vals[16];
    const char* names[16];
    size_t nv = 0;
    /* §11.6: a with-param binds only names the callee DECLARES as
     * xsl:param (leading is_param children) — others are ignored
     * and the callee sees the global (libxslt bugs 41-/43-). */
    for (const XsltInstr* wp = with_params; wp && nv < 16; wp = wp->next) {
        if (wp->kind != XSLT_INSTR_WITH_PARAM || !wp->name) continue;
        if (wp->tunnel) {
            /* §11.7: tunnel with-params ride the exec chain for the
             * whole subtree, independent of the callee's
             * declarations. */
            struct leptris_xpath_result* tv = NULL;
            if (wp->select) tv = xslt_eval(ex, wp->select, node);
            else if (wp->child) tv = xslt_capture_content(ex, wp->child, node);
            if (tv) {
                XsltVar* pv = (XsltVar*)calloc(1, sizeof(*pv));
                if (pv) {
                    pv->name = wp->name;
                    pv->value = tv;
                    pv->prev = ex->tunnel_vars;
                    ex->tunnel_vars = pv;
                } else {
                    leptris_xpath_result_free(tv);
                }
            }
            continue;
        }
        int declared = 0;
        for (const XsltInstr* b = t->body; b; b = b->next) {
            if (b->kind != XSLT_INSTR_VARIABLE || !b->is_param) break;
            if (b->name && strcmp(b->name, wp->name) == 0) {
                declared = 1;
                break;
            }
        }
        if (!declared) continue;
        vals[nv] = wp->select
                      ? xslt_eval(ex, wp->select, node)
                      : (wp->child
                             ? xslt_capture_content(ex, wp->child, node)
                             : NULL);
        names[nv] = wp->name;
        nv++;
    }
    XsltVar* caller = ex->vars;
    XsltVar* mark = ex->global_vars;
    ex->vars = mark;
    for (size_t i = 0; i < nv; i++)
        xslt_push_var(ex, names[i], vals[i]);
    xslt_bind_param_defaults(ex, t->body, node);
    int rc = xslt_exec_instrs(ex, t->body, node);
    ex->current_template = saved_t;
    xslt_pop_vars_to(ex, mark);
    ex->vars = caller;
    return rc;
}

/* §5.6 xsl:apply-imports: select the best matching rule from the
 * stylesheets IMPORTED by (i.e. lower precedence than) the one
 * containing the currently executing rule. */
static int op_apply_templates(XsltExec* ex, const XsltInstr* in,
                              LeptrisElement node);

/* §6.7 on-no-match of the unnamed mode, for a node NO template
 * matched. Returns -1 to defer to the legacy 1.0 built-in walk
 * (unspecified 1.0 sheets and text-only-copy — the existing
 * behavior); otherwise the walker rc. Only the unnamed mode has a
 * captured declaration (named modes are not parsed yet). */
static int builtin_no_match(XsltExec* ex, const char* mode,
                            LeptrisElement node) {
    if (mode && *mode) return -1;
    int v = ex->sheet->mode_on_no_match;
    if (!v) {
        if (ex->sheet->version_major >= 3) v = 3;   /* shallow-copy */
        else return -1;   /* 1.0: the text built-ins */
    }
    if (v == 6) return -1;                 /* text-only-copy: legacy */
    if (v == 5) return 0;                  /* deep-skip: nothing */
    if (v == 7) {                          /* fail: XTDE0500 */
        const char* nn =
            leptris_node_get_type((LeptrisNodeRef)node) ==
                    LEPTRIS_NODE_TYPE_ELEMENT
                ? leptris_element_get_name(node) : "node";
        ex->eval_error = 1;
        snprintf(ex->error, sizeof(ex->error),
                 "XTDE0500: no template rule matches %s in the "
                 "unnamed mode (on-no-match=fail)",
                 nn ? nn : "node");
        return 1;
    }
    if (leptris_node_get_type((LeptrisNodeRef)node) !=
        LEPTRIS_NODE_TYPE_ELEMENT)
        return -1;
    if (v == 2) {                          /* deep-copy: verbatim */
        copy_node_deep(ex, node, ex->pending_parent);
        return 0;
    }
    /* shallow-copy: copy the element, dispatch attributes (they
     * are NOT copied raw — a matching rule runs), then children
     * via the standard no-select walk. shallow-skip: no copy,
     * dispatch attributes and ELEMENT children only (text skipped
     * — Saxon ground truth). */
    LeptrisElement e = NULL;
    LeptrisElement saved_parent = NULL;
    int rc = 0;
    if (v == 3) {
        const char* nn = leptris_element_get_name(node);
        saved_parent = ex->pending_parent;
        e = nn ? out_append_elem(ex, ex->pending_parent, nn, NULL) : NULL;
        if (!e) { return 0; }
        ex->pending_parent = e;
    }
    if (!e) saved_parent = ex->pending_parent;
    /* Attributes dispatch through the XPath attribute AXIS — its
     * synthetic nodes are what pattern identity (owner, name) and
     * the attribute templates operate on; the element's raw
     * attribute records do not share that layout. */
    {
        LeptrisXPathCompiled at = leptris_xpath_compile("@*");
        if (at) {
            struct leptris_xpath_result* ar = xslt_eval(ex, at, node);
            size_t an = ar ? leptris_xpath_result_count(ar) : 0;
            for (size_t ai = 0; ai < an && rc == 0; ai++) {
                LeptrisElement a = (LeptrisElement)
                    leptris_xpath_result_get_node(ar, ai);
                const XsltTemplate* best =
                    xslt_select_template(ex, a, mode, 0);
                if (best)
                    rc = xslt_invoke_template(ex, best, a, NULL);
            }
            if (ar) leptris_xpath_result_free(ar);
            leptris_xpath_compiled_free(at);
        }
    }
    if (rc) return rc;
    if (v == 4) {
        /* shallow-skip: element children, recursive skip. */
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(node));
             c && rc == 0; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            const XsltTemplate* best =
                xslt_select_template(ex, (LeptrisElement)c, mode, 0);
            if (best) {
                rc = xslt_invoke_template(ex, best, (LeptrisElement)c, NULL);
            } else {
                int br = builtin_no_match(ex, mode, (LeptrisElement)c);
                if (br >= 0) rc = br;
                else
                    rc = op_apply_templates(
                        ex,
                        &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES,
                                      .name = mode },
                        (LeptrisElement)c);
            }
        }
        return rc;
    }
    /* shallow-copy: children through the standard walk under e. */
    rc = op_apply_templates(
        ex,
        &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES, .name = mode },
        node);
    ex->pending_parent = saved_parent;
    return rc;
}

static int op_apply_imports(XsltExec* ex, const XsltInstr* in,
                            LeptrisElement node) {
    (void)in;
    int min_rank = ex->current_template
                       ? ex->current_template->import_rank + 1 : 1;
    const XsltTemplate* t = xslt_select_template(
        ex, node, ex->current_template ? ex->current_template->mode : NULL,
        min_rank);
    if (t) return xslt_invoke_template(ex, t, node, NULL);
    /* No imported candidate: libxslt applies the BUILT-IN rule for
     * the node (the element rule re-enters selection for the
     * children — bug-193's <result> gets "passed"). */
    return op_apply_templates(
        ex,
        &(XsltInstr){ .kind = XSLT_INSTR_APPLY_IMPORTS,
                      .name = ex->current_template
                          ? ex->current_template->mode : NULL },
        node);
}

/* §15 fallback for unknown xsl: instructions (forward-compatible
 * containers): execute the xsl:fallback children, skip the rest. */
static int op_unknown_xsl(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement node) {
    (void)node;
    if (!in->child) return 0;
    return xslt_exec_instrs(ex, in->child, node);
}

static int op_apply_templates(XsltExec* ex, const XsltInstr* in,
                              LeptrisElement node) {
    int rc = 0;
    struct leptris_xpath_result* r = in->select
        ? xslt_eval(ex, in->select, node)
        : NULL;
    size_t n = 0;
    LeptrisElement* items = NULL;
    if (r) {
        n = leptris_xpath_result_count(r);
        if (n) {
            items = (LeptrisElement*)calloc(n, sizeof(LeptrisElement));
            if (items) {
                /* Node-typed accessor: selections may carry text/
                 * comment/PI nodes (identity transforms) — the
                 * element-typed result_get nulls them. */
                for (size_t i = 0; i < n; i++)
                    items[i] = (LeptrisElement)
                        leptris_xpath_result_get_node(r, i);
            }
        }
        /* NOT freed here: synthetic attr/namespace items are
         * result-owned; freed after the loop below. */
    } else {
        /* No-select apply-templates ON a text item (built-in TEXT
         * rule via the items loop, §5.8): copy the text itself —
         * the child walk below would enumerate a text node's
         * (nonexistent) children and drop it (bug-161). */
        int sel_ty = node
            ? leptris_node_get_type((LeptrisNodeRef)node) : 0;
        if (sel_ty == LEPTRIS_NODE_TYPE_TEXT ||
            sel_ty == LEPTRIS_NODE_TYPE_CDATA) {
            const char* t =
                leptris_text_get_content((LeptrisTextNode*)node);
            if (ex->pending_parent) {
                out_append_text(ex, ex->pending_parent, t ? t : "");
            } else if (t) {
                char* v = escape_fragment_text(
                    t, ex->sheet->out_method_text);
                out_append_text(ex, NULL, v);
                if (v != t) free(v);
            }
            if (r) leptris_xpath_result_free(r);
            return 0;
        }
        /* Document-node context (§5.4): the children ARE the root
         * element (this engine's document model — top-level
         * comments/PIs live outside the XPath tree), so template
         * selection runs FOR it — not an enumeration of its
         * children. No match → the built-in element rule. */
        if (node &&
            leptris_node_get_type((LeptrisNodeRef)node) ==
                LEPTRIS_NODE_TYPE_DOCUMENT) {
            /* The document node carries its OWN document — foreign
             * docs (document(), RTFs) must not resolve through
             * ex->source (that looped document-inclusion forever). */
            struct leptris_document* nd =
                ((LeptrisDocumentNode*)node)->doc;
            LeptrisElement doc_root =
                (LeptrisElement)(nd ? nd->new_dom_root : NULL);
            if (!doc_root && nd) doc_root = nd->root;
            if (!doc_root) return 0;
            /* Document children in order: the document child chain
             * (prolog top comments/PIs), then the root element — of
             * THIS node's document (nd), never ex->source: a foreign
             * doc node (document(), RTF) walking the source's chain
             * applied templates to the SOURCE's nodes (bug-65). */
            int rc = 0;
            LeptrisNodeRef rootn = leptris_element_as_node(doc_root);
            for (LeptrisNodeRef n = (LeptrisNodeRef)nd->doc_children_head;
                 n && n != rootn && rc == 0;
                 n = leptris_node_next_sibling(n)) {
                const XsltTemplate* best =
                    xslt_select_template(ex, (LeptrisElement)n,
                                        in->name, 0);
                if (best) {
                    rc = xslt_invoke_template(ex, best,
                                              (LeptrisElement)n, in->child);
                }
            }
            if (rc) return rc;
            int root_rc;
            {
                const XsltTemplate* best =
                    xslt_select_template(ex, doc_root, in->name, 0);
                root_rc = best
                    ? xslt_invoke_template(ex, best, doc_root, in->child)
                    : (builtin_no_match(ex, in->name, doc_root) >= 0
                           ? 0
                           : op_apply_templates(
                                 ex,
                                 &(XsltInstr){
                                     .kind = XSLT_INSTR_APPLY_TEMPLATES,
                                     .name = in->name },
                                 doc_root));
            }
            if (root_rc) return root_rc;
            /* The after-root chain (top comments/PIs following the
             * root element) completes document order. */
            for (LeptrisNodeRef n = leptris_node_next_sibling(rootn);
                 n && rc == 0; n = leptris_node_next_sibling(n)) {
                const XsltTemplate* best =
                    xslt_select_template(ex, (LeptrisElement)n,
                                        in->name, 0);
                if (best)
                    rc = xslt_invoke_template(ex, best,
                                              (LeptrisElement)n, in->child);
            }
            return rc;
        }
        /* Default (§5.4): child nodes in DOCUMENT ORDER — text
         * copies inline (built-in text rule, §5.8), elements select
         * and invoke their template AS ENCOUNTERED so output order
         * matches the source (the old batch-then-loop broke
         * interleaving). The node-list being processed here is the
         * child axis — position() counts every child, all kinds.
         * A zero-length text child is a stripped §3.4 node (parse
         * never creates empty text nodes) — it is gone. */
        size_t saved_cpos = ex->current_pos;
        size_t saved_csize = ex->current_size;
        size_t cpos = 0;
        /* last() needs the SAME node-list position() counts — every
         * non-stripped child, all kinds (issue 628). */
        for (LeptrisNodeRef pc =
                 leptris_node_first_child(leptris_element_as_node(node));
             pc; pc = leptris_node_next_sibling(pc)) {
            if (pc->type == LEPTRIS_NODE_TYPE_TEXT &&
                leptris_text_node_get_content(pc)[0] == '\0')
                continue;
            cpos++;
        }
        ex->current_size = cpos;
        cpos = 0;
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(node));
             c && rc == 0; c = leptris_node_next_sibling(c)) {
            int ty = leptris_node_get_type(c);
            if (ty == LEPTRIS_NODE_TYPE_TEXT &&
                leptris_text_node_get_content(c)[0] == '\0')
                continue;
            ex->current_pos = ++cpos;
            if (ty == LEPTRIS_NODE_TYPE_TEXT ||
                ty == LEPTRIS_NODE_TYPE_CDATA) {
                /* §5.4: text children route through template
                 * selection like every other kind — a user
                 * match="text()" overrides the built-in copy rule
                 * (bug-171: match="text()"/ was ignored here). */
                LeptrisElement item = (LeptrisElement)c;
                const XsltTemplate* best =
                    xslt_select_template(ex, item, in->name, 0);
                if (best) {
                    rc = xslt_invoke_template(ex, best, item,
                                              in->child);
                    continue;
                }
                const char* t = leptris_text_get_content(
                    (LeptrisTextNode*)c);
                if (ex->pending_parent) {
                    out_append_text(ex, ex->pending_parent, t ? t : "");
                } else if (t) {
                    char* v = escape_fragment_text(
                        t, ex->sheet->out_method_text);
                    out_append_text(ex, NULL, v);
                    if (v != t) free(v);
                }
                continue;
            }
            if (ty == LEPTRIS_NODE_TYPE_COMMENT ||
                ty == LEPTRIS_NODE_TYPE_PI) {
                /* Comments/PIs go through template selection too
                 * (§5.4); the built-in rule for them is a no-op, but
                 * match='node()'/comment()/processing-instruction()
                 * templates MUST see them (the identity transform). */
                LeptrisElement item = (LeptrisElement)c;
                const XsltTemplate* best =
                    xslt_select_template(ex, item, in->name, 0);
                if (best)
                    rc = xslt_invoke_template(ex, best, item, in->child);
                continue;
            }
            if (ty != LEPTRIS_NODE_TYPE_ELEMENT) continue;
            {
                LeptrisElement item = (LeptrisElement)c;
                const XsltTemplate* best =
                    xslt_select_template(ex, item, in->name, 0);
                if (best) {
                    rc = xslt_invoke_template(ex, best, item, in->child);
                } else {
                    int br = builtin_no_match(ex, in->name, item);
                    if (br >= 0)
                        rc = br;
                    else
                        rc = op_apply_templates(
                            ex,
                            &(XsltInstr){ .kind =
                                              XSLT_INSTR_APPLY_TEMPLATES,
                                          .name = in->name },
                            item);
                }
            }
        }
        ex->current_pos = saved_cpos;
        ex->current_size = saved_csize;
    }
    if (!items) {
        /* Early exit must not strand the selection (Linux LSan —
         * attr/namespace nodesets are result-owned either way). */
        if (r) leptris_xpath_result_free(r);
        return n ? -1 : 0;
    }

    /* §10: xsl:sort children order the selection. */
    if (in->sorts) xslt_sort_items(ex, in, items, n);

    /* with-params bind per invocation (xslt_invoke_template). */
    const char* mode = in->name;   /* mode attr parsed into ->name */
    size_t saved_pos = ex->current_pos;
    size_t saved_size2 = ex->current_size;
    ex->current_size = n;
    for (size_t i = 0; i < n && rc == 0; i++) {
        ex->current_pos = i + 1;
        const XsltTemplate* best =
            xslt_select_template(ex, items[i], mode, 0);
        if (best) {
            rc = xslt_invoke_template(ex, best, items[i], in->child);
        } else {
            /* Unmatched node: tunnel with-params of THIS apply still
             * flow (§11.7 — processing continues down the tree). */
            for (const XsltInstr* wp = in->child; wp; wp = wp->next) {
                if (wp->kind != XSLT_INSTR_WITH_PARAM || !wp->name ||
                    !wp->tunnel)
                    continue;
                struct leptris_xpath_result* tv =
                    wp->select ? xslt_eval(ex, wp->select, items[i]) : NULL;
                XsltVar* pv = (XsltVar*)calloc(1, sizeof(*pv));
                if (pv) {
                    pv->name = wp->name;
                    pv->value = tv;
                    pv->prev = ex->tunnel_vars;
                    ex->tunnel_vars = pv;
                } else if (tv) {
                    leptris_xpath_result_free(tv);
                }
            }
            /* Built-in template rules (§5.8/§6.7): on-no-match
             * variants first, then the 1.0 walk. */
            int br = builtin_no_match(ex, mode, items[i]);
            if (br >= 0)
                rc = br;
            else
                rc = op_apply_templates(
                    ex,
                    &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES,
                                  .name = in->name },
                    items[i]);
        }
    }
    ex->current_pos = saved_pos;
    ex->current_size = saved_size2;
    free(items);
    if (r) leptris_xpath_result_free(r);
    return rc;
}

static int op_element(XsltExec* ex, const XsltInstr* in,
                      LeptrisElement node) {
    char* name = NULL;
    if (in->select) {
        struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
        if (r) name = leptris_xpath_result_string(r);
        if (r) leptris_xpath_result_free(r);
    } else if (in->name) {
        /* §7.1.1: the name is an AVT — {local-name()},
         * {concat('a','b')}, prefix:{...}, {$var} (bug-117/179/35-). */
        name = strchr(in->name, '{')
                   ? eval_avt(ex, in->name, node)
                   : leptris_strdup(in->name);
    }
    if (!name) return 0;
    /* §7.1.1 namespace attribute: an AVT; non-empty binds the
     * element (or its prefix) to the URI via an xmlns declaration. */
    char* ns_avt = in->ns_uri && in->ns_uri[0]
                       ? eval_avt(ex, in->ns_uri, node) : NULL;
    if (ns_avt && !ns_avt[0]) { free(ns_avt); ns_avt = NULL; }
    char* pfx_ns = NULL;
    if (!ns_avt) {
        /* A literal-prefixed name binds through the instruction's
         * ns context (7.1.1) — the declaration follows the element,
         * not the whole in-scope set (bug-92 vs bug-117/179). */
        const char* nc = strchr(name, ':');
        if (nc && nc != name && in->ns) {
            const char* u = leptris_xpath_ns_lookup(
                (const struct leptris_xpath_ns_map*)in->ns, name,
                (size_t)(nc - name));
            if (u) pfx_ns = leptris_strdup(u);
        }
    }
    LeptrisElement e = out_append_elem(ex, ex->pending_parent, name,
                                       ns_avt ? ns_avt : pfx_ns);
    if (pfx_ns) {
        const char* en2 = leptris_element_name(e);
        const char* c2 = en2 ? strchr(en2, ':') : NULL;
        if (c2) {
            char p2[80];
            size_t pl2 = (size_t)(c2 - en2);
            snprintf(p2, sizeof(p2), "%.*s", (int)pl2, en2);
            if (!result_ns_in_scope(e, p2, pfx_ns))
                leptris_element_add_namespace_definition(e, p2, pfx_ns);
        }
        free(pfx_ns);
    }
    free(name);
    if (!e) { free(ns_avt); return -1; }
    if (ns_avt) {
        const char* en = leptris_element_name(e);
        const char* colon = en ? strchr(en, ':') : NULL;
        char pfx[80];
        const char* dpfx = "";
        if (colon) {
            size_t pl = (size_t)(colon - en);
            snprintf(pfx, sizeof(pfx), "%.*s", (int)pl, en);
            dpfx = pfx;
        }
        /* Skip the declaration when the result ancestors already
         * bind the prefix (or default) to the same URI — libxslt
         * relies on inheritance (bug-179). */
        if (!result_ns_in_scope(e, dpfx, ns_avt))
            leptris_element_add_namespace_definition(e, dpfx, ns_avt);
        free(ns_avt);
    }
    /* §7.1.1: xsl:element does NOT copy the instruction's
     * in-scope declarations — only literal result elements do
     * (libxslt bug-92). The element's own namespace (name prefix
     * + namespace attribute) is declared above. */
    xslt_apply_attr_sets(ex, in, e, node);
    LeptrisElement saved = ex->pending_parent;
    ex->pending_parent = e;
    int rc = xslt_exec_instrs(ex, in->child, node);
    ex->pending_parent = saved;
    return rc;
}

static int op_attribute(XsltExec* ex, const XsltInstr* in,
                        LeptrisElement node) {
    if (!in->name || !ex->pending_parent) return 0;
    /* §7.1.3: the name is an ATTRIBUTE VALUE TEMPLATE; the content
     * is the string-value of ALL child instructions (the shared
     * capture — value-of, xsl:number, xsl:text all contribute). */
    char* nm = strchr(in->name, '{')
                   ? eval_avt(ex, in->name, node)
                   : leptris_strdup(in->name);
    /* §7.1.3 namespace attribute: an AVT naming the attribute's
     * namespace. The XML namespace maps to the reserved xml:
     * prefix (bug-177); any OTHER namespace on an unprefixed name
     * mints a generated ns_N prefix — attributes never take the
     * default namespace (libxslt bug-99). */
    char* ans_uri = in->ns_uri && in->ns_uri[0]
                        ? eval_avt(ex, in->ns_uri, node) : NULL;
    if (ans_uri && !ans_uri[0]) { free(ans_uri); ans_uri = NULL; }
    char* final_nm = nm;
    if (nm && ans_uri) {
        const char* nc = strchr(nm, ':');
        if (!nc) {
            if (strcmp(ans_uri,
                       "http://www.w3.org/XML/1998/namespace") == 0) {
                size_t l = strlen(nm);
                final_nm = (char*)malloc(l + 5);
                if (final_nm) snprintf(final_nm, l + 5, "xml:%s", nm);
            } else {
                int n = 1;
                char pbuf[32];
                for (;;) {
                    snprintf(pbuf, sizeof(pbuf), "ns_%d", n);
                    const char* cur =
                        leptris_element_namespace_for_prefix(
                            ex->pending_parent, pbuf);
                    if (!cur) break;
                    if (++n > 999) break;
                }
                size_t l = strlen(nm);
                final_nm = (char*)malloc(l + strlen(pbuf) + 2);
                if (final_nm)
                    snprintf(final_nm, l + strlen(pbuf) + 2,
                             "%s:%s", pbuf, nm);
                if (final_nm)
                    leptris_element_add_namespace_definition(
                        ex->pending_parent, pbuf, ans_uri);
            }
        } else {
            /* Prefixed name + namespace attribute: the declaration
             * follows the given URI (rebinding the prefix). */
            size_t pl = (size_t)(nc - nm);
            char pbuf[80];
            if (pl < sizeof(pbuf)) {
                memcpy(pbuf, nm, pl);
                pbuf[pl] = 0;
                leptris_element_add_namespace_definition(
                    ex->pending_parent, pbuf, ans_uri);
            }
        }
    }
    char* acc = xslt_capture_children_text(ex, in->child, node);
    if (final_nm && acc)
        leptris_element_set_attribute(ex->pending_parent, final_nm,
                                      acc);
    if (final_nm != nm) free(final_nm);
    free(ans_uri);
    free(nm);
    free(acc);
    return 0;
}

/* Run instruction children capturing their string-value (the
 * xsl:comment / xsl:processing-instruction content model, §7.4).
 * Returns a malloc'd accumulation; caller frees. The caller's
 * rtf-capture state and pending parent are preserved. */
static char* xslt_capture_children_text(XsltExec* ex,
                                        const XsltInstr* child,
                                        LeptrisElement node) {
    size_t saved_len = ex->rtf_text_len;
    char* saved_buf = ex->rtf_text;
    size_t saved_cap = ex->rtf_text_cap;
    int saved_capturing = ex->rtf_capturing;
    ex->rtf_capturing = 1;
    ex->rtf_text = (char*)calloc(1, 1);
    ex->rtf_text_len = 0;
    ex->rtf_text_cap = 1;   /* MUST match the fresh 1-byte buffer —
                             * a stale outer cap skips the grow and
                             * overflows (ASAN, nested captures). */
    LeptrisElement saved_pp = ex->pending_parent;
    ex->pending_parent = NULL;   /* children emit text, not nodes */
    xslt_exec_instrs(ex, child, node);
    ex->pending_parent = saved_pp;
    char* acc = ex->rtf_text ? ex->rtf_text : (char*)calloc(1, 1);
    ex->rtf_text = saved_buf;
    ex->rtf_text_len = saved_len;
    ex->rtf_text_cap = saved_cap;
    ex->rtf_capturing = saved_capturing;
    return acc;
}

static int op_comment(XsltExec* ex, const XsltInstr* in,
                      LeptrisElement node) {
    /* Literal comment (template content): in->text carries the
     * content verbatim — no instruction children. */
    if (!in->child && in->text) {
        LeptrisNodeRef cm = leptris_comment_node_create(ex->result,
                                                        in->text);
        if (cm && ex->pending_parent)
            leptris_element_append_child(ex->pending_parent,
                                         (LeptrisElement)cm);
        else if (cm)
            xslt_append_fragment_node(ex, cm);
        return 0;
    }
    char* acc = xslt_capture_children_text(ex, in->child, node);
    LeptrisNodeRef cm = leptris_comment_node_create(ex->result, acc);
    if (cm && ex->pending_parent)
        leptris_element_append_child_internal(
            ex->pending_parent, (LeptrisNode*)cm);
    else if (cm)
        xslt_append_fragment_node(ex, cm);
    free(acc);
    return 0;
}

static int op_pi(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
    /* §7.3: the name is an attribute value template. */
    char* nm = in->name && strchr(in->name, '{')
                   ? eval_avt(ex, in->name, node)
                   : (in->name ? leptris_strdup(in->name) : NULL);
    /* Literal PI (template content). */
    if (!in->child && nm) {
        LeptrisNodeRef pi = leptris_pi_node_create(
            ex->result, nm, in->text ? in->text : "");
        if (pi && ex->pending_parent)
            leptris_element_append_child_internal(
                ex->pending_parent, (LeptrisNode*)pi);
        else if (pi)
            xslt_append_fragment_node(ex, pi);
        free(nm);
        return 0;
    }
    char* acc = xslt_capture_children_text(ex, in->child, node);
    LeptrisNodeRef pi = nm ? leptris_pi_node_create(ex->result, nm, acc)
                           : NULL;
    if (pi && ex->pending_parent)
        leptris_element_append_child_internal(
            ex->pending_parent, (LeptrisNode*)pi);
    else if (pi)
        xslt_append_fragment_node(ex, pi);
    free(acc);
    free(nm);
    return 0;
}

static int op_message(XsltExec* ex, const XsltInstr* in,
                      LeptrisElement node) {
    char* acc = (char*)calloc(1, 1);
    size_t len = 0;
    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind == XSLT_INSTR_TEXT && c->text) {
            size_t tl = strlen(c->text);
            acc = (char*)realloc(acc, len + tl + 1);
            memcpy(acc + len, c->text, tl);
            len += tl;
            acc[len] = 0;
        }
    }
    fprintf(stderr, "xslt:message: %s\n", acc);
    free(acc);
    if (in->terminate) ex->terminated = 1;
    return 0;
}

/* Format a positive integer per XSLT number formats (1, 01, a, A,
 * i, I with format tokens). */
static void format_number_token(unsigned long v, char spec,
                                char* out, size_t outsz) {
    static const char* lower = "abcdefghijklmnopqrstuvwxyz";
    switch (spec) {
        case 'a': {
            if (v == 0) { snprintf(out, outsz, "a"); break; }
            size_t o = 0;
            unsigned long x = v;
            char tmp[16];
            int ti = 0;
            while (x && ti < 15) {
                tmp[ti++] = lower[(x - 1) % 26];
                x = (x - 1) / 26;
            }
            while (ti-- > 0 && o < outsz - 1) out[o++] = tmp[ti];
            out[o] = 0;
            break;
        }
        case 'A': {
            format_number_token(v, 'a', out, outsz);
            for (char* p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
            break;
        }
        case 'i':
        case 'I': {
            if (v > 3999) {
                /* Beyond roman capacity libxslt prints the digits. */
                snprintf(out, outsz, "%lu", v);
                break;
            }
            /* Subtractive algorithm (the 50-entry table only covered
             * the first roman numerals). */
            static const unsigned val[] = {1000, 900, 500, 400, 100, 90,
                                           50, 40, 10, 9, 5, 4, 1};
            static const char* sym[] = {"m", "cm", "d", "cd", "c", "xc",
                                        "l", "xl", "x", "ix", "v", "iv", "i"};
            size_t o = 0;
            unsigned long x = v;
            for (int k = 0; k < 13 && x; k++) {
                while (x >= val[k] && o + 3 < outsz) {
                    size_t sl = strlen(sym[k]);
                    memcpy(out + o, sym[k], sl);
                    o += sl;
                    x -= val[k];
                }
            }
            out[o] = 0;
            if (spec == 'I')
                for (char* p = out; *p; p++)
                    *p = (char)toupper((unsigned char)*p);
            break;
        }
        default:
            snprintf(out, outsz, "%lu", v);
            break;
    }
}

/* ---- xsl:number counting (§7.7) ----
 * Correctness first: the any-level walk is O(document); the pattern
 * fast paths from template matching (expr_name) do not apply here
 * because count/from are arbitrary patterns. */

LeptrisElement xslt_next_doc_order(LeptrisElement e) {
    LeptrisElement c = leptris_element_first_child_any(e);
    if (c) return c;
    while (e) {
        LeptrisElement n = leptris_element_next_sibling_any(e);
        if (n) return n;
        e = leptris_node_parent((LeptrisNodeRef)e);
    }
    return NULL;
}

/* Walk children for parent-relative ladder — the count_matches /
 * from_matches helpers build a stack-side XsltPattern and reuse the
 * same matcher the template engine uses. */

/* Eval hook for count/from patterns: xslt_eval routes through the
 * exec's variable frame and ns map, so patterns like
 * count="node()[@type = $type]" resolve their variables (bug-214). */
static struct leptris_xpath_result* pattern_eval_with_vars(
        void* ud, LeptrisXPathCompiled c, LeptrisDocument doc,
        LeptrisElement node) {
    (void)doc;
    return xslt_eval((XsltExec*)ud, c, node);
}

/* §7.7 default count: same node KIND, and where the kind carries an
 * expanded-name, the same name (element qname, attribute name,
 * namespace prefix). */
static int count_matches_default(LeptrisNodeRef cand, LeptrisNodeRef ref) {
    if (leptris_node_get_type(cand) != leptris_node_get_type(ref))
        return 0;
    switch (leptris_node_get_type(ref)) {
        case LEPTRIS_NODE_TYPE_ELEMENT: {
            const char* a = leptris_element_get_name((LeptrisElement)cand);
            const char* b = leptris_element_get_name((LeptrisElement)ref);
            return a && b && strcmp(a, b) == 0;
        }
        case LEPTRIS_NODE_ATTRIBUTE: {
            const char* a = ((LeptrisAttributeNode*)cand)->name;
            const char* b = ((LeptrisAttributeNode*)ref)->name;
            return a && b && strcmp(a, b) == 0;
        }
        case LEPTRIS_NODE_NAMESPACE: {
            const char* a = ((LeptrisNamespaceNode*)cand)->prefix;
            const char* b = ((LeptrisNamespaceNode*)ref)->prefix;
            if (!a || !b) return !a && !b;
            return strcmp(a, b) == 0;
        }
        default:
            return 1;   /* text/comment: kind only (§7.7) */
    }
}

/* Count predicate: the count pattern, or (default) the same kind +
 * expanded name as the node being numbered. */
static int count_matches(const XsltInstr* in, LeptrisElement cand,
                         LeptrisElement ref, LeptrisDocument doc,
                         XsltExec* ex) {
    if (in->num_count) {
        XsltPattern pat;
        memset(&pat, 0, sizeof(pat));
        pat.expr = in->num_count;
        /* Pattern prefixes AND $vars resolve in the declaring
         * instruction's context. */
        LeptrisXPathNsSet saved = ex->current_ns;
        if (in->ns) ex->current_ns = (LeptrisXPathNsSet)in->ns;
        int m = xslt_pattern_matches_ex(&pat, cand, doc,
                                        (LeptrisXPathNsSet)in->ns,
                                        pattern_eval_with_vars, ex);
        ex->current_ns = saved;
        return m;
    }
    return count_matches_default((LeptrisNodeRef)cand,
                                 (LeptrisNodeRef)ref);
}

static int from_matches(const XsltInstr* in, LeptrisElement cand,
                        LeptrisDocument doc, XsltExec* ex) {
    if (!in->num_from) return 0;
    XsltPattern pat;
    memset(&pat, 0, sizeof(pat));
    pat.expr = in->num_from;
    LeptrisXPathNsSet saved = ex->current_ns;
    if (in->ns) ex->current_ns = (LeptrisXPathNsSet)in->ns;
    int m = xslt_pattern_matches_ex(&pat, cand, doc,
                                    (LeptrisXPathNsSet)in->ns,
                                    pattern_eval_with_vars, ex);
    ex->current_ns = saved;
    return m;
}

/* Position of `target` among preceding siblings matching count. */
static unsigned long sibling_number(const XsltInstr* in,
                                    LeptrisElement target,
                                    LeptrisDocument doc, XsltExec* ex) {
    int tty = leptris_node_get_type((LeptrisNodeRef)target);
    if (tty == LEPTRIS_NODE_ATTRIBUTE) {
        /* Attributes number within the OWNER's attribute list. The
         * list holds each name once, so the default count (same
         * kind + name) numbers by list position. */
        LeptrisAttributeNode* a = (LeptrisAttributeNode*)target;
        LeptrisElement owner = (LeptrisElement)a->owner;
        size_t na = owner ? leptris_element_attribute_count(owner) : 0;
        for (size_t i = 0; i < na; i++) {
            const char* cn =
                leptris_element_attribute_name_at(owner, i);
            if (cn && a->name && strcmp(cn, a->name) == 0)
                return (unsigned long)(i + 1);
        }
        return 1;
    }
    if (tty == LEPTRIS_NODE_NAMESPACE) {
        /* Namespace nodes have no sibling chain — the in-scope list
         * dedups prefixes, so the default count (same kind +
         * prefix) always numbers 1, and XPath 1.0 patterns cannot
         * name namespace nodes for an explicit count. */
        return 1;
    }
    unsigned long pos = 1;
    LeptrisElement parent = leptris_node_parent((LeptrisNodeRef)target);
    for (LeptrisElement c = leptris_element_first_child_any(parent); c;
         c = leptris_element_next_sibling_any(c)) {
        if (c == target) return pos;
        if (count_matches(in, c, target, doc, ex)) pos++;
    }
    return pos;
}

static unsigned long any_number(const XsltInstr* in, LeptrisElement node,
                                LeptrisDocument doc, XsltExec* ex);

/* any: how many matching nodes precede `node` in document order. */
/* Any-kind document-order walk (comments/PIs count too — §7.7). */
static LeptrisNodeRef next_node_doc_order(LeptrisNodeRef n) {
    LeptrisNodeRef c = leptris_node_first_child(n);
    if (c) return c;
    while (n) {
        LeptrisNodeRef s = leptris_node_next_sibling(n);
        if (s) return s;
        /* musl GCC rejects the implicit LeptrisElement store (#582). */
        n = (LeptrisNodeRef)leptris_node_parent(n);
    }
    return NULL;
}

static unsigned long any_number(const XsltInstr* in, LeptrisElement node,
                                LeptrisDocument doc, XsltExec* ex) {
    /* Namespace nodes are synthetic — outside the document-order
     * walk. libxslt numbers them through the OWNER element: the
     * matching nodes before the owner (inclusive) decide the value
     * (bug-199). */
    if (leptris_node_get_type((LeptrisNodeRef)node) ==
        LEPTRIS_NODE_NAMESPACE) {
        LeptrisElement owner =
            (LeptrisElement)((LeptrisNamespaceNode*)node)->owner;
        return owner ? any_number(in, owner, doc, ex) : 0;
    }
    /* Attributes number within their OWNER's attribute list (the
     * libxslt level=any rule — the tree walk never enters attrs). */
    if (leptris_node_get_type((LeptrisNodeRef)node) ==
        LEPTRIS_NODE_ATTRIBUTE) {
        LeptrisAttributeNode* a = (LeptrisAttributeNode*)node;
        LeptrisElement owner = (LeptrisElement)a->owner;
        size_t na = owner ? leptris_element_attribute_count(owner) : 0;
        for (size_t i = 0; i < na; i++) {
            const char* cn =
                leptris_element_attribute_name_at(owner, i);
            if (cn && a->name && strcmp(cn, a->name) == 0)
                return (unsigned long)(i + 1);
        }
        return 1;
    }
    unsigned long pos = 1;
    LeptrisNodeRef target = (LeptrisNodeRef)node;
    struct leptris_document* sd = (struct leptris_document*)doc;
    LeptrisNodeRef start =
        (LeptrisNodeRef)sd->doc_children_head;
    if (!start)
        start = leptris_element_as_node(leptris_document_root(doc));
    for (LeptrisNodeRef n = start; n; n = next_node_doc_order(n)) {
        if (n == target) return pos;
        int m;
        {
            int nty = leptris_node_get_type(n);
            if (nty == LEPTRIS_NODE_TYPE_ELEMENT) {
                m = count_matches(in, (LeptrisElement)n, node, doc, ex);
            } else if (in->num_count) {
                XsltPattern pat;
                memset(&pat, 0, sizeof(pat));
                pat.expr = in->num_count;
                m = pattern_matches_nodekind(&pat, nty, NULL);
            } else {
                /* §7.7 default count: the node's own kind. */
                m = (nty == leptris_node_get_type(target));
            }
        }
        if (m) pos++;
    }
    return 0;   /* detached node: §7.7 says nothing is emitted */
}

static void emit_number_chunk(unsigned long v, char spec,
                              int group_size, char group_sep,
                              char* out, size_t outsz) {
    char raw[64];
    format_number_token(v, spec, raw, sizeof(raw));
    if (group_size > 0 && group_sep) {
        size_t gl = strlen(raw), o = 0, cnt = 0;
        char rev[128];
        for (size_t i = gl; i-- > 0 && o < sizeof(rev) - 2;) {
            rev[o++] = raw[i];
            if (++cnt % (size_t)group_size == 0 && i > 0)
                rev[o++] = group_sep;
        }
        size_t n = o < outsz - 1 ? o : outsz - 1;
        for (size_t i = 0; i < n; i++) out[i] = rev[o - 1 - i];
        out[n] = 0;
    } else {
        snprintf(out, outsz, "%s", raw);
    }
}

/* Non-ASCII decimal-digit (Nd) tokens in format strings — the
 * numbering uses the token's script digits, zero-padded to the
 * token length (bug-219's Arabic-Indic ٠١). Returns the digit VALUE
 * (0-9) for a codepoint, or -1 when it is not a covered Nd digit. */
static int nd_digit_value(unsigned cp) {
    static const struct { unsigned lo, hi; } k_nd[] = {
        {0x0660, 0x0669}, {0x06F0, 0x06F9}, {0x0966, 0x096F},
        {0x09E6, 0x09EF}, {0x0A66, 0x0A6F}, {0x0AE6, 0x0AEF},
        {0x0B66, 0x0B6F}, {0x0C66, 0x0C6F}, {0x0CE6, 0x0CEF},
        {0x0D66, 0x0D6F}, {0x0E50, 0x0E59}, {0x0ED0, 0x0ED9},
        {0x0F20, 0x0F29}, {0xFF10, 0xFF19},
    };
    for (size_t i = 0; i < sizeof(k_nd) / sizeof(k_nd[0]); i++)
        if (cp >= k_nd[i].lo && cp <= k_nd[i].hi)
            return (int)(cp - k_nd[i].lo);
    return -1;
}

/* Decode one UTF-8 codepoint at *pp; advances. */
static unsigned utf8_next(const char** pp) {
    const unsigned char* q = (const unsigned char*)*pp;
    unsigned cp;
    if (q[0] < 0x80) { cp = q[0]; *pp += 1; }
    else if ((q[0] & 0xE0) == 0xC0) {
        cp = (unsigned)(q[0] & 0x1F) << 6 | (q[1] & 0x3F);
        *pp += 2;
    } else if ((q[0] & 0xF0) == 0xE0) {
        cp = (unsigned)(q[0] & 0x0F) << 12 | (q[1] & 0x3F) << 6 |
             (q[2] & 0x3F);
        *pp += 3;
    } else {
        cp = (unsigned)(q[0] & 0x07) << 18 | (q[1] & 0x3F) << 12 |
             (q[2] & 0x3F) << 6 | (q[3] & 0x3F);
        *pp += 4;
    }
    return cp;
}

static size_t utf8_encode(unsigned cp, char* buf) {
    if (cp < 0x80) { buf[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | cp >> 6);
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    buf[0] = (char)(0xE0 | cp >> 12);
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static int fmt_char_is_token(unsigned char c, const char* p) {
    if (c == '1' || c == 'a' || c == 'A' || c == 'i' || c == 'I')
        return 1;
    if (c >= 0x80) {
        const char* q = p;
        return nd_digit_value(utf8_next(&q)) >= 0;
    }
    return 0;
}

/* §7.7.1: number tokens in the format string map to numbers in
 * order — the LAST token repeats for extra numbers; literal runs are
 * the prefix (before the first token), separators (between tokens),
 * and suffix (after the last, emitted only when the last token was
 * used). */
static void emit_formatted_numbers(const unsigned long* values, int nv,
                                   const char* fmt, const XsltInstr* in,
                                   char* out, size_t outsz) {
    char specs[32];
    char sepbuf[32][48];
    char prefixbuf[48];
    size_t prefix_len = 0;
    int ns = 0;
    const char* p = fmt;

    while (*p && !fmt_char_is_token((unsigned char)*p, p)) {
        if (prefix_len + 1 < sizeof(prefixbuf))
            prefixbuf[prefix_len++] = *p;
        p++;
    }
    prefixbuf[prefix_len] = 0;

    unsigned nd_zero[32];
    int nd_width[32];
    while (*p && ns < 32) {
        if ((unsigned char)*p >= 0x80 &&
            nd_digit_value(0) >= 0 /* unreachable guard */) {}
        if ((unsigned char)*p >= 0x80) {
            /* Nd digit run: one token, script digits, width = run
             * length (bug-219). */
            const char* q = p;
            unsigned first_cp = utf8_next(&q);
            int v = nd_digit_value(first_cp);
            unsigned zero_cp = first_cp - (unsigned)v;
            int width = 1;
            p = q;
            while ((unsigned char)*p >= 0x80) {
                const char* q2 = p;
                unsigned cp = utf8_next(&q2);
                if (nd_digit_value(cp) < 0) break;
                width++;
                p = q2;
            }
            specs[ns] = 'D';
            nd_zero[ns] = zero_cp;
            nd_width[ns] = width;
            ns++;
        } else {
            specs[ns++] = *p++;
        }
        size_t sl = 0;
        while (*p && sl + 1 < sizeof(sepbuf[0]) &&
               !fmt_char_is_token((unsigned char)*p, p)) {
            sepbuf[ns - 1][sl++] = *p++;
        }
        sepbuf[ns - 1][sl] = 0;
    }
    if (ns == 0) {
        specs[0] = '1';   /* junk format: default is "1" */
        sepbuf[0][0] = 0;
        ns = 1;
    }

    size_t o = 0;
    for (const char* s = prefixbuf; *s && o < outsz - 1; s++) out[o++] = *s;
    for (int i = 0; i < nv && o < outsz - 1; i++) {
        if (i > 0) {
            /* §7.7.1: once the format tokens are exhausted the last
             * token repeats; the join separator is "." (libxslt's
             * default when the format has a single token). */
            const char* sep = (ns > 1)
                ? sepbuf[i - 1 <= ns - 2 ? i - 1 : ns - 2]
                : ".";
            for (const char* s = sep; *s && o < outsz - 1; s++) out[o++] = *s;
        }
        char chunk[128];
        char spec = specs[i < ns ? i : ns - 1];
        if (spec == 'D') {
            /* Nd token: decimal digits in the token's script,
             * zero-padded to the token width. */
            unsigned zcp = nd_zero[i < ns ? i : ns - 1];
            int wdt = nd_width[i < ns ? i : ns - 1];
            char dec[48];
            snprintf(dec, sizeof(dec), "%lu", values[i]);
            size_t dl = strlen(dec);
            size_t pad = (dl < (size_t)wdt) ? (size_t)wdt - dl : 0;
            size_t co = 0;
            for (size_t z = 0; z < pad && co + 4 < sizeof(chunk); z++)
                co += utf8_encode(zcp, chunk + co);
            for (size_t z = 0; z < dl && co + 4 < sizeof(chunk); z++)
                co += utf8_encode(zcp + (unsigned)(dec[z] - '0'),
                                  chunk + co);
            chunk[co] = 0;
        } else if (values[i] == 0 &&
            (spec == 'a' || spec == 'A' || spec == 'i' || spec == 'I')) {
            snprintf(chunk, sizeof(chunk), "0");   /* no zeroth letter */
        } else {
            emit_number_chunk(values[i], spec, in->num_group_size,
                              in->num_group_sep, chunk, sizeof(chunk));
        }
        for (const char* s = chunk; *s && o < outsz - 1; s++) out[o++] = *s;
    }
    if (nv >= ns) {   /* the last token was used → its tail is the suffix */
        for (const char* s = sepbuf[ns - 1]; *s && o < outsz - 1; s++)
            out[o++] = *s;
    }
    out[o] = 0;
}

static int op_number(XsltExec* ex, const XsltInstr* in,
                     LeptrisElement node) {
    unsigned long values[64];
    int nv = 0;

    if (in->num_value) {
        struct leptris_xpath_result* r =
            xslt_eval(ex, in->num_value, node);
        if (r) {
            double d = leptris_xpath_result_number(r);
            leptris_xpath_result_free(r);
            if (!(d == d)) {
                op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT,
                                          .text = "NaN" }, node);
                return 0;
            }
            if (d < 0) {
                /* libxslt: a negative value emits the literal "0"
                 * (never NaN, never token-formatted). */
                op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT,
                                          .text = "0" }, node);
                return 0;
            }
            values[0] = (unsigned long)(d + 0.5);
        } else {
            values[0] = 1;
        }
        nv = 1;
    } else if (in->num_level == 2) {
        values[0] = any_number(in, node, ex->source, ex);
        if (!values[0]) return 0;
        if (in->num_start_at) values[0] += (unsigned long)(in->num_start_at - 1);
        nv = 1;
    } else if (in->num_level == 1) {
        /* multiple: every ancestor-or-self matching count, outermost
         * first, stopping above the nearest `from` match. */
        unsigned long rev[64];
        int nr = 0;
        for (LeptrisElement a = node; a && nr < 64;
             a = leptris_node_parent((LeptrisNodeRef)a)) {
            if (count_matches(in, a, node, ex->source, ex))
                rev[nr++] = sibling_number(in, a, ex->source, ex) +
                            (in->num_start_at
                                 ? (unsigned long)(in->num_start_at - 1)
                                 : 0);
            if (from_matches(in, a, ex->source, ex)) break;
        }
        for (int i = 0; i < nr; i++) values[i] = rev[nr - 1 - i];
        nv = nr;
    } else {
        /* single: nearest ancestor-or-self matching count. */
        LeptrisElement target = NULL;
        for (LeptrisElement a = node; a;
             a = leptris_node_parent((LeptrisNodeRef)a)) {
            if (count_matches(in, a, node, ex->source, ex)) {
                target = a; break;
            }
            if (from_matches(in, a, ex->source, ex)) break;
        }
        if (!target) return 0;   /* §7.7: no match → nothing emitted */
        values[0] = sibling_number(in, target, ex->source, ex) +
                    (in->num_start_at
                         ? (unsigned long)(in->num_start_at - 1) : 0);
        nv = 1;
    }

    /* §7.7 letter-value disambiguator: with an alphabetic token
     * that could mean either, "alphabetic" forces a/A; the default
     * heuristic keeps format-driven behavior. */
    char* fmt_avt = in->num_format && strchr(in->num_format, '{')
        ? eval_avt(ex, in->num_format, node) : NULL;
    char fmtbuf[128];
    const char* fmt_use = fmt_avt ? fmt_avt
                       : (in->num_format ? in->num_format : "1");
    if (fmt_avt && strlen(fmt_avt) < sizeof(fmtbuf)) {
        snprintf(fmtbuf, sizeof(fmtbuf), "%s", fmt_avt);
        fmt_use = fmtbuf;
    }
    if (in->letter_value &&
        strcmp(in->letter_value, "alphabetic") == 0 &&
        strchr(fmt_use, 'i')) {
        /* Replace roman tokens with alphabetic on a copy. */
        snprintf(fmtbuf, sizeof(fmtbuf), "%s", fmt_use);
        for (char* q = fmtbuf; *q; q++) {
            if (*q == 'i') *q = 'a';
            if (*q == 'I') *q = 'A';
        }
        fmt_use = fmtbuf;
    }
    char out[512];
    emit_formatted_numbers(values, nv, fmt_use, in, out, sizeof(out));
    free(fmt_avt);
    op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT, .text = out }, node);
    return 0;
}

static int op_choose(XsltExec* ex, const XsltInstr* in,
                     LeptrisElement node) {
    /* Arms are WHEN (with test) / OTHERWISE children in order. */
    for (const XsltInstr* arm = in->child; arm; arm = arm->next) {
        if (arm->kind == XSLT_INSTR_WHEN && arm->test) {
            struct leptris_xpath_result* r = xslt_eval(ex, arm->test, node);
            int truth = r ? leptris_xpath_result_boolean(r) : 0;
            if (r) leptris_xpath_result_free(r);
            if (truth) return xslt_exec_instrs(ex, arm->child, node);
        } else if (arm->kind == XSLT_INSTR_OTHERWISE) {
            return xslt_exec_instrs(ex, arm->child, node);
        }
    }
    return 0;
}

/* EXSLT func:result: store the user function's return value and
 * unwind the body walker (the function call reads ex->fn_result).
 * select → that value verbatim; content → the RTF's string value
 * (EXSLT: content results are strings). */
static int op_func_result(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement node) {
    if (ex->fn_result) {
        leptris_xpath_result_free(ex->fn_result);
        ex->fn_result = NULL;
    }
    if (in->select) {
        ex->fn_result = xslt_eval(ex, in->select, node);
    } else if (in->child) {
        /* RTF capture (the op_variable pattern). Content results
         * are NODE-SETS (the fragment's document node) — value-of
         * contexts see the string value, copy-of copies the nodes
         * (EXSLT: bug-209 <result/>, bug-212 'a'). */
        LeptrisDocument main_result = ex->result;
        LeptrisElement saved = ex->pending_parent;
        ex->result = leptris_document_create();
        ex->pending_parent = NULL;
        ex->rtf_capturing = 1;
        ex->rtf_text_len = 0;
        xslt_exec_instrs(ex, in->child, node);
        ex->rtf_capturing = 0;
        ex->pending_parent = saved;
        LeptrisDocument frag_doc = ex->result;
        ex->result = main_result;
        LeptrisElement rr = leptris_document_root(frag_doc);
        if (!rr && ex->rtf_text && ex->rtf_text_len) {
            /* Pure-text RTF: the value is the string. */
            ex->fn_result = xpath_result_new(XPATH_RESULT_STRING);
            if (ex->fn_result)
                ex->fn_result->value.string_value =
                    leptris_strdup(ex->rtf_text);
            leptris_document_free(frag_doc);
        } else {
            ex->fn_result = xpath_result_new(XPATH_RESULT_NODESET);
            if (ex->fn_result && rr) {
                ex->fn_result->value.nodeset_value = xpath_nodeset_new();
                if (ex->fn_result->value.nodeset_value) {
                    LeptrisNodeRef dn = (LeptrisNodeRef)
                        leptris_document_get_node(frag_doc);
                    if (dn)
                        xpath_nodeset_add(
                            ex->fn_result->value.nodeset_value, dn);
                }
            }
            /* The nodeset's nodes live in frag_doc — move it into
             * the exec's RTF chain so they outlive the call. */
            if (rr) {
                struct xslt_rtf_entry* ent =
                    (struct xslt_rtf_entry*)malloc(sizeof(*ent));
                if (ent) {
                    ent->doc = frag_doc;
                    ent->next = (struct xslt_rtf_entry*)ex->rtf_chain;
                    ex->rtf_chain = ent;
                } else {
                    leptris_document_free(frag_doc);
                }
            } else {
                leptris_document_free(frag_doc);
            }
        }
    }
    ex->fn_yield = 1;
    return 0;
}

/* ---- The walker ---- */

int xslt_exec_instrs(XsltExec* ex, const XsltInstr* list,
                     LeptrisElement node) {
    if (ex->terminated) return -1;
    if (ex->eval_error) return -1;
    /* §11 block scope: variables pushed while executing this
     * sequence pop when the sequence ends. Re-entrancy: nested
     * sequences snapshot their own mark. */
    XsltVar* scope_mark = ex->vars;
    int rc = 0;
    for (const XsltInstr* in = list; in; in = in->next) {
        if (ex->fn_yield) break;   /* func:result unwinds to the call */
        if (ex->iterate_signal) break;   /* next-iteration/break unwind
                                          * to the enclosing iterate */
        if (ex->eval_error) { rc = -1; break; }
        /* §11.6: xsl:param declarations are consumed by the invoker
         * (with-param binding or default evaluation) — not executed
         * as ordinary variables on the walk. */
        if (in->kind == XSLT_INSTR_VARIABLE && in->is_param) continue;
        LeptrisXPathNsSet saved_ns = ex->current_ns;
        ex->current_ns = in->ns;
        XsltInstrFn fn = g_ops[in->kind];
        if (fn) {
            rc = fn(ex, in, node);
            if (rc) { ex->current_ns = saved_ns; break; }
        } else if (in->kind == XSLT_INSTR_VARIABLE) {
            rc = op_variable(ex, in, node);
            if (rc) { ex->current_ns = saved_ns; break; }
        }
        ex->current_ns = saved_ns;
    }
    xslt_pop_vars_to(ex, scope_mark);
    return rc;
}

/* CHOOSE execution needs its own scan; simplest correct wiring:
 * expose it through a WHEN handler that consults its siblings via
 * the walker order (see xslt_exec_choose below). */

static void register_ops(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    g_ops[XSLT_INSTR_RESULT_ELEM] = op_result_elem;
    g_ops[XSLT_INSTR_TEXT] = op_text;
    g_ops[XSLT_INSTR_VALUE_OF] = op_value_of;
    g_ops[XSLT_INSTR_SEQUENCE] = op_sequence;
    g_ops[XSLT_INSTR_ON_NON_EMPTY] = op_on_non_empty;
    g_ops[XSLT_INSTR_WHERE_POPULATED] = op_where_populated;
    g_ops[XSLT_INSTR_NEXT_MATCH] = op_next_match;
    g_ops[XSLT_INSTR_FORK] = op_fork;
    g_ops[XSLT_INSTR_NAMESPACE] = op_namespace;
    g_ops[XSLT_INSTR_DOCUMENT] = op_document;
    g_ops[XSLT_INSTR_MERGE] = op_merge;
    g_ops[XSLT_INSTR_RESULT_DOCUMENT] = op_result_document;
    g_ops[XSLT_INSTR_FOR_EACH] = op_for_each;
    g_ops[XSLT_INSTR_ITERATE] = op_iterate;
    g_ops[XSLT_INSTR_NEXT_ITERATION] = op_next_iteration;
    g_ops[XSLT_INSTR_BREAK] = op_break;
    g_ops[XSLT_INSTR_FOR_EACH_GROUP] = op_for_each_group;
    g_ops[XSLT_INSTR_EVALUATE] = op_evaluate;
    g_ops[XSLT_INSTR_ANALYZE_STRING] = op_analyze_string;
    g_ops[XSLT_INSTR_TRY] = op_try;
    g_ops[XSLT_INSTR_IF] = op_if;
    g_ops[XSLT_INSTR_APPLY_TEMPLATES] = op_apply_templates;
    g_ops[XSLT_INSTR_CALL_TEMPLATE] = op_call_template;
    g_ops[XSLT_INSTR_COPY_OF] = op_copy_of;
    g_ops[XSLT_INSTR_COPY] = op_copy;
    g_ops[XSLT_INSTR_ELEMENT] = op_element;
    g_ops[XSLT_INSTR_ATTRIBUTE] = op_attribute;
    g_ops[XSLT_INSTR_COMMENT] = op_comment;
    g_ops[XSLT_INSTR_PI] = op_pi;
    g_ops[XSLT_INSTR_MESSAGE] = op_message;
    g_ops[XSLT_INSTR_NUMBER] = op_number;
    g_ops[XSLT_INSTR_CHOOSE] = op_choose;
    g_ops[XSLT_INSTR_ATTR_SET_REF] = op_attr_set_ref;
    g_ops[XSLT_INSTR_APPLY_IMPORTS] = op_apply_imports;
    g_ops[XSLT_INSTR_UNKNOWN_XSL] = op_unknown_xsl;
    g_ops[XSLT_INSTR_FUNC_RESULT] = op_func_result;
}

/* ---- Public transform ---- */

void xslt_exec_free(XsltExec* ex) {
    if (!ex) return;
    xslt_keys_free(ex);
    xslt_accs_free(ex);
    xslt_docs_free(ex);
    xslt_bridge_free(ex);
    xslt_ufn_free(ex);
    xslt_gids_free(ex);
    xslt_avt_free(ex);
    if (ex->fn_result) leptris_xpath_result_free(ex->fn_result);
    while (ex->vars) xslt_pop_var(ex, NULL);
    while (ex->tunnel_vars) {
        XsltVar* t = ex->tunnel_vars;
        ex->tunnel_vars = t->prev;
        if (t->value) leptris_xpath_result_free(t->value);
        free(t);
    }
    while (ex->frag_nodes) {
        XsltFragNode* f = (XsltFragNode*)ex->frag_nodes;
        ex->frag_nodes = f->next;
        free(f);
    }
    free(ex->rtf_text);
    if (ex->varset) xpath_variable_set_free(ex->varset);
    while (ex->rtf_chain) {
        struct xslt_rtf_entry* ent = (struct xslt_rtf_entry*)ex->rtf_chain;
        ex->rtf_chain = ent->next;
        if (ent->doc) leptris_document_free(ent->doc);
        free(ent);
    }
    if (ex->scratch) leptris_document_free(ex->scratch);
    if (ex->result) leptris_document_free(ex->result);
    free(ex->message);
    free(ex);
}

XsltExec* xslt_transform_doc(const XsltStylesheet* sheet,
                             LeptrisDocument sheet_doc,
                             LeptrisDocument source) {
    if (!sheet || !source) return NULL;
    register_ops();

    XsltExec* ex = (XsltExec*)calloc(1, sizeof(*ex));
    if (!ex) return NULL;
    ex->sheet = sheet;
    ex->sheet_doc = sheet_doc;   /* set BEFORE the body: document('') */
    ex->source = source;
    ex->current_pos = 1;   /* §12.4: position() default context position */
    ex->current_size = 1;  /* last() default context size */
    ex->result = leptris_document_create();
    if (!ex->result) { xslt_exec_free(ex); return NULL; }

    /* Install the function-bridge state on the SOURCE document for
     * the transform's duration: every XPath eval on this doc (both
     * the VM path and the AST-interpreter path) builds its registry
     * through leptris_xpath_build_custom_registry, which registers
     * key()/current()/format-number()/... with `ex` as user_data
     * while xslt_state is set. Save/restore makes nesting safe.
     * The state pointer keys the doc's cached registry — drop the
     * cache so it rebuilds for this exec (and again on restore). */
    struct leptris_document* src_doc = (struct leptris_document*)source;
    void* saved_xslt_state = src_doc->xslt_state;
    leptris_xpath_invalidate_fn_registry(src_doc);
    src_doc->xslt_state = ex;


    /* §3.4: strip source whitespace before any template sees it. */
    /* Issue #606: libxslt's document loader applies ATTLIST defaults
     * (XML_PARSE_DTDATTR is on by default there), so stylesheet
     * processing always sees defaulted attributes — apply them at
     * the transform boundary regardless of how the caller parsed. */
    leptris_dtd_apply_attribute_defaults((struct leptris_document*)source);

    strip_source_whitespace(ex);

    /* Globals — each with its DECLARING element's namespace
     * context (included sheets bind their own prefixes, bug-36-).
     * §11: the select expression evaluates with the source
     * DOCUMENT node as context (relative paths resolve against the
     * source, bug-224). */
    LeptrisElement globals_ctx = (LeptrisElement)
        leptris_document_get_node((struct leptris_document*)source);
    for (const XsltInstr* g = sheet->globals; g; g = g->next) {
        if (g->kind == XSLT_INSTR_VARIABLE) {
            LeptrisXPathNsSet saved_gns = ex->current_ns;
            ex->current_ns = g->ns;
            op_variable(ex, g, globals_ctx);
            ex->current_ns = saved_gns;
            /* §11: a named template invoked while later globals are
             * still evaluating (bug-192: $template-value calls
             * get-dummy mid-loop) must see the globals bound so far —
             * keep the call-template reset point current instead of
             * leaving it NULL until the loop ends. */
            ex->global_vars = ex->vars;
        }
    }
    ex->global_vars = ex->vars;   /* §11: the call-template reset point */
    if (!ex->terminated) {

    /* Root invocation (§5.1): the context is the DOCUMENT node —
     * patterns match it via "/" only; other patterns never match
     * the root. No match → the built-in root rule (apply-templates
     * over the document's children). */
    {
        LeptrisElement dnode = (LeptrisElement)leptris_document_get_node(
            (struct leptris_document*)source);
        if (dnode) {
            const XsltTemplate* root_t =
                xslt_select_template(ex, dnode, NULL, 0);
            if (root_t) {
                xslt_invoke_template(ex, root_t, dnode, NULL);
            } else {
                op_apply_templates(
                    ex,
                    &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES },
                    dnode);
            }
        }
    }
    }   /* !terminated */

    src_doc->xslt_state = saved_xslt_state;
    leptris_xpath_invalidate_fn_registry(src_doc);
    return ex;
}

