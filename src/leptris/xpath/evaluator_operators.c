/* evaluator_operators.c - XPath operator evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * All XPath 1.0 operators: arithmetic, comparison, logical, union
 */

#include "evaluator_internal.h"
#include "../leptris_internal.h"
#include "../dom/element.h"  /* For LeptrisElement structure */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Full-lifetime copy of a nodeset: pointers are shared for document
 * nodes, but synthetic text members are DEEP-copied when the source
 * owns them — the copy outlives the source's storage (let bindings
 * are unwound while results referencing them still live). */
XPathNodeSet* xpath_nodeset_deep_copy(const XPathNodeSet* src) {
    if (!src) return NULL;
    XPathNodeSet* dst = xpath_nodeset_new_with_capacity(src->count);
    if (!dst) return NULL;
    if (!src->owns_synthetic_text) {
        for (size_t i = 0; i < src->count; i++)
            xpath_nodeset_add(dst, src->nodes[i]);
        return dst;
    }
    dst->owns_synthetic_text = 1;
    for (size_t i = 0; i < src->count; i++) {
        void* n = src->nodes[i];
        if (n && XPATH_NODE_TYPE(n) == LEPTRIS_NODE_TEXT) {
            char* txt = get_node_text(n);
            size_t len = txt ? strlen(txt) : 0;
            XPathTextNode* tn = xpath_synth_text(txt ? txt : "", len);
            free(txt);
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

struct leptris_xpath_result* evaluate_operator(XPathContext* ctx,
                                              XPathASTNode* ast) {
    if (!ast || ast->child_count < 1) return NULL;

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
        for (size_t i = 0; i < n; i++) {
            XPathVariable* var = xpath_variable_set_add(
                ctx->variable_set, ast->value, XPATH_VAR_TYPE_NODE_SET);
            if (!var) break;
            XPathNodeSet* one = xpath_nodeset_new();
            if (!one) break;
            if (ns) {
                xpath_nodeset_add(one, ns->nodes[i]);
            } else {
                char* sv = xpath_to_string(domain);
                XPathTextNode* tn =
                    synth_text(sv ? sv : "", sv ? strlen(sv) : 0);
                free(sv);
                if (tn) xpath_nodeset_add(one, tn);
            }
            xpath_variable_set_nodeset(var, one);

            struct leptris_xpath_result* item =
                evaluate_expr(ctx, ast->children[1]);
            if (item) {
                char* piece = xpath_to_string(item);
                xpath_result_free(item);
                XPathTextNode* tn = synth_text(piece ? piece : "",
                                               piece ? strlen(piece) : 0);
                free(piece);
                if (tn) xpath_nodeset_add(out, tn);
            }
            /* The variable OWNS the nodeset after set_nodeset —
             * remove frees it; do not double-free. */
            xpath_variable_set_remove(ctx->variable_set, ast->value);
        }
        xpath_result_free(domain);
        if (scratch) ctx->variable_set = NULL;
        xpath_variable_set_free(scratch);

        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
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
            char* v = vr ? xpath_to_string(vr) : NULL;
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

        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) { xpath_result_free(left); return NULL; }
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;

        XPathNodeSet* ns = (left->type == XPATH_RESULT_NODESET)
                               ? left->value.nodeset_value : NULL;
        size_t n = ns ? ns->count : 1;

        struct leptris_element* saved_node = ctx->context_node;
        size_t saved_pos = ctx->context_position;
        size_t saved_size = ctx->context_size;

        for (size_t i = 0; i < n; i++) {
            ctx->context_node = ns ? (struct leptris_element*)ns->nodes[i]
                                   : ctx->context_node;
            ctx->context_position = i + 1;
            ctx->context_size = n;
            struct leptris_xpath_result* item =
                evaluate_expr(ctx, ast->children[1]);
            if (item) {
                char* piece = xpath_to_string(item);
                xpath_result_free(item);
                XPathTextNode* tn =
                    synth_text(piece ? piece : "", piece ? strlen(piece) : 0);
                free(piece);
                if (tn) xpath_nodeset_add(out, tn);
            } else {
                ctx->context_node = saved_node;
                ctx->context_position = saved_pos;
                ctx->context_size = saved_size;
                xpath_result_free(left);
                xpath_nodeset_free(out);
                return NULL;
            }
        }
        ctx->context_node = saved_node;
        ctx->context_position = saved_pos;
        ctx->context_size = saved_size;

        xpath_result_free(left);
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (!result) { xpath_nodeset_free(out); return NULL; }
        result->value.nodeset_value = out;
        return result;
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
            char* tv = leptris_xpath_result_string(test);
            double tn = leptris_xpath_result_number(test);
            int hit = (ov && tv && strcmp(ov, tv) == 0) ||
                      (!ov && !tv) || (on == tn && ov && tv &&
                                       strcmp(ov, "NaN") != 0);
            leptris_xpath_result_free(test);
            free(tv);
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
        long lo = (long)xpath_to_number(lo_r);
        long hi = (long)xpath_to_number(hi_r);
        xpath_result_free(lo_r);
        xpath_result_free(hi_r);

        XPathNodeSet* out = xpath_nodeset_new();
        if (!out) return NULL;
        out->owns_synthetic_text = 1;
        out->is_sequence = 1;
        for (long v = lo; v <= hi && v - lo < 100000; v++) {
            char buf[24];
            int l = snprintf(buf, sizeof buf, "%ld", v);
            XPathTextNode* tn = synth_text(buf, (size_t)l);
            if (!tn) break;
            xpath_nodeset_add(out, tn);
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
        char base[80];
        if (tlen >= sizeof(base)) tlen = sizeof(base) - 1;
        memcpy(base, ty, tlen);
        base[tlen] = '\0';

        if (op == XPATH_OP_INSTANCE_OF) {
            struct leptris_xpath_result* out =
                xpath_result_new(XPATH_RESULT_BOOLEAN);
            if (!out) { xpath_result_free(v); return NULL; }
            int m = 0;
            if (strcmp(base, "node()") == 0) {
                m = v->type == XPATH_RESULT_NODESET &&
                    v->value.nodeset_value &&
                    (occ != '+' || v->value.nodeset_value->count > 0);
            } else if (strcmp(base, "item()") == 0) {
                m = v->type != XPATH_RESULT_NODESET ||
                    !v->value.nodeset_value ||
                    v->value.nodeset_value->count > 0 ||
                    occ == '*' || occ == '?';
            } else if (strcmp(base, "xs:string") == 0 ||
                       strcmp(base, "xs:anyURI") == 0 ||
                       strncmp(base, "xs:date", 7) == 0 ||
                       strcmp(base, "xs:time") == 0 ||
                       strcmp(base, "xs:duration") == 0) {
                m = v->type == XPATH_RESULT_STRING;
            } else if (strcmp(base, "xs:boolean") == 0) {
                m = v->type == XPATH_RESULT_BOOLEAN;
            } else {
                /* xs:integer/double/decimal/float — numeric family. */
                m = v->type == XPATH_RESULT_NUMBER;
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
                double d = xpath_to_number(v);
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
                char* piece = xpath_to_string(item);
                XPathTextNode* tn = synth_text(piece ? piece : "",
                                               piece ? strlen(piece) : 0);
                free(piece);
                if (tn) xpath_nodeset_add(out, tn);
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
        op == XPATH_OP_DIV || op == XPATH_OP_MOD) {
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
                struct leptris_xpath_result* other =
                    (left->type == XPATH_RESULT_NODESET) ? right : left;
                XPathNodeSet* ns = (left->type == XPATH_RESULT_NODESET)
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
                    /* nodeset vs nodeset: any-pair. */
                    XPathNodeSet* on = other->value.nodeset_value;
                    for (size_t i = 0; !matches && ns && i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        for (size_t j = 0; !matches && on && j < on->count; j++) {
                            char* b = get_node_text(on->nodes[j]);
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
                    /* nodeset vs scalar: any-node numeric compare. */
                    double scalar = xpath_to_number(other);
                    for (size_t i = 0; !matches && ns && i < ns->count; i++) {
                        char* a = get_node_text(ns->nodes[i]);
                        if (!a) continue;
                        matches = op_relational_cmp(op, atof(a), scalar);
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