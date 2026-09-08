/* rng/parse.c — RELAX NG XML syntax → pattern IR (#878 phase 1).
 *
 * The schema is parsed by the standard XML parser and lowered into
 * the RngPattern tree: every RELAX NG pattern element maps to one
 * IR node; <define>/@combine merge bodies at parse time; <ref>
 * records the name for phase-2 linking. Allocation is plain heap
 * (schema-sized, freed whole by leptris_rng_free).
 */
#include "rng_internal.h"
#include "../dom/element.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RNG_NS "http://relaxng.org/ns/structure/1.0"

static RngPattern* pat_new(RngPatternKind kind) {
    RngPattern* p = (RngPattern*)calloc(1, sizeof(*p));
    if (p) p->kind = kind;
    return p;
}

static void pat_append(RngPattern* list, RngPattern* p) {
    if (!list->first_child) {
        list->first_child = p;
        return;
    }
    RngPattern* c = list->first_child;
    while (c->next) c = c->next;
    c->next = p;
}

static char* dup_attr(LeptrisElement e, const char* name) {
    const char* v = leptris_element_attribute(e, name);
    return v ? leptris_strdup(v) : NULL;
}

/* Is `e` a RELAX NG element (correct namespace + local name)?
 * Schema trees are namespace-resolved: the element name stores the
 * local part, ns via leptris_element_get_namespace_uri. */
static int is_rng(LeptrisElement e, const char* local) {
    const char* ns = leptris_element_get_namespace_uri(e);
    const char* n = leptris_element_name(e);
    return ns && strcmp(ns, RNG_NS) == 0 && n && strcmp(n, local) == 0;
}

static RngPattern* parse_pattern(RngGrammar* g, LeptrisElement e,
                                 char* err, size_t errsz);

/* Parse the children of `e` as a pattern list into fresh `wrap`. */
static void parse_children_into(RngGrammar* g, LeptrisElement e,
                                RngPattern* wrap, char* err, size_t errsz) {
    for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)e); c;
         c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT) continue;
        RngPattern* p = parse_pattern(g, (LeptrisElement)c, err, errsz);
        if (!p) return;
        pat_append(wrap, p);
    }
}

static RngPattern* parse_pattern(RngGrammar* g, LeptrisElement e,
                                 char* err, size_t errsz) {
    RngPattern* p = NULL;
    const char* n = leptris_element_name(e);

    if (is_rng(e, "element")) {
        p = pat_new(RNG_ELEMENT);
        p->name = dup_attr(e, "name");
        p->ns = dup_attr(e, "ns");
        if (!p->name) {
            snprintf(err, errsz, "element: missing @name");
            free(p);
            return NULL;
        }
    } else if (is_rng(e, "attribute")) {
        p = pat_new(RNG_ATTRIBUTE);
        p->name = dup_attr(e, "name");
        p->ns = dup_attr(e, "ns");
        if (!p->name) {
            snprintf(err, errsz, "attribute: missing @name");
            free(p);
            return NULL;
        }
    } else if (is_rng(e, "choice") || is_rng(e, "interleave") ||
               is_rng(e, "group")) {
        p = pat_new(is_rng(e, "choice") ? RNG_CHOICE
                    : is_rng(e, "interleave") ? RNG_INTERLEAVE
                                              : RNG_GROUP);
    } else if (is_rng(e, "optional")) {
        p = pat_new(RNG_OPTIONAL);
    } else if (is_rng(e, "zeroOrMore")) {
        p = pat_new(RNG_ZERO_OR_MORE);
    } else if (is_rng(e, "oneOrMore")) {
        p = pat_new(RNG_ONE_OR_MORE);
    } else if (is_rng(e, "list")) {
        p = pat_new(RNG_LIST);
    } else if (is_rng(e, "mixed")) {
        p = pat_new(RNG_MIXED);
    } else if (is_rng(e, "data")) {
        p = pat_new(RNG_DATA);
        p->datatype = dup_attr(e, "type");
        p->datatype_lib = dup_attr(e, "datatypeLibrary");
        if (!p->datatype) p->datatype = leptris_strdup("string");
    } else if (is_rng(e, "value")) {
        p = pat_new(RNG_VALUE);
        p->datatype = dup_attr(e, "type");
        p->datatype_lib = dup_attr(e, "datatypeLibrary");
        p->ns = dup_attr(e, "ns");
        p->value = leptris_strdup(leptris_element_text(e));
    } else if (is_rng(e, "param")) {
        p = pat_new(RNG_PARAM);
        p->name = dup_attr(e, "name");
        p->value = leptris_strdup(leptris_element_text(e));
    } else if (is_rng(e, "except")) {
        p = pat_new(RNG_EXCEPT);
    } else if (is_rng(e, "empty")) {
        return pat_new(RNG_EMPTY);
    } else if (is_rng(e, "notAllowed")) {
        return pat_new(RNG_NOT_ALLOWED);
    } else if (is_rng(e, "text")) {
        return pat_new(RNG_TEXT);
    } else if (is_rng(e, "ref")) {
        p = pat_new(RNG_REF);
        p->name = dup_attr(e, "name");
        if (!p->name) {
            snprintf(err, errsz, "ref: missing @name");
            free(p);
            return NULL;
        }
        return p;   /* children invalid per spec; no walk */
    } else if (is_rng(e, "parentRef") || is_rng(e, "externalRef")) {
        snprintf(err, errsz, "%s: not supported in phase 1", n);
        return NULL;
    } else {
        snprintf(err, errsz, "unknown pattern element: %s",
                 n ? n : "(null)");
        return NULL;
    }

    parse_children_into(g, e, p, err, errsz);
    if (err[0] && !p) return NULL;
    return p;
}

/* <grammar><start/>...<define/>...</grammar> */
static int parse_grammar_body(RngGrammar* g, LeptrisElement grammar,
                              char* err, size_t errsz) {
    g->default_ns = dup_attr(grammar, "ns");
    g->default_lib = dup_attr(grammar, "datatypeLibrary");

    for (LeptrisNodeRef c =
             leptris_node_first_child((LeptrisNodeRef)grammar);
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT) continue;
        LeptrisElement ce = (LeptrisElement)c;

        if (is_rng(ce, "start")) {
            RngPattern* body = pat_new(RNG_GROUP);
            parse_children_into(g, ce, body, err, errsz);
            if (err[0]) { rng_pattern_free(body); return 0; }
            if (!body->first_child) {
                rng_pattern_free(body);
            } else if (!g->start) {
                g->start = body;
            } else {
                /* Multiple <start> = implicit choice. */
                RngPattern* ch = pat_new(RNG_CHOICE);
                pat_append(ch, g->start);
                pat_append(ch, body->first_child);
                body->first_child = NULL;
                rng_pattern_free(body);
                g->start = ch;
            }
        } else if (is_rng(ce, "define")) {
            char* name = dup_attr(ce, "name");
            char* combine = dup_attr(ce, "combine");
            if (!name) {
                snprintf(err, errsz, "define: missing @name");
                free(combine);
                return 0;
            }
            RngPattern* body = pat_new(RNG_GROUP);
            parse_children_into(g, ce, body, err, errsz);
            if (err[0]) {
                free(name); free(combine);
                rng_pattern_free(body);
                return 0;
            }
            RngDefine* d = g->defines;
            while (d && strcmp(d->name, name) != 0) d = d->next;
            if (!d) {
                d = (RngDefine*)calloc(1, sizeof(*d));
                if (!d) { free(name); free(combine);
                          rng_pattern_free(body); return 0; }
                d->name = name;
                d->combine = combine;
                d->body = body->first_child;
                body->first_child = NULL;
                rng_pattern_free(body);
                d->next = g->defines;
                g->defines = d;
            } else {
                free(name);
                /* @combine may sit on either declaration. */
                if (!combine) combine = d->combine;
                free(d->combine);
                d->combine = combine;
                /* Redefinition without combine on either is an
                 * error; with it, merge into choice/interleave. */
                if (!d->combine) {
                    snprintf(err, errsz,
                             "define %s: redefined without combine",
                             d->name);
                    rng_pattern_free(body);
                    return 0;
                }
                RngPattern* wrap =
                    pat_new(strcmp(d->combine, "interleave") == 0
                                ? RNG_INTERLEAVE : RNG_CHOICE);
                pat_append(wrap, d->body);
                pat_append(wrap, body->first_child);
                body->first_child = NULL;
                rng_pattern_free(body);
                d->body = wrap;
            }
        } else if (is_rng(ce, "include")) {
            snprintf(err, errsz, "include: not supported in phase 1");
            return 0;
        }
    }
    if (!g->start) {
        snprintf(err, errsz, "grammar: no <start>");
        return 0;
    }
    return 1;
}

struct leptris_relaxng* rng_parse_document(LeptrisDocument doc) {
    if (!doc) return NULL;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) return NULL;
    struct leptris_relaxng* rng =
        (struct leptris_relaxng*)calloc(1, sizeof(*rng));
    if (!rng) return NULL;
    rng->grammar = (RngGrammar*)calloc(1, sizeof(RngGrammar));
    if (!rng->grammar) { free(rng); return NULL; }

    if (!is_rng(root, "grammar") && !is_rng(root, "element")) {
        rng->error = leptris_strdup(
            "root element is not in the RELAX NG namespace");
        return rng;   /* caller checks error */
    }

    char err[256];
    err[0] = 0;
    if (is_rng(root, "grammar")) {
        if (!parse_grammar_body(rng->grammar, root, err, sizeof(err))) {
            rng->error = leptris_strdup(err[0] ? err : "grammar parse failed");
        }
        return rng;
    }

    /* Bare <element> as root: implicit grammar with one start. */
    RngPattern* p = parse_pattern(rng->grammar, root, err, sizeof(err));
    if (!p) {
        rng->error = leptris_strdup(err);
        return rng;
    }
    rng->grammar->start = p;
    return rng;
}

void rng_pattern_free(RngPattern* p) {
    while (p) {
        RngPattern* next = p->next;
        rng_pattern_free(p->first_child);
        free(p->name);
        free(p->ns);
        free(p->datatype);
        free(p->datatype_lib);
        free(p->value);
        free(p);
        p = next;
    }
}

void rng_grammar_free(RngGrammar* g) {
    if (!g) return;
    rng_pattern_free(g->start);
    RngDefine* d = g->defines;
    while (d) {
        RngDefine* next = d->next;
        rng_pattern_free(d->body);
        free(d->name);
        free(d->combine);
        free(d);
        d = next;
    }
    free(g->default_ns);
    free(g->default_lib);
    free(g);
}
