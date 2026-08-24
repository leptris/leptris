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
#include "../dom/text.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define XSLT_MAX_DEPTH 512

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
     * interpreter's prefixed-test resolution). */
    if (!ex->vars) {
        struct leptris_xpath_result* r = ex->current_ns
            ? leptris_xpath_compiled_eval_ns(
                  c, ex->source, node, ex->current_ns)
            : leptris_xpath_compiled_eval(c, ex->source, node);
        ex->current_node = saved_cur;
        return r;
    }

    if (!ex->varset) {
        ex->varset = xpath_variable_set_new();
        if (!ex->varset) { ex->current_node = saved_cur; return NULL; }
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

    struct leptris_xpath_result* r = ex->current_ns
        ? leptris_xpath_compiled_eval_ns_vars(
              c, ex->source, node,
              (struct leptris_xpath_ns_map*)ex->current_ns, ex->varset)
        : leptris_xpath_compiled_eval_vars(
              c, ex->source, node,
              (LeptrisXPathVariableSet)ex->varset);

    /* Clear the scratch set for the next evaluation. */
    while (ex->varset && ex->varset->count > 0) {
        xpath_variable_set_remove(ex->varset,
                                  ex->varset->variables[0]->name);
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
}

void xslt_pop_var(XsltExec* ex, const char* name) {
    if (!ex || !ex->vars) return;
    XsltVar* top = ex->vars;
    ex->vars = top->prev;
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
static void xslt_pop_vars_to(XsltExec* ex, XsltVar* mark) {
    while (ex->vars && ex->vars != mark) xslt_pop_var(ex, NULL);
}

/* ---- Shared small helpers ---- */

static LeptrisElement out_append_elem(XsltExec* ex, LeptrisElement parent,
                                      const char* name, const char* ns) {
    LeptrisElement e = leptris_element_create(ex->result, name);
    if (!e) return NULL;
    (void)ns;   /* namespace declarations on output: phase 03 */
    if (!parent) {
        LeptrisElement root = leptris_document_root(ex->result);
        if (!root) {
            leptris_document_set_root(ex->result, e);
        } else {
            /* XSLT permits multiple top-level result elements (the
             * result is a fragment, not a document). The flat
             * layout carries them as the root's SIBLING chain;
             * apply_string serializes the whole chain. */
            LeptrisElement last = root;
            while (leptris_node_get_next_sibling(
                       leptris_element_as_node(last)))
                last = (LeptrisElement)leptris_node_get_next_sibling(
                    leptris_element_as_node(last));
            leptris_node_set_next_sibling(
                leptris_element_as_node(last), leptris_element_as_node(e));
        }
    } else {
        leptris_element_append_child(parent, e);
    }
    return e;
}

static void out_append_text(XsltExec* ex, LeptrisElement parent,
                            const char* text) {
    if (!text || !*text) return;
    if (parent) {
        /* §7.1.1 cdata-section-elements: parent name is in the list
         * → emit the text as a CDATA node instead of a text node. */
        if (ex->sheet->out_cdata_elems) {
            const char* parent_name = leptris_element_get_name(parent);
            if (parent_name) {
                for (size_t i = 0; ex->sheet->out_cdata_elems[i]; i++) {
                    if (strcmp(ex->sheet->out_cdata_elems[i],
                               parent_name) == 0) {
                        LeptrisNodeRef c =
                            leptris_cdata_node_create(ex->result, text);
                        if (c) leptris_element_append_child(
                            parent, (LeptrisElement)c);
                        return;
                    }
                }
            }
        }
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
    char** buf;
    size_t* len;
    size_t* cap;
    if (ex->rtf_capturing) {
        buf = &ex->rtf_text;
        len = &ex->rtf_text_len;
        cap = &ex->rtf_text_cap;
    } else if (leptris_document_root(ex->result) != NULL) {
        buf = &ex->tail_text;
        len = &ex->tail_text_len;
        cap = &ex->tail_text_cap;
    } else {
        buf = &ex->top_text;
        len = &ex->top_text_len;
        cap = &ex->top_text_cap;
    }
    if (*len + tl + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 64;
        while (nc < *len + tl + 1) nc *= 2;
        char* grown = (char*)realloc(*buf, nc);
        if (!grown) return;
        *buf = grown;
        *cap = nc;
    }
    memcpy(*buf + *len, text, tl);
    *len += tl;
    (*buf)[*len] = 0;
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
            /* Find the matching close brace (no nesting in AVTs). */
            const char* close = strchr(p + 1, '}');
            if (!close) {
                if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                out[len++] = *p++;
                continue;
            }
            size_t elen = (size_t)(close - p - 1);
            char* expr = (char*)malloc(elen + 1);
            if (expr) {
                memcpy(expr, p + 1, elen);
                expr[elen] = 0;
                LeptrisXPathCompiled c = leptris_xpath_compile(expr);
                free(expr);
                if (c) {
                    struct leptris_xpath_result* r =
                        xslt_eval(ex, c, node);
                    char* sv = r ? leptris_xpath_result_string(r) : NULL;
                    if (r) leptris_xpath_result_free(r);
                    leptris_xpath_compiled_free(c);
                    if (sv) {
                        size_t sl = strlen(sv);
                        while (len + sl + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
                        memcpy(out + len, sv, sl);
                        len += sl;
                        leptris_free_string(sv);
                    }
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

/* Apply (in order) the attribute-set names in `in->attr_set_names`,
 * evaluating AVTs against `node`. Existing attributes on the target
 * are NOT overwritten — explicit attrs and later sets win (§7.1.4). */
void xslt_apply_attr_sets(XsltExec* ex, const XsltInstr* in,
                          LeptrisElement target, LeptrisElement node) {
    if (!ex || !ex->sheet || !target || !in || in->attr_set_count == 0) return;
    for (size_t i = 0; i < in->attr_set_count; i++) {
        const char* want = in->attr_set_names[i];
        if (!want) continue;
        for (XsltAttrSet* s = ex->sheet->attrsets; s; s = s->next) {
            if (!s->name || strcmp(s->name, want) != 0) continue;
            for (XsltLAttr* a = s->attrs; a; a = a->next) {
                /* Skip if target already has this attribute
                 * (later precedence wins). */
                if (leptris_element_attribute(target, a->name)) continue;
                char* v = eval_avt(ex, a->value, node);
                if (v) {
                    leptris_element_set_attribute(target, a->name, v);
                    free(v);
                }
            }
        }
    }
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
    LeptrisElement parent = ex->pending_parent;
    const char* out_name = apply_ns_alias(ex->sheet, in->name);
    LeptrisElement e = out_append_elem(ex, parent, out_name, in->ns_uri);
    if (!e) return -1;
    /* Literal attrs FIRST — they win over any defaults coming
     * from the named attribute-set (§7.1.4 — explicit attrs and
     * later sets take precedence over earlier sets). */
    for (XsltLAttr* a = in->attrs; a; a = a->next) {
        char* v = eval_avt(ex, a->value, node);
        if (v) {
            leptris_element_set_attribute(e, a->name, v);
            free(v);
        }
    }
    /* Attribute-value templates on literal result elements. */
    xslt_apply_attr_sets(ex, in, e, node);
    ex->pending_parent = e;
    int rc = xslt_exec_instrs(ex, in->child, node);
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

static int op_text(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
    LeptrisElement parent = ex->pending_parent;
    if (parent) {
        if (in->doe && in->text) {
            /* §16.4 disable-output-escaping: raw flag — the
             * serializer emits the string verbatim. */
            LeptrisNodeRef t = leptris_text_node_create(ex->result, in->text);
            if (t) {
                ((LeptrisTextNode*)t)->base.raw = 1;
                leptris_element_append_child(parent, (LeptrisElement)t);
            }
            return 0;
        }
        /* Non-DOE: out_append_text (carries the cdata-section-
         * elements conversion for listed parents). */
        out_append_text(ex, parent, in->text);
        return 0;
    }
    /* Fragment-level: accumulate verbatim-safe — escape unless DOE
     * or method=text (§16.3 never escapes). */
    char* v = in->text ? escape_fragment_text(
                   in->text, in->doe || ex->sheet->out_method_text) : NULL;
    if (v) {
        out_append_text(ex, NULL, v);
        if (v != in->text) free(v);
    }
    return 0;
}

static int op_value_of(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    struct leptris_xpath_result* r = xslt_eval(ex, in->select, node);
    if (!r) return 0;
    char* sv = leptris_xpath_result_string(r);
    leptris_xpath_result_free(r);
    if (sv) {
        op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT, .text = sv,
                                  .doe = in->doe },
                node);
        leptris_free_string(sv);
    }
    return 0;
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
        for (size_t i = 0; i < n; i++) {
            items[i] = leptris_xpath_result_get(r, i);
        }
    }
    leptris_xpath_result_free(r);
    if (!items) return n ? -1 : 0;

    /* xsl:sort v1: stable ordering by string/number key. */
    if (in->sorts) {
        /* Build a key array per sort (single-key v1: first sort). */
        XsltSort* s = in->sorts;
        char** keys = (char**)calloc(n, sizeof(char*));
        double* nums = s->numeric ? (double*)calloc(n, sizeof(double)) : NULL;
        for (size_t i = 0; i < n; i++) {
            if (s->select) {
                struct leptris_xpath_result* kr =
                    xslt_eval(ex, s->select, items[i]);
                if (kr) {
                    /* Numeric keys convert through the STRING value
                     * — result_number of a nodeset is NaN. */
                    char* sv = leptris_xpath_result_string(kr);
                    if (s->numeric) {
                        nums[i] = sv ? strtod(sv, NULL) : 0.0;
                        if (sv) free(sv);
                    } else {
                        keys[i] = sv;   /* ownership kept for compare */
                    }
                    leptris_xpath_result_free(kr);
                }
            } else {
                char* sv = string_value_deep(items[i]);
                if (sv) {
                    if (s->numeric) nums[i] = strtod(sv, NULL);
                    else keys[i] = sv;
                    if (s->numeric) free(sv);
                }
            }
        }
        /* Insertion sort (stable, N is typical transform-sized). */
        for (size_t i = 1; i < n; i++) {
            for (size_t j = i; j > 0; j--) {
                int swap;
                if (s->numeric) {
                    swap = s->descending ? nums[j-1] < nums[j]
                                         : nums[j-1] > nums[j];
                } else {
                    const char* a = keys[j-1] ? keys[j-1] : "";
                    const char* b = keys[j] ? keys[j] : "";
                    /* §10 case-order: for strings differing only by
                     * case, "upper-first" (the default) sorts by
                     * codepoint — 'A' < 'a' — and "lower-first"
                     * reverses. Other strings compare normally. */
                    int cmp = (xslt_ci_eq(a, b) &&
                               s->case_upper_first == 0)
                                  ? -strcmp(a, b) : strcmp(a, b);
                    swap = s->descending ? cmp < 0 : cmp > 0;
                }
                if (!swap) break;
                LeptrisElement te = items[j-1]; items[j-1] = items[j]; items[j] = te;
                if (s->numeric) { double td = nums[j-1]; nums[j-1] = nums[j]; nums[j] = td; }
                else { char* tk = keys[j-1]; keys[j-1] = keys[j]; keys[j] = tk; }
            }
        }
        for (size_t i = 0; i < n; i++) free(keys[i]);
        free(keys);
        free(nums);
    }

    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++) {
        rc = xslt_exec_instrs(ex, in->child, items[i]);
    }
    free(items);
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
            if (cn && leptris_node_get_type(cn) == LEPTRIS_NODE_TYPE_ELEMENT) {
                copy_node_deep(ex, (LeptrisElement)cn, ex->pending_parent);
            } else if (cn) {
                /* Text/other nodes: append their string value. */
                char* sv = NULL;
                if (leptris_node_get_type(cn) == LEPTRIS_NODE_TYPE_TEXT ||
                    leptris_node_get_type(cn) == LEPTRIS_NODE_TYPE_CDATA) {
                    sv = leptris_strdup(leptris_text_get_content((LeptrisTextNode*)cn));
                }
                if (sv) {
                    out_append_text(ex, ex->pending_parent, sv);
                    free(sv);
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
    const char* name = leptris_element_get_name(node);
    if (!name) return 0;
    LeptrisElement e = out_append_elem(ex, ex->pending_parent, name, NULL);
    if (!e) return -1;
    /* Attribute sets contribute defaults for missing names;
     * source node attrs win (we apply them after so their values
     * are NOT overwritten). */
    xslt_apply_attr_sets(ex, in, e, node);
    /* Copy the attributes (v1: literal names). */
    size_t na = leptris_element_attribute_count(node);
    for (size_t i = 0; i < na; i++) {
        const char* an = leptris_element_attribute_name_at(node, i);
        const char* av = leptris_element_attribute_value_at(node, i);
        if (an && av) leptris_element_set_attribute(e, an, av);
    }
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
    LeptrisElement e = out_append_elem(ex, parent, name, NULL);
    if (!e) return -1;
    size_t na = leptris_element_attribute_count(node);
    for (size_t i = 0; i < na; i++) {
        const char* an = leptris_element_attribute_name_at(node, i);
        const char* av = leptris_element_attribute_value_at(node, i);
        if (an && av) leptris_element_set_attribute(e, an, av);
    }
    char* sv = string_value_deep(node);
    if (sv) {
        if (sv[0]) out_append_text(ex, e, sv);
        free(sv);
    }
    /* Deep-copy child ELEMENTS too (mixed content round-trips). */
    for (LeptrisNodeRef c = leptris_node_first_child(leptris_element_as_node(node));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) {
            copy_node_deep(ex, (LeptrisElement)c, e);
        }
    }
    return 0;
}

static int op_variable(XsltExec* ex, const XsltInstr* in,
                       LeptrisElement node) {
    if (!in->name) return 0;
    struct leptris_xpath_result* v = NULL;
    if (in->select) {
        v = xslt_eval(ex, in->select, node);
    } else if (in->child) {
        /* RTF: build into a scratch document with no parent so the
         * emitted top-level nodes collect at the document root.
         * The variable holds a nodeset of those top-level nodes —
         * exslt:node-set semantics without an explicit call. */
        LeptrisElement saved = ex->pending_parent;
        ex->pending_parent = NULL;
        ex->rtf_capturing = 1;
        ex->rtf_text_len = 0;
        xslt_exec_instrs(ex, in->child, node);
        ex->rtf_capturing = 0;
        ex->pending_parent = saved;
        LeptrisElement rr = leptris_document_root(ex->result);
        if (!rr && ex->rtf_text && ex->rtf_text_len) {
            /* Pure-text RTF: the value is the string. */
            v = xpath_result_new(XPATH_RESULT_STRING);
            if (v) v->value.string_value = leptris_strdup(ex->rtf_text);
            xslt_push_var(ex, in->name, v);
            return 0;
        }
        v = xpath_result_new(XPATH_RESULT_NODESET);
        if (v && rr) {
            v->value.nodeset_value = xpath_nodeset_new();
            if (v->value.nodeset_value) {
                /* XSLT allows multiple top-level result elements;
                 * out_append_elem with pending_parent=NULL keeps
                 * them as a sibling chain under the document root.
                 * Walk from the root and on through next-siblings to
                 * capture them all in document order. */
                for (LeptrisElement c = rr;
                     c;
                     c = leptris_element_next_sibling_any(c)) {
                    xpath_nodeset_add(v->value.nodeset_value,
                                      (LeptrisNodeRef)c);
                }
            }
        }
        /* The nodeset's node pointers live in `rr`'s document. Move
         * ownership of the result document into the exec's RTF
         * chain so the nodes outlive the variable's frame. */
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
    xslt_push_var(ex, in->name, v);
    return 0;
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
    /* with-params + §11.6 param defaults + current-template tracking. */
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

static int name_in_list(char** list, const char* name) {
    if (!list) return 0;
    for (size_t i = 0; list[i]; i++)
        if (strcmp(list[i], name) == 0) return 1;
    return 0;
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

static void strip_source_whitespace(XsltExec* ex) {
    if (!ex || !ex->source || !ex->sheet->ws_strip) return;
    /* libxslt reference semantics: source whitespace is PRESERVED
     * by default; only names listed in xsl:strip-space (minus
     * preserve-space) strip, with xml:space="preserve" winning. */
    for (LeptrisElement e = leptris_document_root(ex->source); e;
         e = xslt_next_doc_order(e)) {
        const char* name = leptris_element_name(e);
        if (!name) continue;
        int strip = name_in_list(ex->sheet->ws_strip, name) &&
                    !name_in_list(ex->sheet->ws_preserve, name);
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
        if (!xslt_pattern_matches(t->matches, node, ex->source)) continue;
        /* Max priority among the MATCHING alternatives. */
        double pri = 0; int have = 0;
        for (const XsltPattern* pa = t->matches; pa; pa = pa->next) {
            XsltPattern one = *pa; one.next = NULL;
            if (!xslt_pattern_matches(&one, node, ex->source)) continue;
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
        struct leptris_xpath_result* v = NULL;
        if (p->select) {
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
    XsltVar* mark = ex->vars;
    const XsltTemplate* saved_t = ex->current_template;
    ex->current_template = t;
    for (const XsltInstr* wp = with_params; wp; wp = wp->next) {
        if (wp->kind != XSLT_INSTR_WITH_PARAM || !wp->name) continue;
        struct leptris_xpath_result* v =
            wp->select ? xslt_eval(ex, wp->select, node) : NULL;
        xslt_push_var(ex, wp->name, v);
    }
    xslt_bind_param_defaults(ex, t->body, node);
    int rc = xslt_exec_instrs(ex, t->body, node);
    ex->current_template = saved_t;
    xslt_pop_vars_to(ex, mark);
    return rc;
}

/* §5.6 xsl:apply-imports: select the best matching rule from the
 * stylesheets IMPORTED by (i.e. lower precedence than) the one
 * containing the currently executing rule. */
static int op_apply_imports(XsltExec* ex, const XsltInstr* in,
                            LeptrisElement node) {
    (void)in;
    int min_rank = ex->current_template
                       ? ex->current_template->import_rank + 1 : 1;
    const XsltTemplate* t = xslt_select_template(
        ex, node, ex->current_template ? ex->current_template->mode : NULL,
        min_rank);
    if (!t) return 0;
    return xslt_invoke_template(ex, t, node, NULL);
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
                for (size_t i = 0; i < n; i++)
                    items[i] = leptris_xpath_result_get(r, i);
            }
        }
        leptris_xpath_result_free(r);
    } else {
        /* Default (§5.4): child nodes in DOCUMENT ORDER — text
         * copies inline (built-in text rule, §5.8), elements select
         * and invoke their template AS ENCOUNTERED so output order
         * matches the source (the old batch-then-loop broke
         * interleaving). */
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(node));
             c && rc == 0; c = leptris_node_next_sibling(c)) {
            int ty = leptris_node_get_type(c);
            if (ty == LEPTRIS_NODE_TYPE_TEXT ||
                ty == LEPTRIS_NODE_TYPE_CDATA) {
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
            if (ty != LEPTRIS_NODE_TYPE_ELEMENT) continue;
            LeptrisElement item = (LeptrisElement)c;
            const XsltTemplate* best =
                xslt_select_template(ex, item, in->name, 0);
            if (best) {
                rc = xslt_invoke_template(ex, best, item, in->child);
            } else {
                rc = op_apply_templates(
                    ex,
                    &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES },
                    item);
            }
        }
    }
    if (!items) return n ? -1 : 0;

    /* Bind with-params for the selected template(s). */
    int bound = 0;
    for (const XsltInstr* wp = in->child; wp; wp = wp->next) {
        if (wp->kind != XSLT_INSTR_WITH_PARAM || !wp->name) continue;
        struct leptris_xpath_result* v =
            wp->select ? xslt_eval(ex, wp->select, node) : NULL;
        xslt_push_var(ex, wp->name, v);
        bound++;
    }

    const char* mode = in->name;   /* mode attr parsed into ->name */
    for (size_t i = 0; i < n && rc == 0; i++) {
        const XsltTemplate* best =
            xslt_select_template(ex, items[i], mode, 0);
        if (best) {
            rc = xslt_invoke_template(ex, best, items[i], in->child);
        } else {
            /* Built-in template rules (§5.8): apply-templates for
             * elements; text copied for text; nothing otherwise. */
            rc = op_apply_templates(
                ex,
                &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES },
                items[i]);
        }
    }
    for (int i = 0; i < bound; i++) xslt_pop_var(ex, NULL);
    free(items);
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
        name = leptris_strdup(in->name);
    }
    if (!name) return 0;
    /* §7.1.1 namespace attribute: an AVT; non-empty binds the
     * element (or its prefix) to the URI via an xmlns declaration. */
    char* ns_avt = in->ns_uri && in->ns_uri[0]
                       ? eval_avt(ex, in->ns_uri, node) : NULL;
    if (ns_avt && !ns_avt[0]) { free(ns_avt); ns_avt = NULL; }
    LeptrisElement e = out_append_elem(ex, ex->pending_parent, name,
                                       ns_avt);
    free(name);
    if (!e) { free(ns_avt); return -1; }
    if (ns_avt) {
        const char* colon = strchr(
            leptris_element_name(e) ? leptris_element_name(e) : "", ':');
        if (colon) {
            char attr[80];
            size_t pl = (size_t)(colon -
                         (leptris_element_name(e) ? leptris_element_name(e) : ""));
            snprintf(attr, sizeof(attr), "xmlns:%.*s", (int)pl,
                     leptris_element_name(e));
            leptris_element_set_attribute(e, attr, ns_avt);
        } else {
            leptris_element_set_attribute(e, "xmlns", ns_avt);
        }
        free(ns_avt);
    }
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
    /* Content becomes the value: execute into a text accumulator. */
    char* acc = (char*)calloc(1, 1);
    size_t len = 0;
    for (const XsltInstr* c = in->child; c; c = c->next) {
        if (c->kind == XSLT_INSTR_TEXT && c->text) {
            size_t tl = strlen(c->text);
            acc = (char*)realloc(acc, len + tl + 1);
            if (!acc) return -1;
            memcpy(acc + len, c->text, tl);
            len += tl;
            acc[len] = 0;
        } else if (c->kind == XSLT_INSTR_VALUE_OF && c->select) {
            struct leptris_xpath_result* r = xslt_eval(ex, c->select, node);
            if (r) {
                char* sv = leptris_xpath_result_string(r);
                leptris_xpath_result_free(r);
                if (sv) {
                    size_t sl = strlen(sv);
                    acc = (char*)realloc(acc, len + sl + 1);
                    if (acc) {
                        memcpy(acc + len, sv, sl);
                        len += sl;
                        acc[len] = 0;
                    }
                    leptris_free_string(sv);
                }
            }
        }
    }
    leptris_element_set_attribute(ex->pending_parent, in->name, acc);
    free(acc);
    return 0;
}

static int op_comment(XsltExec* ex, const XsltInstr* in,
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
    LeptrisNodeRef cm = leptris_comment_node_create(ex->result, acc);
    if (cm && ex->pending_parent) {
        leptris_element_append_child(ex->pending_parent, (LeptrisElement)cm);
    }
    free(acc);
    return 0;
}

static int op_pi(XsltExec* ex, const XsltInstr* in, LeptrisElement node) {
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
    LeptrisNodeRef pi = leptris_pi_node_create(ex->result, in->name, acc);
    if (pi && ex->pending_parent) {
        leptris_element_append_child(ex->pending_parent, (LeptrisElement)pi);
    }
    free(acc);
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
    static const char* roman_l[] =
        {"","i","ii","iii","iv","v","vi","vii","viii","ix","x",
         "xi","xii","xiii","xiv","xv","xvi","xvii","xviii","xix","xx",
         "xxi","xxii","xxiii","xxiv","xxv","xxvi","xxvii","xxviii","xxix","xxx",
         "xl","xli","xlii","xliii","xliv","xlv","xlvi","xlvii","xlviii","xlix","l"};
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
            const char* r = v <= 50 ? roman_l[v] : "";
            if (spec == 'I') {
                snprintf(out, outsz, "%s", r);
                for (char* p = out; *p; p++)
                    *p = (char)toupper((unsigned char)*p);
            } else {
                snprintf(out, outsz, "%s", r);
            }
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

/* Count predicate: the count pattern, or (default) the same expanded
 * name as the node being numbered. */
static int count_matches(const XsltInstr* in, LeptrisElement cand,
                         LeptrisElement ref, LeptrisDocument doc) {
    if (in->num_count) {
        XsltPattern pat;
        memset(&pat, 0, sizeof(pat));
        pat.expr = in->num_count;
        return xslt_pattern_matches(&pat, cand, doc);
    }
    const char* a = leptris_element_get_name(cand);
    const char* b = leptris_element_get_name(ref);
    return a && b && strcmp(a, b) == 0;
}

static int from_matches(const XsltInstr* in, LeptrisElement cand,
                        LeptrisDocument doc) {
    if (!in->num_from) return 0;
    XsltPattern pat;
    memset(&pat, 0, sizeof(pat));
    pat.expr = in->num_from;
    return xslt_pattern_matches(&pat, cand, doc);
}

/* Position of `target` among preceding siblings matching count. */
static unsigned long sibling_number(const XsltInstr* in,
                                    LeptrisElement target,
                                    LeptrisDocument doc) {
    unsigned long pos = 1;
    LeptrisElement parent = leptris_node_parent((LeptrisNodeRef)target);
    for (LeptrisElement c = leptris_element_first_child_any(parent); c;
         c = leptris_element_next_sibling_any(c)) {
        if (c == target) return pos;
        if (count_matches(in, c, target, doc)) pos++;
    }
    return pos;
}

/* any: how many matching nodes precede `node` in document order. */
static unsigned long any_number(const XsltInstr* in, LeptrisElement node,
                                LeptrisDocument doc) {
    unsigned long pos = 1;
    for (LeptrisElement e = leptris_document_root(doc); e;
         e = xslt_next_doc_order(e)) {
        if (e == node) return pos;
        if (count_matches(in, e, node, doc)) pos++;
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

    while (*p && !(*p == '1' || *p == 'a' || *p == 'A' ||
                   *p == 'i' || *p == 'I')) {
        if (prefix_len + 1 < sizeof(prefixbuf))
            prefixbuf[prefix_len++] = *p;
        p++;
    }
    prefixbuf[prefix_len] = 0;

    while (*p && ns < 32) {
        specs[ns++] = *p++;
        size_t sl = 0;
        while (*p && sl + 1 < sizeof(sepbuf[0]) &&
               !(*p == '1' || *p == 'a' || *p == 'A' ||
                 *p == 'i' || *p == 'I')) {
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
            const char* sep = "";
            if (ns > 1) sep = sepbuf[i - 1 <= ns - 2 ? i - 1 : ns - 2];
            for (const char* s = sep; *s && o < outsz - 1; s++) out[o++] = *s;
        }
        char chunk[128];
        emit_number_chunk(values[i], specs[i < ns ? i : ns - 1],
                          in->num_group_size, in->num_group_sep,
                          chunk, sizeof(chunk));
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
            if (!(d == d) || d < 0) {
                op_text(ex, &(XsltInstr){ .kind = XSLT_INSTR_TEXT,
                                          .text = "NaN" }, node);
                return 0;
            }
            values[0] = (unsigned long)(d + 0.5);
        } else {
            values[0] = 1;
        }
        nv = 1;
    } else if (in->num_level == 2) {
        values[0] = any_number(in, node, ex->source);
        if (!values[0]) return 0;
        nv = 1;
    } else if (in->num_level == 1) {
        /* multiple: every ancestor-or-self matching count, outermost
         * first, stopping above the nearest `from` match. */
        unsigned long rev[64];
        int nr = 0;
        for (LeptrisElement a = node; a && nr < 64;
             a = leptris_node_parent((LeptrisNodeRef)a)) {
            if (count_matches(in, a, node, ex->source))
                rev[nr++] = sibling_number(in, a, ex->source);
            if (from_matches(in, a, ex->source)) break;
        }
        for (int i = 0; i < nr; i++) values[i] = rev[nr - 1 - i];
        nv = nr;
    } else {
        /* single: nearest ancestor-or-self matching count. */
        LeptrisElement target = NULL;
        for (LeptrisElement a = node; a;
             a = leptris_node_parent((LeptrisNodeRef)a)) {
            if (count_matches(in, a, node, ex->source)) { target = a; break; }
            if (from_matches(in, a, ex->source)) break;
        }
        if (!target) return 0;   /* §7.7: no match → nothing emitted */
        values[0] = sibling_number(in, target, ex->source);
        nv = 1;
    }

    /* §7.7 letter-value disambiguator: with an alphabetic token
     * that could mean either, "alphabetic" forces a/A; the default
     * heuristic keeps format-driven behavior. */
    char fmtbuf[128];
    const char* fmt_use = in->num_format ? in->num_format : "1";
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

/* ---- The walker ---- */

int xslt_exec_instrs(XsltExec* ex, const XsltInstr* list,
                     LeptrisElement node) {
    if (ex->terminated) return -1;
    /* §11 block scope: variables pushed while executing this
     * sequence pop when the sequence ends. Re-entrancy: nested
     * sequences snapshot their own mark. */
    XsltVar* scope_mark = ex->vars;
    int rc = 0;
    for (const XsltInstr* in = list; in; in = in->next) {
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
    g_ops[XSLT_INSTR_FOR_EACH] = op_for_each;
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
}

/* ---- Public transform ---- */

void xslt_exec_free(XsltExec* ex) {
    if (!ex) return;
    xslt_keys_free(ex);
    xslt_docs_free(ex);
    xslt_bridge_free(ex);
    while (ex->vars) xslt_pop_var(ex, NULL);
    free(ex->top_text);
    free(ex->tail_text);
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
    ex->result = leptris_document_create();
    if (!ex->result) { xslt_exec_free(ex); return NULL; }

    /* Install the function-bridge state on the SOURCE document for
     * the transform's duration: every XPath eval on this doc (both
     * the VM path and the AST-interpreter path) builds its registry
     * through leptris_xpath_build_custom_registry, which registers
     * key()/current()/format-number()/... with `ex` as user_data
     * while xslt_state is set. Save/restore makes nesting safe. */
    struct leptris_document* src_doc = (struct leptris_document*)source;
    void* saved_xslt_state = src_doc->xslt_state;
    src_doc->xslt_state = ex;

    /* §3.4: strip source whitespace before any template sees it. */
    strip_source_whitespace(ex);

    /* Globals. */
    for (const XsltInstr* g = sheet->globals; g; g = g->next) {
        if (g->kind == XSLT_INSTR_VARIABLE) op_variable(ex, g, NULL);
    }
    if (!ex->terminated) {

    /* Root invocation: the best-matching template for the root
     * element (same selection rule as apply-templates), else the
     * built-in rule (which recurses into children). */
    LeptrisElement root = leptris_document_root(source);
    if (root) {
        const XsltTemplate* root_t = xslt_select_template(ex, root, NULL, 0);
        if (root_t) {
            xslt_invoke_template(ex, root_t, root, NULL);
        } else {
            op_apply_templates(
                ex, &(XsltInstr){ .kind = XSLT_INSTR_APPLY_TEMPLATES },
                root);
        }
    }
    }   /* !terminated */

    src_doc->xslt_state = saved_xslt_state;
    return ex;
}

const char* xslt_exec_take_top_text(XsltExec* ex) {
    return ex ? ex->top_text : NULL;
}
