/* lib/src/leptris/xsd/compile.c — #1075 XSD tier 1, slice 1: the
 * compilation surface. Parses schema text through the standard XML
 * parser and models the top-level declaration set. Later slices
 * grow the model (datatypes, facets, content models, identity
 * constraints) — this slice's contract is the API shape, the
 * xs:schema identity check, and the declaration enumeration. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/leptris.h"
#include "xsd_internal.h"

#define XSD_NS "http://www.w3.org/2001/XMLSchema"

void xsd_capture_ics(struct leptris_xsd_schema* s,
                     LeptrisElement elem, const char* element_name);
extern int xsd_simple_valid(const XsdSimple* simple, const char* v);
extern struct xsd_validator* xsd_validator_new(
    struct leptris_xsd_schema* s);
extern void xsd_validator_free(struct xsd_validator* v);
extern int xsd_validator_run(struct xsd_validator* v, LeptrisDocument doc);
extern size_t xsd_validator_error_count(struct xsd_validator* v);
extern const char* xsd_validator_error_at(struct xsd_validator* v,
                                          size_t i);

extern int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count);

struct leptris_xsd_schema {
    size_t declaration_count;
    XsdSimple* simple_types;
    XsdElementDecl* elements;
    XsdCm* complex_types; /* name-carrying model roots */
    XsdGroupDef* groups;  /* top-level xs:group definitions */
    XsdCm* default_open;  /* xs:defaultOpenContent wildcard */
    int default_oc_empty; /* appliesToEmpty="true" */
    XsdAttrGroupDef* attr_groups;
    XsdAttrDecl* top_attrs; /* top-level xs:attribute declarations */
    XsdTypeAttrs* type_attrs;
    XsdElementIcs* element_ics;
    char* target_ns;
    int version_11;    /* xs:schema @version = "1.1" */
    char* base_dir;    /* schemaLocation resolution (file compile) */
    int include_depth; /* include/import recursion guard */
    void* validator; /* slice-4 parked error list */
    char* error;
};

/* ---- slice 3: content-model capture ------------------------------ */

static char* xsd_strdup(const char* s);

/* #1592: capture an INLINE anonymous xs:complexType / xs:simpleType
 * under a synthesized name ("element:NAME" — ':' cannot appear in
 * xs: NCName type names, so the slot is collision-free). Returns 1
 * when a capture happened. Forward decls below; definitions later
 * in this file. */
static int xsd_capture_inline(struct leptris_xsd_schema* s,
                              LeptrisElement elem, const char* synth);
static XsdCm* xsd_cm_new(XsdCmKind kind);
static void xsd_cm_occurrence(XsdCm* cm, LeptrisElement e);
static XsdCm* xsd_cm_parse(struct leptris_xsd_schema* s,
                           LeptrisElement group, XsdCm** tail_out);
static XsdAttrDecl* xsd_capture_attr_rows(struct leptris_xsd_schema* s,
                                          LeptrisElement container,
                                          int depth);
extern XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,
                               const char* type_name);
extern const XsdTypeAttrs* xsd_find_type_attrs(
    struct leptris_xsd_schema* s, const char* type_name);
static XsdCm* xsd_cm_copy_list(const XsdCm* head);
static void xsd_capture_root(struct leptris_xsd_schema* s,
                             LeptrisElement root);
static char* xsd_read_file(const char* path, size_t* out_len);
static XsdAttrDecl* xsd_copy_attr_rows(const XsdAttrDecl* src);
static void xsd_register_complex(struct leptris_xsd_schema* s,
                                 XsdCm* root, XsdAttrDecl* attrs,
                                 const char* name);
static XsdCm* xsd_capture_complex_model(struct leptris_xsd_schema* s,
                                        LeptrisElement ct,
                                        const char* name);
static XsdAssert* xsd_capture_assert(struct leptris_xsd_schema* s,
                                     LeptrisElement e);
static XsdCm* xsd_capture_any(LeptrisElement any_child);
static void xsd_capture_alternatives(struct leptris_xsd_schema* s,
                                     LeptrisElement decl_elem,
                                     XsdElementDecl* d);
static XsdSimple* xsd_capture_simple(LeptrisElement st);


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
        XsdAssert* a = cm->asserts;
        while (a) {
            XsdAssert* an = a->next;
            if (a->compiled) leptris_xquery_free(a->compiled);
            free(a->test);
            free(a);
            a = an;
        }
        if (cm->oc_owned && cm->open_any) xsd_cm_free(cm->open_any);
        xsd_cm_free(cm->first_child);
        free(cm->name);
        free(cm->type);
        free(cm->ns);
        free(cm->any_ns);
        free(cm->text_type);
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
/* #1592: capture an INLINE anonymous xs:complexType / xs:simpleType
 * under a synthesized name ("element:NAME" — ':' cannot appear in
 * xs: NCName type names, so the slot is collision-free). Returns 1
 * when a capture happened. */
static int xsd_capture_inline(struct leptris_xsd_schema* s,
                              LeptrisElement elem, const char* synth) {
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(elem));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
        if (strcmp(local, "complexType") == 0) {
            return xsd_capture_complex_model(s, e, synth) ? 1 : 0;
        }
        if (strcmp(local, "simpleType") == 0) {
            XsdSimple* captured = xsd_capture_simple(e);
            if (!captured) return 0;
            free(captured->name);
            captured->name = xsd_strdup(synth);
            captured->next = s->simple_types;
            s->simple_types = captured;
            return 1;
        }
    }
    return 0;
}

static XsdCm* xsd_cm_parse(struct leptris_xsd_schema* s,
                           LeptrisElement group, XsdCm** tail_out) {
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
            if (cm) cm->first_child = xsd_cm_parse(s, e, NULL);
        } else if (strcmp(local, "element") == 0) {
            cm = xsd_cm_new(XSD_CM_ELEMENT);
            if (cm) {
                const char* nm =
                    leptris_element_attribute(e, "name");
                if (!nm) nm = leptris_element_attribute(e, "ref");
                cm->name = xsd_strdup(nm);
                const char* pty =
                    leptris_element_attribute(e, "type");
                if (pty) {
                    cm->type = xsd_strdup(pty);
                } else if (s && nm) {
                    /* #1592: inline anonymous type on the particle */
                    char synth[512];
                    snprintf(synth, sizeof(synth), "element:%s", nm);
                    if (xsd_capture_inline(s, e, synth))
                        cm->type = xsd_strdup(synth);
                }
            }
        } else if (strcmp(local, "group") == 0) {
            /* <xs:group ref="g"/> — expanded at NFA build so
             * forward references need no capture order */
            cm = xsd_cm_new(XSD_CM_GROUP_REF);
            if (cm)
                cm->name = xsd_strdup(
                    leptris_element_attribute(e, "ref"));
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
    /* Anonymous types ride their element — allowed: the caller
     * renames the captured entry into its synthesized slot
     * (#1592). */

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
        if (strcmp(local, "list") == 0) {
            simple->deriv = XSD_SIMPLE_LIST;
            const char* it = leptris_element_attribute(e, "itemType");
            if (it && *it) {
                simple->item_type = xsd_strdup(it);
            } else {
                /* inline anonymous item type rides this capture */
                for (LeptrisNodeRef q = leptris_node_first_child(
                         leptris_element_as_node(e));
                     q && !simple->item_def;
                     q = leptris_node_next_sibling(q)) {
                    if (leptris_node_get_type(q) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement qe = (LeptrisElement)q;
                    const char* ql = NULL;
                    leptris_element_expanded_name(qe, &ql, NULL, NULL);
                    if (ql && strcmp(ql, "simpleType") == 0)
                        simple->item_def = xsd_capture_simple(qe);
                }
            }
        } else if (strcmp(local, "union") == 0) {
            simple->deriv = XSD_SIMPLE_UNION;
            const char* mt =
                leptris_element_attribute(e, "memberTypes");
            if (mt && *mt) {
                size_t n = 0;
                for (const char* p = mt; *p;) {
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (!*p) break;
                    while (*p && !isspace((unsigned char)*p)) p++;
                    n++;
                }
                if (n) {
                    simple->members = (char**)calloc(n, sizeof(char*));
                    if (simple->members) {
                        size_t i = 0;
                        const char* p = mt;
                        while (*p && i < n) {
                            while (*p && isspace((unsigned char)*p)) p++;
                            if (!*p) break;
                            const char* start = p;
                            while (*p && !isspace((unsigned char)*p)) p++;
                            size_t len = (size_t)(p - start);
                            char* tok = (char*)malloc(len + 1);
                            if (!tok) break;
                            memcpy(tok, start, len);
                            tok[len] = 0;
                            simple->members[i++] = tok;
                        }
                        simple->member_count = i;
                    }
                }
            }
            /* inline anonymous members ride this capture */
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (!ql || strcmp(ql, "simpleType") != 0) continue;
                XsdSimple* def = xsd_capture_simple(qe);
                if (!def) continue;
                XsdSimple** grown = (XsdSimple**)realloc(
                    simple->member_defs,
                    (simple->member_def_count + 1) *
                        sizeof(XsdSimple*));
                if (!grown) break;
                simple->member_defs = grown;
                simple->member_defs[simple->member_def_count++] = def;
            }
        } else if (strcmp(local, "restriction") == 0) {
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
                else if (strcmp(fl, "assertion") == 0)
                    simple->assertion = xsd_strdup(
                        leptris_element_attribute(fe, "test"));
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

extern int xsd_builtin_valid(const char* type, const char* v);
extern int rng_regex_matches(const char* pat, const char* text);
static int xsd_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v, int depth);

/* One list item or union member: a builtin reference, a local
 * schema type (chain-walked), or an inline capture. Unresolved
 * names accept (the compile-time leniency the corpus gate pins). */
static int xsd_item_valid(struct leptris_xsd_schema* s,
                          const char* name, const XsdSimple* def,
                          const char* v, int depth) {
    if (name && strncmp(name, "xs:", 3) == 0)
        return xsd_builtin_valid(name, v);
    if (def) return xsd_valid_chain(s, def, v, depth + 1);
    if (name) {
        const XsdSimple* next = xsd_find_simple(s, name);
        if (next) return xsd_valid_chain(s, next, v, depth + 1);
    }
    return 1;
}

static int xsd_deriv_valid(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v,
                           int depth) {
    if (t->deriv == XSD_SIMPLE_LIST) {
        char* dup = xsd_strdup(v);
        if (!dup) return 0;
        size_t n = 0;
        int ok = 1;
        for (char* p = dup; *p;) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char* end = p;
            while (*end && !isspace((unsigned char)*end)) end++;
            char was = *end;
            *end = 0;
            if (ok && !xsd_item_valid(s, t->item_type, t->item_def, p,
                                      depth))
                ok = 0;
            *end = was;
            p = end;
            n++;
        }
        free(dup);
        /* length family counts ITEMS for lists (XSD semantics) */
        if (ok && t->length >= 0 && (long)n != t->length) ok = 0;
        if (ok && t->min_length >= 0 && (long)n < t->min_length) ok = 0;
        if (ok && t->max_length >= 0 && (long)n > t->max_length) ok = 0;
        /* pattern applies to the whole lexical */
        if (ok && t->pattern && !rng_regex_matches(t->pattern, v))
            ok = 0;
        return ok;
    }
    if (t->deriv == XSD_SIMPLE_UNION) {
        for (size_t i = 0; i < t->member_count; i++)
            if (xsd_item_valid(s, t->members[i], NULL, v, depth))
                return 1;
        for (size_t i = 0; i < t->member_def_count; i++)
            if (xsd_item_valid(s, NULL, t->member_defs[i], v, depth))
                return 1;
        return 0;
    }
    return xsd_simple_valid(t, v);
}

static int xsd_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v, int depth) {
    if (!t || depth > 32) return 0;
    if (t->base && strncmp(t->base, "xs:", 3) != 0) {
        const XsdSimple* next = xsd_find_simple(s, t->base);
        if (next && !xsd_valid_chain(s, next, v, depth + 1)) return 0;
    }
    if (t->deriv != XSD_SIMPLE_RESTRICT)
        return xsd_deriv_valid(s, t, v, depth);
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
        "defaultOpenContent",
        "import",     "redefine",     NULL
    };
    for (int i = 0; names[i]; i++)
        if (strcmp(local, names[i]) == 0) return 1;
    return 0;
}

static char* xsd_read_file(const char* path, size_t* out_len) {
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    *out_len = got;
    return buf;
}

static LeptrisXsdSchema xsd_compile_impl(const char* xsd_text,
                                         size_t len,
                                         const char* base_dir,
                                         LeptrisStatus* status);

LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile(const char* xsd_text,
                                                 size_t len,
                                                 LeptrisStatus* status) {
    return xsd_compile_impl(xsd_text, len, NULL, status);
}

LEPTRIS_API LeptrisXsdSchema leptris_xsd_compile_file(
    const char* path, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!path || !*path) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    size_t len = 0;
    char* text = xsd_read_file(path, &len);
    if (!text) {
        if (status) *status = LEPTRIS_ERROR_PARSE;
        return NULL;
    }
    /* relative schemaLocations resolve against the schema's own
     * directory (both separators: Windows paths carry '\') */
    const char* slash = strrchr(path, '/');
    const char* bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    char* base_dir = NULL;
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n == 0) n = 1; /* "/f.xsd" roots at "/" */
        base_dir = (char*)malloc(n + 1);
        if (base_dir) {
            memcpy(base_dir, path, n);
            base_dir[n] = 0;
        }
    } else {
        base_dir = xsd_strdup(".");
    }
    LeptrisXsdSchema s = xsd_compile_impl(text, len, base_dir, status);
    free(base_dir);
    free(text);
    return s;
}

static LeptrisXsdSchema xsd_compile_impl(const char* xsd_text,
                                         size_t len,
                                         const char* base_dir,
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
    s->base_dir = xsd_strdup(base_dir);

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
    {
        const char* ver = leptris_element_attribute(root, "version");
        s->version_11 = (ver && strcmp(ver, "1.1") == 0);
    }

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

    xsd_capture_root(s, root);

    leptris_document_free(doc);
    return s;

fail:
    if (doc) leptris_document_free(doc);
    if (status) *status = LEPTRIS_ERROR_PARSE;
    /* Failure returns an error-carrying handle (the documented
     * contract): leptris_xsd_error says why, free it the same way. */
    return (LeptrisXsdSchema)s;
}

/* Every top-level declaration under an xs:schema root. xs:include
 * / xs:import / xs:redefine resolve their schemaLocation against
 * the owning schema's base_dir (string-compiled schemas have none
 * and skip them); the referenced root's declarations merge into
 * the same tables. */
static void xsd_capture_root(struct leptris_xsd_schema* s,
                             LeptrisElement root) {
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(root));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        const char* local = NULL;
        const char* prefix = NULL;
        const char* uri = NULL;
        leptris_element_expanded_name((LeptrisElement)n, &local, &prefix,
                                      &uri);
        if (!xsd_is_declaration(local, uri)) continue;
        s->declaration_count++;

        if (strcmp(local, "include") == 0 ||
            strcmp(local, "import") == 0 ||
            strcmp(local, "redefine") == 0) {
            const char* loc = leptris_element_attribute(
                (LeptrisElement)n, "schemaLocation");
            if (loc && s->base_dir && s->include_depth < 16) {
                char path[4096];
                snprintf(path, sizeof(path), "%s/%s", s->base_dir,
                         loc);
                size_t flen = 0;
                char* text = xsd_read_file(path, &flen);
                if (text) {
                    LeptrisDocument sub =
                        leptris_parse_string(text, flen, NULL);
                    if (sub) {
                        LeptrisElement subroot =
                            leptris_document_root(sub);
                        const char* tns =
                            subroot
                                ? leptris_element_attribute(
                                      subroot, "targetNamespace")
                                : NULL;
                        /* same-namespace merges and no-namespace
                         * chameleon includes; foreign-namespace
                         * imports stay lax at this tier */
                        if (subroot &&
                            (!tns || !s->target_ns ||
                             strcmp(tns, s->target_ns) == 0)) {
                            s->include_depth++;
                            xsd_capture_root(s, subroot);
                            s->include_depth--;
                        }
                        leptris_document_free(sub);
                    }
                    free(text);
                }
            }
            continue;
        }

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
                    xsd_capture_alternatives(s, (LeptrisElement)n, d);
                    d->fixed = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "fixed"));
                    const char* sg = leptris_element_attribute(
                        (LeptrisElement)n, "substitutionGroup");
                    if (sg) {
                        const char* colon = strchr(sg, ':');
                        d->sub_head = xsd_strdup(
                            colon ? colon + 1 : sg);
                    }
                    d->next = s->elements;
                    s->elements = d;
                }
                xsd_capture_ics(s, (LeptrisElement)n, nm);
            } else if (nm) {
                /* slice 6: identity constraints ride the element. */
                xsd_capture_ics(s, (LeptrisElement)n, nm);
                /* #1592: inline anonymous type — capture under the
                 * synthesized slot and point the declaration at it. */
                char synth[512];
                snprintf(synth, sizeof(synth), "element:%s", nm);
                if (xsd_capture_inline(s, (LeptrisElement)n, synth)) {
                    XsdElementDecl* d =
                        (XsdElementDecl*)calloc(1, sizeof(*d));
                    if (d) {
                        d->name = xsd_strdup(nm);
                        d->type = xsd_strdup(synth);
                        d->next = s->elements;
                        s->elements = d;
                    }
                }
            }
        } else if (strcmp(local, "complexType") == 0 && uri &&
                   !strcmp(uri, XSD_NS)) {
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm)
                xsd_capture_complex_model(s, (LeptrisElement)n, nm);
        } else if (strcmp(local, "defaultOpenContent") == 0 && uri &&
                   !strcmp(uri, XSD_NS)) {
            if (s->version_11) {
                const char* ape = leptris_element_attribute(
                    (LeptrisElement)n, "appliesToEmpty");
                for (LeptrisNodeRef q = leptris_node_first_child(
                         leptris_element_as_node((LeptrisElement)n));
                     q && !s->default_open;
                     q = leptris_node_next_sibling(q)) {
                    if (leptris_node_get_type(q) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement qe = (LeptrisElement)q;
                    const char* ql = NULL;
                    leptris_element_expanded_name(qe, &ql, NULL, NULL);
                    if (ql && strcmp(ql, "any") == 0)
                        s->default_open = xsd_capture_any(qe);
                }
                if (s->default_open)
                    s->default_oc_empty =
                        (ape && strcmp(ape, "true") == 0);
            }
            continue;
        } else if (strcmp(local, "attribute") == 0 && uri && !strcmp(uri, XSD_NS)) {
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
                if (d) {
                    d->name = xsd_strdup(nm);
                    d->type = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "type"));
                    d->fixed = xsd_strdup(
                        leptris_element_attribute((LeptrisElement)n,
                                                  "fixed"));
                    const char* use = leptris_element_attribute(
                        (LeptrisElement)n, "use");
                    d->required = (use && strcmp(use, "required") == 0);
                    d->next = s->top_attrs;
                    s->top_attrs = d;
                }
            }
        } else if (strcmp(local, "attributeGroup") == 0 && uri && !strcmp(uri, XSD_NS)) {
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdAttrGroupDef* g =
                    (XsdAttrGroupDef*)calloc(1, sizeof(*g));
                if (g) {
                    g->name = xsd_strdup(nm);
                    g->attrs = xsd_capture_attr_rows(
                        s, (LeptrisElement)n, 0);
                    g->next = s->attr_groups;
                    s->attr_groups = g;
                }
            }
        } else if (strcmp(local, "group") == 0 && uri && !strcmp(uri, XSD_NS)) {
            const char* nm =
                leptris_element_attribute((LeptrisElement)n, "name");
            if (nm) {
                XsdGroupDef* g = (XsdGroupDef*)calloc(1, sizeof(*g));
                if (g) {
                    g->name = xsd_strdup(nm);
                    g->model = xsd_cm_parse(s, (LeptrisElement)n, NULL);
                    g->next = s->groups;
                    s->groups = g;
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
}

static void xsd_free_simple_one(XsdSimple* t) {
    if (!t) return;
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
    if (t->assertion_q) leptris_xquery_free(t->assertion_q);
    free(t->assertion);
    free(t->item_type);
    xsd_free_simple_one(t->item_def);
    for (size_t i = 0; i < t->member_count; i++) free(t->members[i]);
    free(t->members);
    for (size_t i = 0; i < t->member_def_count; i++)
        xsd_free_simple_one(t->member_defs[i]);
    free(t->member_defs);
    free(t);
}

LEPTRIS_API void leptris_xsd_free(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return;
    xsd_cm_free(s->complex_types);
    XsdElementIcs* slot = s->element_ics;
    while (slot) {
        XsdElementIcs* sn = slot->next;
        XsdIc* ic = slot->constraints;
        while (ic) {
            XsdIc* in = ic->next;
            free(ic->name);
            free(ic->selector);
            for (size_t f = 0; f < ic->field_count; f++)
                free(ic->fields[f]);
            free(ic->refer);
            free(ic);
            ic = in;
        }
        free(slot->element_name);
        free(slot);
        slot = sn;
    }
    XsdTypeAttrs* ta = s->type_attrs;
    while (ta) {
        XsdTypeAttrs* tn = ta->next;
        XsdAttrDecl* a = ta->attrs;
        while (a) {
            XsdAttrDecl* an = a->next;
            free(a->name);
            free(a->type);
            free(a->fixed);
            free(a);
            a = an;
        }
        free(ta->type_name);
        free(ta);
        ta = tn;
    }
    XsdElementDecl* d = s->elements;
    while (d) {
        XsdElementDecl* dn = d->next;
        XsdAlternative* a = d->alternatives;
        while (a) {
            XsdAlternative* an = a->next;
            if (a->compiled) leptris_xquery_free(a->compiled);
            free(a->test);
            free(a->type);
            free(a);
            a = an;
        }
        free(d->name);
        free(d->type);
        free(d->fixed);
        free(d->sub_head);
        free(d);
        d = dn;
    }
    XsdAttrGroupDef* ag = s->attr_groups;
    while (ag) {
        XsdAttrGroupDef* agn = ag->next;
        free(ag->name);
        XsdAttrDecl* a = ag->attrs;
        while (a) {
            XsdAttrDecl* an2 = a->next;
            free(a->name);
            free(a->type);
            free(a->fixed);
            free(a);
            a = an2;
        }
        free(ag);
        ag = agn;
    }
    XsdAttrDecl* ta_row = s->top_attrs;
    while (ta_row) {
        XsdAttrDecl* ta_next = ta_row->next;
        free(ta_row->name);
        free(ta_row->type);
        free(ta_row->fixed);
        free(ta_row);
        ta_row = ta_next;
    }
    XsdGroupDef* g = s->groups;
    while (g) {
        XsdGroupDef* gn = g->next;
        free(g->name);
        xsd_cm_free(g->model);
        free(g);
        g = gn;
    }
    if (s->default_open) xsd_cm_free(s->default_open);
    free(s->target_ns);
    free(s->base_dir);
    if (s->validator)
        xsd_validator_free((struct xsd_validator*)s->validator);
    XsdSimple* t = s->simple_types;
    while (t) {
        XsdSimple* next = t->next;
        xsd_free_simple_one(t);
        t = next;
    }
    free(s->error);
    free(s);
}

/* ---- slice 2: lexical validation --------------------------------- */

static XsdCm* xsd_cm_copy_list(const XsdCm* head) {
    XsdCm* out = NULL;
    XsdCm* out_tail = NULL;
    for (const XsdCm* src = head; src; src = src->next) {
        XsdCm* d = (XsdCm*)calloc(1, sizeof(*d));
        if (!d) break;
        d->kind = src->kind;
        d->name = xsd_strdup(src->name);
        d->type = xsd_strdup(src->type);
        d->ns = xsd_strdup(src->ns);
        d->any_ns = xsd_strdup(src->any_ns);
        d->process_skip = src->process_skip;
        d->min = src->min;
        d->max = src->max;
        d->first_child = xsd_cm_copy_list(src->first_child);
        if (out_tail)
            out_tail->next = d;
        else
            out = d;
        out_tail = d;
    }
    return out;
}

static void xsd_register_complex(struct leptris_xsd_schema* s,
                                 XsdCm* root, XsdAttrDecl* attrs,
                                 const char* name) {
    if (!root) return;
    root->name = xsd_strdup(name);
    root->next = s->complex_types;
    s->complex_types = root;
    if (attrs) {
        XsdTypeAttrs* ta = (XsdTypeAttrs*)calloc(1, sizeof(*ta));
        if (ta) {
            ta->type_name = xsd_strdup(name);
            ta->attrs = attrs;
            ta->next = s->type_attrs;
            s->type_attrs = ta;
        }
    }
}

/* XSD 1.1: xs:alternative children of an element declaration. */
static void xsd_capture_alternatives(struct leptris_xsd_schema* s,
                                     LeptrisElement decl_elem,
                                     XsdElementDecl* d) {
    if (!s->version_11) return;
    XsdAlternative* tail = NULL;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(decl_elem));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL;
        leptris_element_expanded_name(e, &local, NULL, NULL);
        if (!local || strcmp(local, "alternative") != 0) continue;
        const char* test = leptris_element_attribute(e, "test");
        const char* type = leptris_element_attribute(e, "type");
        if (!test || !type) continue;
        XsdAlternative* a = (XsdAlternative*)calloc(1, sizeof(*a));
        if (!a) continue;
        a->test = xsd_strdup(test);
        a->type = xsd_strdup(type);
        a->compiled = leptris_xquery_parse(test, strlen(test));
        if (tail)
            tail->next = a;
        else
            d->alternatives = a;
        tail = a;
    }
}

static XsdCm* xsd_capture_any(LeptrisElement any_child) {
    XsdCm* w = xsd_cm_new(XSD_CM_ANY);
    if (!w) return NULL;
    w->any_ns = xsd_strdup(
        leptris_element_attribute(any_child, "namespace"));
    const char* pc = leptris_element_attribute(any_child,
                                               "processContents");
    w->process_skip =
        (pc && (strcmp(pc, "skip") == 0 || strcmp(pc, "lax") == 0))
            ? 1
            : 0;
    w->min = 0;
    w->max = -1;
    return w;
}

static XsdAssert* xsd_capture_assert(struct leptris_xsd_schema* s,
                                     LeptrisElement e) {
    const char* test = leptris_element_attribute(e, "test");
    if (!test || !*test) return NULL;
    XsdAssert* a = (XsdAssert*)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->test = xsd_strdup(test);
    if (s->version_11) {
        a->compiled = leptris_xquery_parse(test, strlen(test));
        /* NULL = syntax error: evaluation reports the assertion
         * as failed (XSD 1.1: an error is a failure) */
    }
    return a;
}

/* Append every xs:assert child under `container` to the type. */
static void xsd_capture_assert_children(
    struct leptris_xsd_schema* s, LeptrisElement container,
    XsdCm* root) {
    XsdAssert* tail = root->asserts;
    while (tail && tail->next) tail = tail->next;
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(container));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL;
        leptris_element_expanded_name(e, &local, NULL, NULL);
        if (!local || strcmp(local, "assert") != 0) continue;
        XsdAssert* a = xsd_capture_assert(s, e);
        if (!a) continue;
        if (!root->asserts) root->asserts = a;
        tail = a;
    }
}

/* One complexType capture, shared by top-level declarations and
 * inline anonymous types: content model + derivation
 * (complexContent/simpleContent extension|restriction) + the
 * attribute rows the derivation carries. */
static XsdCm* xsd_capture_complex_model(struct leptris_xsd_schema* s,
                                        LeptrisElement ct,
                                        const char* name) {
    XsdCm* root = xsd_cm_new(XSD_CM_SEQ); /* wrapper keeps the name */
    if (!root) return NULL;
    XsdAttrDecl* attrs = NULL;

    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(ct));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;

        if (strcmp(local, "complexContent") == 0) {
            /* extension|restriction child carries base + model */
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement dq = (LeptrisElement)q;
                const char* dl = NULL;
                leptris_element_expanded_name(dq, &dl, NULL, NULL);
                if (!dl || (strcmp(dl, "extension") != 0 &&
                            strcmp(dl, "restriction") != 0))
                    continue;
                const char* base =
                    leptris_element_attribute(dq, "base");
                XsdCm* base_root =
                    (base && strncmp(base, "xs:", 3) != 0)
                        ? xsd_find_complex(s, base)
                        : NULL;
                if (strcmp(dl, "extension") == 0) {
                    /* base content first, derived content after */
                    root->first_child = xsd_cm_copy_list(
                        base_root ? base_root->first_child : NULL);
                    XsdCm* own_tail = root->first_child;
                    while (own_tail && own_tail->next)
                        own_tail = own_tail->next;
                    XsdCm* own = xsd_cm_parse(s, dq, NULL);
                    if (own_tail)
                        own_tail->next = own;
                    else if (!root->first_child)
                        root->first_child = own;
                    if (base_root) {
                        const XsdTypeAttrs* bt = xsd_find_type_attrs(
                            s, base);
                        attrs = xsd_copy_attr_rows(bt ? bt->attrs
                                                      : NULL);
                    }
                    XsdAttrDecl* own_attrs =
                        xsd_capture_attr_rows(s, dq, 0);
                    if (own_attrs) {
                        XsdAttrDecl* at = attrs;
                        while (at && at->next) at = at->next;
                        if (at)
                            at->next = own_attrs;
                        else
                            attrs = own_attrs;
                    }
                } else {
                    /* restriction: the derived model REPLACES */
                    root->first_child = xsd_cm_parse(s, dq, NULL);
                    attrs = xsd_capture_attr_rows(s, dq, 0);
                }
                /* XSD 1.1: asserts nested under the derivation */
                if (s->version_11)
                    xsd_capture_assert_children(s, dq, root);
            }
        } else if (strcmp(local, "simpleContent") == 0) {
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q; q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement dq = (LeptrisElement)q;
                const char* dl = NULL;
                leptris_element_expanded_name(dq, &dl, NULL, NULL);
                if (!dl || (strcmp(dl, "extension") != 0 &&
                            strcmp(dl, "restriction") != 0))
                    continue;
                const char* base =
                    leptris_element_attribute(dq, "base");
                if (base) {
                    if (strncmp(base, "xs:", 3) == 0) {
                        root->text_type = xsd_strdup(base);
                    } else {
                        /* restriction facets under a local base
                         * ride a synthesized "complex:NAME" slot */
                        int has_facets = 0;
                        for (LeptrisNodeRef f =
                                 leptris_node_first_child(
                                     leptris_element_as_node(dq));
                             f; f = leptris_node_next_sibling(f)) {
                            if (leptris_node_get_type(f) !=
                                LEPTRIS_NODE_TYPE_ELEMENT)
                                continue;
                            const char* fl = NULL;
                            leptris_element_expanded_name(
                                (LeptrisElement)f, &fl, NULL, NULL);
                            if (fl && strcmp(fl, "facet") != 0 &&
                                strcmp(fl, "simpleType") != 0)
                                has_facets = 1;
                        }
                        XsdSimple* captured =
                            has_facets
                                ? xsd_capture_simple(dq)
                                : NULL;
                        if (captured) {
                            char synth[512];
                            snprintf(synth, sizeof(synth),
                                     "complex:%s", name);
                            free(captured->name);
                            captured->name = xsd_strdup(synth);
                            captured->next = s->simple_types;
                            s->simple_types = captured;
                            root->text_type = xsd_strdup(synth);
                        } else {
                            root->text_type = xsd_strdup(base);
                        }
                    }
                }
                attrs = xsd_capture_attr_rows(s, dq, 0);
            }
        }
    }
    if (!root->first_child && !root->text_type) {
        /* no derivation: the direct-content spelling. A derivation
         * that captured attributes but produced no element model
         * (e.g. an attributes-only restriction) already set them —
         * re-capturing would orphan those rows. */
        root->first_child = xsd_cm_parse(s, ct, NULL);
        if (!attrs) attrs = xsd_capture_attr_rows(s, ct, 0);
    }
    /* XSD 1.1: openContent on the type, else the schema default */
    if (s->version_11) {
        for (LeptrisNodeRef n =
                 leptris_node_first_child(leptris_element_as_node(ct));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement e = (LeptrisElement)n;
            const char* ocl = NULL;
            leptris_element_expanded_name(e, &ocl, NULL, NULL);
            if (!ocl || strcmp(ocl, "openContent") != 0) continue;
            const char* mode = leptris_element_attribute(e, "mode");
            for (LeptrisNodeRef q = leptris_node_first_child(
                     leptris_element_as_node(e));
                 q && !root->open_any;
                 q = leptris_node_next_sibling(q)) {
                if (leptris_node_get_type(q) !=
                    LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement qe = (LeptrisElement)q;
                const char* ql = NULL;
                leptris_element_expanded_name(qe, &ql, NULL, NULL);
                if (ql && strcmp(ql, "any") == 0) {
                    root->open_any = xsd_capture_any(qe);
                    if (root->open_any) root->oc_owned = 1;
                }
            }
            if (mode && strcmp(mode, "suffix") == 0)
                root->oc_mode = 1;
            else if (mode && strcmp(mode, "interleave") == 0)
                root->oc_mode = 2;
            else
                root->oc_mode = 0;
        }
    }
    if (!root->oc_mode && s->default_open && root->first_child)
        root->oc_mode = 2;
    if (!root->oc_mode && s->default_open && !root->first_child &&
        s->default_oc_empty)
        root->oc_mode = 2;
    if (root->oc_mode == 2 && !root->open_any && s->default_open)
        root->open_any = s->default_open;
    /* XSD 1.1: direct xs:assert children of the complexType */
    if (s->version_11) xsd_capture_assert_children(s, ct, root);
    /* the NFA and model walkers consume ONE root particle —
     * a multi-particle model (derivation splices) normalizes
     * under a sequence */
    if (root->first_child && root->first_child->next) {
        XsdCm* seq = xsd_cm_new(XSD_CM_SEQ);
        if (seq) {
            seq->first_child = root->first_child;
            root->first_child = seq;
        }
    }
    /* open-content semantics ride the model root (the NFA builds
     * from ct->first_child, never the name wrapper) */
    if (root->first_child) {
        root->first_child->open_any = root->open_any;
        root->first_child->oc_mode = root->oc_mode;
        root->first_child->oc_owned = root->oc_owned;
        root->open_any = NULL;
        root->oc_owned = 0;
    } else if (root->open_any && root->oc_owned) {
        xsd_cm_free(root->open_any);
        root->open_any = NULL;
    }
    xsd_register_complex(s, root, attrs, name);
    return root;
}

static const XsdElementDecl* xsd_find_element_decl(
    struct leptris_xsd_schema* s, const char* name) {
    for (XsdElementDecl* d = s->elements; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

int xsd_is_substitute(struct leptris_xsd_schema* s,
                      const char* name, const char* head) {
    const XsdElementDecl* d = xsd_find_element_decl(s, name);
    for (int hops = 0; d && d->sub_head && hops < 16; hops++) {
        if (strcmp(d->sub_head, head) == 0) return 1;
        d = xsd_find_element_decl(s, d->sub_head);
    }
    return 0;
}

int xsd_schema_version_11(struct leptris_xsd_schema* s) {
    return s ? s->version_11 : 0;
}

XsdGroupDef* xsd_find_group(struct leptris_xsd_schema* s,
                            const char* name) {
    if (!s || !name) return NULL;
    for (XsdGroupDef* g = s->groups; g; g = g->next)
        if (strcmp(g->name, name) == 0) return g;
    return NULL;
}

static const XsdAttrDecl* xsd_find_top_attr(
    struct leptris_xsd_schema* s, const char* name) {
    if (!s || !name) return NULL;
    for (XsdAttrDecl* a = s->top_attrs; a; a = a->next)
        if (strcmp(a->name, name) == 0) return a;
    return NULL;
}

/* Deep-copy a captured row chain (attributeGroup refs splice
 * copies — every referencing type owns its rows). */
static XsdAttrDecl* xsd_copy_attr_rows(const XsdAttrDecl* src) {
    XsdAttrDecl* head = NULL;
    XsdAttrDecl* tail = NULL;
    for (; src; src = src->next) {
        XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
        if (!d) break;
        d->name = xsd_strdup(src->name);
        d->type = xsd_strdup(src->type);
        d->fixed = xsd_strdup(src->fixed);
        d->required = src->required;
        if (tail)
            tail->next = d;
        else
            head = d;
        tail = d;
    }
    return head;
}

/* Capture the attribute rows of a complexType or attributeGroup:
 * named rows, inline anonymous simpleTypes (synthesized slots),
 * refs to top-level attributes, and attributeGroup refs. */
/* Named row with an inline anonymous simpleType rides a
 * synthesized "attribute:NAME" slot, the element-slot pattern. */
static const char* xsd_inline_attr_type(
    struct leptris_xsd_schema* s, LeptrisElement ae,
    const char* attr_name) {
    for (LeptrisNodeRef q = leptris_node_first_child(
             leptris_element_as_node(ae));
         q; q = leptris_node_next_sibling(q)) {
        if (leptris_node_get_type(q) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement qe = (LeptrisElement)q;
        const char* ql = NULL;
        leptris_element_expanded_name(qe, &ql, NULL, NULL);
        if (!ql || strcmp(ql, "simpleType") != 0) continue;
        char synth[512];
        snprintf(synth, sizeof(synth), "attribute:%s", attr_name);
        XsdSimple* captured = xsd_capture_simple(qe);
        if (!captured) return NULL;
        free(captured->name);
        captured->name = xsd_strdup(synth);
        captured->next = s->simple_types;
        s->simple_types = captured;
        return captured->name;
    }
    return NULL;
}

static XsdAttrDecl* xsd_capture_attr_rows(struct leptris_xsd_schema* s,
                                          LeptrisElement container,
                                          int depth) {
    XsdAttrDecl* head = NULL;
    XsdAttrDecl* tail = NULL;
    if (depth > 8) return NULL;
    for (LeptrisNodeRef ac = leptris_node_first_child(
             leptris_element_as_node(container));
         ac; ac = leptris_node_next_sibling(ac)) {
        if (leptris_node_get_type(ac) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement ae = (LeptrisElement)ac;
        const char* al = NULL, *ap = NULL, *au = NULL;
        leptris_element_expanded_name(ae, &al, &ap, &au);
        if (!al || (au && strcmp(au, XSD_NS) != 0)) continue;

        if (strcmp(al, "attribute") == 0) {
            const char* an = leptris_element_attribute(ae, "name");
            const char* use = leptris_element_attribute(ae, "use");
            XsdAttrDecl* d = (XsdAttrDecl*)calloc(1, sizeof(*d));
            if (!d) continue;
            if (an) {
                d->name = xsd_strdup(an);
                d->type = xsd_strdup(
                    leptris_element_attribute(ae, "type"));
                if (!d->type)
                    d->type = xsd_strdup(
                        xsd_inline_attr_type(s, ae, an));
                d->fixed = xsd_strdup(
                    leptris_element_attribute(ae, "fixed"));
                d->required = (use && strcmp(use, "required") == 0);
            } else {
                /* ref to a top-level attribute declaration */
                const char* ref =
                    leptris_element_attribute(ae, "ref");
                const XsdAttrDecl* src =
                    ref ? xsd_find_top_attr(s, ref) : NULL;
                if (!src) {
                    free(d);
                    continue;
                }
                d->name = xsd_strdup(src->name);
                d->type = xsd_strdup(src->type);
                d->fixed = xsd_strdup(src->fixed);
                d->required = use
                                  ? (strcmp(use, "required") == 0)
                                  : src->required;
            }
            if (tail)
                tail->next = d;
            else
                head = d;
            tail = d;
        } else if (strcmp(al, "attributeGroup") == 0) {
            const char* ref =
                leptris_element_attribute(ae, "ref");
            if (!ref) continue;
            XsdAttrGroupDef* g = s->attr_groups;
            for (; g; g = g->next)
                if (strcmp(g->name, ref) == 0) break;
            if (!g || !g->attrs) continue;
            XsdAttrDecl* copied = xsd_copy_attr_rows(g->attrs);
            if (!copied) continue;
            if (!head)
                head = copied;
            else
                tail->next = copied;
            tail = copied;
            while (tail->next) tail = tail->next;
        }
    }
    return head;
}

LEPTRIS_API int leptris_xsd_builtin_valid(const char* builtin,
                                          const char* lexical) {
    extern int xsd_builtin_valid(const char* type, const char* v);
    return xsd_builtin_valid(builtin, lexical);
}

/* The instance validator routes every user simpleType through the
 * chain walker so list/union derivations dispatch with the schema
 * in hand (the bare xsd_simple_valid cannot resolve item/member
 * names). */
int xsd_simple_valid_chain(struct leptris_xsd_schema* s,
                           const XsdSimple* t, const char* v) {
    return xsd_valid_chain(s, t, v, 0);
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

/* ---- slice 6: identity-constraint capture ------------------------ */

/* The xs:selector child's @xpath (a child element, not an
 * attribute of the constraint element itself). */
static char* xsd_ic_selector_xpath(LeptrisElement constraint) {
    for (LeptrisNodeRef n =
             leptris_node_first_child(leptris_element_as_node(constraint));
         n; n = leptris_node_next_sibling(n)) {
        if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
            continue;
        LeptrisElement e = (LeptrisElement)n;
        const char* local = NULL, *prefix = NULL, *uri = NULL;
        leptris_element_expanded_name(e, &local, &prefix, &uri);
        if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
        if (strcmp(local, "selector") == 0)
            return xsd_strdup(leptris_element_attribute(e, "xpath"));
    }
    return NULL;
}

void xsd_capture_ics(struct leptris_xsd_schema* s,
                     LeptrisElement elem, const char* element_name) {
    XsdIc* head = NULL;
    XsdIc* tail = NULL;
    /* ICs are direct children of xs:element, or children of its
     * inline complexType — one hop covers the second spelling. */
    for (int hop = 0; hop < 2; hop++) {
        for (LeptrisNodeRef n =
                 leptris_node_first_child(leptris_element_as_node(elem));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement e = (LeptrisElement)n;
            const char* local = NULL, *prefix = NULL, *uri = NULL;
            leptris_element_expanded_name(e, &local, &prefix, &uri);
            if (!local || !uri || strcmp(uri, XSD_NS) != 0) continue;
            if (hop == 0 &&
                (strcmp(local, "key") == 0 ||
                 strcmp(local, "unique") == 0 ||
                 strcmp(local, "keyref") == 0)) {
                XsdIc* ic = (XsdIc*)calloc(1, sizeof(*ic));
                if (!ic) continue;
                ic->kind = strcmp(local, "key") == 0     ? 0
                           : strcmp(local, "unique") == 0 ? 1
                                                          : 2;
                ic->name = xsd_strdup(
                    leptris_element_attribute(e, "name"));
                ic->selector = xsd_ic_selector_xpath(e);
                ic->refer = xsd_strdup(
                    leptris_element_attribute(e, "refer"));
                for (LeptrisNodeRef fc = leptris_node_first_child(
                         leptris_element_as_node(e));
                     fc && ic->field_count < 8;
                     fc = leptris_node_next_sibling(fc)) {
                    if (leptris_node_get_type(fc) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement fe = (LeptrisElement)fc;
                    const char* fl = NULL, *fp = NULL, *fu = NULL;
                    leptris_element_expanded_name(fe, &fl, &fp, &fu);
                    if (!fl || !fu || strcmp(fu, XSD_NS) != 0) continue;
                    if (strcmp(fl, "field") != 0) continue;
                    ic->fields[ic->field_count++] = xsd_strdup(
                        leptris_element_attribute(fe, "xpath"));
                }
                if (tail)
                    tail->next = ic;
                else
                    head = ic;
                tail = ic;
            } else if (hop == 0 && strcmp(local, "complexType") == 0) {
                /* descend into the inline type's ICs */
                for (LeptrisNodeRef ic_n = leptris_node_first_child(
                         leptris_element_as_node(e));
                     ic_n; ic_n = leptris_node_next_sibling(ic_n)) {
                    if (leptris_node_get_type(ic_n) !=
                        LEPTRIS_NODE_TYPE_ELEMENT)
                        continue;
                    LeptrisElement ie = (LeptrisElement)ic_n;
                    const char* il = NULL, *ip = NULL, *iu = NULL;
                    leptris_element_expanded_name(ie, &il, &ip, &iu);
                    if (!il || !iu || strcmp(iu, XSD_NS) != 0) continue;
                    if (strcmp(il, "key") != 0 &&
                        strcmp(il, "unique") != 0 &&
                        strcmp(il, "keyref") != 0)
                        continue;
                    XsdIc* ic = (XsdIc*)calloc(1, sizeof(*ic));
                    if (!ic) continue;
                    ic->kind = strcmp(il, "key") == 0     ? 0
                               : strcmp(il, "unique") == 0 ? 1
                                                            : 2;
                    ic->name = xsd_strdup(
                        leptris_element_attribute(ie, "name"));
                    ic->selector = xsd_ic_selector_xpath(ie);
                    ic->refer = xsd_strdup(
                        leptris_element_attribute(ie, "refer"));
                    for (LeptrisNodeRef fc =
                             leptris_node_first_child(
                                 leptris_element_as_node(ie));
                         fc && ic->field_count < 8;
                         fc = leptris_node_next_sibling(fc)) {
                        if (leptris_node_get_type(fc) !=
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            continue;
                        LeptrisElement fe = (LeptrisElement)fc;
                        const char* fl = NULL, *fp = NULL, *fu = NULL;
                        leptris_element_expanded_name(fe, &fl, &fp,
                                                   &fu);
                        if (!fl || !fu || strcmp(fu, XSD_NS) != 0)
                            continue;
                        if (strcmp(fl, "field") != 0) continue;
                        ic->fields[ic->field_count++] = xsd_strdup(
                            leptris_element_attribute(fe, "xpath"));
                    }
                    if (tail)
                        tail->next = ic;
                    else
                        head = ic;
                    tail = ic;
                }
            }
        }
    }
    if (!head) return;
    XsdElementIcs* slot = (XsdElementIcs*)calloc(1, sizeof(*slot));
    if (!slot) return;
    slot->element_name = xsd_strdup(element_name);
    slot->constraints = head;
    slot->next = s->element_ics;
    s->element_ics = slot;
}

const XsdElementIcs* xsd_find_element_ics(struct leptris_xsd_schema* s,
                                          const char* element_name) {
    if (!s || !element_name) return NULL;
    for (XsdElementIcs* t = s->element_ics; t; t = t->next)
        if (strcmp(t->element_name, element_name) == 0) return t;
    return NULL;
}

/* ---- validator accessors (validate.c) --------------------------- */

const XsdElementDecl* xsd_find_element(struct leptris_xsd_schema* s,
                                       const char* name) {
    if (!s || !name) return NULL;
    for (XsdElementDecl* d = s->elements; d; d = d->next)
        if (strcmp(d->name, name) == 0) return d;
    return NULL;
}

XsdCm* xsd_find_complex(struct leptris_xsd_schema* s,
                        const char* type_name) {
    if (!s || !type_name) return NULL;
    for (XsdCm* c = s->complex_types; c; c = c->next)
        if (strcmp(c->name, type_name) == 0) return c;
    return NULL;
}

const XsdSimple* xsd_find_simple_pub(struct leptris_xsd_schema* s,
                                     const char* name) {
    return xsd_find_simple(s, name);
}

const char* xsd_target_ns(struct leptris_xsd_schema* s) {
    return s ? s->target_ns : NULL;
}

const XsdTypeAttrs* xsd_find_type_attrs(struct leptris_xsd_schema* s,
                                        const char* type_name) {
    if (!s || !type_name) return NULL;
    for (XsdTypeAttrs* t = s->type_attrs; t; t = t->next)
        if (strcmp(t->type_name, type_name) == 0) return t;
    return NULL;
}

/* ---- slice 4: instance validation (validate.c engine) ------------ */

LEPTRIS_API int leptris_xsd_validate(LeptrisXsdSchema schema,
                                     LeptrisDocument doc) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s || !doc) return -1;
    struct xsd_validator* v = xsd_validator_new(s);
    if (!v) return -1;
    int r = xsd_validator_run(v, doc);
    /* park the run's errors on the schema for the accessors; a new
     * validate run replaces them. */
    struct xsd_validator* prev = (struct xsd_validator*)s->validator;
    if (prev) xsd_validator_free(prev);
    s->validator = v;
    return r;
}

LEPTRIS_API size_t leptris_xsd_error_count(LeptrisXsdSchema schema) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return 0;
    return xsd_validator_error_count(
        (struct xsd_validator*)s->validator);
}

LEPTRIS_API const char* leptris_xsd_error_at(LeptrisXsdSchema schema,
                                             size_t i) {
    struct leptris_xsd_schema* s = (struct leptris_xsd_schema*)schema;
    if (!s) return NULL;
    return xsd_validator_error_at((struct xsd_validator*)s->validator,
                                  i);
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
