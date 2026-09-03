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
            int is_num_member = mc && mc[0] == '\x03' && mc[1] == 'N';
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
        op0 != XPATH_OP_DOCUMENT_CTOR)
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
                char* piece = xpath_to_string(item);
                xpath_result_free(item);
                XPathTextNode* tn = synth_text(piece ? piece : "",
                                               piece ? strlen(piece) : 0);
                free(piece);
                if (tn) xpath_nodeset_add(out, tn);
            }
            /* The variable OWNS the nodeset after set_nodeset —
             * remove frees it; do not double-free. */
            if (pos_var)
                xpath_variable_set_remove(ctx->variable_set, pos_var);
            xpath_variable_set_remove(ctx->variable_set, loop_var);
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
                    /* set_nodeset overwrites without freeing —
                     * cover the name a previous tuple bound. */
                    if (var->value.v.nodeset_value)
                        xpath_nodeset_free(var->value.v.nodeset_value);
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
    if (op == XPATH_OP_TEXT_CTOR || op == XPATH_OP_ATTRIBUTE_CTOR ||
        op == XPATH_OP_ELEMENT_CTOR) {
        if (op != XPATH_OP_ELEMENT_CTOR) {
            char* s = NULL;
            if (ast->child_count >= 1) {
                struct leptris_xpath_result* v =
                    evaluate_expr(ctx, ast->children[0]);
                s = v ? xpath_to_string(v) : NULL;
                if (v) xpath_result_free(v);
            }
            if (!s) s = leptris_strdup("");
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
        struct leptris_xpath_result* out =
            xpath_result_new(XPATH_RESULT_STRING);
        if (!out) { free(val); return NULL; }
        out->value.string_value = val ? val : leptris_strdup("");
        return out;
    }

    /* 3.1 square array constructor `[ a, b, ... ]` (08C): members
     * in order on the shared map representation with positional
     * keys — every array:* accessor is the map operation with a
     * formatted index. */
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
            char buf[28];
            int l = snprintf(buf, sizeof buf, "\x03N%ld", v);
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
                                        mc[1] == 'N';
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
                char* piece = xpath_to_string(item);
                if (item->type == XPATH_RESULT_NUMBER) {
                    /* "\x03N" marks numeric members for per-member
                     * type checks (instance of); get_node_text
                     * strips it for string consumers. */
                    size_t pl = piece ? strlen(piece) : 0;
                    char* marked = (char*)malloc(pl + 3);
                    if (marked) {
                        marked[0] = '\x03'; marked[1] = 'N';
                        if (pl) memcpy(marked + 2, piece, pl);
                        marked[2 + pl] = 0;
                        XPathTextNode* tn = synth_text(marked, pl + 2);
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
                result->value.boolean_value = (lnode == rnode);
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
