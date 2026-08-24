/* xslt/xslt_public.c — public XSLT 1.0 API (TODO.transform).
 *
 * leptris_xslt_transform: compile-once handle, apply-many. The
 * stylesheet document is parsed by the caller (or from a string via
 * the convenience entry), compiled once, and applied per source
 * document. Output honors xsl:output (method xml|text, indent,
 * omit-xml-declaration, encoding declaration). */
#include "xslt_internal.h"
#include <stdlib.h>

struct leptris_xslt {
    LeptrisDocument sheet_doc;    /* owns the stylesheet tree */
    XsltStylesheet* compiled;
};
LEPTRIS_API LeptrisXslt leptris_xslt_parse(const char* stylesheet_xml,
                                            size_t length) {
    if (!stylesheet_xml || length == 0) return NULL;
    LeptrisDocument doc = leptris_parse_string(stylesheet_xml, length, NULL);
    if (!doc) return NULL;
    XsltStylesheet* sheet = xslt_stylesheet_parse(doc);
    if (!sheet) { leptris_document_free(doc); return NULL; }
    struct leptris_xslt* x =
        (struct leptris_xslt*)calloc(1, sizeof(*x));
    if (!x) {
        xslt_stylesheet_free(sheet);
        leptris_document_free(doc);
        return NULL;
    }
    x->sheet_doc = doc;
    x->compiled = sheet;
    return x;
}

LEPTRIS_API void leptris_xslt_free(LeptrisXslt xslt) {
    if (!xslt) return;
    xslt_stylesheet_free(xslt->compiled);
    leptris_document_free(xslt->sheet_doc);
    free(xslt);
}

LEPTRIS_API LeptrisDocument leptris_xslt_apply(LeptrisXslt xslt,
                                               LeptrisDocument source) {
    if (!xslt || !source) return NULL;
    XsltExec* ex = xslt_transform(xslt->compiled, source);
    if (!ex) return NULL;
    LeptrisDocument out = ex->result;
    ex->result = NULL;    /* ownership moved */
    xslt_exec_free(ex);
    return out;
}

LEPTRIS_API char* leptris_xslt_apply_string(LeptrisXslt xslt,
                                            LeptrisDocument source) {
    if (!xslt || !source) return NULL;
    XsltExec* ex = xslt_transform(xslt->compiled, source);
    if (!ex) return NULL;
    LeptrisDocument out = ex->result;
    ex->result = NULL;

    LeptrisElement root = out ? leptris_document_root(out) : NULL;
    const char* top = ex->top_text;
    size_t tlen = top ? strlen(top) : 0;

    if (!root) {
        /* Text-only result (xsl:output method=text or no elements). */
        char* r = (char*)malloc(tlen + 1);
        if (r) {
            if (top) memcpy(r, top, tlen + 1);
            else r[0] = 0;
        }
        if (out) leptris_document_free(out);
        xslt_exec_free(ex);
        return r;
    }

    /* XML result: the declaration (unless text-method) + top text +
     * every top-level element of the fragment chain. */
    LeptrisSerializeOptions opts = {0};
    opts.xml_declaration = ex->sheet->out_method_text ? 0 : 1;
    opts.indent = 0;
    size_t cap = tlen + 64, total = 0;
    char* acc = (char*)malloc(cap);
    if (!acc) {
        leptris_document_free(out);
        xslt_exec_free(ex);
        return NULL;
    }
    if (tlen) {
        memcpy(acc, top, tlen);
        total = tlen;
    }
    char* first = leptris_document_serialize(out, &opts);
    if (first) {
        size_t fl = strlen(first);
        while (total + fl + 1 > cap) cap *= 2;
        acc = (char*)realloc(acc, cap);
        if (acc) {
            memcpy(acc + total, first, fl + 1);
            total += fl;
        }
        leptris_free_string(first);
    }
    for (LeptrisNodeRef sib = leptris_node_next_sibling(
             leptris_element_as_node(root));
         sib; sib = leptris_node_next_sibling(sib)) {
        char* piece = leptris_element_serialize((LeptrisElement)sib,
                                                &opts);
        if (piece) {
            size_t pl = strlen(piece);
            while (total + pl + 1 > cap) cap *= 2;
            acc = (char*)realloc(acc, cap);
            if (acc) {
                memcpy(acc + total, piece, pl + 1);
                total += pl;
            }
            leptris_free_string(piece);
        }
    }
    leptris_document_free(out);
    xslt_exec_free(ex);
    return acc;
}
