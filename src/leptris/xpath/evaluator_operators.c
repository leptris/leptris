/* evaluator_operators.c - XPath operator evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * All XPath 1.0 operators: arithmetic, comparison, logical, union
 */

#include "evaluator_internal.h"
#include "../leptris_internal.h"
#include "../dom/element.h"  /* For LeptrisElement structure */
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
/* ---- ISO 8601 dayTimeDuration value model (op:duration batch,
 * lever 6 stage-2). Durations ride the STRING lexicon; these
 * helpers give arithmetic and comparisons a typed view. Strictly
 * dayTime forms only — yearMonth ("P1Y2M") is rejected so it never
 * half-participates. */
/* yearMonthDuration / plain-duration months (Y and M components
 * only — the corpus's equality shapes are Y/M-only or zero). */
int leptris_dur_try_months(const char* s, double* out) {
    if (!s) return 0;
    const char* p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p++ != 'P') return 0;
    double years = 0, months = 0;
    int any = 0;
    while (*p && *p != 'T') {
        char* end = NULL;
        double v = strtod(p, &end);
        if (end == p) return 0;
        if (*end == 'Y') { years = v; any = 1; }
        else if (*end == 'M') { months = v; any = 1; }
        else if (*end == 'D') { if (v != 0) return 0; }
        else return 0;
        p = end + 1;
    }
    /* zero time components are fine (P1Y12M0DT0H0M0S is pure);
     * nonzero ones make it a mixed duration, not months-comparable */
    if (*p == 'T') {
        while (*p) {
            char* end = NULL;
            double v = strtod(p, &end);
            if (end == p) return 0;
            if (v != 0) return 0;
            p = end + 1;
        }
    }
    if (!any) return 0;
    double total = years * 12 + months;
    if (neg) total = -total;
    *out = total;
    return 1;
}

int leptris_dur_try_seconds(const char* s, double* out) {
    if (!s) return 0;
    const char* p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p++ != 'P') return 0;
    double days = 0, hours = 0, mins = 0, secs = 0;
    int any = 0, in_time = 0;
    while (*p) {
        if (*p == 'T') { in_time = 1; p++; continue; }
        char* end = NULL;
        double v = strtod(p, &end);
        if (end == p) return 0;
        char unit = *end;
        if (unit == 'Y' || unit == 'W') return 0;
        if (!in_time) {
            if (unit == 'M') return 0;        /* months: yearMonth */
            if (unit != 'D') return 0;
            days = v;
        } else {
            if (unit == 'H') hours = v;
            else if (unit == 'M') mins = v;
            else if (unit == 'S') secs = v;
            else return 0;
        }
        any = 1;
        p = end + 1;
    }
    if (!any) return 0;
    double total = days * 86400 + hours * 3600 + mins * 60 + secs;
    if (neg) total = -total;
    *out = total;
    return 1;
}

void leptris_dur_format(double secs, char* buf, size_t cap) {
    /* Defensive: out-of-range input formats as zero — the callers
     * range-check, this keeps the (long) casts below defined. */
    if (!isfinite(secs) || fabs(secs) > 8.0e22) secs = 0;
    int neg = signbit(secs);
    if (neg) secs = -secs;
    if (secs == 0) {
        snprintf(buf, cap, "%s", neg ? "-PT0S" : "PT0S");
        return;
    }
    /* long long: MSVC's long is 32-bit and the day count can
     * exceed it for guarded-but-huge durations. */
    long long days = (long long)(secs / 86400);
    double rem = secs - (double)days * 86400.0;
    long long hours = (long long)(rem / 3600);
    rem -= (double)hours * 3600.0;
    long long mins = (long long)(rem / 60);
    rem -= (double)mins * 60.0;
    char sec[32];
    if (rem == (double)(long)rem) {
        snprintf(sec, sizeof sec, "%lldS", (long long)rem);
    } else {
        long long ip = (long long)rem;
        double fr = rem - ip;
        char raw[32];
        snprintf(raw, sizeof raw, "%.9f", fr);
        char* dot = strchr(raw, '.');
        char* frac = dot + 1;
        char* last = frac + strlen(frac) - 1;
        while (last > frac && *last == '0') *last-- = '\0';
        if (*frac == '0' && frac[1] == '\0') frac = (char*)"";
        snprintf(sec, sizeof sec, "%lld.%sS", ip, frac);
    }
    char t[48] = "";
    if (hours) snprintf(t + strlen(t), sizeof t - strlen(t), "%lldH", hours);
    if (mins) snprintf(t + strlen(t), sizeof t - strlen(t), "%lldM", mins);
    /* canonical form drops a bare zero-seconds when a coarser
     * component exists ("PT1H", not "PT1H0S") */
    if (strcmp(sec, "0S") != 0 || (!hours && !mins && !days))
        strcat(t, sec);
    if (days && !hours && !mins && strcmp(sec, "0S") == 0)
        snprintf(buf, cap, "%sP%lldD", neg ? "-" : "", days);
    else if (days)
        snprintf(buf, cap, "%sP%lldDT%s", neg ? "-" : "", days, t);
    else
        snprintf(buf, cap, "%sPT%s", neg ? "-" : "", t);
}



extern char* get_node_text(void* node);

/* ============================================================================
 * Operator Evaluation
 * ============================================================================ */

/* Synthetic sequence member (XSLT 3.0): a text node carrying one
 * member's string form. Freed via the nodeset's
 * owns_synthetic_text. */
XPathTextNode* xpath_synth_text(const char* content, size_t len) {
    XPathTextNode* tn = (XPathTextNode*)calloc(1, sizeof(*tn));
    if (!tn) return NULL;
    char* copy = (char*)malloc(len + 1);
    if (!copy) { free(tn); return NULL; }
    memcpy(copy, content, len);
    copy[len] = 0;
    tn->node_type = LEPTRIS_NODE_TEXT;
    tn->content = copy;
    return tn;
}
#define synth_text xpath_synth_text

/* functions_ext31.c — shared value-level map builder (08). */
void* xpath_map_builder_new(void);
void xpath_map_builder_add(void* b, const char* k, const char* v);
struct leptris_xpath_result* xpath_map_builder_finish(void* b);
char* xpath_map_lookup_result(struct leptris_xpath_result* r,
                              const char* key);

/* Full-lifetime copy of a nodeset: pointers are shared for document
 * nodes, but synthetic text members are DEEP-copied when the source
 * owns them — the copy outlives the source's storage (let bindings
 * are unwound while results referencing them still live). */
/* Apply a zero/one-arg fn per nodeset member (`E/fn()` steps);
 * returns an owned synthetic-text sequence. Shared by the MAP
 * operator and the PATH_EXPR fn-step continuation. */
XPathNodeSet* xpath_map_fn_over(XPathContext* ctx, XPathNodeSet* ns,
                                XPathASTNode* fc) {
    XPathNodeSet* out = xpath_nodeset_new();
    if (!out) return NULL;
    out->owns_synthetic_text = 1;
    out->is_sequence = 1;
    size_t n = ns ? ns->count : 0;
    struct leptris_element* saved_node = ctx->context_node;
    size_t saved_pos = ctx->context_position;
    size_t saved_size = ctx->context_size;
    for (size_t i = 0; i < n; i++) {
        ctx->context_node = (struct leptris_element*)ns->nodes[i];
        ctx->context_position = i + 1;
        ctx->context_size = n;
        struct leptris_xpath_result* item =
            evaluate_expr(ctx, fc);
        if (!item) {
            ctx->context_node = saved_node;
            ctx->context_position = saved_pos;
            ctx->context_size = saved_size;
            xpath_nodeset_free(out);
            return NULL;
        }
        char* piece =
            (ctx->xquery_spelling &&
             item->type == XPATH_RESULT_NUMBER && !item->is_int)
                ? xpath_number_to_string_xq(item->value.number_value)
                : xpath_to_string(item);
        xpath_result_free(item);
        XPathTextNode* tn =
            synth_text(piece ? piece : "", piece ? strlen(piece) : 0);
        free(piece);
        if (tn) xpath_nodeset_add(out, tn);
    }
    ctx->context_node = saved_node;
    ctx->context_position = saved_pos;
    ctx->context_size = saved_size;
    return out;
}

XPathNodeSet* xpath_nodeset_deep_copy(const XPathNodeSet* src) {
    if (!src) return NULL;
    XPathNodeSet* dst = xpath_nodeset_new_with_capacity(src->count);
    if (!dst) return NULL;
    if (src->owns_attributes || src->owns_namespaces) {
        /* Owned attribute/namespace members are CLONED, not
         * shared: the copy can outlive the source (XQuery LET
         * bindings outlive the path result that produced them —
         * sharing left dangling attribute nodes, QT3 group-by
         * keys came out empty). Document nodes stay shared. */
        dst->owns_attributes = src->owns_attributes;
        dst->owns_namespaces = src->owns_namespaces;
        dst->owns_synthetic_text = src->owns_synthetic_text;
        for (size_t i = 0; i < src->count; i++) {
            void* n = src->nodes[i];
            if (!n) continue;
            int ty = XPATH_NODE_TYPE(n);
            if (ty == LEPTRIS_NODE_ATTRIBUTE) {
                LeptrisAttributeNode* a = (LeptrisAttributeNode*)n;
                LeptrisAttributeNode* c =
                    LEPTRIS_ALLOC(LeptrisAttributeNode);
                if (!c) continue;
                memset(c, 0, sizeof(*c));
                c->node_type = LEPTRIS_NODE_ATTRIBUTE;
                c->name = a->name ? leptris_strdup(a->name) : NULL;
                c->value = a->value ? leptris_strdup(a->value) : NULL;
                c->namespace_uri = a->namespace_uri
                                       ? leptris_strdup(a->namespace_uri)
                                       : NULL;
                c->owner = a->owner;
                xpath_nodeset_add(dst, c);
            } else if (ty == LEPTRIS_NODE_NAMESPACE) {
                LeptrisNamespaceNode* ns = (LeptrisNamespaceNode*)n;
                LeptrisNamespaceNode* c =
                    LEPTRIS_ALLOC(LeptrisNamespaceNode);
                if (!c) continue;
                memset(c, 0, sizeof(*c));
                c->node_type = LEPTRIS_NODE_NAMESPACE;
                c->prefix = ns->prefix ? leptris_strdup(ns->prefix)
                                       : NULL;
                c->uri = ns->uri ? leptris_strdup(ns->uri) : NULL;
                c->owner = ns->owner;
                xpath_nodeset_add(dst, c);
            } else if (ty == LEPTRIS_NODE_TEXT &&
                       src->owns_synthetic_text) {
                const char* cc = ((XPathTextNode*)n)->content;
                XPathTextNode* tn =
                    xpath_synth_text(cc ? cc : "", cc ? strlen(cc) : 0);
                if (tn) xpath_nodeset_add(dst, tn);
            } else {
                xpath_nodeset_add(dst, n);
            }
        }
        return dst;
    }
    if (!src->owns_synthetic_text) {
        for (size_t i = 0; i < src->count; i++)
            xpath_nodeset_add(dst, src->nodes[i]);
        return dst;
    }
    dst->owns_synthetic_text = 1;
    for (size_t i = 0; i < src->count; i++) {
        void* n = src->nodes[i];
        if (n && XPATH_NODE_TYPE(n) == LEPTRIS_NODE_TEXT) {
            /* RAW content copy — the \x03N numeric marker is part
             * of the internal value (get_node_text strips it for
             * public string consumers; deep copies must not). */
            const char* c = ((XPathTextNode*)n)->content;
            XPathTextNode* tn =
                xpath_synth_text(c ? c : "", c ? strlen(c) : 0);
            if (tn) xpath_nodeset_add(dst, tn);
        } else {
            xpath_nodeset_add(dst, n);
        }
    }
    return dst;
}

/* §3.4 relational compare over a double pair. */
static int op_relational_cmp(XPathOperatorType op, double a, double b) {
    switch (op) {
        case XPATH_OP_LESS:          return a <  b;
        case XPATH_OP_LESS_EQUAL:    return a <= b;
        case XPATH_OP_GREATER:       return a >  b;
        case XPATH_OP_GREATER_EQUAL: return a >= b;
        default: return 0;
    }
}

/* Per-member SequenceType classification — shared by
 * `instance of` and XQuery typeswitch. */
int xpath_result_matches_type(struct leptris_xpath_result* v,
                              const char* base) {
    int is_string_ty = strcmp(base, "xs:string") == 0 ||
                       strcmp(base, "xs:anyURI") == 0 ||
                       strncmp(base, "xs:date", 7) == 0 ||
                       strcmp(base, "xs:time") == 0 ||
                       strcmp(base, "xs:duration") == 0;
    int is_bool_ty = strcmp(base, "xs:boolean") == 0;
    int is_num_ty = !is_string_ty && !is_bool_ty &&
                    strcmp(base, "node()") != 0 &&
                    strcmp(base, "item()") != 0 &&
                    strcmp(base, "element()") != 0 &&
                    strcmp(base, "attribute()") != 0 &&
                    strcmp(base, "text()") != 0 &&
                    strcmp(base, "comment()") != 0 &&
                    strcmp(base, "processing-instruction()") != 0;
    if (v->type == XPATH_RESULT_NODESET && v->value.nodeset_value) {
        XPathNodeSet* ns = v->value.nodeset_value;
        for (size_t i = 0; i < ns->count; i++) {
            void* n = ns->nodes[i];
            int tag = n ? (int)XPATH_NODE_TYPE(n) : -1;
            const char* mc =
                (tag == (int)LEPTRIS_NODE_TEXT && n)
                    ? ((XPathTextNode*)n)->content : NULL;
            int is_num_member = mc && mc[0] == '\x03' &&
                                (mc[1] == 'N' ||
                                 (mc[1] == 'F' &&
                                  !((mc[2] == 'N' && mc[3] == '\x02') ||
                                    mc[2] == 'R')));
            if (strcmp(base, "item()") == 0) {
                /* every member is an item */
            } else if (strcmp(base, "node()") == 0) {
                if (!(tag >= 0 && tag <= 7)) return 0;
            } else if (strcmp(base, "element()") == 0) {
                if (tag != (int)LEPTRIS_NODE_ELEMENT) return 0;
            } else if (strcmp(base, "attribute()") == 0) {
                if (tag != (int)LEPTRIS_NODE_ATTRIBUTE) return 0;
            } else if (strcmp(base, "text()") == 0) {
                if (!(tag == 1 || tag == 3)) return 0;
            } else if (strcmp(base, "comment()") == 0) {
                if (tag != 2) return 0;
            } else if (strcmp(base, "processing-instruction()") == 0) {
                if (tag != 4) return 0;
            } else if (is_string_ty) {
                if (!(tag == (int)LEPTRIS_NODE_TEXT && !is_num_member))
                    return 0;
            } else if (is_bool_ty) {
                return 0;
            } else if (is_num_ty) {
                if (!is_num_member) return 0;
            } else {
                return 0;
            }
        }
        return 1;
    }
    if (is_string_ty) return v->type == XPATH_RESULT_STRING;
    if (is_bool_ty) return v->type == XPATH_RESULT_BOOLEAN;
    if (is_num_ty) return v->type == XPATH_RESULT_NUMBER;
    if (strcmp(base, "item()") == 0) return 1;
    return 0;   /* node kinds: a scalar is not a node */
}

/* XSD numeric lexical check: trimmed, converts whole (integer
 * targets additionally reject '.'-bearing forms via the caller's
 * truncation). Used by the cast operator (#790). */
static int xq_numeric_lexical(const char* s) {
    while (isspace((unsigned char)*s)) s++;
    const char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    if (e <= s) return 0;
    char* endp = NULL;
    strtod(s, &endp);
    return endp == e;
}

struct leptris_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast) {
    if (!ast) return NULL;
    XPathOperatorType op0 = (XPathOperatorType)ast->number_value;
    /* FN_REF is the one operator with no children; it must run
     * before the arity guard below. */
    if (op0 == XPATH_OP_FN_REF && ast->type == XPATH_AST_OPERATOR) {
        size_t rl = ast->value ? strlen(ast->value) : 0;
        char content[192];
        if (rl + 4 >= sizeof(content)) return NULL;
        memcpy(content, "\x03" "FR", 3);
        if (rl) memcpy(content + 3, ast->value, rl);
        content[3 + rl] = 0;
        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) return NULL;
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        XPathTextNode* tn = synth_text(content, rl + 3);
        if (!tn) { xpath_nodeset_free(out); return NULL; }
        xpath_nodeset_add(out, tn);
        struct leptris_xpath_result* r =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!r) { xpath_nodeset_free(out); return NULL; }
        r->value.nodeset_value = out;
        return r;
    }
    /* Legal zero-child forms: SEQUENCE (the `()` literal and the
     * where-desugar's else-arm) and EMPTY computed constructors
     * (`element n { }`, `attribute n { }`, `text { }`,
     * `document { }` — #684). */
    if (ast->child_count < 1 && op0 != XPATH_OP_SEQUENCE &&
        op0 != XPATH_OP_ELEMENT_CTOR &&
        op0 != XPATH_OP_ATTRIBUTE_CTOR &&
        op0 != XPATH_OP_TEXT_CTOR &&
        op0 != XPATH_OP_DOCUMENT_CTOR &&
        op0 != XPATH_OP_ARRAY_CONSTRUCTOR &&
        op0 != XPATH_OP_MAP_CONSTRUCTOR)
        return NULL;

    XPathOperatorType op = (XPathOperatorType)ast->number_value;

    /* XSLT 3.0 conditional (XPath 2.0+): lazy — evaluate the
     * condition, then only the chosen branch. */
    if (op == XPATH_OP_IF) {
        if (ast->child_count < 3) return NULL;
        struct leptris_xpath_result* cond = evaluate_expr(ctx, ast->children[0]);
        if (!cond) return NULL;
        int truth = xpath_to_boolean(cond);
        xpath_result_free(cond);
        return evaluate_expr(ctx, truth ? ast->children[1]
                                        : ast->children[2]);
    }

    /* XSLT 3.0 `for $v in DOMAIN return EXPR` (XPath 2.0+): iterate
     * the domain, bind $v per iteration, evaluate EXPR; the results
     * join space-separated (the sequence's string form). */
    if (op == XPATH_OP_FOR) {
        if (ast->child_count < 2 || !ast->value) return NULL;
        /* order-by keys ride as key,NUMBER-flag pairs after the
         * return child (expression-level for) */
        typedef struct { char** keys; XPathTextNode* tn; } ObItem;
        size_t n_obk = (ast->child_count > 2)
                           ? (ast->child_count - 2) / 2 : 0;
        ObItem* obitems = NULL;
        size_t n_obit = 0;
        unsigned ob_strmask = 0;   /* bit k: key k is xs:string-typed
                                    * (codepoint order) */
        /* A bare eval context carries no variable set — own a
         * scratch one for the loop binding. */
        XPathVariableSet* scratch = NULL;
        if (!ctx->variable_set) {
            scratch = xpath_variable_set_new();
            if (!scratch) return NULL;
            ctx->variable_set = scratch;
        }
        struct leptris_xpath_result* domain =
            evaluate_expr(ctx, ast->children[0]);
        if (!domain) {
            if (scratch) ctx->variable_set = NULL;
            xpath_variable_set_free(scratch);
            return NULL;
        }

        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) {
            xpath_result_free(domain);
            if (scratch) ctx->variable_set = NULL;
            xpath_variable_set_free(scratch);
            return NULL;
        }
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;

        XPathNodeSet* ns =
            (domain->type == XPATH_RESULT_NODESET)
                ? domain->value.nodeset_value : NULL;
        size_t n = ns ? ns->count : 1;
        /* ->value may carry "var\x01pos" (XQuery `for $x at $p`). */
        const char* pos_sep = strchr(ast->value, '\x01');
        char var_buf[128];
        if (pos_sep) {
            size_t vl = (size_t)(pos_sep - ast->value);
            if (vl >= sizeof(var_buf)) vl = sizeof(var_buf) - 1;
            memcpy(var_buf, ast->value, vl);
            var_buf[vl] = 0;
        }
        const char* loop_var =
            pos_sep ? var_buf : ast->value;
        const char* pos_var = pos_sep ? pos_sep + 1 : NULL;
        /* Shadowed bindings (XQuery scoping): the loop var may
         * already be bound by an outer for/let or a function
         * parameter. Rebinding over the existing entry would
         * clobber its payload — leaking the nodeset (the ASAN
         * trail on QT3 fn-string-join). Snapshot, remove, restore
         * after the loop: the LET operator's discipline. */
        typedef struct {
            int had;
            XPathVariableType type;
            double num;
            int b;
            char* str;             /* owned strdup */
            XPathNodeSet* ns;      /* owned deep copy */
        } ForShadow;
        ForShadow sv = {0, XPATH_VAR_TYPE_NONE, 0, 0, NULL, NULL};
        ForShadow psv = {0, XPATH_VAR_TYPE_NONE, 0, 0, NULL, NULL};
        {
            XPathVariable* old =
                xpath_variable_set_get(ctx->variable_set, loop_var);
            if (old) {
                sv.had = 1;
                sv.type = old->value.type;
                switch (old->value.type) {
                    case XPATH_VAR_TYPE_NUMBER:
                        sv.num = old->value.v.number_value;
                        break;
                    case XPATH_VAR_TYPE_BOOLEAN:
                        sv.b = old->value.v.boolean_value;
                        break;
                    case XPATH_VAR_TYPE_STRING:
                        sv.str = leptris_strdup(
                            old->value.v.string_value
                                ? old->value.v.string_value : "");
                        break;
                    case XPATH_VAR_TYPE_NODE_SET:
                        sv.ns = xpath_nodeset_deep_copy(
                            old->value.v.nodeset_value);
                        break;
                    default:
                        break;
                }
                xpath_variable_set_remove(ctx->variable_set, loop_var);
            }
            if (pos_var) {
                XPathVariable* pold =
                    xpath_variable_set_get(ctx->variable_set, pos_var);
                if (pold) {
                    psv.had = 1;
                    psv.type = pold->value.type;
                    switch (pold->value.type) {
                        case XPATH_VAR_TYPE_NUMBER:
                            psv.num = pold->value.v.number_value;
                            break;
                        case XPATH_VAR_TYPE_BOOLEAN:
                            psv.b = pold->value.v.boolean_value;
                            break;
                        case XPATH_VAR_TYPE_STRING:
                            psv.str = leptris_strdup(
                                pold->value.v.string_value
                                    ? pold->value.v.string_value : "");
                            break;
                        case XPATH_VAR_TYPE_NODE_SET:
                            psv.ns = xpath_nodeset_deep_copy(
                                pold->value.v.nodeset_value);
                            break;
                        default:
                            break;
                    }
                    xpath_variable_set_remove(ctx->variable_set, pos_var);
                }
            }
        }
        for (size_t i = 0; i < n; i++) {
            XPathVariable* var = xpath_variable_set_add(
                ctx->variable_set, loop_var, XPATH_VAR_TYPE_NODE_SET);
            if (!var) break;
            XPathNodeSet* one = xpath_nodeset_new();
            if (!one) break;
            if (ns) {
                xpath_nodeset_add(one, ns->nodes[i]);
            } else {
                char* sv = xpath_to_string(domain);
                if (domain->type == XPATH_RESULT_NUMBER && sv) {
                    /* numeric marker — instance of / typeswitch */
                    size_t sl = strlen(sv);
                    char* marked = (char*)malloc(sl + 3);
                    if (marked) {
                        marked[0] = '\x03';
                        marked[1] = 'N';
                        memcpy(marked + 2, sv, sl + 1);
                        XPathTextNode* tn =
                            synth_text(marked, sl + 2);
                        free(marked);
                        if (tn) xpath_nodeset_add(one, tn);
                    }
                    free(sv);
                } else {
                    XPathTextNode* tn =
                        synth_text(sv ? sv : "", sv ? strlen(sv) : 0);
                    free(sv);
                    if (tn) xpath_nodeset_add(one, tn);
                }
                /* synth members above are var-owned */
                one->owns_synthetic_text = 1;
            }
            xpath_variable_set_nodeset(var, one);
            if (pos_var) {
                char nb[24];
                int nl = snprintf(nb, sizeof(nb), "\x03N%zu", i + 1);
                XPathNodeSet* pone = xpath_nodeset_new();
                if (pone) {
                    pone->owns_synthetic_text = 1;
                    XPathTextNode* ptn = synth_text(nb, (size_t)nl);
                    if (ptn) xpath_nodeset_add(pone, ptn);
                    XPathVariable* pvar = xpath_variable_set_add(
                        ctx->variable_set, pos_var,
                        XPATH_VAR_TYPE_NODE_SET);
                    if (pvar) xpath_variable_set_nodeset(pvar, pone);
                    else xpath_nodeset_free(pone);
                }
            }

            struct leptris_xpath_result* item =
                evaluate_expr(ctx, ast->children[1]);
            if (item) {
                /* Sequence semantics (#814): an EMPTY result —
                 * notably the where-desugar's `()` else-arm —
                 * contributes NOTHING; it must not stringify into
                 * an empty-string member (Saxon drops it). */
                if (item->type == XPATH_RESULT_NODESET &&
                    (!item->value.nodeset_value ||
                     item->value.nodeset_value->count == 0)) {
                    xpath_result_free(item);
                    if (pos_var)
                        xpath_variable_set_remove(
                            ctx->variable_set, pos_var);
                    xpath_variable_set_remove(
                        ctx->variable_set, loop_var);
                    continue;
                }
                /* a multi-member sequence result spreads: one member
                 * per output item (K2: `return ($i, 2)` is TWO
                 * members, not their first) */
                if (item->type == XPATH_RESULT_NODESET &&
                    item->value.nodeset_value &&
                    item->value.nodeset_value->count > 1) {
                    XPathNodeSet* sq = item->value.nodeset_value;
                    for (size_t si = 0; si < sq->count; si++) {
                        char* piece = get_node_text(sq->nodes[si]);
                        XPathTextNode* tn =
                            synth_text(piece ? piece : "",
                                       piece ? strlen(piece) : 0);
                        free(piece);
                        if (!tn) continue;
                        if (n_obk) {
                            /* order-by collect (keys, member) */
                            char** ks =
                                (char**)calloc(n_obk, sizeof(char*));
                            if (!ks) {
                                xpath_nodeset_add(out, tn);
                                continue;
                            }
                            for (size_t ki = 0; ki < n_obk; ki++) {
                                struct leptris_xpath_result* kr =
                                    evaluate_expr(
                                        ctx,
                                        ast->children[2 + ki * 2]);
                                char* kv =
                                    kr ? xpath_to_string(kr) : NULL;
                                if (n_obit == 0 && kr &&
                                    kr->type == XPATH_RESULT_STRING)
                                    ob_strmask |= 1u << ki;
                                if (kr) xpath_result_free(kr);
                                ks[ki] = kv ? kv
                                            : leptris_strdup("");
                            }
                            ObItem* grown = (ObItem*)realloc(
                                obitems,
                                (n_obit + 1) * sizeof(ObItem));
                            if (grown) {
                                obitems = grown;
                                obitems[n_obit].keys = ks;
                                obitems[n_obit].tn = tn;
                                n_obit++;
                            } else {
                                for (size_t ki = 0; ki < n_obk; ki++)
                                    free(ks[ki]);
                                free(ks);
                                xpath_nodeset_add(out, tn);
                            }
                        } else {
                            xpath_nodeset_add(out, tn);
                        }
                    }
                    xpath_result_free(item);
                    if (pos_var)
                        xpath_variable_set_remove(
                            ctx->variable_set, pos_var);
                    xpath_variable_set_remove(
                        ctx->variable_set, loop_var);
                    continue;
                }
                char* piece =
                    (ctx->xquery_spelling &&
                     item->type == XPATH_RESULT_NUMBER && !item->is_int)
                        ? xpath_number_to_string_xq(
                              item->value.number_value)
                        : xpath_to_string(item);
                xpath_result_free(item);
                XPathTextNode* tn = synth_text(piece ? piece : "",
                                               piece ? strlen(piece) : 0);
                free(piece);
                if (n_obk && tn) {
                    /* order-by: collect (keys, member) — the sort
                     * runs after the loop; keys evaluated with the
                     * loop variable still bound */
                    char** ks = (char**)calloc(n_obk, sizeof(char*));
                    if (ks) {
                        int ok2 = 1;
                        for (size_t ki = 0; ki < n_obk && ok2; ki++) {
                            struct leptris_xpath_result* kr =
                                evaluate_expr(ctx, ast->children[2 + ki * 2]);
                            char* kv = kr ? xpath_to_string(kr) : NULL;
                            if (n_obit == 0 && kr &&
                                kr->type == XPATH_RESULT_STRING)
                                ob_strmask |= 1u << ki;
                            if (kr) xpath_result_free(kr);
                            ks[ki] = kv ? kv : leptris_strdup("");
                            if (!ks[ki]) ok2 = 0;
                        }
                        if (ok2) {
                            ObItem* grown = (ObItem*)realloc(
                                obitems,
                                (n_obit + 1) * sizeof(ObItem));
                            if (grown) {
                                obitems = grown;
                                obitems[n_obit].keys = ks;
                                obitems[n_obit].tn = tn;
                                n_obit++;
                                ks = NULL;
                                tn = NULL;
                            }
                        }
                        for (size_t ki = 0; ki < n_obk; ki++)
                            if (ks && ks[ki]) {
                                free(ks[ki]);
                                ks[ki] = NULL;
                            }
                        free(ks);
                    }
                    if (tn) xpath_nodeset_add(out, tn);
                } else if (tn) {
                    xpath_nodeset_add(out, tn);
                }
            }
            /* The variable OWNS the nodeset after set_nodeset —
             * remove frees it; do not double-free. */
            if (pos_var)
                xpath_variable_set_remove(ctx->variable_set, pos_var);
            xpath_variable_set_remove(ctx->variable_set, loop_var);
        }
        /* order-by: stable insertion sort over the collected
         * (keys, member) pairs, then append in order. Flags: bit0
         * descending, bit1 empty-least (default greatest). */
        if (n_obit > 1) {
            for (size_t i = 1; i < n_obit; i++) {
                ObItem tmp = obitems[i];
                size_t j = i;
                while (j > 0) {
                    int cmp = 0;
                    for (size_t k = 0; k < n_obk && !cmp; k++) {
                        int flag = (int)ast->children[3 + k * 2]
                                       ->number_value;
                        const char* ka = obitems[j - 1].keys[k];
                        const char* kb = tmp.keys[k];
                        int c;
                        int ea = !ka || !ka[0];
                        int eb = !kb || !kb[0];
                        if (ea || eb) {
                            if (ea && eb)
                                c = 0;
                            else if (flag & 2)
                                c = ea ? -1 : 1;
                            else
                                c = ea ? 1 : -1;
                        } else if ((ob_strmask >> k) & 1) {
                            c = strcmp(ka, kb);
                        } else {
                            char *ea2 = NULL, *eb2 = NULL;
                            double va = strtod(ka, &ea2);
                            double vb = strtod(kb, &eb2);
                            int na2 = ea2 && *ea2 == '\0' && ea2 != ka;
                            int nb2 = eb2 && *eb2 == '\0' && eb2 != kb;
                            if (na2 && nb2) {
                                int ana = (va != va);
                                int bnb = (vb != vb);
                                if (ana || bnb) {
                                    if (ana && bnb)
                                        c = 0;
                                    else
                                        c = ana ? -1 : 1;
                                } else {
                                    c = (va < vb) ? -1
                                                  : (va > vb) ? 1 : 0;
                                }
                            } else {
                                c = strcmp(ka, kb);
                            }
                        }
                        cmp = (flag & 1) ? -c : c;
                    }
                    if (cmp > 0) {
                        obitems[j] = obitems[j - 1];
                        j--;
                    } else {
                        break;
                    }
                }
                obitems[j] = tmp;
            }
        }
        for (size_t i = 0; i < n_obit; i++) {
            if (obitems[i].tn) xpath_nodeset_add(out, obitems[i].tn);
            for (size_t k = 0; k < n_obk; k++) free(obitems[i].keys[k]);
            free(obitems[i].keys);
        }
        free(obitems);

        /* Restore the shadowed bindings (snapshot above); entries
         * that fail to re-bind fall through to the frees below. */
        if (sv.had) {
            XPathVariable* var = xpath_variable_set_add(
                ctx->variable_set, loop_var, sv.type);
            if (var) {
                switch (sv.type) {
                    case XPATH_VAR_TYPE_NUMBER:
                        xpath_variable_set_number(var, sv.num);
                        break;
                    case XPATH_VAR_TYPE_BOOLEAN:
                        xpath_variable_set_boolean(var, sv.b);
                        break;
                    case XPATH_VAR_TYPE_STRING:
                        xpath_variable_set_string(var,
                            sv.str ? sv.str : "");
                        break;
                    case XPATH_VAR_TYPE_NODE_SET:
                        xpath_variable_set_nodeset(var, sv.ns);
                        sv.ns = NULL;   /* transferred */
                        break;
                    default:
                        break;
                }
            }
        }
        if (psv.had) {
            XPathVariable* var = xpath_variable_set_add(
                ctx->variable_set, pos_var, psv.type);
            if (var) {
                switch (psv.type) {
                    case XPATH_VAR_TYPE_NUMBER:
                        xpath_variable_set_number(var, psv.num);
                        break;
                    case XPATH_VAR_TYPE_BOOLEAN:
                        xpath_variable_set_boolean(var, psv.b);
                        break;
                    case XPATH_VAR_TYPE_STRING:
                        xpath_variable_set_string(var,
                            psv.str ? psv.str : "");
                        break;
                    case XPATH_VAR_TYPE_NODE_SET:
                        xpath_variable_set_nodeset(var, psv.ns);
                        psv.ns = NULL;   /* transferred */
                        break;
                    default:
                        break;
                }
            }
        }
        if (sv.str) free(sv.str);
        if (sv.ns) xpath_nodeset_free(sv.ns);
        if (psv.str) free(psv.str);
        if (psv.ns) xpath_nodeset_free(psv.ns);
        xpath_result_free(domain);
        if (scratch) ctx->variable_set = NULL;
        xpath_variable_set_free(scratch);

        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
    }

    /* XPath 2.0 quantified expressions (#684): cartesian product
     * over the BINDING domains; SOME short-circuits on the first
     * satisfying tuple, EVERY on the first failing one (an empty
     * domain makes SOME false and EVERY vacuously true). Children:
     * XPATH_OP_BINDING nodes (value = var name, child = domain)
     * then the test expression last. */
    if (op == XPATH_OP_SOME || op == XPATH_OP_EVERY) {
        if (ast->child_count < 2) return NULL;
        enum { MAXB = 16 };
        size_t nb = ast->child_count - 1;
        if (nb > MAXB) nb = MAXB;
        XPathASTNode* test = ast->children[ast->child_count - 1];

        XPathVariableSet* scratch = NULL;
        if (!ctx->variable_set) {
            scratch = xpath_variable_set_new();
            if (!scratch) return NULL;
            ctx->variable_set = scratch;
        }

        /* Domains evaluate once. */
        struct leptris_xpath_result* dom[MAXB] = {0};
        int ok = 1;
        for (size_t b = 0; b < nb && ok; b++) {
            XPathASTNode* bind = ast->children[b];
            if ((XPathOperatorType)bind->number_value !=
                    XPATH_OP_BINDING ||
                !bind->value || bind->child_count < 1) {
                ok = 0;
                break;
            }
            dom[b] = evaluate_expr(ctx, bind->children[0]);
            if (!dom[b]) ok = 0;
        }

        int want = (op == XPATH_OP_SOME);
        int outcome = !want;
        if (ok) {
            size_t dn[MAXB] = {0};
            for (size_t b = 0; b < nb; b++)
                dn[b] = (dom[b]->type == XPATH_RESULT_NODESET &&
                         dom[b]->value.nodeset_value)
                            ? dom[b]->value.nodeset_value->count
                            : 1;
            /* An empty domain means zero tuples — no bindings,
             * no test evaluations. */
            int any_empty = 0;
            for (size_t b = 0; b < nb && !any_empty; b++)
                any_empty = dn[b] == 0;

            size_t idx[MAXB] = {0};
            for (; !any_empty;) {
                /* Bind every variable to its current item (same
                 * per-item nodeset discipline as FOR: numeric
                 * members carry the \x03N type marker). */
                int bound_all = 1;
                for (size_t b = 0; b < nb && bound_all; b++) {
                    XPathASTNode* bind = ast->children[b];
                    XPathVariable* var = xpath_variable_set_add(
                        ctx->variable_set, bind->value,
                        XPATH_VAR_TYPE_NODE_SET);
                    XPathNodeSet* one = xpath_nodeset_new();
                    if (!var || !one) {
                        if (one) xpath_nodeset_free(one);
                        bound_all = 0;
                        break;
                    }
                    if (dom[b]->type == XPATH_RESULT_NODESET &&
                        dom[b]->value.nodeset_value) {
                        xpath_nodeset_add(
                            one, dom[b]->value.nodeset_value
                                     ->nodes[idx[b]]);
                    } else {
                        char* sv = xpath_to_string(dom[b]);
                        if (dom[b]->type == XPATH_RESULT_NUMBER &&
                            sv) {
                            size_t sl = strlen(sv);
                            char* marked = (char*)malloc(sl + 3);
                            if (marked) {
                                marked[0] = '\x03';
                                marked[1] = 'N';
                                memcpy(marked + 2, sv, sl + 1);
                                XPathTextNode* tn =
                                    synth_text(marked, sl + 2);
                                free(marked);
                                if (tn) {
                                    xpath_nodeset_add(one, tn);
                                    one->owns_synthetic_text = 1;
                                }
                            }
                        } else {
                            XPathTextNode* tn = synth_text(
                                sv ? sv : "", sv ? strlen(sv) : 0);
                            if (tn) {
                                xpath_nodeset_add(one, tn);
                                one->owns_synthetic_text = 1;
                            }
                        }
                        free(sv);
                    }
                    /* set_nodeset frees any previous binding. */
                    xpath_variable_set_nodeset(var, one);
                }

                if (bound_all) {
                    struct leptris_xpath_result* t =
                        evaluate_expr(ctx, test);
                    int truth = t ? xpath_to_boolean(t) : 0;
                    if (t) xpath_result_free(t);
                    if (truth == want) {
                        outcome = want;
                        break;
                    }
                } else {
                    break;
                }

                /* Odometer: advance the LAST binding, carrying. */
                size_t b = nb;
                while (b > 0) {
                    b--;
                    if (++idx[b] < dn[b]) break;
                    idx[b] = 0;
                }
                if (b == 0 && idx[0] == 0) break;   /* wrapped */
            }

            /* Unbind every distinct name (reverse order; a repeated
             * name is only removed once — remove frees the entry). */
            for (size_t b = nb; b > 0; b--) {
                const char* nm = ast->children[b - 1]->value;
                int dup = 0;
                for (size_t c = 0; c < b - 1 && !dup; c++)
                    dup = strcmp(ast->children[c]->value, nm) == 0;
                if (!dup)
                    xpath_variable_set_remove(ctx->variable_set, nm);
            }
        }

        for (size_t b = 0; b < nb; b++)
            if (dom[b]) xpath_result_free(dom[b]);
        if (scratch) ctx->variable_set = NULL;
        xpath_variable_set_free(scratch);

        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (!result) return NULL;
        result->value.boolean_value = outcome;
        return result;
    }

    /* XPath 3.1 `let $x := E1, $y := E2 ... return B`: bind each
     * value (each binding sees the earlier ones AND the outer
     * scope), evaluate the body, then restore. Bindings remove and
     * re-add their set entry so a type change rebinds cleanly;
     * snapshots are deep (remove() frees the original). */
    if (op == XPATH_OP_LET) {
        if (ast->child_count < 2 || !ast->value) return NULL;
        size_t nbind = ast->child_count - 1;

        XPathVariableSet* scratch = NULL;
        if (!ctx->variable_set) {
            scratch = xpath_variable_set_new();
            if (!scratch) return NULL;
            ctx->variable_set = scratch;
        }
        XPathVariableSet* set = (XPathVariableSet*)ctx->variable_set;

        /* Split the space-joined names. */
        char** names = (char**)calloc(nbind, sizeof(char*));
        size_t* name_lens = (size_t*)calloc(nbind, sizeof(size_t));
        if (!names || !name_lens) {
            free(names); free(name_lens);
            if (scratch) { ctx->variable_set = NULL;
                           xpath_variable_set_free(scratch); }
            return NULL;
        }
        const char* p = ast->value;
        for (size_t i = 0; i < nbind; i++) {
            names[i] = (char*)p;
            while (*p && *p != ' ') p++;
            name_lens[i] = (size_t)(p - names[i]);
            if (*p == ' ') p++;
        }

        typedef struct {
            int had;
            XPathVariableType type;
            double num;
            int b;
            char* str;             /* owned strdup */
            XPathNodeSet* ns;      /* owned deep copy */
        } LetSave;
        LetSave* saves = (LetSave*)calloc(nbind, sizeof(LetSave));
        if (!saves) {
            free(names); free(name_lens);
            if (scratch) { ctx->variable_set = NULL;
                           xpath_variable_set_free(scratch); }
            return NULL;
        }

        size_t bound = 0;
        int failed = 0;
        struct leptris_xpath_result* body = NULL;

        for (; bound < nbind; bound++) {
            /* Null-terminate the name over a stack copy. */
            char name[128];
            if (name_lens[bound] >= sizeof(name)) { failed = 1; break; }
            memcpy(name, names[bound], name_lens[bound]);
            name[name_lens[bound]] = '\0';

            struct leptris_xpath_result* v =
                evaluate_expr(ctx, ast->children[bound]);
            if (!v) { failed = 1; break; }

            /* Snapshot the shadowed binding (deep: remove frees). */
            XPathVariable* old = xpath_variable_set_get(set, name);
            if (old) {
                saves[bound].had = 1;
                saves[bound].type = old->value.type;
                switch (old->value.type) {
                    case XPATH_VAR_TYPE_NUMBER:
                        saves[bound].num = old->value.v.number_value;
                        break;
                    case XPATH_VAR_TYPE_BOOLEAN:
                        saves[bound].b = old->value.v.boolean_value;
                        break;
                    case XPATH_VAR_TYPE_STRING:
                        saves[bound].str = leptris_strdup(
                            old->value.v.string_value
                                ? old->value.v.string_value : "");
                        if (!saves[bound].str) failed = 1;
                        break;
                    case XPATH_VAR_TYPE_NODE_SET:
                        saves[bound].ns = xpath_nodeset_deep_copy(
                            old->value.v.nodeset_value);
                        if (old->value.v.nodeset_value &&
                            !saves[bound].ns)
                            failed = 1;
                        break;
                    default: break;
                }
            }
            if (failed) { xpath_result_free(v); break; }

            xpath_variable_set_remove(set, name);

            XPathVariableType vt;
            switch (v->type) {
                case XPATH_RESULT_BOOLEAN: vt = XPATH_VAR_TYPE_BOOLEAN; break;
                case XPATH_RESULT_NUMBER:  vt = XPATH_VAR_TYPE_NUMBER;  break;
                case XPATH_RESULT_NODESET: vt = XPATH_VAR_TYPE_NODE_SET; break;
                default:                   vt = XPATH_VAR_TYPE_STRING;   break;
            }
            XPathVariable* var = xpath_variable_set_add(set, name, vt);
            if (!var) { bound++; failed = 1; break; }
            int ok = 0;
            switch (vt) {
                case XPATH_VAR_TYPE_BOOLEAN:
                    ok = xpath_variable_set_boolean(
                        var, v->value.boolean_value);
                    break;
                case XPATH_VAR_TYPE_NUMBER:
                    ok = xpath_variable_set_number(
                        var, v->value.number_value);
                    break;
                case XPATH_VAR_TYPE_NODE_SET:
                    ok = xpath_variable_set_nodeset(
                        var, v->value.nodeset_value);
                    if (ok) v->value.nodeset_value = NULL;  /* moved */
                    break;
                default:
                    ok = xpath_variable_set_string(
                        var, v->value.string_value
                                 ? v->value.string_value : "");
                    break;
            }
            xpath_result_free(v);
            if (!ok) { bound++; failed = 1; break; }
        }

        if (!failed) {
            body = evaluate_expr(ctx, ast->children[nbind]);
            if (!body) failed = 1;
        }

        /* Unwind in reverse: drop our entry, restore the snapshot. */
        for (size_t j = bound; j-- > 0;) {
            char name[128];
            if (name_lens[j] >= sizeof(name)) continue;
            memcpy(name, names[j], name_lens[j]);
            name[name_lens[j]] = '\0';
            xpath_variable_set_remove(set, name);
            if (!saves[j].had) continue;
            XPathVariable* var =
                xpath_variable_set_add(set, name, saves[j].type);
            if (!var) continue;
            switch (saves[j].type) {
                case XPATH_VAR_TYPE_NUMBER:
                    xpath_variable_set_number(var, saves[j].num);
                    break;
                case XPATH_VAR_TYPE_BOOLEAN:
                    xpath_variable_set_boolean(var, saves[j].b);
                    break;
                case XPATH_VAR_TYPE_STRING:
                    xpath_variable_set_string(var,
                        saves[j].str ? saves[j].str : "");
                    break;
                case XPATH_VAR_TYPE_NODE_SET:
                    xpath_variable_set_nodeset(var, saves[j].ns);
                    saves[j].ns = NULL;   /* transferred */
                    break;
                default: break;
            }
        }
        for (size_t j = 0; j < nbind; j++) {
            if (saves[j].str) free(saves[j].str);
            if (saves[j].ns) xpath_nodeset_free(saves[j].ns);
        }
        free(saves);
        free(names);
        free(name_lens);

        if (scratch) {
            ctx->variable_set = NULL;
            xpath_variable_set_free(scratch);
        }
        if (failed && body) {
            xpath_result_free(body);
            body = NULL;
        }
        return body;
    }

    /* ---- 3.0 function items (TODO.xslt-full/07) ----
     * Value-level: a closure is ONE synthetic text node —
     * "\x03FN\x02" params-'\x01'-joined "\x02" + the raw body-AST
     * pointer bytes (borrowed from the enclosing compiled
     * expression — transform-lifetime in XSLT; v1 scope). A named
     * reference is "\x03FR" + "name#arity". Args/results are
     * string-typed in this slice. */
    if (op == XPATH_OP_INLINE_FN) {
        size_t plen = ast->value ? strlen(ast->value) : 0;
        size_t cap = plen + 24;
        char* content = (char*)malloc(cap);
        if (!content) return NULL;
        memcpy(content, "\x03" "FN\x02", 4);
        size_t len = 4;
        if (plen) { memcpy(content + len, ast->value, plen); len += plen; }
        content[len++] = '\x02';
        /* The body pointer rides as 16 hex chars: raw pointer bytes
         * carry NULs (high bytes), and the let machinery DEEP-COPIES
         * synthetic nodes strlen-wise — a truncated closure made the
         * call memcpy read past the buffer (ASAN heap-overflow). */
        XPathASTNode* body = ast->children[0];
        len += (size_t)snprintf(content + len, 17, "%016llx",
                                (unsigned long long)(uintptr_t)body);
        cap = len + 2;   /* snprintf may have needed more than 17 */
        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) { free(content); return NULL; }
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        XPathTextNode* tn = synth_text(content, len);
        free(content);
        if (!tn) { xpath_nodeset_free(out); return NULL; }
        xpath_nodeset_add(out, tn);
        struct leptris_xpath_result* r =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!r) { xpath_nodeset_free(out); return NULL; }
        r->value.nodeset_value = out;
        return r;
    }
    if (op == XPATH_OP_DYN_CALL) {
        struct leptris_xpath_result* callee =
            evaluate_expr(ctx, ast->children[0]);
        if (!callee) return NULL;
        const char* cc = NULL;
        if (callee->type == XPATH_RESULT_NODESET &&
            callee->value.nodeset_value &&
            callee->value.nodeset_value->count > 0)
            cc = ((XPathTextNode*)
                      callee->value.nodeset_value->nodes[0])->content;
        if (!cc) { xpath_result_free(callee); return NULL; }
        if (strncmp(cc, "\x03" "FR", 3) == 0) {
            /* Named reference: synthesize a function-call AST over
             * the borrowed arg ASTs and dispatch through the
             * ordinary call path. */
            char name[128];
            snprintf(name, sizeof(name), "%s", cc + 3);
            char* hash = strchr(name, '#');
            if (hash) *hash = 0;
            XPathASTNode fc;
            memset(&fc, 0, sizeof(fc));
            fc.type = XPATH_AST_FUNCTION_CALL;
            fc.value = name;
            fc.children = ast->children + 1;
            fc.child_count = ast->child_count - 1;
            struct leptris_xpath_result* out =
                evaluate_function_call_inline(ctx, &fc);
            xpath_result_free(callee);
            return out;
        }
        if (strncmp(cc, "\x03" "FN", 3) != 0) {
            xpath_result_free(callee);
            return NULL;
        }
        const char* p = cc + 4;
        const char* pe = strchr(p, '\x02');
        if (!pe || pe[1] == 0) {
            xpath_result_free(callee);
            return NULL;
        }
        XPathASTNode* body = (XPathASTNode*)(uintptr_t)strtoull(
            pe + 1, NULL, 16);
        XPathVariableSet* scratch = NULL;
        if (!ctx->variable_set) {
            scratch = xpath_variable_set_new();
            if (!scratch) { xpath_result_free(callee); return NULL; }
            ctx->variable_set = scratch;
        }
        const char* param = p;
        size_t ai = 1;
        size_t bound = 0;
        while (param < pe) {
            const char* ne = strchr(param, '\x01');
            if (!ne || ne > pe) ne = pe;
            char pname[128];
            size_t pn = (size_t)(ne - param);
            if (pn >= sizeof(pname)) pn = sizeof(pname) - 1;
            memcpy(pname, param, pn);
            pname[pn] = 0;
            XPathNodeSet* one = xpath_nodeset_new();
            if (one) {
                /* set_remove frees the nodeset — without this flag
                 * the synthetic arg node leaks (48B, Linux LSan). */
                one->owns_synthetic_text = 1;
                if (ai < (size_t)ast->child_count) {
                    struct leptris_xpath_result* ar =
                        evaluate_expr(ctx, ast->children[ai]);
                    char* sv = ar ? xpath_to_string(ar) : NULL;
                    if (ar) xpath_result_free(ar);
                    XPathTextNode* tn =
                        synth_text(sv ? sv : "", sv ? strlen(sv) : 0);
                    free(sv);
                    if (tn) xpath_nodeset_add(one, tn);
                }
                XPathVariable* var = xpath_variable_set_add(
                    ctx->variable_set, pname, XPATH_VAR_TYPE_NODE_SET);
                if (var) {
                    xpath_variable_set_nodeset(var, one);
                    bound++;
                } else {
                    xpath_nodeset_free(one);
                }
            }
            if (*ne != '\x01') break;
            param = ne + 1;
            ai++;
        }
        struct leptris_xpath_result* out =
            body ? evaluate_expr(ctx, body) : NULL;
        param = p;
        while (bound--) {
            const char* ne = strchr(param, '\x01');
            if (!ne || ne > pe) ne = pe;
            char pname[128];
            size_t pn = (size_t)(ne - param);
            if (pn >= sizeof(pname)) pn = sizeof(pname) - 1;
            memcpy(pname, param, pn);
            pname[pn] = 0;
            xpath_variable_set_remove(ctx->variable_set, pname);
            if (*ne != '\x01') break;
            param = ne + 1;
        }
        if (scratch) {
            ctx->variable_set = NULL;
            xpath_variable_set_free(scratch);
        }
        xpath_result_free(callee);
        return out;
    }

    /* XQuery 3.0 typeswitch: children[0] = operand, then one
     * return per case; the value's trailing empty entry marks the
     * default arm (its return is the last child). */
    if (op == XPATH_OP_TYPESWITCH) {
        struct leptris_xpath_result* v =
            evaluate_expr(ctx, ast->children[0]);
        if (!v) return NULL;
        const char* types = ast->value ? ast->value : "";
        size_t case_i = 1;
        const char* p = types;
        for (; *p || *(p + 1); ) {
            const char* sep = strchr(p, '\x01');
            size_t tlen = sep ? (size_t)(sep - p) : strlen(p);
            if (*p == '\0' || tlen == 0) break;   /* default arm */
            char base[80];
            if (tlen >= sizeof(base)) tlen = sizeof(base) - 1;
            memcpy(base, p, tlen);
            base[tlen] = 0;
            if (xpath_result_matches_type(v, base)) {
                xpath_result_free(v);
                return evaluate_expr(ctx, ast->children[case_i]);
            }
            case_i++;
            if (!sep) break;
            p = sep + 1;
        }
        xpath_result_free(v);
        /* default: the last child */
        return evaluate_expr(ctx, ast->children[ast->child_count - 1]);
    }

    /* XQuery 3.0 try/catch (#692): children[0] = try body,
     * children[1..] = catch bodies; value = name-tests joined by
     * '\x01'. No error-code model yet: "*" catches everything,
     * named tests never match (the error propagates). */
    if (op == XPATH_OP_TRY) {
        struct leptris_xpath_result* v = evaluate_expr(ctx, ast->children[0]);
        if (v) return v;
        const char* tests = ast->value ? ast->value : "";
        char desc_save[256];
        snprintf(desc_save, sizeof(desc_save), "%s", ctx->error_msg);
        const char* sep = strchr(tests, '\x01');
        for (size_t i = 1; i < ast->child_count; i++) {
            size_t tlen = sep ? (size_t)(sep - tests) : strlen(tests);
            int matched = 0;
            if (tlen == 1 && tests[0] == '*') {
                matched = 1;
            } else {
                /* Named test: the error code's local part. */
                const char* colon = memchr(tests, ':', tlen);
                size_t local_len = colon
                    ? tlen - (size_t)(colon - tests) - 1 : tlen;
                const char* local = colon ? colon + 1 : tests;
                if (local_len == strlen(ctx->error_code) &&
                    strncmp(local, ctx->error_code, local_len) == 0)
                    matched = 1;
            }
            if (matched) {
                /* Bind $err:* for the handler. */
                XPathVariableSet* vs = (XPathVariableSet*)ctx->variable_set;
                int created = 0;
                if (!vs) {
                    vs = xpath_variable_set_new();
                    if (!vs) return NULL;
                    ctx->variable_set = vs;
                    created = 1;
                }
                char numbuf[24];
                snprintf(numbuf, sizeof(numbuf), "\x03N0");
                char code_save[32];
                snprintf(code_save, sizeof(code_save), "%s",
                         ctx->error_code);
                const char* bindings[][2] = {
                    {"err:code", code_save},
                    {"err:description", desc_save[0] ? desc_save : "error"},
                    {"err:value", ""},
                };
                for (size_t b = 0; b < 3; b++) {
                    XPathNodeSet* one = xpath_nodeset_new();
                    if (!one) continue;
                    one->owns_synthetic_text = 1;
                    XPathTextNode* tn = xpath_synth_text(
                        bindings[b][1], strlen(bindings[b][1]));
                    if (tn) xpath_nodeset_add(one, tn);
                    XPathVariable* var = xpath_variable_set_add(
                        vs, bindings[b][0], XPATH_VAR_TYPE_NODE_SET);
                    if (var) xpath_variable_set_nodeset(var, one);
                    else xpath_nodeset_free(one);
                }
                (void)numbuf;
                ctx->error_msg[0] = '\0';
                ctx->error_code[0] = '\0';
                struct leptris_xpath_result* out =
                    evaluate_expr(ctx, ast->children[i]);
                for (size_t b = 0; b < 3; b++)
                    xpath_variable_set_remove(vs, bindings[b][0]);
                if (created) {
                    ctx->variable_set = NULL;
                    xpath_variable_set_free(vs);
                }
                return out;
            }
            if (sep) {
                tests = sep + 1;
                sep = strchr(tests, '\x01');
            }
        }
        return NULL;   /* no matching catch: propagate */
    }

    /* document { content } (TODO 11): serialize children with no
     * wrapper tag — the ELEMENT_CTOR content pass minus the tag. */
    if (op == XPATH_OP_DOCUMENT_CTOR) {
        size_t cap = 64, len = 0;
        char* buf = (char*)malloc(cap);
        if (!buf) return NULL;
        buf[0] = 0;
        for (size_t i = 0; i < ast->child_count; i++) {
            struct leptris_xpath_result* v = evaluate_expr(ctx, ast->children[i]);
            if (!v) continue;
            if (v->type == XPATH_RESULT_NODESET && v->value.nodeset_value) {
                for (size_t m = 0; m < v->value.nodeset_value->count; m++) {
                    char* t = get_node_text(v->value.nodeset_value->nodes[m]);
                    if (!t) continue;
                    while (len + strlen(t) + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
                    memcpy(buf + len, t, strlen(t));
                    len += strlen(t);
                    buf[len] = 0;
                    free(t);
                }
            } else {
                char* t = xpath_to_string(v);
                if (t) {
                    while (len + strlen(t) + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
                    memcpy(buf + len, t, strlen(t));
                    len += strlen(t);
                    buf[len] = 0;
                    free(t);
                }
            }
            xpath_result_free(v);
        }
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!out) { free(buf); return NULL; }
        out->value.string_value = buf;
        return out;
    }

    /* ---- XQuery 1.0 constructors (TODO.xslt-full/11): value-level
     * — the result is the serialized XML string. Attribute values
     * escape &<"', text content escapes &<; raw expression content
     * passes through (a nested constructor's result is already
     * markup; arbitrary-string escaping is a value-model limit). */
    /* Computed PI/comment constructors (string-level ctor model):
     * serialize to the XML markup form. PI data is raw (no
     * entity-escaping in the data position); empty content drops
     * the data and the separating space. */
    if (op == XPATH_OP_PI_CTOR || op == XPATH_OP_COMMENT_CTOR) {
        char* s = NULL;
        if (ast->child_count >= 1) {
            struct leptris_xpath_result* v =
                evaluate_expr(ctx, ast->children[0]);
            if (v) s = xpath_to_string(v);
            xpath_result_free(v);
        }
        if (!s) s = leptris_strdup("");
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!out) {
            free(s);
            return NULL;
        }
        const char* target =
            op == XPATH_OP_PI_CTOR && ast->value ? ast->value : "";
        size_t tl = strlen(target);
        size_t dl = strlen(s);
        char* buf = (char*)malloc(tl + dl + 8);
        if (!buf) {
            free(s);
            xpath_result_free(out);
            return NULL;
        }
        if (op == XPATH_OP_PI_CTOR)
            snprintf(buf, tl + dl + 8, "<?%s%s%s?>", target,
                     dl ? " " : "", s);
        else
            snprintf(buf, dl + 8, "<!--%s-->", s);
        out->value.string_value = buf;
        free(s);
        return out;
    }

    if (op == XPATH_OP_TEXT_CTOR || op == XPATH_OP_ATTRIBUTE_CTOR ||
        op == XPATH_OP_ELEMENT_CTOR) {
        if (op != XPATH_OP_ELEMENT_CTOR) {
            char* s = NULL;
            if (ast->child_count >= 1) {
                struct leptris_xpath_result* v =
                    evaluate_expr(ctx, ast->children[0]);
                if (v &&
                    leptris_xpath_result_type(v) ==
                        LEPTRIS_XPATH_NODESET &&
                    leptris_xpath_result_count(v) > 1) {
                    /* text{(a,b,c)}: the atomics join with single
                     * spaces (XQuery 3.0 §3.7.3.1); xpath_to_string
                     * keeps only the first member. */
                    size_t n = leptris_xpath_result_count(v);
                    size_t len = 0;
                    for (size_t i = 0; i < n; i++) {
                        const char* iv =
                            leptris_xpath_result_node_value(v, i);
                        len += (iv ? strlen(iv) : 0) + 1;
                    }
                    s = (char*)malloc(len + 1);
                    if (s) {
                        s[0] = 0;
                        for (size_t i = 0; i < n; i++) {
                            const char* iv =
                                leptris_xpath_result_node_value(v, i);
                            if (i) strcat(s, " ");
                            if (iv) strcat(s, iv);
                        }
                    }
                } else {
                    s = v ? xpath_to_string(v) : NULL;
                }
                if (v) xpath_result_free(v);
            }
            if (!s) s = leptris_strdup("");
            if (op == XPATH_OP_ATTRIBUTE_CTOR) {
                /* Top-level attribute ctor keeps its name: one
                 * synthetic member carrying "\x03A" name "\x01"
                 * value — deep-equal compares name+value
                 * (K2-SeqDeepEqualFunc-25/31/32), string consumers
                 * strip to the value. \x01 cannot occur in XML
                 * content. */
                const char* an = ast->value ? ast->value : "";
                size_t alen = strlen(an), vlen = strlen(s);
                size_t clen = alen + vlen + 3;
                char* carrier = (char*)malloc(clen + 1);
                if (!carrier) { free(s); return NULL; }
                carrier[0] = '\x03';
                carrier[1] = 'A';
                memcpy(carrier + 2, an, alen);
                carrier[2 + alen] = '\x01';
                memcpy(carrier + 3 + alen, s, vlen + 1);
                free(s);
                XPathNodeSet* ns = xpath_nodeset_new();
                if (!ns) { free(carrier); return NULL; }
                ns->owns_synthetic_text = 1;
                ns->is_sequence = 1;
                XPathTextNode* tn = synth_text(carrier, clen);
                free(carrier);
                if (!tn) { xpath_nodeset_free(ns); return NULL; }
                xpath_nodeset_add(ns, tn);
                struct leptris_xpath_result* out =
                    xpath_result_new(XPATH_RESULT_NODESET);
                if (!out) { xpath_nodeset_free(ns); return NULL; }
                out->value.nodeset_value = ns;
                return out;
            }
            struct leptris_xpath_result* out =
                xpath_result_new(XPATH_RESULT_STRING);
            if (!out) { free(s); return NULL; }
            out->value.string_value = s;
            return out;
        }

        /* ELEMENT_CTOR: attribute children first, then content. */
        size_t cap = 64, len = 0;
        char* buf = (char*)malloc(cap);
        if (!buf) return NULL;
        buf[0] = 0;
        const char* name = ast->value ? ast->value : "e";
        len += (size_t)snprintf(buf, cap, "<%s", name);
        /* XQuery default element namespace: unprefixed constructed
         * elements carry it (serialized as the xmlns declaration). */
        if (ctx->xquery_default_ns && ctx->xquery_default_ns[0] &&
            !strchr(name, ':')) {
            size_t need = len + strlen(ctx->xquery_default_ns) + 11;
            while (need + 1 > cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); return NULL; }
                buf = nb;
            }
            len += (size_t)snprintf(buf + len, cap - len,
                                    " xmlns=\"%s\"",
                                    ctx->xquery_default_ns);
        }
        for (size_t i = 0; i < ast->child_count; i++) {
            XPathASTNode* c = ast->children[i];
            size_t attr_n = 1;
            XPathASTNode** attrs = &c;
            if (c->type == XPATH_AST_OPERATOR &&
                (XPathOperatorType)c->number_value == XPATH_OP_SEQUENCE) {
                attrs = c->children;
                attr_n = c->child_count;
            }
            for (size_t at = 0; at < attr_n; at++) {
            XPathASTNode* ca = attrs[at];
            if (ca->type == XPATH_AST_OPERATOR &&
                (XPathOperatorType)ca->number_value ==
                    XPATH_OP_ATTRIBUTE_CTOR) {
                struct leptris_xpath_result* av =
                    evaluate_expr(ctx, ca->children ? ca->children[0]
                                                    : NULL);
                char* v = av ? xpath_to_string(av) : NULL;
                if (av) xpath_result_free(av);
                if (!v) v = leptris_strdup("");
                size_t need = len + strlen(name) + strlen(v) * 6 + 8;
                while (need + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
                buf[len++] = ' ';
                memcpy(buf + len, c->value, strlen(c->value));
                len += strlen(c->value);
                buf[len++] = '=';
                buf[len++] = '"';
                for (const char* q = v; *q; q++) {
                    if (*q == '&') { memcpy(buf + len, "&amp;", 5); len += 5; }
                    else if (*q == '<') { memcpy(buf + len, "&lt;", 4); len += 4; }
                    else if (*q == '"') { memcpy(buf + len, "&quot;", 6); len += 6; }
                    else buf[len++] = *q;
                }
                buf[len++] = '"';
                buf[len] = 0;
                free(v);
            }
            }
        }
        buf[len] = 0;
        size_t content_start_len = len;
        buf[len++] = '>';
        buf[len] = 0;
        /* Comma-separated ctor bodies arrive as SEQUENCE nodes —
         * flatten one level so attribute/content children are seen
         * directly. */
        for (size_t i = 0; i < ast->child_count; i++) {
            XPathASTNode* c = ast->children[i];
            size_t item_n = 1;
            XPathASTNode** items = &c;
            if (c->type == XPATH_AST_OPERATOR &&
                (XPathOperatorType)c->number_value == XPATH_OP_SEQUENCE) {
                items = c->children;
                item_n = c->child_count;
            }
            for (size_t it = 0; it < item_n; it++) {
            XPathASTNode* ci = items[it];
            if (ci->type == XPATH_AST_OPERATOR &&
                (XPathOperatorType)ci->number_value ==
                    XPATH_OP_ATTRIBUTE_CTOR)
                continue;
            int escape = ci->type == XPATH_AST_OPERATOR &&
                         (XPathOperatorType)ci->number_value ==
                             XPATH_OP_TEXT_CTOR;
            struct leptris_xpath_result* v = evaluate_expr(ctx, ci);
            if (!v) continue;
            if (v->type == XPATH_RESULT_NODESET &&
                v->value.nodeset_value) {
                /* Sequence: members concatenate with no separator
                 * (adjacent constructed items). */
                for (size_t m = 0; m < v->value.nodeset_value->count;
                     m++) {
                    char* t = get_node_text(
                        v->value.nodeset_value->nodes[m]);
                    if (!t) continue;
                    for (const char* q = t; *q; q++) {
                        while (len + 8 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
                        if (escape && *q == '&') { memcpy(buf + len, "&amp;", 5); len += 5; }
                        else if (escape && *q == '<') { memcpy(buf + len, "&lt;", 4); len += 4; }
                        else buf[len++] = *q;
                    }
                    buf[len] = 0;
                    free(t);
                }
            } else {
                char* t = xpath_to_string(v);
                if (t) {
                    for (const char* q = t; *q; q++) {
                        while (len + 8 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
                        if (escape && *q == '&') { memcpy(buf + len, "&amp;", 5); len += 5; }
                        else if (escape && *q == '<') { memcpy(buf + len, "&lt;", 4); len += 4; }
                        else buf[len++] = *q;
                    }
                    buf[len] = 0;
                    free(t);
                }
            }
            xpath_result_free(v);
            }
        }
        /* Empty content self-closes (Saxon serialization). */
        if (len == content_start_len + 1) {
            len--;             /* the '>' */
            buf[len++] = '/';
            buf[len++] = '>';
            buf[len] = 0;
        } else {
            while (len + strlen(name) + 4 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); if (!buf) return NULL; }
            len += (size_t)snprintf(buf + len, cap - len, "</%s>", name);
        }
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!out) { free(buf); return NULL; }
        out->value.string_value = buf;
        return out;
    }

    /* 3.1 postfix lookup `V?k` / `V?2` (08 tail): the map entry for
     * the key — array indices ARE the positional keys. */
    if (op == XPATH_OP_LOOKUP) {
        struct leptris_xpath_result* v = evaluate_expr(ctx, ast->children[0]);
        if (!v) return NULL;
        char* val = xpath_map_lookup_result(v, ast->value);
        xpath_result_free(v);
        if (!val) val = leptris_strdup("");
        /* A stored nested aggregate ("MAP...") returns as a
         * CARRIER nodeset member so the next lookup in a chain
         * decodes it (#692). */
        if (val[0] == '\x03' && val[1] == 'M' && val[2] == 'A' &&
            val[3] == 'P') {
            for (char* q = val; *q; q++) {
                if (*q == '\x0E') *q = '\x02';
                else if (*q == '\x0F') *q = '\x01';
            }
            XPathNodeSet* out = xpath_nodeset_new();
            if (out) {
                out->owns_synthetic_text = 1;
                out->is_sequence = 1;
                XPathTextNode* tn =
                    synth_text(val, strlen(val));
                free(val);
                if (tn) {
                    xpath_nodeset_add(out, tn);
                    struct leptris_xpath_result* r =
                        xpath_result_new(XPATH_RESULT_NODESET);
                    if (r) {
                        r->value.nodeset_value = out;
                        return r;
                    }
                    xpath_nodeset_free(out);
                }
            }
        }
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!out) { free(val); return NULL; }
        out->value.string_value = val;
        return out;
    }

    /* 3.1 square array constructor `[ a, b, ... ]` (08C): members
     * in order on the shared map representation with positional
     * keys — every array:* accessor is the map operation with a
     * formatted index. */
    /* XQuery 3.0 `array { E1, E2, ... }`: members are the ITEMS of
     * every content expression, in order (#692 §3.10.3). */
    if (op == XPATH_OP_ARRAY_OF) {
        void* b = xpath_map_builder_new();
        if (!b) return NULL;
        size_t idx = 1;
        for (size_t c = 0; c < ast->child_count; c++) {
            struct leptris_xpath_result* content =
                evaluate_expr(ctx, ast->children[c]);
            if (!content) { /* skip failed members */ continue; }
            if (content->type == XPATH_RESULT_NODESET &&
                content->value.nodeset_value) {
                XPathNodeSet* ns = content->value.nodeset_value;
                for (size_t i = 0; i < ns->count; i++) {
                    char* v = get_node_text(ns->nodes[i]);
                    char key[24];
                    snprintf(key, sizeof(key), "%zu", idx++);
                    xpath_map_builder_add(b, key, v ? v : "");
                    free(v);
                }
            } else {
                char* v = xpath_to_string(content);
                char key[24];
                snprintf(key, sizeof(key), "%zu", idx++);
                xpath_map_builder_add(b, key, v ? v : "");
                free(v);
            }
            xpath_result_free(content);
        }
        return xpath_map_builder_finish(b);
    }

    if (op == XPATH_OP_ARRAY_CONSTRUCTOR) {
        void* b = xpath_map_builder_new();
        if (!b) return NULL;
        for (size_t i = 0; i < ast->child_count; i++) {
            struct leptris_xpath_result* vr =
                evaluate_expr(ctx, ast->children[i]);
            char* v = vr ? xpath_to_string(vr) : NULL;
            if (vr) xpath_result_free(vr);
            char key[24];
            snprintf(key, sizeof(key), "%zu", i + 1);
            xpath_map_builder_add(b, key, v ? v : "");
            free(v);
        }
        return xpath_map_builder_finish(b);
    }

    /* 3.1 map constructor `map { k: v, ... }` (TODO.xslt-full/08):
     * value-level representation — ONE synthetic text node whose
     * content encodes the entries ("\x03MAP" + "\x02"key"\x01"value
     * per entry, insertion order), flowing through the existing
     * NODESET channel. map:* accessors decode it. */
    if (op == XPATH_OP_MAP_CONSTRUCTOR) {
        size_t cap = 32, len = 0;
        char* buf = (char*)malloc(cap);
        if (!buf) return NULL;
        memcpy(buf, "\x03MAP", 4);
        len = 4;
        for (size_t i = 0; i + 1 < ast->child_count; i += 2) {
            struct leptris_xpath_result* kr =
                evaluate_expr(ctx, ast->children[i]);
            char* k = kr ? xpath_to_string(kr) : NULL;
            if (kr) xpath_result_free(kr);
            struct leptris_xpath_result* vr =
                evaluate_expr(ctx, ast->children[i + 1]);
            /* Nested map/array values keep their RAW carrier
             * encoding ("MAP...") so chained lookups (?k?2)
             * decode the inner aggregate (#692); get_node_text
             * strips the marker for string consumers. */
            char* v = NULL;
            if (vr && vr->type == XPATH_RESULT_NODESET &&
                vr->value.nodeset_value &&
                vr->value.nodeset_value->count == 1) {
                void* mv = vr->value.nodeset_value->nodes[0];
                if (mv && XPATH_NODE_TYPE(mv) == (int)LEPTRIS_NODE_TEXT) {
                    const char* mc = ((XPathTextNode*)mv)->content;
                    if (mc && mc[0] == '\x03' && mc[1] == 'M' &&
                        mc[2] == 'A' && mc[3] == 'P') {
                        /* Escape the inner entry separators so the
                         * OUTER value scan (ends at the next \x02)
                         * cannot stop inside the nested aggregate;
                         * the lookup un-escapes on carrier wrap. */
                        size_t ml = strlen(mc);
                        v = LEPTRIS_ALLOC_N(char, ml + 1);
                        if (v) {
                            for (size_t mi = 0; mi < ml; mi++) {
                                char c = mc[mi];
                                v[mi] = c == '\x02' ? '\x0E'
                                        : c == '\x01' ? '\x0F' : c;
                            }
                            v[ml] = 0;
                        }
                    }
                }
            }
            if (!v && vr) v = xpath_to_string(vr);
            if (vr) xpath_result_free(vr);
            size_t kn = k ? strlen(k) : 0, vn = v ? strlen(v) : 0;
            while (len + kn + vn + 3 > cap) {
                cap *= 2;
                char* nb = (char*)realloc(buf, cap);
                if (!nb) { free(buf); free(k); free(v); return NULL; }
                buf = nb;
            }
            buf[len++] = '\x02';
            if (kn) { memcpy(buf + len, k, kn); len += kn; }
            buf[len++] = '\x01';
            if (vn) { memcpy(buf + len, v, vn); len += vn; }
            free(k);
            free(v);
        }
        buf[len] = '\0';
        /* Canonical entry order: sort by key so deep-equal and the
         * serialized carrier are key-order-insensitive (maps-4:
         * {1:a, 2:b} = {2:b, 1:a}); map iteration order is
         * implementation-defined in XQuery. */
        {
            size_t body_len = len - 4;   /* past "\x03MAP" */
            char* body = buf + 4;
            size_t count = 0;
            for (size_t i = 0; i < body_len; i++)
                if (body[i] == '\x02') count++;
            if (count > 1) {
                char** keys = (char**)malloc(count * sizeof(char*));
                size_t* klens = (size_t*)malloc(count * sizeof(size_t));
                char** vals = (char**)malloc(count * sizeof(char*));
                size_t* vlens = (size_t*)malloc(count * sizeof(size_t));
                if (keys && klens && vals && vlens) {
                    size_t n = 0, p = 0;
                    while (p < body_len) {
                        if (body[p] != '\x02') { p++; continue; }
                        p++;
                        size_t ks = p;
                        while (p < body_len && body[p] != '\x01') p++;
                        keys[n] = body + ks;
                        klens[n] = p - ks;
                        p++;
                        size_t vs = p;
                        while (p < body_len && body[p] != '\x02') p++;
                        vals[n] = body + vs;
                        vlens[n] = p - vs;
                        n++;
                    }
                    for (size_t a = 1; a < n; a++) {
                        char* ek = keys[a]; size_t ekl = klens[a];
                        char* ev = vals[a]; size_t evl = vlens[a];
                        size_t b2 = a;
                        while (b2 > 0) {
                            int c = strncmp(keys[b2 - 1], ek,
                                            klens[b2 - 1] < ekl
                                                ? klens[b2 - 1] : ekl);
                            if (c == 0)
                                c = klens[b2 - 1] < ekl ? -1
                                    : klens[b2 - 1] > ekl ? 1 : 0;
                            if (c <= 0) break;
                            keys[b2] = keys[b2 - 1];
                            klens[b2] = klens[b2 - 1];
                            vals[b2] = vals[b2 - 1];
                            vlens[b2] = vlens[b2 - 1];
                            b2--;
                        }
                        keys[b2] = ek; klens[b2] = ekl;
                        vals[b2] = ev; vlens[b2] = evl;
                    }
                    /* Sort-order permutes segments in place: write
                     * through a scratch (a forward write clobbers a
                     * not-yet-copied source segment). */
                    char* scratch = (char*)malloc(body_len + 1);
                    if (scratch) {
                        size_t w2 = 0;
                        for (size_t e = 0; e < n; e++) {
                            scratch[w2++] = '\x02';
                            memcpy(scratch + w2, keys[e], klens[e]);
                            w2 += klens[e];
                            scratch[w2++] = '\x01';
                            memcpy(scratch + w2, vals[e], vlens[e]);
                            w2 += vlens[e];
                        }
                        memcpy(body, scratch, w2);
                        len = 4 + w2;
                        buf[len] = '\0';
                        free(scratch);
                    }
                }
                free(keys); free(klens); free(vals); free(vlens);
            }
        }
        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) { free(buf); return NULL; }
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        XPathTextNode* tn = synth_text(buf, len);
        free(buf);
        if (!tn) { xpath_nodeset_free(out); return NULL; }
        xpath_nodeset_add(out, tn);
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
    }

    /* XPath 3.0 simple map `L ! R`: R runs once per item of L with
     * the context item/position/size set to that item's slot;
     * results concatenate in order (members stringify, like the
     * for-expression form). */
    if (op == XPATH_OP_MAP) {
        if (ast->child_count < 2) return NULL;
        struct leptris_xpath_result* left =
            evaluate_expr(ctx, ast->children[0]);
        if (!left) return NULL;

        XPathNodeSet* ns = (left->type == XPATH_RESULT_NODESET)
                               ? left->value.nodeset_value : NULL;
        if (!ns) {
            /* scalar left: map once over the ambient context
             * (defensive parity with the old inline body) */
            XPathNodeSet* one = xpath_nodeset_new();
            if (one && ctx->context_node)
                xpath_nodeset_add(one, ctx->context_node);
            xpath_result_free(left);
            if (!one) return NULL;
            XPathNodeSet* out = xpath_map_fn_over(ctx, one,
                                                  ast->children[1]);
            xpath_nodeset_free(one);
            if (!out) return NULL;
            struct leptris_xpath_result* result =
                xpath_result_new(XPATH_RESULT_NODESET);
            if (!result) {
                xpath_nodeset_free(out);
                return NULL;
            }
            result->value.nodeset_value = out;
            return result;
        }
        /* delegate to the shared per-member mapper */
        {
            XPathNodeSet* owned = xpath_nodeset_new();
            if (owned) {
                for (size_t i = 0; i < ns->count; i++)
                    xpath_nodeset_add(owned, ns->nodes[i]);
            }
            if (!owned) {
                xpath_result_free(left);
                return NULL;
            }
            /* map FIRST — `left` owns the synthetic members */
            XPathNodeSet* out = xpath_map_fn_over(ctx, owned,
                                                  ast->children[1]);
            xpath_nodeset_free(owned);
            xpath_result_free(left);
            if (!out) return NULL;
            struct leptris_xpath_result* result =
                xpath_result_new(XPATH_RESULT_NODESET);
            if (!result) {
                xpath_nodeset_free(out);
                return NULL;
            }
            result->value.nodeset_value = out;
            return result;
        }
    }

    /* XPath 3.1 switch (3.0 §3.9-style): first eq-matching case
     * result, else the default (last child when the
     * __switch_default sentinel is set), else empty. */
    if (op == XPATH_OP_SWITCH) {
        if (ast->child_count < 1) return NULL;
        struct leptris_xpath_result* operand =
            evaluate_expr(ctx, ast->children[0]);
        if (!operand) return NULL;
        char* ov = leptris_xpath_result_string(operand);
        double on = leptris_xpath_result_number(operand);
        leptris_xpath_result_free(operand);
        int has_default = ast->value &&
            strcmp(ast->value, "__switch_default") == 0;
        size_t pair_end = ast->child_count - (has_default ? 1 : 0);
        struct leptris_xpath_result* out = NULL;
        for (size_t i = 1; i + 1 < pair_end && !out; i += 2) {
            struct leptris_xpath_result* test =
                evaluate_expr(ctx, ast->children[i]);
            if (!test) { free(ov); return NULL; }
            int hit = 0;
            size_t tcount = 0;
            if (leptris_xpath_result_type(test) ==
                LEPTRIS_XPATH_NODESET)
                tcount = leptris_xpath_result_count(test);
            if (tcount > 1) {
                /* multi-case test (sequence): any member hits */
                for (size_t k = 0; k < tcount && !hit; k++) {
                    const char* iv =
                        leptris_xpath_result_node_value(test, k);
                    hit = (ov && iv && strcmp(ov, iv) == 0);
                }
            } else {
                char* tv = leptris_xpath_result_string(test);
                double tn = leptris_xpath_result_number(test);
                hit = (ov && tv && strcmp(ov, tv) == 0) ||
                      (!ov && !tv) || (on == tn && ov && tv &&
                                       strcmp(ov, "NaN") != 0);
                free(tv);
            }
            leptris_xpath_result_free(test);
            if (hit)
                out = evaluate_expr(ctx, ast->children[i + 1]);
        }
        if (!out && has_default)
            out = evaluate_expr(
                ctx, ast->children[ast->child_count - 1]);
        free(ov);
        if (!out) {
            struct leptris_xpath_result* empty =
                xpath_result_new(XPATH_RESULT_NODESET);
            if (empty) {
                empty->value.nodeset_value = xpath_nodeset_new();
                empty->value.nodeset_value->is_sequence = 1;
            }
            return empty;
        }
        return out;
    }

    /* XPath 3.0 string concat `A || B`: string() both, join. */
    if (op == XPATH_OP_CONCAT) {
        if (ast->child_count < 2) return NULL;
        struct leptris_xpath_result* l =
            evaluate_expr(ctx, ast->children[0]);
        if (!l) return NULL;
        char* ls = xpath_to_string(l);
        xpath_result_free(l);
        struct leptris_xpath_result* r =
            evaluate_expr(ctx, ast->children[1]);
        if (!r) { free(ls); return NULL; }
        char* rs = xpath_to_string(r);
        xpath_result_free(r);

        size_t ll = ls ? strlen(ls) : 0;
        size_t rl = rs ? strlen(rs) : 0;
        char* joined = (char*)malloc(ll + rl + 1);
        if (!joined) { free(ls); free(rs); return NULL; }
        if (ll) memcpy(joined, ls, ll);
        if (rl) memcpy(joined + ll, rs, rl);
        joined[ll + rl] = '\0';
        free(ls);
        free(rs);

        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!result) { free(joined); return NULL; }
        result->value.string_value = joined;
        return result;
    }


    /* XSLT 3.0 range `A to B` (XPath 2.0+): integer sequence as a
     * nodeset of synthetic text nodes — predicates and numeric
     * comparisons then see each member. */
    if (op == XPATH_OP_RANGE) {
        if (ast->child_count < 2) return NULL;
        struct leptris_xpath_result* lo_r =
            evaluate_expr(ctx, ast->children[0]);
        if (!lo_r) return NULL;
        struct leptris_xpath_result* hi_r =
            evaluate_expr(ctx, ast->children[1]);
        if (!hi_r) { xpath_result_free(lo_r); return NULL; }
        double lod = xpath_to_number(lo_r);
        double hid = xpath_to_number(hi_r);
        xpath_result_free(lo_r);
        xpath_result_free(hi_r);

        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) return NULL;
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        /* XPath 2.0+: an empty (NaN) operand yields the empty
         * sequence — never cast NaN to long (UB: arm64 gives 0,
         * x86 gives INT32/64_MIN, so the "range" silently became
         * a 75/100000-item sequence; cbcl-codepoints-to-string-020).
         * Huge finite values clamp instead of UB-casting. */
        if (!isnan(lod) && !isnan(hid)) {
            long lo = lod > 9.0e18 ? LONG_MAX
                    : lod < -9.0e18 ? LONG_MIN
                    : (long)lod;
            long hi = hid > 9.0e18 ? LONG_MAX
                    : hid < -9.0e18 ? LONG_MIN
                    : (long)hid;
            for (long v = lo; v <= hi && v - lo < 100000; v++) {
                char buf[28];
                int l = snprintf(buf, sizeof buf, "\x03N%ld", v);
                XPathTextNode* tn = synth_text(buf, (size_t)l);
                if (!tn) break;
                xpath_nodeset_add(out, tn);
            }
        }
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
    }

    /* ---- 2.0 type operators (TODO.xslt-full/06): child[0] plus the
     * SequenceType carried in ast->value ("xs:integer", "node()+").
     * Value-level v1: kind tests over the 1.0 result model; treat
     * as asserts nothing and passes the operand through. ---- */
    if (op == XPATH_OP_INSTANCE_OF || op == XPATH_OP_CASTABLE ||
        op == XPATH_OP_CAST || op == XPATH_OP_TREAT) {
        struct leptris_xpath_result* v = evaluate_expr(ctx, ast->children[0]);
        if (!v) return NULL;
        if (op == XPATH_OP_TREAT) return v;
        const char* ty = ast->value ? ast->value : "";
        size_t tlen = strlen(ty);
        char occ = tlen ? ty[tlen - 1] : 0;
        if (occ == '?' || occ == '*' || occ == '+') tlen--;
        else occ = 0;   /* exact-one cardinality */
        char base[80];
        if (tlen >= sizeof(base)) tlen = sizeof(base) - 1;
        memcpy(base, ty, tlen);
        base[tlen] = '\0';

        if (op == XPATH_OP_INSTANCE_OF) {
            struct leptris_xpath_result* out =
                xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (!out) { xpath_result_free(v); return NULL; }
            int m = 0;
            /* Per-member check with cardinality gating (#744): the
             * tag space classifies real nodes (0 element, 1 text,
             * 2 comment, 3 cdata, 4 pi) against synthetics (6
             * attribute, 8 atomic-string carrier); "\x03N"-marked
             * tag-8 members are numerics. */
            int is_string_ty = strcmp(base, "xs:string") == 0 ||
                               strcmp(base, "xs:anyURI") == 0 ||
                               strncmp(base, "xs:date", 7) == 0 ||
                               strcmp(base, "xs:time") == 0 ||
                               strcmp(base, "xs:duration") == 0;
            int is_bool_ty = strcmp(base, "xs:boolean") == 0;
            int is_num_ty = !is_string_ty && !is_bool_ty &&
                            strcmp(base, "node()") != 0 &&
                            strcmp(base, "item()") != 0 &&
                            strcmp(base, "element()") != 0 &&
                            strcmp(base, "attribute()") != 0 &&
                            strcmp(base, "text()") != 0 &&
                            strcmp(base, "comment()") != 0 &&
                            strcmp(base, "processing-instruction()") != 0;
            if (v->type == XPATH_RESULT_NODESET && v->value.nodeset_value) {
                XPathNodeSet* ns = v->value.nodeset_value;
                size_t cnt = ns->count;
                m = (occ == 0) ? (cnt == 1)
                  : (occ == '?') ? (cnt <= 1)
                  : (occ == '+') ? (cnt >= 1)
                                 : 1;   /* '*' */
                for (size_t i = 0; m && i < cnt; i++) {
                    void* n = ns->nodes[i];
                    int tag = n ? (int)XPATH_NODE_TYPE(n) : -1;
                    const char* mc =
                        (tag == (int)LEPTRIS_NODE_TEXT && n)
                            ? ((XPathTextNode*)n)->content : NULL;
                    int is_num_member = mc && mc[0] == '\x03' &&
                                        (mc[1] == 'N' ||
                                         (mc[1] == 'F' &&
                                          !((mc[2] == 'N' &&
                                             mc[3] == '\x02') ||
                                            mc[2] == 'R')));
                    if (strcmp(base, "item()") == 0) {
                        /* every member is an item */
                    } else if (strcmp(base, "node()") == 0) {
                        m = tag >= 0 && tag <= 7;
                    } else if (strcmp(base, "element()") == 0) {
                        m = tag == (int)LEPTRIS_NODE_ELEMENT;
                    } else if (strcmp(base, "attribute()") == 0) {
                        m = tag == (int)LEPTRIS_NODE_ATTRIBUTE;
                    } else if (strcmp(base, "text()") == 0) {
                        /* real text/cdata; NOT synthetic carriers */
                        m = tag == 1 || tag == 3;
                    } else if (strcmp(base, "comment()") == 0) {
                        m = tag == 2;
                    } else if (strcmp(base, "processing-instruction()") == 0) {
                        m = tag == 4;
                    } else if (is_string_ty) {
                        m = tag == (int)LEPTRIS_NODE_TEXT && !is_num_member;
                    } else if (is_bool_ty) {
                        m = 0;
                    } else if (is_num_ty) {
                        m = is_num_member;
                    } else {
                        m = 0;
                    }
                }
            } else {
                /* Scalar result: exactly one item — every
                 * occurrence indicator admits it. */
                if (is_string_ty) m = v->type == XPATH_RESULT_STRING;
                else if (is_bool_ty) m = v->type == XPATH_RESULT_BOOLEAN;
                else if (is_num_ty) m = v->type == XPATH_RESULT_NUMBER;
                else if (strcmp(base, "item()") == 0) m = 1;
                else m = 0;   /* node kinds: a scalar is not a node */
            }
            out->value.boolean_value = m;
            xpath_result_free(v);
            return out;
        }
        if (op == XPATH_OP_CASTABLE) {
            struct leptris_xpath_result* out =
                xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (!out) { xpath_result_free(v); return NULL; }
            int ok = 1;
            int numeric = strcmp(base, "xs:integer") == 0 ||
                          strcmp(base, "xs:double") == 0 ||
                          strcmp(base, "xs:decimal") == 0 ||
                          strcmp(base, "xs:float") == 0;
            if (numeric) {
                char* s = xpath_to_string(v);
                ok = 0;
                if (s) {
                    char* end = NULL;
                    strtod(s, &end);
                    while (end && *end == ' ') end++;
                    ok = end && *end == '\0' && s[0] != '\0';
                    free(s);
                }
            } else if (strcmp(base, "xs:boolean") == 0) {
                char* s = xpath_to_string(v);
                ok = s && (strcmp(s, "true") == 0 ||
                           strcmp(s, "false") == 0 ||
                           strcmp(s, "1") == 0 || strcmp(s, "0") == 0);
                free(s);
            }
            out->value.boolean_value = ok;
            xpath_result_free(v);
            return out;
        }
        /* XPATH_OP_CAST: constructor semantics (xs:integer truncates
         * toward zero — same rule as the registered constructors). */
        {
            struct leptris_xpath_result* out = NULL;
            if (strcmp(base, "xs:string") == 0 ||
                strcmp(base, "xs:anyURI") == 0 ||
                strncmp(base, "xs:date", 7) == 0 ||
                strcmp(base, "xs:time") == 0 ||
                strcmp(base, "xs:duration") == 0) {
                out = xpath_result_new(XPATH_RESULT_STRING);
                if (out) out->value.string_value = xpath_to_string(v);
            } else if (strcmp(base, "xs:boolean") == 0) {
                out = xpath_result_new(XPATH_RESULT_BOOLEAN);
                if (out) out->value.boolean_value = xpath_to_boolean(v);
            } else {
                /* Numeric targets validate string lexicals (#790):
                 * 'nope' cast as xs:integer is a dynamic error
                 * (Saxon), so try/catch can participate — a quiet
                 * NaN is the silent-wrong class. */
                double d;
                if (v->type == XPATH_RESULT_STRING &&
                    v->value.string_value &&
                    !xq_numeric_lexical(v->value.string_value)) {
                    snprintf(ctx->error_msg, sizeof(ctx->error_msg),
                             "Cannot cast '%s' to %s",
                             v->value.string_value, base);
                    snprintf(ctx->error_code, sizeof(ctx->error_code),
                             "FORG0001");
                    xpath_result_free(v);
                    return NULL;
                }
                d = xpath_to_number(v);
                if (strcmp(base, "xs:integer") == 0)
                    d = (d < 0) ? ceil(d) : floor(d);
                out = xpath_result_new(XPATH_RESULT_NUMBER);
                if (out) out->value.number_value = d;
            }
            xpath_result_free(v);
            return out;
        }
    }

    /* XSLT 3.0 parenthesized item sequence: each child evaluates to
     * one member (nodeset children contribute their nodes in
     * order). */
    if (op == XPATH_OP_SEQUENCE) {
        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) return NULL;
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        for (size_t i = 0; i < ast->child_count; i++) {
            struct leptris_xpath_result* item =
                evaluate_expr(ctx, ast->children[i]);
            if (!item) { xpath_nodeset_free(out); return NULL; }
            if (item->type == XPATH_RESULT_NODESET &&
                item->value.nodeset_value) {
                XPathNodeSet* is = item->value.nodeset_value;
                for (size_t j = 0; j < is->count; j++)
                    xpath_nodeset_add(out, is->nodes[j]);
                /* #720: an item nodeset may OWN synthetic nodes
                 * (attribute/namespace materialized by an axis eval)
                 * — the borrowed pointers dangle once the item is
                 * freed. Transfer ownership to the sequence (same
                 * discipline as XPATH_OP_UNION, issue #514). */
                out->owns_attributes |= is->owns_attributes;
                out->owns_namespaces |= is->owns_namespaces;
                out->owns_synthetic_text |= is->owns_synthetic_text;
                is->owns_attributes = 0;
                is->owns_namespaces = 0;
                is->owns_synthetic_text = 0;
            } else {
                char* piece =
                    item->decimal_lex
                        ? leptris_strdup(item->decimal_lex)
                        : (ctx->xquery_spelling &&
                           item->type == XPATH_RESULT_NUMBER &&
                           !item->is_int)
                              ? xpath_number_to_string_xq_typed(
                                    item->value.number_value,
                                    item->atomic_type &&
                                        strcmp(item->atomic_type,
                                               "xs:float") == 0)
                              : xpath_to_string(item);
                if (item->type == XPATH_RESULT_NUMBER) {
                    /* "\x03N" marks numeric members for per-member
                     * type checks (instance of); get_node_text
                     * strips it for string consumers. "F" carries
                     * xs:float members (float32-exact values) so
                     * eq-based functions apply float promotion. */
                    size_t pl = piece ? strlen(piece) : 0;
                    char* marked = (char*)malloc(pl + 3);
                    if (marked) {
                        marked[0] = '\x03';
                        marked[1] = (item->atomic_type &&
                                     strcmp(item->atomic_type,
                                            "xs:float") == 0)
                                        ? 'F'
                                    : (item->atomic_type &&
                                       strcmp(item->atomic_type,
                                              "xs:decimal") == 0)
                                        ? 'D' : 'N';
                        if (pl) memcpy(marked + 2, piece, pl);
                        marked[2 + pl] = 0;
                        XPathTextNode* tn = synth_text(marked, pl + 2);
                        free(marked);
                        if (tn) xpath_nodeset_add(out, tn);
                    }
                } else if (item->type == XPATH_RESULT_BOOLEAN) {
                    /* "\x03B" marks boolean members so EBV
                     * round-trips (a false boolean must stay falsy
                     * after subsequence/remove round-trips). */
                    const char* bp =
                        item->value.boolean_value ? "true" : "false";
                    char* marked = (char*)malloc(strlen(bp) + 3);
                    if (marked) {
                        marked[0] = '\x03';
                        marked[1] = 'B';
                        strcpy(marked + 2, bp);
                        XPathTextNode* tn =
                            synth_text(marked, strlen(bp) + 2);
                        free(marked);
                        if (tn) xpath_nodeset_add(out, tn);
                    }
                } else {
                    XPathTextNode* tn =
                        synth_text(piece ? piece : "",
                                   piece ? strlen(piece) : 0);
                    if (tn) xpath_nodeset_add(out, tn);
                }
                free(piece);
            }
            xpath_result_free(item);
        }
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
    }

    /* Unary negation */
    if (op == XPATH_OP_NEGATION) {
        struct leptris_xpath_result* operand = evaluate_expr(ctx, ast->children[0]);
        if (!operand) return NULL;
        double value = xpath_to_number(operand);
        xpath_result_free(operand);

        struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) result->value.number_value = -value;
        return result;
    }

    /* Binary operators require 2 operands */
    if (ast->child_count < 2) return NULL;

    struct leptris_xpath_result* left = evaluate_expr(ctx, ast->children[0]);
    if (!left) return NULL;

    /* Short-circuit for logical operators */
    if (op == XPATH_OP_AND) {
        if (!xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 0;
            return result;
        }
    } else if (op == XPATH_OP_OR) {
        if (xpath_to_boolean(left)) {
            xpath_result_free(left);
            struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (result) result->value.boolean_value = 1;
            return result;
        }
    }

    struct leptris_xpath_result* right = evaluate_expr(ctx, ast->children[1]);
    if (!right) {
        xpath_result_free(left);
        return NULL;
    }

    struct leptris_xpath_result* result = NULL;

    /* Arithmetic operators */
    if (op == XPATH_OP_PLUS || op == XPATH_OP_MINUS || op == XPATH_OP_MULTIPLY ||
        op == XPATH_OP_DIV || op == XPATH_OP_MOD || op == XPATH_OP_IDIV) {
        /* dayTimeDuration semantics first (F&O): dur±dur -> dur,
         * dur*num / num*dur -> dur, dur÷dur -> number, dur÷num ->
         * dur; anything else falls to the numeric path as before. */
        {
            const char* ls = (left->type == XPATH_RESULT_STRING)
                                 ? left->value.string_value : NULL;
            const char* rs = (right->type == XPATH_RESULT_STRING)
                                 ? right->value.string_value : NULL;
            double lsec = 0, rsec = 0;
            int ld = ls && leptris_dur_try_seconds(ls, &lsec);
            int rd = rs && leptris_dur_try_seconds(rs, &rsec);
            /* yearMonth family rides the months measure (PnYnM) */
            double lmo = 0, rmo = 0;
            int lmo_ok = !ld && ls &&
                         leptris_dur_try_months(ls, &lmo);
            int rmo_ok = !rd && rs &&
                         leptris_dur_try_months(rs, &rmo);
            if ((lmo_ok || rmo_ok) &&
                (op == XPATH_OP_PLUS || op == XPATH_OP_MINUS ||
                 op == XPATH_OP_MULTIPLY || op == XPATH_OP_DIV)) {
                char mbuf[40];
                struct leptris_xpath_result* mres = NULL;
                if ((op == XPATH_OP_PLUS || op == XPATH_OP_MINUS) &&
                    lmo_ok && rmo_ok) {
                    double v = (op == XPATH_OP_PLUS) ? lmo + rmo
                                                     : lmo - rmo;
                    leptris_dur_format_months(v, mbuf, sizeof mbuf);
                    mres = xpath_result_new(XPATH_RESULT_STRING);
                    if (mres)
                        mres->value.string_value =
                            leptris_strdup(mbuf);
                } else if (op == XPATH_OP_MULTIPLY &&
                           (lmo_ok || rmo_ok)) {
                    double m = lmo_ok ? lmo : rmo;
                    double num = lmo_ok ? xpath_to_number(right)
                                        : xpath_to_number(left);
                    /* F&O: fractional months round half-up */
                    leptris_dur_format_months(
                        (double)llrint(m * num), mbuf, sizeof mbuf);
                    mres = xpath_result_new(XPATH_RESULT_STRING);
                    if (mres)
                        mres->value.string_value =
                            leptris_strdup(mbuf);
                } else if (op == XPATH_OP_DIV && lmo_ok && rmo_ok) {
                    mres = xpath_result_new(XPATH_RESULT_NUMBER);
                    if (mres)
                        mres->value.number_value = lmo / rmo;
                } else if (op == XPATH_OP_DIV && lmo_ok) {
                    leptris_dur_format_months(
                        (double)llrint(lmo / xpath_to_number(right)),
                        mbuf, sizeof mbuf);
                    mres = xpath_result_new(XPATH_RESULT_STRING);
                    if (mres)
                        mres->value.string_value =
                            leptris_strdup(mbuf);
                }
                if (mres) {
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return mres;
                }
            }
            /* month shift for date/dateTime lexicals */
            if ((op == XPATH_OP_PLUS || op == XPATH_OP_MINUS) &&
                (lmo_ok != rmo_ok)) {
                const char* ds = lmo_ok ? rs : ls;
                double mo = lmo_ok ? lmo : rmo;
                if (ds && ds[0] >= '0' && ds[0] <= '9') {
                    double delta = (op == XPATH_OP_PLUS)
                                       ? mo : -mo;
                    char dbuf[64];
                    if (leptris_dt_shift_months(ds, delta, dbuf,
                                                sizeof dbuf)) {
                        result = xpath_result_new(
                            XPATH_RESULT_STRING);
                        if (result)
                            result->value.string_value =
                                leptris_strdup(dbuf);
                        xpath_result_free(left);
                        xpath_result_free(right);
                        return result;
                    }
                }
            }
            if (ld || rd) {
                double num = ld ? xpath_to_number(right)
                                : xpath_to_number(left);
                double dursec = ld ? lsec : rsec;
                char buf[64];
                /* op-date family: date/time/dateTime +- duration */
                if ((op == XPATH_OP_PLUS || op == XPATH_OP_MINUS) &&
                    (ld != rd)) {
                    const char* ds = ld ? rs : ls;
                    double dsec = ld ? lsec : rsec;
                    if (ds && ds[0] >= '0' && ds[0] <= '9') {
                        double delta = (op == XPATH_OP_PLUS)
                                           ? dsec : -dsec;
                        char dbuf[64];
                        if (leptris_dt_shift(ds, delta, dbuf,
                                             sizeof dbuf)) {
                            result = xpath_result_new(
                                XPATH_RESULT_STRING);
                            if (result)
                                result->value.string_value =
                                    leptris_strdup(dbuf);
                            xpath_result_free(left);
                            xpath_result_free(right);
                            return result;
                        }
                    }
                }
                if ((op == XPATH_OP_PLUS || op == XPATH_OP_MINUS) &&
                    ld && rd) {
                    double v = (op == XPATH_OP_PLUS) ? lsec + rsec
                                                     : lsec - rsec;
                    /* F&O range: beyond the seconds range is a
                     * dynamic error (FODT0002), not a wild format. */
                    if (!isfinite(v) || fabs(v) > 8.0e22)
                        return NULL;   /* FODT0002 range error */
                    if (v == 0) v = 0;   /* 0 * huge: PT0S, no sign */
                    leptris_dur_format(v, buf, sizeof buf);
                    result = xpath_result_new(XPATH_RESULT_STRING);
                    if (result)
                        result->value.string_value = leptris_strdup(buf);
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
                if (op == XPATH_OP_MULTIPLY) {
                    if (!isfinite(dursec * num) ||
                        fabs(dursec * num) > 8.0e22)
                        return NULL;   /* FODT0002 */
                    if (dursec * num == 0) num = 0, dursec = 0;
                    leptris_dur_format(dursec * num, buf, sizeof buf);
                    result = xpath_result_new(XPATH_RESULT_STRING);
                    if (result)
                        result->value.string_value = leptris_strdup(buf);
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
                if (op == XPATH_OP_DIV && ld && rd) {
                    result = xpath_result_new(XPATH_RESULT_NUMBER);
                    if (result) result->value.number_value = lsec / rsec;
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
                if (op == XPATH_OP_DIV && ld) {
                    if (!isfinite(dursec / num) ||
                        fabs(dursec / num) > 8.0e22)
                        return NULL;   /* FODT0002 */
                    if (dursec / num == 0) dursec = 0, num = 1;
                    leptris_dur_format(dursec / num, buf, sizeof buf);
                    result = xpath_result_new(XPATH_RESULT_STRING);
                    if (result)
                        result->value.string_value = leptris_strdup(buf);
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
            }
        }
        double lval = xpath_to_number(left);
        double rval = xpath_to_number(right);
        result = xpath_result_new(XPATH_RESULT_NUMBER);
        if (result) {
            switch (op) {
                case XPATH_OP_PLUS: result->value.number_value = lval + rval; break;
                case XPATH_OP_MINUS: result->value.number_value = lval - rval; break;
                case XPATH_OP_MULTIPLY: result->value.number_value = lval * rval; break;
                case XPATH_OP_DIV: result->value.number_value = lval / rval; break;
                case XPATH_OP_MOD: result->value.number_value = fmod(lval, rval); break;
                case XPATH_OP_IDIV: result->value.number_value =
                  (rval == 0) ? 0 : (double)((long long)lval / (long long)rval); break;
                default: break;
            }
        }
    }
    /* Comparison operators */
    else if (op >= XPATH_OP_EQUAL && op <= XPATH_OP_GREATER_EQUAL) {
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) {
            /* XPath 1.0 spec Section 3.4:
             * - If both are strings: string comparison for = and !=
             * - If one is nodeset: compare nodeset string-values
             * - Otherwise: numeric comparison
             */
            int is_equality_op = (op == XPATH_OP_EQUAL || op == XPATH_OP_NOT_EQUAL);

            /* Duration comparisons (F&O value semantics):
             * dayTime by seconds, yearMonth/plain by months, and a
             * zero of one family equals a zero of the other
             * (PT0S eq P0M is true); mixed nonzero families are
             * never equal. */
            if (left->type == XPATH_RESULT_STRING &&
                right->type == XPATH_RESULT_STRING) {
                double lsec = 0, rsec = 0, lmo = 0, rmo = 0;
                int ldt = leptris_dur_try_seconds(
                              left->value.string_value, &lsec);
                int rdt = leptris_dur_try_seconds(
                              right->value.string_value, &rsec);
                int lmo_ok = leptris_dur_try_months(
                                 left->value.string_value, &lmo);
                int rmo_ok = leptris_dur_try_months(
                                 right->value.string_value, &rmo);
                int lzero = (ldt && lsec == 0) || (lmo_ok && lmo == 0);
                int rzero = (rdt && rsec == 0) || (rmo_ok && rmo == 0);
                double lv = 0, rv = 0;
                int comparable = 0;
                if (ldt && rdt) { lv = lsec; rv = rsec; comparable = 1; }
                else if (lmo_ok && rmo_ok) { lv = lmo; rv = rmo; comparable = 1; }
                else if (lzero && rzero) { lv = rv = 0; comparable = 1; }
                if (comparable) {
                    result->value.boolean_value =
                        op == XPATH_OP_EQUAL ? lv == rv
                        : op == XPATH_OP_NOT_EQUAL ? lv != rv
                        : op == XPATH_OP_LESS ? lv < rv
                        : op == XPATH_OP_LESS_EQUAL ? lv <= rv
                        : op == XPATH_OP_GREATER ? lv > rv
                        : lv >= rv;
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
                if ((ldt || lmo_ok) && (rdt || rmo_ok) && is_equality_op) {
                    /* mixed nonzero families: never equal */
                    result->value.boolean_value =
                        op == XPATH_OP_NOT_EQUAL;
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
            }

            /* ISO date/time-shaped strings compare chronologically
             * with plain lexical order (zero-padded, same-zone
             * corpus forms) — the numeric path would NaN them. */
            if (left->type == XPATH_RESULT_STRING &&
                right->type == XPATH_RESULT_STRING) {
                const char* lvs = left->value.string_value;
                const char* rvs = right->value.string_value;
                int lok = lvs && lvs[0] >= '0' && lvs[0] <= '9' &&
                          (strchr(lvs, ':') ||
                           (strlen(lvs) >= 8 && lvs[4] == '-' &&
                            lvs[7] == '-'));
                int rok = rvs && rvs[0] >= '0' && rvs[0] <= '9' &&
                          (strchr(rvs, ':') ||
                           (strlen(rvs) >= 8 && rvs[4] == '-' &&
                            rvs[7] == '-'));
                if (lok && rok) {
                    /* time lexical 24:00:00 == 00:00:00 — fold the
                     * hour before comparing (xs:time midnight form) */
                    char lfold[48], rfold[48];
                    if (lvs[2] == ':' && lvs[0] == '2' && lvs[1] == '4') {
                        snprintf(lfold, sizeof lfold, "00%s", lvs + 2);
                        lvs = lfold;
                    }
                    if (rvs[2] == ':' && rvs[0] == '2' && rvs[1] == '4') {
                        snprintf(rfold, sizeof rfold, "00%s", rvs + 2);
                        rvs = rfold;
                    }
                    int c = strcmp(lvs, rvs);
                    result->value.boolean_value =
                        op == XPATH_OP_EQUAL ? c == 0
                        : op == XPATH_OP_NOT_EQUAL ? c != 0
                        : op == XPATH_OP_LESS ? c < 0
                        : op == XPATH_OP_LESS_EQUAL ? c <= 0
                        : op == XPATH_OP_GREATER ? c > 0
                        : c >= 0;
                    xpath_result_free(left);
                    xpath_result_free(right);
                    return result;
                }
            }

            /* Handle nodeset comparisons (§3.4): a nodeset NEVER
             * collapses to its first node — nodeset op nodeset is
             * ANY-PAIR, nodeset op scalar is ANY-NODE. The old
             * to_string shortcut compared first nodes only and broke
             * the distinct-values idiom
             * @name = preceding::G/@name (bug-5-, VM path is the
             * twin in vm.c's comparison op). */
            if (left->type == XPATH_RESULT_NODESET ||
                right->type == XPATH_RESULT_NODESET) {
                int negate = (op == XPATH_OP_NOT_EQUAL);
                int ns_is_left = (left->type == XPATH_RESULT_NODESET);
                struct leptris_xpath_result* other =
                    ns_is_left ? right : left;
                XPathNodeSet* ns = ns_is_left
                                       ? left->value.nodeset_value
                                       : right->value.nodeset_value;
                int matches = 0;

                if (other->type == XPATH_RESULT_BOOLEAN && is_equality_op) {
                    /* boolean vs nodeset: boolean() both sides. */
                    int lb = xpath_to_boolean(left);
                    int rb = xpath_to_boolean(right);
                    result->value.boolean_value =
                        negate ? (lb != rb) : (lb == rb);
                } else if (other->type == XPATH_RESULT_NODESET) {
                    /* nodeset vs nodeset: any-pair. Operand ORDER is
                     * part of the semantics for relational ops
                     * (#965): a runs the LEFT nodeset, b the RIGHT,
                     * whatever side `ns` came from. */
                    XPathNodeSet* on = other->value.nodeset_value;
                    XPathNodeSet* lns = ns_is_left ? ns : on;
                    XPathNodeSet* rns = ns_is_left ? on : ns;
                    for (size_t i = 0; !matches && lns && i < lns->count; i++) {
                        char* a = get_node_text(lns->nodes[i]);
                        if (!a) continue;
                        for (size_t j = 0; !matches && rns && j < rns->count; j++) {
                            char* b = get_node_text(rns->nodes[j]);
                            if (!b) continue;
                            if (is_equality_op) {
                                matches = negate ? (strcmp(a, b) != 0)
                                                 : (strcmp(a, b) == 0);
                            } else {
                                matches = op_relational_cmp(
                                    op, atof(a), atof(b));
                            }
                            LEPTRIS_FREE(b);
                        }
                        LEPTRIS_FREE(a);
                    }
                    result->value.boolean_value = matches;
                } else if (is_equality_op) {
                    /* nodeset vs scalar: any-node string compare. */
                    char* scalar = xpath_to_string(other);
                    for (size_t i = 0; !matches && ns && scalar &&
                             i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        matches = negate ? (strcmp(a, scalar) != 0)
                                         : (strcmp(a, scalar) == 0);
                        LEPTRIS_FREE(a);
                    }
                    if (scalar) LEPTRIS_FREE(scalar);
                    result->value.boolean_value = matches;
                } else {
                    /* nodeset vs scalar: any-node numeric compare.
                     * Operand order matters (#965): a nodeset on the
                     * RIGHT means scalar op node-value, not the
                     * reverse. */
                    double scalar = xpath_to_number(other);
                    for (size_t i = 0; !matches && ns && i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        double nv = atof(a);
                        matches = ns_is_left
                            ? op_relational_cmp(op, nv, scalar)
                            : op_relational_cmp(op, scalar, nv);
                        LEPTRIS_FREE(a);
                    }
                    result->value.boolean_value = matches;
                }
            }
            /* String comparison for equality operators when both are strings */
            else if (is_equality_op &&
                     left->type == XPATH_RESULT_STRING &&
                     right->type == XPATH_RESULT_STRING) {
                char* lstr = xpath_to_string(left);
                char* rstr = xpath_to_string(right);
                const char* ls = lstr ? lstr : "";
                const char* rs = rstr ? rstr : "";
                int cmp = strcmp(ls, rs);

                switch (op) {
                    case XPATH_OP_EQUAL: result->value.boolean_value = (cmp == 0); break;
                    case XPATH_OP_NOT_EQUAL: result->value.boolean_value = (cmp != 0); break;
                    default: break;
                }

                if (lstr) LEPTRIS_FREE(lstr);
                if (rstr) LEPTRIS_FREE(rstr);
            }
            /* Numeric comparison (all relational ops and mixed types) */
            else {
                double lval = xpath_to_number(left);
                double rval = xpath_to_number(right);
                switch (op) {
                    case XPATH_OP_EQUAL: result->value.boolean_value = (lval == rval); break;
                    case XPATH_OP_NOT_EQUAL: result->value.boolean_value = (lval != rval); break;
                    case XPATH_OP_LESS: result->value.boolean_value = (lval < rval); break;
                    case XPATH_OP_LESS_EQUAL: result->value.boolean_value = (lval <= rval); break;
                    case XPATH_OP_GREATER: result->value.boolean_value = (lval > rval); break;
                    case XPATH_OP_GREATER_EQUAL: result->value.boolean_value = (lval >= rval); break;
                    default: break;
                }
            }
        }
    }
    /* Logical operators */
    else if (op == XPATH_OP_AND || op == XPATH_OP_OR) {
        int lbool = xpath_to_boolean(left);
        int rbool = xpath_to_boolean(right);
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) {
            result->value.boolean_value = (op == XPATH_OP_AND) ? (lbool && rbool) : (lbool || rbool);
        }
    }
    /* XPath 2.0 node comparisons (#684): apply to the FIRST node
     * of each operand; an empty operand makes the result false.
     * Document order rides the shared rank machinery through a
     * two-node sort. */
    else if (op == XPATH_OP_IS || op == XPATH_OP_NODE_BEFORE ||
             op == XPATH_OP_NODE_AFTER) {
        result = xpath_result_new(XPATH_RESULT_BOOLEAN);
        if (result) {
            void* lnode = NULL;
            void* rnode = NULL;
            if (left->type == XPATH_RESULT_NODESET &&
                left->value.nodeset_value &&
                left->value.nodeset_value->count)
                lnode = left->value.nodeset_value->nodes[0];
            if (right->type == XPATH_RESULT_NODESET &&
                right->value.nodeset_value &&
                right->value.nodeset_value->count)
                rnode = right->value.nodeset_value->nodes[0];
            if (!lnode || !rnode) {
                result->value.boolean_value = 0;
            } else if (op == XPATH_OP_IS) {
                /* synthetic members are deep-copied at each variable
                 * read (let-unwind safety) — pointer identity would
                 * be false for `$x is $x`. Content+kind equality is
                 * the identity proxy for them; DOM nodes keep
                 * pointer identity. */
                int lty = XPATH_NODE_TYPE(lnode);
                int rty = XPATH_NODE_TYPE(rnode);
                if (lty == LEPTRIS_NODE_TEXT ||
                    rty == LEPTRIS_NODE_TEXT) {
                    char* ls = get_node_text(lnode);
                    char* rs = get_node_text(rnode);
                    result->value.boolean_value =
                        lty == rty && ls && rs &&
                        strcmp(ls, rs) == 0;
                    free(ls);
                    free(rs);
                } else {
                    result->value.boolean_value = (lnode == rnode);
                }
            } else if (lnode == rnode) {
                result->value.boolean_value = 0;
            } else {
                XPathNodeSet* pair = xpath_nodeset_new();
                int lb = 0;
                if (pair) {
                    xpath_nodeset_add(pair, lnode);
                    xpath_nodeset_add(pair, rnode);
                    xpath_nodeset_sort_doc_order(ctx, pair, 0);
                    lb = pair->nodes[0] == lnode;
                    xpath_nodeset_free(pair);   /* members borrowed */
                }
                result->value.boolean_value =
                    (op == XPATH_OP_NODE_BEFORE) ? lb : !lb;
            }
        }
    }
    /* XPath 2.0 set algebra (#684): identity membership against the
     * right operand; the left operand's (document) order carries.
     * Membership is a linear scan — operands are small in practice. */
    else if (op == XPATH_OP_INTERSECT || op == XPATH_OP_EXCEPT) {
        if (left->type != XPATH_RESULT_NODESET ||
            right->type != XPATH_RESULT_NODESET) {
            xpath_result_free(left);
            xpath_result_free(right);
            return NULL;
        }
        result = xpath_result_new(XPATH_RESULT_NODESET);
        if (result) {
            XPathNodeSet* ns = xpath_nodeset_new();
            XPathNodeSet* L = left->value.nodeset_value;
            XPathNodeSet* R = right->value.nodeset_value;
            if (ns) {
                int keep = (op == XPATH_OP_INTERSECT);
                for (size_t i = 0; i < L->count; i++) {
                    int found = 0;
                    for (size_t j = 0; j < R->count && !found; j++)
                        found = (L->nodes[i] == R->nodes[j]);
                    if (found == keep)
                        xpath_nodeset_add(ns, L->nodes[i]);
                }
                /* Kept members borrow the left operand's synthetic
                 * nodes — transfer ownership before its free (the
                 * UNION discipline, issue #514). */
                ns->owns_attributes = L->owns_attributes;
                ns->owns_namespaces = L->owns_namespaces;
                ns->owns_synthetic_text = L->owns_synthetic_text;
                L->owns_attributes = 0;
                L->owns_namespaces = 0;
                L->owns_synthetic_text = 0;
                result->value.nodeset_value = ns;
            }
        }
    }
    /* XSLT 3.0 conditional (XPath 2.0+): the children were already
     * evaluated above (they are lazy in the real semantics — see the
     * dedicated branch at the top of this function). */
    /* Union operator */
    else if (op == XPATH_OP_UNION) {
        if (left->type != XPATH_RESULT_NODESET || right->type != XPATH_RESULT_NODESET) {
            xpath_result_free(left);
            xpath_result_free(right);
            return NULL;
        }
        result = xpath_result_new(XPATH_RESULT_NODESET);
        if (result) {
            XPathNodeSet* ns = xpath_nodeset_new();
            /* Concatenate both sides; the sort dedups adjacent
             * duplicates (the old per-candidate linear duplicate
             * scan was O(n^2)). */
            for (size_t i = 0; i < xpath_nodeset_count(left->value.nodeset_value); i++) {
                xpath_nodeset_add(ns, xpath_nodeset_get(left->value.nodeset_value, i));
            }
            for (size_t i = 0; i < xpath_nodeset_count(right->value.nodeset_value); i++) {
                xpath_nodeset_add(ns, xpath_nodeset_get(right->value.nodeset_value, i));
            }

            /* CRITICAL: Sort in document order per XPath 1.0 spec
             * (issue #485: pointer order is not document order). */
            xpath_nodeset_sort_doc_order(ctx, ns, 0);

            /* Issue #514: the concatenated entries BORROW the
             * operands' synthetic nodes (attributes, namespaces,
             * EXSLT text). The operand frees below would release
             * them and leave this nodeset with dangling pointers —
             * transfer the ownership flags before they run. */
            ns->owns_attributes =
                left->value.nodeset_value->owns_attributes ||
                right->value.nodeset_value->owns_attributes;
            ns->owns_namespaces =
                left->value.nodeset_value->owns_namespaces ||
                right->value.nodeset_value->owns_namespaces;
            ns->owns_synthetic_text =
                left->value.nodeset_value->owns_synthetic_text ||
                right->value.nodeset_value->owns_synthetic_text;
            left->value.nodeset_value->owns_attributes = 0;
            left->value.nodeset_value->owns_namespaces = 0;
            left->value.nodeset_value->owns_synthetic_text = 0;
            right->value.nodeset_value->owns_attributes = 0;
            right->value.nodeset_value->owns_namespaces = 0;
            right->value.nodeset_value->owns_synthetic_text = 0;

            result->value.nodeset_value = ns;
        }
    }

    xpath_result_free(left);
    xpath_result_free(right);
    return result;
}
/* Lane 07B: call a function item by its closure content with N
 * arguments given as strings — the HOF combiners (for-each,
 * filter, fold-left, fold-right) and fn:function-lookup dispatch
 * go through here. cc = the synthetic "\x03FRname#arity" or
 * "\x03FN\x02params\x02hex" content; argv strings are borrowed for
 * the duration of the call. */
struct leptris_xpath_result* xpath_call_function_item(
    XPathContext* ctx, const char* cc, char** argv, size_t argc) {
    if (!cc) return NULL;

    if (strncmp(cc, "\x03" "FR", 3) == 0) {
        char name[128];
        snprintf(name, sizeof(name), "%s", cc + 3);
        char* hash = strchr(name, '#');
        if (hash) *hash = 0;

        /* String-literal argument nodes live on the stack and borrow
         * argv; evaluate() strdups ->value, so nothing outlives the
         * call. */
        XPathASTNode argn[16];
        XPathASTNode* child_arr[16];
        size_t na = argc < 16 ? argc : 16;
        for (size_t i = 0; i < na; i++) {
            memset(&argn[i], 0, sizeof(argn[i]));
            argn[i].type = XPATH_AST_STRING;
            argn[i].value = argv[i];
            child_arr[i] = &argn[i];
        }
        XPathASTNode fc;
        memset(&fc, 0, sizeof(fc));
        fc.type = XPATH_AST_FUNCTION_CALL;
        fc.value = name;
        fc.children = child_arr;
        fc.child_count = na;
        return evaluate_function_call_inline(ctx, &fc);
    }

    if (strncmp(cc, "\x03" "FN", 3) != 0) return NULL;

    const char* p = cc + 4;
    const char* pe = strchr(p, '\x02');
    if (!pe || pe[1] == 0) return NULL;
    XPathASTNode* body =
        (XPathASTNode*)(uintptr_t)strtoull(pe + 1, NULL, 16);

    XPathVariableSet* scratch = NULL;
    if (!ctx->variable_set) {
        scratch = xpath_variable_set_new();
        if (!scratch) return NULL;
        ctx->variable_set = scratch;
    }
    const char* param = p;
    size_t ai = 0;
    size_t bound = 0;
    while (param < pe) {
        const char* ne = strchr(param, '\x01');
        if (!ne || ne > pe) ne = pe;
        char pname[128];
        size_t pn = (size_t)(ne - param);
        if (pn >= sizeof(pname)) pn = sizeof(pname) - 1;
        memcpy(pname, param, pn);
        pname[pn] = 0;

        XPathNodeSet* one = xpath_nodeset_new();
        if (!one) break;
        /* set_remove frees the nodeset — synthetic members must be
         * owned (Linux LSan). */
        one->owns_synthetic_text = 1;
        if (ai < argc) {
            XPathTextNode* tn = synth_text(argv[ai], strlen(argv[ai]));
            if (tn) xpath_nodeset_add(one, tn);
        }
        XPathVariable* var = xpath_variable_set_add(
            ctx->variable_set, pname, XPATH_VAR_TYPE_NODE_SET);
        if (var) {
            xpath_variable_set_nodeset(var, one);
            bound++;
        } else {
            xpath_nodeset_free(one);
        }
        if (*ne != '\x01') break;
        param = ne + 1;
        ai++;
    }

    struct leptris_xpath_result* out = body ? evaluate_expr(ctx, body)
                                            : NULL;

    param = p;
    while (bound--) {
        const char* ne = strchr(param, '\x01');
        if (!ne || ne > pe) ne = pe;
        char pname[128];
        size_t pn = (size_t)(ne - param);
        if (pn >= sizeof(pname)) pn = sizeof(pname) - 1;
        memcpy(pname, param, pn);
        pname[pn] = 0;
        xpath_variable_set_remove(ctx->variable_set, pname);
        if (*ne != '\x01') break;
        param = ne + 1;
    }
    if (scratch) {
        ctx->variable_set = NULL;
        xpath_variable_set_free(scratch);
    }
    return out;
}
