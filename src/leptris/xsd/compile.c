/* lib/src/leptris/xsd/compile.c — #1075 XSD tier 1, slice 1: the
 * compilation surface. Parses schema text through the standard XML
 * parser and models the top-level declaration set. Later slices
 * grow the model (datatypes, facets, content models, identity
 * constraints) — this slice's contract is the API shape, the
 * xs:schema identity check, and the declaration enumeration. */
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "xsd_internal.h"

#define XSD_NS "http://www.w3.org/2001/XMLSchema"

extern int xsd_simple_valid(const XsdSimple* simple, const char* v);
extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count);

struct leptris_xsd_schema {
    size_t declaration_count;
    XsdSimple* simple_types;
    XsdElementDecl* elements;
    XsdCm* complex_types; /* name-carrying model roots */
    char* target_ns;
    char* error;
};

/* ---- slice 3: content-model capture ------------------------------ */

static char* xsd_strdup(const char* s);

static XsdCm* xsd_cm_new(XsdCmKind kind) {
    XsdCm* cm = (XsdCm*)calloc(1, sizeof(*cm));
    if (!cm) return NULL;
    cm->kind = kind;
    cm->min = 1;
    cm->max = 1;
    return cm;
}

static void xsd_cm_free(XsdCm* cm) {
    while (cm) {
        XsdCm* next = cm->next;
        xsd_cm_free(cm->first_child);
        free(cm->name);
        free(cm->ns);
        free(cm->any_ns);
        free(cm);
        cm = next;
    }
}

static void xsd_cm_occurrence(XsdCm* cm, LeptrisElement e) {
    const char* mn = leptris_element_attribute(e, "minOccurs");
    if (mn) cm->min = (strcmp(mn, "unbounded") == 0) ? 0 : atoi(mn);
    const char* mx = leptris_element_attribute(e, "maxOccurs");
    if (mx) cm->max = (strcmp(mx, "unbounded") == 0) ? -1 : atoi(mx);
}

/* Recursive model build over sequence | choice | all | element |
 * any children. Returns the child list head; *tail_out receives
 * the list tail for sibling chaining. */
static XsdCm* xsd_cm_parse(LeptrisElement group, XsdCm** tail_out) {
    XsdCm* head = NULL;
    XsdCm* tail = NULL;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(group));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || (uri && strcmp(uri, XSD_NS) != 0)) continue;

        XsdCm* cm = NULL;
        if (strcmp(local, "sequence") == 0 || strcmp(local, "choice") == 0 ||
            strcmp(local, "all") == 0) {
            cm = xsd_cm_new(strcmp(local, "sequence") == 0 ? XSD_CM_SEQ
                            : strcmp(local, "choice") == 0 ? XSD_CM_CHOICE
                                                           : XSD_CM_ALL);
            if (cm) cm->first_child = xsd_cm_parse(e, NULL);
        } else if (strcmp(local, "element") == 0) {
            cm = xsd_cm_new(XSD_CM_ELEMENT);
            if (cm) {
                const char* nm =
                    leptris_element_attribute(e, "name");
                if (!nm) nm = leptris_element_attribute(e, "ref");
                cm->name = xsd_strdup(nm);
            }
        } else if (strcmp(local, "any") == 0) {
            cm = xsd_cm_new(XSD_CM_ANY);
            if (cm) {
                cm->any_ns = xsd_strdup(
                    leptris_element_attribute(e, "namespace"));
                const char* pc = leptris_element_attribute(
                    e, "processContents");
                cm->process_skip =
                    (pc && (strcmp(pc, "skip") == 0 ||
                            strcmp(pc, "lax") == 0)) ? 1 : 0;
            }
        } else {
            continue; /* annotation & friends */
        }
        if (!cm) continue;
        xsd_cm_occurrence(cm, e);
        if (tail)
            tail->next = cm;
        else
            head = cm;
        tail = cm;
    }
    if (tail_out) *tail_out = tail;
    return head;
}

static char* xsd_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* out = (char*)malloc(n);
    if (out) memcpy(out, s, n);
    return out;
}

/* Capture one xs:simpleType's restriction model (slice 2). */
static XsdSimple* xsd_capture_simple(LeptrisElement st) {
    const char* name = leptris_element_attribute(st, "name");
    if (!name) return NULL; /* anonymous types ride their element */

    XsdSimple* simple = (XsdSimple*)calloc(1, sizeof(*simple));
    if (!simple) return NULL;
    simple->name = xsd_strdup(name);
    simple->length = simple->min_length = simple->max_length = -1;
    simple->total_digits = simple->fraction_digits = -1;

    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(st));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (uri && strcmp(uri, XSD_NS) != 0) continue;
        if (!local) continue;
        if (strcmp(local, "restriction") == 0) {
            simple->base =
                xsd_strdup(leptris_element_attribute(e, "base"));
            for (LeptrisNodeRef f = leptris_node_first_child(
                     leptris_element_as_node(e));
                 f; f = leptris_node_next_sibling(f)) {
                if (leptris_node_get_type(f) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement fe = (LeptrisElement)f;
                const char* fl = NULL, *fp = NULL, *fu = NULL;
                leptris_element_expanded_name(fe, &fl, &fp, &fu);
                if (!fl || (fu && strcmp(fu, XSD_NS) != 0)) continue;
                const char* val =
                    leptris_element_attribute(fe, "value");
                if (strcmp(fl, "pattern") == 0)
                    simple->pattern = xsd_strdup(val);
                else if (strcmp(fl, "enumeration") == 0) {
                    XsdFacetValue* fv =
                        (XsdFacetValue*)calloc(1, sizeof(*fv));
                    if (fv) {
                        fv->text = xsd_strdup(val);
                        fv->next = simple->enum_values;
                        simple->enum_values = fv;
                        simple->enum_count++;
                    }
                } else if (strcmp(fl, "minInclusive") == 0)
                    simple->min_inclusive = xsd_strdup(val);
                else if (strcmp(fl, "minExclusive") == 0)
                    simple->min_exclusive = xsd_strdup(val);
                else if (strcmp(fl, "maxInclusive") == 0)
                    simple->max_inclusive = xsd_strdup(val);
                else if (strcmp(fl, "maxExclusive") == 0)
                    simple->max_exclusive = xsd_strdup(val);
                else if (strcmp(fl, "length") == 0)
                    simple->length = val ? strtol(val, NULL, 10) : -1;
                else if (strcmp(fl, "minLength") == 0)
                    simple->min_length = val ? strtol(val, NULL, 10) : -1;
                else if (strcmp(fl, "maxLength") == 0)
                    simple->max_length = val ? strtol(val, NULL, 10) : -1;
                else if (strcmp(fl, "totalDigits") == 0)
                    simple->total_digits = val ? strtol(val, NULL, 10) : -1;
                else if (strcmp(fl, "fractionDigits") == 0)
                    simple->fraction_digits = val ? strtol(val, NULL, 10) : -1;
                else if (strcmp(fl, "whiteSpace") == 0)
                    simple->whitespace = xsd_strdup(val);
            }
        }
    }

    return simple;
}

/* Depth-capped local-base resolution with a cycle guard: the
 * restriction chain walks to its built-in, facets of EVERY hop
 * apply (XSD semantics). */
static const XsdSimple* xsd_find_simple(struct leptris_xsd_schema* s,
                                        const char* name) {
    for (XsdSimple* t = s->simple_types; t; t = t->next)
        if (strcmp(t->name, name) == 0) return t;
    return NULL;
}

static int xsd_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v, int depth) {
    if (!t || depth > 32) return 0;
    if (t->base && strncmp(t->base, "xs:", 3) != 0) {
        const XsdSimple* next = xsd_find_simple(s, t->base);
        if (next && !xsd_valid_chain(s, next, v, depth + 1)) return 0;
    }
    return xsd_simple_valid(t, v);
}

static void xsd_set_error(struct leptris_xsd_schema* s, const char* msg) {
    free(s->error);
    s->error = NULL;
    if (msg) {
        size_t n = strlen(msg) + 1;
        s->error = (char*)malloc(n);
        if (s->error) memcpy(s->error, msg, n);
    }
}

/* A top-level schema-language declaration component. */
static int xsd_is_declaration(const char* local, const char* uri) {
    if (uri && strcmp(uri, XSD_NS) != 0) return 0;
    if (!local) return 0;
    static const char* names[] = {
        "element",    "attribute",    "simpleType", "complexType",
        "group",      "attributeGroup", "notation", "include",
        "import",     "redefine",     NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(local, names[i]) == 0) return 1;
    return 0;
}

LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile(const char* xsd_text,
                                                 size_t len,
                                                 LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!xsd_text || len == 0) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }

    struct leptris_xsd_schema* s =
        (struct leptris_xsd_schema*)calloc(1, sizeof(*s));
    if (!s) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }

    /* target namespace (xs:any ##other/##targetNamespace filters) */
    s->target_ns = NULL;

    LeptrisDocument doc = leptris_parse_string(xsd_text, len, status);
    if (!doc) {
        xsd_set_error(s, "schema document is not well-formed XML");
        goto fail;
    }

    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        xsd_set_error(s, "schema document has no root element");
        goto fail;
    }
    s->target_ns = xsd_strdup(
        leptris_element_attribute(root, "targetNamespace"));

    const char* local = NULL;
    const char* prefix = NULL;
    const char* uri = NULL;
    leptris_element_expanded_name(root, &local, &prefix, &uri);
    /* Accept the XML Schema namespace under any prefix, and the
     * unprefixed spelling libxml2 fixtures use without a default
     * xmlns (compilation is namespace-driven from here on). */
    if (!local || strcmp(local, "schema") != 0 ||
        (uri && strcmp(uri, XSD_NS) != 0)) {
        xsd_set_error(s, "root element is not xs:schema");
        goto fail;
    }

    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        leptris_element_expanded_name((LeptrisElement)n, &local, &prefix,
                                      &uri);
        if (!xsd_is_declaration(local, uri)) continue;
        s->declaration_count++;
        if (strcmp(local, "element") == 0 && uri && !strcmp(uri, XSD_NS)) {
            const char* nm = leptris_element_attribute((LeptrisElement)n,
                                                       "name");
            const char* ty = leptris_element_attribute((LeptrisElement)n,
                                                       "type");
            if (nm && ty) {
                XsdElementDecl* d =
                    (XsdElementDecl*)calloc(1, sizeof(*d));
                if (d) {
                    d->name = xsd_strdup(nm);
                    d->type = xsd_strdup(ty);
                    d->next = s->elements;
                    s->elements = d;
                }
            }
        } else if (strcmp(local, "complexType") == 0 && uri &&
                   !strcmp(uri, XSD_NS)) {
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdCm* root =
                    xsd_cm_new(XSD_CM_SEQ); /* wrapper keeps the name */
                if (root) {
                    root->name = xsd_strdup(nm);
                    root->first_child =
                        xsd_cm_parse((LeptrisElement)n, NULL);
                    root->next = s->complex_types;
                    s->complex_types = root;
                }
            }
        } else if (strcmp(local, "simpleType") == 0 && uri && !strcmp(uri, XSD_NS)) {
            XsdSimple* captured = xsd_capture_simple((LeptrisElement)n);
            if (captured) {
                captured->next = s->simple_types;
                s->simple_types = captured;
            }
        }
    }

    leptris_document_free(doc);
    return s;

fail:
    if (doc) leptris_document_free(doc);
    if (status) *status = LEPTRIS_ERROR_PARSE;
    /* Failure returns an error-carrying handle (the documented
     * contract): leptris_xsd_error says why, free it the same way. */
    return (LeptrisXsdSchema)s;
}

LEPTRIS_API void leptris_xsd_free(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return;
    xsd_cm_free(s->complex_types);
    XsdElementDecl* d = s->elements;
    while (d) {
        XsdElementDecl* dn = d->next;
        free(d->name);
        free(d->type);
        free(d);
        d = dn;
    }
    free(s->target_ns);
    XsdSimple* t = s->simple_types;
    while (t) {
        XsdSimple* next = t->next;
        free(t->name);
        free(t->base);
        free(t->pattern);
        XsdFacetValue* fv = t->enum_values;
        while (fv) {
            XsdFacetValue* fn = fv->next;
            free(fv->text);
            free(fv);
            fv = fn;
        }
        free(t->min_inclusive);
        free(t->min_exclusive);
        free(t->max_inclusive);
        free(t->max_exclusive);
        free(t->whitespace);
        free(t);
        t = next;
    }
    free(s->error);
    free(s);
}

/* ---- slice 2: lexical validation --------------------------------- */

LEPTRIS_API int leptris_xsd_builtin_valid(const char* builtin,
                                          const char* lexical) {
    extern int xsd_builtin_valid(const char* type, const char* v);
    return xsd_builtin_valid(builtin, lexical);
}

LEPTRIS_API int leptris_xsd_simple_valid(LeptrisXsdSchema schema,
                                         const char* type_name,
                                         const char* lexical) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !type_name || !lexical) return 0;
    const XsdSimple* t = xsd_find_simple(s, type_name);
    if (!t) return -1; /* unknown type */
    return xsd_valid_chain(s, t, lexical, 0);
}

LEPTRIS_API int leptris_xsd_content_valid(
    LeptrisXsdSchema schema, const char* element_name,
    const char* const* child_names, const char* const* child_ns,
    size_t child_count) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !element_name) return -1;

    const XsdElementDecl* d = s->elements;
    while (d && strcmp(d->name, element_name) != 0) d = d->next;
    if (!d) return -1; /* unknown element */

    if (!d->type) return 1; /* no type attr: anyType accepts all */

    /* built-in or simpleType ref: element content is text-only —
     * any element child fails */
    if (strncmp(d->type, "xs:", 3) == 0 ||
        xsd_find_simple(s, d->type))
        return child_count == 0;

    XsdCm* ct = s->complex_types;
    while (ct && strcmp(ct->name, d->type) != 0) ct = ct->next;
    if (!ct) return child_count == 0; /* unknown type: accept-empty */

    XsdCm* model = ct->first_child;
    if (!model) return child_count == 0;

    return xsd_content_valid(s, model, s->target_ns, child_names,
                             child_ns, child_count);
}

LEPTRIS_API size_t leptris_xsd_declaration_count(LeptrisXsdSchema schema) {
    if (!schema) return 0;
    return ((struct leptris_xsd_schema*)schema)->declaration_count;
}

LEPTRIS_API const char* leptris_xsd_error(LeptrisXsdSchema schema) {
    if (!schema) return NULL;
    return ((struct leptris_xsd_schema*)schema)->error;
}
