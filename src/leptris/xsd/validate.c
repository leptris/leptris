/* xsd/validate.c — #1075 slice 4: instance-document validation.
 *
 * Walks an instance tree against the compiled schema: element
 * declarations resolve by local name (global declarations), their
 * types drive attribute checking (declared rows, required use),
 * text-content lexical checks (builtins + user simpleTypes), and
 * child-sequence content-model checks (slice 3's NFA). Failures
 * accumulate in the schema-owned error list — the RNG accessor
 * pattern: validate returns 1/0, errors enumerate every miss.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "xsd_internal.h"

extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count);
extern int xsd_builtin_valid(const char* type, const char* v);
extern int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                                  const XsdSimple* t, const char* v);

struct xsd_error {
    char* text;
    struct xsd_error* next;
};

struct leptris_xsd_schema; /* opaque here; compile.c owns the struct */

/* The validator reaches the compiled model through accessors the
 * compiler exports (compile.c defines the struct). */
extern const XsdElementDecl* xsd_find_element(
    struct leptris_xsd_schema* s, const char* name);
extern XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,
                               const char* type_name);
extern const XsdSimple* xsd_find_simple_pub(
    struct leptris_xsd_schema* s, const char* name);
extern const char* xsd_target_ns(struct leptris_xsd_schema* s);
extern const XsdTypeAttrs* xsd_find_type_attrs(
    struct leptris_xsd_schema* s, const char* type_name);
extern const XsdElementIcs* xsd_find_element_ics(
    struct leptris_xsd_schema* s, const char* element_name);

/* ---- error list ---------------------------------------------------- */

static void verr(struct leptris_xsd_schema* s, const char* fmt,
                 const char* a, const char* b);
struct xsd_validator {
    struct leptris_xsd_schema* s;
    LeptrisDocument doc; /* the instance (assertion evaluation) */
    struct xsd_error* errors;
    struct xsd_error* tail;
    size_t count;
};

static void verr_push(struct xsd_validator* v, const char* msg) {
    struct xsd_error* e = (struct xsd_error*)malloc(sizeof(*e));
    if (!e) return;
    size_t n = strlen(msg) + 1;
    e->text = (char*)malloc(n);
    if (!e->text) {
        free(e);
        return;
    }
    memcpy(e->text, msg, n);
    e->next = NULL;
    if (v->tail)
        v->tail->next = e;
    else
        v->errors = e;
    v->tail = e;
    v->count++;
}

static void verrf(struct xsd_validator* v, const char* what,
                  const char* name, const char* detail) {
    /* "element 'x': detail" — bounded composition */
    char buf[256];
    int w = snprintf(buf, sizeof(buf), "%s '%s': %s", what,
                     name ? name : "?", detail ? detail : "invalid");
    (void)w;
    verr_push(v, buf);
}

/* ---- lexical helpers ---------------------------------------------- */

static int text_valid(struct xsd_validator* v,
                      struct leptris_xsd_schema* s, const char* type,
                      const char* lexical, const char* what,
                      const char* name, LeptrisElement elem) {
    if (!type) return 1;
    int r;
    const XsdSimple* st = NULL;
    if (strncmp(type, "xs:", 3) == 0)
        r = xsd_builtin_valid(type, lexical);
    else {
        st = xsd_find_simple_pub(s, type);
        if (!st) return 1; /* unresolved: accept */
        r = xsd_simple_valid_chain(s, st, lexical);
    }
    if (r == 0) {
        verrf(v, what, name, "lexical value does not match its type");
        return 0;
    }
    /* XSD 1.1 xs:assertion facet (document validation only):
     * $value binds the lexical — numeric lexicals bind bare,
     * everything else as a string — and the context is the
     * owning element. */
    if (st && st->assertion && elem && v->doc) {
        XsdSimple* mut = (XsdSimple*)st;
        if (!mut->assertion_tried) {
            mut->assertion_tried = 1;
            if (xsd_schema_version_11(s)) {
                /* eval_params binds DECLARED external variables —
                 * the test compiles under a $value declaration */
                char wrapped[1024];
                int w = snprintf(wrapped, sizeof(wrapped),
                                 "declare variable $value external; %s",
                                 mut->assertion);
                if (w > 0 && (size_t)w < sizeof(wrapped))
                    mut->assertion_q =
                        leptris_xquery_parse(wrapped, (size_t)w);
            }
        }
        if (mut->assertion_q) {
            char sel[256];
            int numeric = 1;
            for (const char* p = lexical; *p; p++)
                if (!isdigit((unsigned char)*p) && *p != '.' &&
                    *p != '-' && *p != '+' && *p != 'e' && *p != 'E')
                    numeric = 0;
            if (numeric && *lexical)
                snprintf(sel, sizeof(sel), "%s", lexical);
            else {
                size_t w = 0;
                sel[w++] = '\'';
                for (const char* p = lexical; *p && w < sizeof(sel) - 3;
                     p++) {
                    if (*p == '\'') sel[w++] = '\'';
                    sel[w++] = *p;
                }
                sel[w++] = '\'';
                sel[w] = 0;
            }
            const char* names[1] = {"value"};
            const char* sels[1] = {sel};
            LeptrisXPathResult r2 = leptris_xquery_eval_params(
                mut->assertion_q, v->doc, elem, names, sels, 1);
            int ebv = 0;
            if (r2) {
                ebv = leptris_xpath_result_boolean(r2);
                leptris_xpath_result_free(r2);
            }
            if (!ebv) {
                verrf(v, what, name, "assertion failed");
                return 0;
            }
        }
    }
    return 1;
}

/* One element: attributes, text, children. */
static void validate_element(struct xsd_validator* v,
                             struct leptris_xsd_schema* s,
                             LeptrisElement elem, int depth);
static void validate_local(struct xsd_validator* v,
                           struct leptris_xsd_schema* s,
                           LeptrisElement elem, const char* local,
                           const char* type);

static void validate_children(struct xsd_validator* v,
                              struct leptris_xsd_schema* s,
                              LeptrisElement elem, XsdCm* ct) {
    XsdCm* model = ct->first_child;
    if (!model) return;
    /* collect child names + uris */
    size_t n = 0;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c))
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) n++;
    if (!n) return;
    const char** names = (const char**)malloc(n * sizeof(char*));
    const char** uris = (const char**)calloc(n, sizeof(char*));
    if (!names || !uris) {
        free(names);
        free(uris);
        return;
    }
    size_t i = 0;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(ce, &local, &prefix, &uri);
        names[i] = local ? local : "";
        uris[i] = uri;
        i++;
    }
    int ok = xsd_content_valid(s, model, xsd_target_ns(s), names, uris,
                               n);
    free(names);
    free(uris);
    if (!ok) {
        const char* local = NULL;
        leptris_element_expanded_name(elem, &local, NULL, NULL);
        verrf(v, "element", local, "children do not match the content model");
    }
    /* recurse: a global declaration governs the child when one
     * exists; otherwise the parent's particle declaration does
     * (first-name-match DFS — position-exact particle typing
     * arrives with the NFA trace). */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ce = (LeptrisElement)c;
        const char* cl = NULL, *cp = NULL, *cu = NULL;
        leptris_element_expanded_name(ce, &cl, &cp, &cu);
        if (!cl) continue;
        if (xsd_find_element(s, cl)) {
            validate_element(v, s, ce, 0);
            continue;
        }
        const char* ptype = NULL;
        if (model) {
            XsdCm* stack[64];
            int sp = 0;
            stack[sp++] = model;
            while (sp > 0 && ptype == NULL) {
                XsdCm* p = stack[--sp];
                for (; p; p = p->next) {
                    if (p->kind == XSD_CM_ELEMENT && p->name &&
                        strcmp(p->name, cl) == 0 && p->type) {
                        ptype = p->type;
                        break;
                    }
                    if (p->kind == XSD_CM_GROUP_REF) {
                        XsdGroupDef* g = xsd_find_group(s, p->name);
                        if (g && g->model && sp < 64)
                            stack[sp++] = g->model;
                    }
                    if (sp < 64 && p->first_child)
                        stack[sp++] = p->first_child;
                }
            }
        }
        if (ptype)
            validate_local(v, s, ce, cl, ptype);
    }
}

static void validate_attributes(struct xsd_validator* v,
                                struct leptris_xsd_schema* s,
                                LeptrisElement elem,
                                const XsdTypeAttrs* ta) {
    if (!ta) return;
    /* declared rows: present? value typed? */
    for (XsdAttrDecl* a = ta->attrs; a; a = a->next) {
        const char* val = leptris_element_attribute(elem, a->name);
        if (!val) {
            /* lenient: the qualified spelling (#1586 semantics) */
            for (LeptrisAttribute at =
                     leptris_element_first_attribute(elem);
                 at; at = leptris_attribute_next(at)) {
                const char* qn = leptris_attribute_get_name(at);
                const char* colon = qn ? strchr(qn, ':') : NULL;
                if (qn && colon && strcmp(colon + 1, a->name) == 0) {
                    val = leptris_attribute_get_value(elem, at);
                    break;
                }
                if (qn && !colon && strcmp(qn, a->name) == 0) {
                    val = leptris_attribute_get_value(elem, at);
                    break;
                }
            }
        }
        if (!val) {
            if (a->required)
                verrf(v, "attribute", a->name, "required but absent");
            continue;
        }
        text_valid(v, s, a->type, val, "attribute", a->name, elem);
        if (a->fixed && strcmp(val, a->fixed) != 0)
            verrf(v, "attribute", a->name,
                  "value does not match the fixed value");
    }
}

static void validate_element(struct xsd_validator* v,
                             struct leptris_xsd_schema* s,
                             LeptrisElement elem, int depth) {
    if (depth > 64) return;
    const char* local = NULL, *prefix = NULL, *uri = NULL;
    leptris_element_expanded_name(elem, &local, &prefix, &uri);
    if (!local) return;
    const XsdElementDecl* d = xsd_find_element(s, local);
    if (!d) return; /* undeclared: lax at this depth (strict later) */

    /* XSD 1.1: the first alternative whose test passes types the
     * element for this instance (context = the element). */
    const char* eff_type = d->type;
    for (XsdAlternative* alt = d->alternatives; alt; alt = alt->next) {
        int ebv = 0;
        if (alt->compiled && v->doc) {
            LeptrisXPathResult r =
                leptris_xquery_eval(alt->compiled, v->doc, elem);
            if (r) {
                ebv = leptris_xpath_result_boolean(r);
                leptris_xpath_result_free(r);
            }
        }
        if (ebv) {
            eff_type = alt->type;
            break;
        }
    }
    if (!eff_type) return; /* anyType */
    if (d->fixed) {
        const char* text = leptris_element_text(elem);
        if (text && *text && strcmp(text, d->fixed) != 0)
            verrf(v, "element", local,
                  "content does not match the fixed value");
    }
    validate_local(v, s, elem, local, eff_type);
}

/* Validate one element against a type name that comes from a
 * global declaration or a local particle: attributes and content
 * model for complex types, lexical text otherwise. */
static void validate_local(struct xsd_validator* v,
                           struct leptris_xsd_schema* s,
                           LeptrisElement elem, const char* local,
                           const char* type) {
    if (!type) return;
    XsdCm* ct = xsd_find_complex(s, type);
    if (ct) {
        validate_attributes(v, s, elem, xsd_find_type_attrs(s, type));
        validate_children(v, s, elem, ct);
        /* XSD 1.1: the type's xs:assert list — context is the
         * element; evaluation errors are failures */
        for (XsdAssert* a = ct->asserts; a; a = a->next) {
            int ebv = 0;
            if (a->compiled && v->doc) {
                LeptrisXPathResult r =
                    leptris_xquery_eval(a->compiled, v->doc, elem);
                if (r) {
                    ebv = leptris_xpath_result_boolean(r);
                    leptris_xpath_result_free(r);
                }
            }
            if (!ebv)
                verrf(v, "assertion", local, a->test ? a->test
                                                     : "(invalid)");
        }
        if (ct->text_type) {
            const char* text = leptris_element_text(elem);
            if (text && *text)
                text_valid(v, s, ct->text_type, text, "element",
                           local, elem);
        }
        return;
    }

    /* simple-typed element: text must be lexically valid */
    if (strncmp(type, "xs:", 3) == 0 ||
        xsd_find_simple_pub(s, type)) {
        const char* text = leptris_element_text(elem);
        if (text && *text)
            text_valid(v, s, type, text, "element", local, elem);
    }
}

/* ---- slice 6: identity constraints -------------------------------- */

/* A key tuple: the field values of one selected node. */
typedef struct xsd_key_tuple {
    char* values[8];
    size_t count;
    struct xsd_key_tuple* next;
} XsdKeyTuple;

/* All tuples collected for one constraint name, keyed by name. */
typedef struct xsd_key_table {
    char* name;
    XsdKeyTuple* tuples;
    XsdKeyTuple* tail;
    struct xsd_key_table* next;
} XsdKeyTable;

static XsdKeyTable* table_find(XsdKeyTable* head, const char* name) {
    for (; head; head = head->next)
        if (strcmp(head->name, name) == 0) return head;
    return NULL;
}

static char* xsd_dup_str(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

static void table_add(XsdKeyTable** head, const char* name,
                      char* const* values, size_t count) {
    XsdKeyTable* t = table_find(*head, name);
    if (!t) {
        t = (XsdKeyTable*)calloc(1, sizeof(*t));
        if (!t) return;
        size_t n = strlen(name) + 1;
        t->name = (char*)malloc(n);
        if (!t->name) { free(t); return; }
        memcpy(t->name, name, n);
        t->next = *head;
        *head = t;
    }
    XsdKeyTuple* tup = (XsdKeyTuple*)calloc(1, sizeof(*tup));
    if (!tup) return;
    for (size_t i = 0; i < count && i < 8; i++)
        tup->values[i] = xsd_dup_str(values[i]);
    tup->count = count < 8 ? count : 8;
    if (t->tail)
        t->tail->next = tup;
    else
        t->tuples = tup;
    t->tail = tup;
}

/* Evaluate one IC against the scoping element. pass1: collect
 * key/unique tuples and enforce uniqueness (key: also non-empty
 * fields). pass2: keyref tuples resolve against collected tables. */
static void validate_ics(struct xsd_validator* v,
                         struct leptris_xsd_schema* s,
                         LeptrisElement elem, const char* elem_name,
                         XsdKeyTable** tables, int pass) {
    (void)v;
    const XsdElementIcs* ics = xsd_find_element_ics(s, elem_name);
    if (!ics) return;
    extern struct leptris_document* leptris_element_get_document(
        LeptrisElement elem);
    LeptrisDocument doc = leptris_element_get_document(elem);
    for (XsdIc* ic = ics->constraints; ic; ic = ic->next) {
        int is_ref = ic->kind == 2;
        if (pass == 1 && is_ref) continue;
        if (pass == 2 && !is_ref) continue;
        if (!ic->selector || !ic->name) continue;
        LeptrisXPathResult sel =
            leptris_xpath_eval(doc, elem, ic->selector);
        if (!sel) continue;
        size_t sel_count = leptris_xpath_result_count(sel);
        for (size_t i = 0; i < sel_count; i++) {
            LeptrisElement node = leptris_xpath_result_get(sel, i);
            if (!node) continue;
            /* field values: string of each field expression */
            char* vals[8] = {0};
            size_t nv = 0;
            int missing = 0;
            for (size_t f = 0; f < ic->field_count && f < 8; f++) {
                LeptrisXPathResult fr =
                    leptris_xpath_eval(doc, node, ic->fields[f]);
                if (!fr || leptris_xpath_result_count(fr) == 0) {
                    missing = 1;
                    if (fr) leptris_xpath_result_free(fr);
                    break;
                }
                /* String-value of the first node — attribute nodes
                 * included (leptris_xpath_result_string handles every
                 * node kind; #3526 contract). */
                char* sv = leptris_xpath_result_string(fr);
                leptris_xpath_result_free(fr);
                vals[nv] = xsd_dup_str(sv ? sv : "");
                leptris_free_string(sv);
                nv++;
            }
            for (size_t k = 0; k < nv; k++)
                if (!vals[k] || !vals[k][0]) missing = 1;
            if (missing) {
                if (ic->kind == 0)
                    verrf(v, "key", ic->name,
                          "a selected node is missing a field value");
                for (size_t k = 0; k < nv; k++) free(vals[k]);
                continue;
            }
            if (pass == 1) {
                /* uniqueness within this constraint */
                XsdKeyTable* t = table_find(*tables, ic->name);
                int dup = 0;
                if (t) {
                    for (XsdKeyTuple* tup = t->tuples; tup;
                         tup = tup->next) {
                        int eq = tup->count == nv;
                        for (size_t k = 0; eq && k < nv; k++)
                            if (strcmp(tup->values[k], vals[k]) != 0)
                                eq = 0;
                        if (eq) { dup = 1; break; }
                    }
                }
                if (dup) {
                    verrf(v, ic->kind == 0 ? "key" : "unique",
                          ic->name, "duplicate key tuple");
                }
                table_add(tables, ic->name, vals, nv);
            } else {
                /* keyref: resolve later (deferred to pass 2 end) —
                 * store under the REFER name */
                table_add(tables, ic->refer ? ic->refer : "",
                          vals, nv);
            }
            for (size_t k = 0; k < nv; k++) free(vals[k]);
        }
        leptris_xpath_result_free(sel);
    }
}

/* Pass 2 tail: every keyref tuple must appear in the referenced
 * key/unique table. */
static void resolve_keyrefs(struct xsd_validator* v,
                            struct leptris_xsd_schema* s,
                            XsdKeyTable* all, XsdKeyTable* refs) {
    (void)s;
    for (XsdKeyTable* rt = refs; rt; rt = rt->next) {
        XsdKeyTable* target = table_find(all, rt->name);
        if (!target) continue; /* unknown refer: capture-time lax */
        for (XsdKeyTuple* want = rt->tuples; want; want = want->next) {
            int found = 0;
            for (XsdKeyTuple* have = target->tuples; have;
                 have = have->next) {
                int eq = have->count == want->count;
                for (size_t k = 0; eq && k < want->count; k++)
                    if (strcmp(have->values[k], want->values[k]) != 0)
                        eq = 0;
                if (eq) { found = 1; break; }
            }
            if (!found)
                verrf(v, "keyref", rt->name,
                      "reference does not resolve to any key");
        }
    }
}

static void tables_free(XsdKeyTable* t) {
    while (t) {
        XsdKeyTable* n = t->next;
        XsdKeyTuple* tup = t->tuples;
        while (tup) {
            XsdKeyTuple* tn = tup->next;
            for (size_t k = 0; k < tup->count; k++) free(tup->values[k]);
            free(tup);
            tup = tn;
        }
        free(t->name);
        free(t);
        t = n;
    }
}

/* Walk every element carrying constraints in both passes. */
static void ic_walk(struct xsd_validator* v, struct leptris_xsd_schema* s,
                    LeptrisElement elem, XsdKeyTable** keys,
                    XsdKeyTable** refs, int pass) {
    const char* local = NULL;
    leptris_element_expanded_name(elem, &local, NULL, NULL);
    if (local) validate_ics(v, s, elem, local, pass == 1 ? keys : refs, pass);
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        ic_walk(v, s, (LeptrisElement)c, keys, refs, pass);
    }
}

struct xsd_validator* xsd_validator_new(struct leptris_xsd_schema* s) {
    struct xsd_validator* v =
        (struct xsd_validator*)calloc(1, sizeof(*v));
    if (v) v->s = s;
    return v;
}

void xsd_validator_free(struct xsd_validator* v) {
    if (!v) return;
    struct xsd_error* e = v->errors;
    while (e) {
        struct xsd_error* n = e->next;
        free(e->text);
        free(e);
        e = n;
    }
    free(v);
}

int xsd_validator_run(struct xsd_validator* v, LeptrisDocument doc) {
    if (!v || !doc) return -1;
    v->doc = doc; /* XSD 1.1: assertion evaluation context */
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return -1;
    validate_element(v, v->s, root, 0);

    /* slice 6: identity constraints — collect (pass 1), resolve
     * keyrefs (pass 2). */
    XsdKeyTable* keys = NULL;
    XsdKeyTable* refs = NULL;
    ic_walk(v, v->s, root, &keys, &refs, 1);
    ic_walk(v, v->s, root, &keys, &refs, 2);
    resolve_keyrefs(v, v->s, keys, refs);
    tables_free(keys);
    tables_free(refs);

    return v->count == 0 ? 1 : 0;
}

size_t xsd_validator_error_count(struct xsd_validator* v) {
    return v ? v->count : 0;
}

const char* xsd_validator_error_at(struct xsd_validator* v, size_t i) {
    if (!v) return NULL;
    struct xsd_error* e = v->errors;
    for (size_t k = 0; e && k < i; k++) e = e->next;
    return e ? e->text : NULL;
}
