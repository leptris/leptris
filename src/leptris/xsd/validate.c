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
extern int xsd_simple_valid(const XsdSimple* simple, const char* v);

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

/* ---- error list ---------------------------------------------------- */

static void verr(struct leptris_xsd_schema* s, const char* fmt,
                 const char* a, const char* b);
struct xsd_validator {
    struct leptris_xsd_schema* s;
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
                      const char* name) {
    if (!type) return 1;
    int r;
    if (strncmp(type, "xs:", 3) == 0)
        r = xsd_builtin_valid(type, lexical);
    else {
        const XsdSimple* st = xsd_find_simple_pub(s, type);
        if (!st) return 1; /* unresolved: accept */
        r = xsd_simple_valid(st, lexical);
    }
    if (r == 0) {
        verrf(v, what, name, "lexical value does not match its type");
        return 0;
    }
    return 1;
}

/* One element: attributes, text, children. */
static void validate_element(struct xsd_validator* v,
                             struct leptris_xsd_schema* s,
                             LeptrisElement elem, int depth);

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
    /* recurse; local particles carry types (first-name-match DFS —
     * position-exact particle typing arrives with the NFA trace). */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(elem));
         c; c = leptris_node_next_sibling(c)) {
        LeptrisElement ce = (LeptrisElement)c;
        const char* cl = NULL, *cp = NULL, *cu = NULL;
        leptris_element_expanded_name(ce, &cl, &cp, &cu);
        if (cl && !xsd_find_element(s, cl) && model) {
            const char* ptype = NULL;
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
                    if (sp < 64 && p->first_child)
                        stack[sp++] = p->first_child;
                }
            }
            if (ptype) {
                const char* text = leptris_element_text(ce);
                if (text && *text)
                    text_valid(v, s, ptype, text, "element", cl);
            }
        }
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        validate_element(v, s, ce, 0);
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
        text_valid(v, s, a->type, val, "attribute", a->name);
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

    if (!d->type) return; /* anyType */

    XsdCm* ct = xsd_find_complex(s, d->type);
    if (ct) {
        validate_attributes(v, s, elem, xsd_find_type_attrs(s, d->type));
        validate_children(v, s, elem, ct);
        return;
    }

    /* simple-typed element: text must be lexically valid */
    if (strncmp(d->type, "xs:", 3) == 0 ||
        xsd_find_simple_pub(s, d->type)) {
        const char* text = leptris_element_text(elem);
        if (text && *text)
            text_valid(v, s, d->type, text, "element", local);
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
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return -1;
    validate_element(v, v->s, root, 0);
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
