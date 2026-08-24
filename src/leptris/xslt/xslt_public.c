/* xslt/xslt_public.c — public XSLT 1.0 API (TODO.transform).
 *
 * leptris_xslt_transform: compile-once handle, apply-many. The
 * stylesheet document is parsed by the caller (or from a string via
 * the convenience entry), compiled once, and applied per source
 * document. Output honors xsl:output (method xml|text, indent,
 * omit-xml-declaration, encoding declaration). */
#include "xslt_internal.h"
#include "../dom/text.h"
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

struct leptris_xslt {
    LeptrisDocument sheet_doc;    /* owns the stylesheet tree */
    XsltStylesheet* compiled;
};
/* §2.7 Embedding Stylesheets: when the document root is not an
 * xsl:stylesheet, resolve the xml-stylesheet PI's href="#id"
 * fragment and compile THAT element as the stylesheet root. */
static LeptrisElement resolve_embedded_root(LeptrisDocument doc) {
    if (!doc) return NULL;
    for (struct leptris_processing_instruction* pi =
             ((struct leptris_document*)doc)->pis;
         pi; pi = pi->next) {
        if (!pi->target || strcmp(pi->target, "xml-stylesheet") != 0)
            continue;
        if (!pi->data || !strstr(pi->data, "text/xsl")) continue;
        const char* h = strstr(pi->data, "href=");
        if (!h) continue;
        char q = h[5];
        if ((q != '"' && q != '\'') || h[6] != '#') continue;
        char id[128];
        size_t il = 0;
        for (const char* p = h + 7; *p && *p != q && il + 1 < sizeof(id); p++)
            id[il++] = *p;
        id[il] = 0;
        /* Find the element with that id attribute. */
        for (LeptrisElement e = leptris_document_root(doc); e; ) {
            const char* v = leptris_element_attribute(e, "id");
            if (v && strcmp(v, id) == 0) return e;
            LeptrisNodeRef sib =
                leptris_node_next_sibling(leptris_element_as_node(e));
            /* walk children first */
            for (LeptrisNodeRef c =
                     leptris_node_first_child(leptris_element_as_node(e));
                 c; c = leptris_node_next_sibling(c)) {
                if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) {
                    v = leptris_element_attribute((LeptrisElement)c, "id");
                    if (v && strcmp(v, id) == 0) return (LeptrisElement)c;
                }
            }
            e = (LeptrisElement)sib;
        }
    }
    return NULL;
}

LEPTRIS_API LeptrisXslt leptris_xslt_parse(const char* stylesheet_xml,
                                            size_t length) {
    if (!stylesheet_xml || length == 0) return NULL;
    LeptrisDocument doc = leptris_parse_string(stylesheet_xml, length, NULL);
    if (!doc) return NULL;
    XsltStylesheet* sheet = xslt_stylesheet_parse(doc);
    if (!sheet) {
        /* §2.7: try the xml-stylesheet PI embedding route. */
        LeptrisElement embed = resolve_embedded_root(doc);
        if (embed) sheet = xslt_stylesheet_parse_root(doc, embed);
    }
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


LEPTRIS_API LeptrisXslt leptris_xslt_parse_file(const char* path) {
    if (!path || !*path) return NULL;
    size_t size = 0;
    char* data = leptris_load_file(path, &size);
    if (!data) return NULL;
    LeptrisXslt x = leptris_xslt_parse(data, size);
    leptris_free_string(data);
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

/* §16.3 text output: the string-value of every text node in the
 * result tree in document order, without escaping. */
static char* serialize_text_method(LeptrisDocument out, const char* top) {
    size_t cap = 64, len = 0;
    char* acc = (char*)malloc(cap);
    if (!acc) return NULL;
    acc[0] = 0;
    LeptrisElement root = out ? leptris_document_root(out) : NULL;
    for (LeptrisElement e = root; e; ) {
        for (LeptrisNodeRef c =
                 leptris_node_first_child(leptris_element_as_node(e));
             c; c = leptris_node_next_sibling(c)) {
            if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_TEXT) {
                const char* t = leptris_text_get_content((LeptrisTextNode*)c);
                if (!t) continue;
                size_t tl = strlen(t);
                while (len + tl + 1 > cap) { cap *= 2; acc = (char*)realloc(acc, cap); }
                memcpy(acc + len, t, tl); len += tl;
                acc[len] = 0;
            }
        }
        /* Continue with next top-level element in the fragment chain. */
        LeptrisNodeRef sib = leptris_node_next_sibling(leptris_element_as_node(e));
        e = (LeptrisElement)sib;
    }
    return acc;
}

/* §16.2 HTML output: no XML declaration; known void elements emit
 * without the XHTML self-closing slash. Applied as a post-pass on
 * the XML serialization (script/style content is already raw via
 * the text-node raw flag). */
static char* to_html_method(const char* xml) {
    if (!xml) return NULL;
    static const char* kVoid[] = {
        "area", "base", "br", "col", "hr", "img", "input", "link",
        "meta", "param", NULL };
    size_t len = strlen(xml);
    size_t cap = len + 1, o = 0;
    char* out = (char*)malloc(cap);
    if (!out) return NULL;
    for (size_t i = 0; i < len; ) {
        if (xml[i] == '<') {
            /* Find "<name .../>" self-closing; convert known voids. */
            size_t j = i + 1;
            while (j < len && xml[j] != '>' && xml[j] != ' ' && xml[j] != '/') j++;
            size_t namelen = j - (i + 1);
            int is_void = 0;
            for (size_t k = 0; kVoid[k]; k++) {
                if (strlen(kVoid[k]) == namelen &&
                    strncasecmp(kVoid[k], xml + i + 1, namelen) == 0) {
                    is_void = 1; break;
                }
            }
            if (is_void) {
                /* Copy up to the '/>' and drop the slash. */
                while (i < len && xml[i] != '/') {
                    if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                    out[o++] = xml[i++];
                }
                i++;   /* skip '/' */
                if (i < len && xml[i] == '>') {
                    if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                    out[o++] = '>';
                    i++;
                }
                continue;
            }
        }
        if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        out[o++] = xml[i++];
    }
    out[o] = 0;
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

    /* §16.3 text method: text nodes only — elements never serialize.
     * Fragment-level text (top then tail) frames the tree text. */
    if (ex->sheet->out_method_text) {
        size_t tl = ex->tail_text ? ex->tail_text_len : 0;
        char* r = serialize_text_method(out, top);
        size_t rl = r ? strlen(r) : 0;
        char* full = (char*)malloc(tlen + rl + tl + 1);
        if (full) {
            if (top) memcpy(full, top, tlen);
            if (r) memcpy(full + tlen, r, rl);
            if (tl) memcpy(full + tlen + rl, ex->tail_text, tl + 1);
            else full[tlen + rl] = 0;
        }
        free(r);
        if (out) leptris_document_free(out);
        xslt_exec_free(ex);
        return full;
    }

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

    /* §16.1 standalone: surface via the document state the shared
     * serializer already reads for the declaration. */
    if (out && ex->sheet->out_standalone >= 0) {
        struct leptris_document* rd = (struct leptris_document*)out;
        rd->standalone = ex->sheet->out_standalone;
        /* The serializer drops standalone when the fallback version
         * path fires (no xml_version) — provide one. */
        if (!rd->xml_version) rd->xml_version = leptris_strdup("1.0");
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

    char* final = acc;
    if (ex->sheet->out_method_html) {
        /* §16.2: post-pass — drop any XML declaration and unslash
         * the known void elements. */
        char* html = to_html_method(acc);
        if (html) {
            /* Also strip a leading declaration if present. */
            char* decl = strstr(html, "<?xml");
            if (decl == html) {
                char* end = strstr(html, "?>");
                if (end) {
                    size_t rest = strlen(end + 2);
                    memmove(html, end + 2, rest + 1);
                }
            }
            free(acc);
            final = html;
        }
    }
    (void)0;
    /* Fragment-order tail text (emitted after the last element). */
    if (ex->tail_text && ex->tail_text_len) {
        size_t tl = ex->tail_text_len;
        size_t fl = final ? strlen(final) : 0;
        char* with_tail = (char*)malloc(fl + tl + 1);
        if (with_tail) {
            if (final) memcpy(with_tail, final, fl);
            memcpy(with_tail + fl, ex->tail_text, tl + 1);
            free(final);
            final = with_tail;
        }
    }
    leptris_document_free(out);
    xslt_exec_free(ex);
    return final;
}
