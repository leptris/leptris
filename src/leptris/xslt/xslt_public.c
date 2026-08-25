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
#include <ctype.h>

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
    XsltExec* ex = xslt_transform_doc(xslt->compiled,
                                     xslt->sheet_doc, source);
    if (!ex) return NULL;
    LeptrisDocument out = ex->result;
    ex->result = NULL;    /* ownership moved */
    xslt_exec_free(ex);
    return out;
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
        /* §16.2 HTML PIs: no trailing '?' (<?php ... >, not XML's
         * <?php ... ?>). The XML declaration is stripped earlier. */
        if (xml[i] == '<' && xml[i + 1] == '?' &&
            strncmp(xml + i + 2, "xml", 3) != 0) {
            /* leave the declaration to the strip step */
            const char* close = strstr(xml + i, "?>");
            if (close) {
                size_t body = (size_t)(close - (xml + i));  /* excl. '?' */
                while (body--) {
                    if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                    out[o++] = xml[i++];
                }
                if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                out[o++] = '>';
                i += 2;   /* skip "?>" */
                continue;
            }
        }
        if (xml[i] == '<') {
            /* Find "<name .../>" self-closing; convert known voids. */
            size_t j = i + 1;
            while (j < len && xml[j] != '>' && xml[j] != ' ' && xml[j] != '/') j++;
            size_t namelen = j - (i + 1);
            int is_void = 0;
            for (size_t k = 0; kVoid[k]; k++) {
                if (strlen(kVoid[k]) == namelen) {
                    size_t q = 0;
                    int same = 1;
                    const unsigned char* p =
                        (const unsigned char*)xml + i + 1;
                    for (; q < namelen; q++) {
                        if (tolower(p[q]) !=
                            tolower((unsigned char)kVoid[k][q])) {
                            same = 0; break;
                        }
                    }
                    if (same) { is_void = 1; break; }
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


/* One fragment node by kind: element → subtree serialize;
 * comment/PI/text → their literal serialization. */
static char* serialize_frag_node_text(LeptrisNodeRef n) {
    int ty = leptris_node_get_type(n);
    if (ty == LEPTRIS_NODE_TYPE_ELEMENT)
        return leptris_element_serialize((LeptrisElement)n, NULL);
    const char* body = NULL;
    const char* target = NULL;
    if (ty == LEPTRIS_NODE_TYPE_TEXT || ty == LEPTRIS_NODE_TYPE_CDATA)
        body = leptris_text_node_get_content(n);
    else if (ty == LEPTRIS_NODE_TYPE_COMMENT)
        body = leptris_comment_node_get_content(n);
    else if (ty == LEPTRIS_NODE_TYPE_PI) {
        target = leptris_pi_node_get_target(n);
        body = leptris_pi_node_get_data(n);
    }
    size_t cap = 16 + (body ? strlen(body) : 0) +
                 (target ? strlen(target) : 0);
    char* out = (char*)malloc(cap + 8);
    if (!out) return NULL;
    if (ty == LEPTRIS_NODE_TYPE_PI)
        snprintf(out, cap + 8, "<?%s%s%s?>",
                 target ? target : "", body && *body ? " " : "",
                 body ? body : "");
    else if (ty == LEPTRIS_NODE_TYPE_COMMENT)
        snprintf(out, cap + 8, "<!--%s-->", body ? body : "");
    else
        snprintf(out, cap + 8, "%s", body ? body : "");
    return out;
}

/* §16.2 post-pass: strip the XML declaration, unslash void
 * elements, PHP-style PIs. Returns a fresh string (caller frees the
 * input). */
static char* html_post_pass(char* acc) {
    char* html = to_html_method(acc);
    if (!html) return acc;
    char* decl = strstr(html, "<?xml");
    if (decl == html) {
        char* end = strstr(html, "?>");
        if (end) {
            size_t rest = strlen(end + 2);
            memmove(html, end + 2, rest + 1);
        }
    }
    free(acc);
    return html;
}

LEPTRIS_API char* leptris_xslt_apply_string(LeptrisXslt xslt,
                                            LeptrisDocument source) {
    if (!xslt || !source) return NULL;
    XsltExec* ex = xslt_transform_doc(xslt->compiled,
                                     xslt->sheet_doc, source);
    if (!ex) return NULL;
    LeptrisDocument out = ex->result;
    ex->result = NULL;

    LeptrisElement root = out ? leptris_document_root(out) : NULL;

    /* §16.3 text method: every TEXT node in fragment order — the
     * fragment chain carries that order now (pre-root list + the
     * root's sibling chain, elements' children walked). */
    if (ex->sheet->out_method_text) {
        size_t cap = 64, len = 0;
        char* acc = (char*)malloc(cap);
        if (!acc) { xslt_exec_free(ex); return NULL; }
        acc[0] = 0;
        for (XsltFragNode* f = (XsltFragNode*)ex->frag_nodes; f;
             f = f->next) {
            int ty = leptris_node_get_type(f->node);
            if (ty == LEPTRIS_NODE_TYPE_TEXT ||
                ty == LEPTRIS_NODE_TYPE_CDATA) {
                const char* t = leptris_text_node_get_content(f->node);
                if (t) {
                    size_t tl = strlen(t);
                    while (len + tl + 1 > cap) cap *= 2;
                    acc = (char*)realloc(acc, cap);
                    memcpy(acc + len, t, tl + 1);
                    len += tl;
                }
            }
        }
        for (LeptrisElement e = root; e;
             e = (LeptrisElement)leptris_node_get_next_sibling(
                     leptris_element_as_node(e))) {
            for (LeptrisNodeRef c =
                     leptris_node_first_child(leptris_element_as_node(e));
                 c; c = leptris_node_next_sibling(c)) {
                int ty = leptris_node_get_type(c);
                if (ty == LEPTRIS_NODE_TYPE_TEXT ||
                    ty == LEPTRIS_NODE_TYPE_CDATA) {
                    const char* t = leptris_text_node_get_content(c);
                    if (t) {
                        size_t tl = strlen(t);
                        while (len + tl + 1 > cap) cap *= 2;
                        acc = (char*)realloc(acc, cap);
                        memcpy(acc + len, t, tl + 1);
                        len += tl;
                    }
                }
            }
        }
        if (out) leptris_document_free(out);
        xslt_exec_free(ex);
        return acc;
    }

    if (!root) {
        /* No element anchored the fragment: the pre-root list IS
         * the result — text/comment/PI in emitted order. */
        size_t cap = 64, len = 0;
        char* acc = (char*)malloc(cap);
        if (!acc) { xslt_exec_free(ex); return NULL; }
        acc[0] = 0;
        for (XsltFragNode* f = (XsltFragNode*)ex->frag_nodes; f;
             f = f->next) {
            char* piece = serialize_frag_node_text(f->node);
            if (!piece) continue;
            size_t pl = strlen(piece);
            while (len + pl + 1 > cap) cap *= 2;
            acc = (char*)realloc(acc, cap);
            memcpy(acc + len, piece, pl + 1);
            len += pl;
            free(piece);
        }
        if (out) leptris_document_free(out);
        if (ex->sheet->out_method_html) acc = html_post_pass(acc);
        xslt_exec_free(ex);
        return acc;
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
    /* §16.1 indent="yes": 2-space pretty-print via the shared
     * serializer (text-only elements stay inline — libxslt rule). */
    opts.indent = ex->sheet->out_indent ? 2 : 0;
    opts.cdata_elements = ex->sheet->out_cdata;
    opts.cdata_element_count = ex->sheet->out_cdata_count;
    /* Pre-root fragment nodes go FIRST — they were emitted before
     * any element anchored the chain. */
    char* first_pre = NULL;
    size_t pre_len = 0;
    {
        size_t pc = 64;
        first_pre = (char*)malloc(pc);
        if (first_pre) {
            first_pre[0] = 0;
            for (XsltFragNode* f = (XsltFragNode*)ex->frag_nodes; f;
                 f = f->next) {
                char* piece = serialize_frag_node_text(f->node);
                if (!piece) continue;
                size_t pl = strlen(piece);
                while (pre_len + pl + 1 > pc) {
                    pc *= 2;
                    first_pre = (char*)realloc(first_pre, pc);
                }
                memcpy(first_pre + pre_len, piece, pl + 1);
                pre_len += pl;
                free(piece);
            }
        }
    }
    size_t cap = pre_len + 64, total = 0;
    char* acc = (char*)malloc(cap);
    if (!acc) {
        free(first_pre);
        leptris_document_free(out);
        xslt_exec_free(ex);
        return NULL;
    }
    if (pre_len) {
        memcpy(acc, first_pre, pre_len);
        total = pre_len;
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
        char* piece = serialize_frag_node_text(sib);
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

    /* libxslt always breaks the line after the declaration —
     * including non-indenting outputs (all 162 decl-bearing
     * expected outputs in the adopted suite do). */
    if (acc && strncmp(acc, "<?xml", 5) == 0) {
        const char* q = strstr(acc, "?>");
        if (q && q[2] != '\n') {
            size_t at = (size_t)(q - acc) + 2;
            size_t len = strlen(acc);
            char* fixed = (char*)malloc(len + 2);
            if (fixed) {
                memcpy(fixed, acc, at);
                fixed[at] = '\n';
                memcpy(fixed + at + 1, acc + at, len - at + 1);
                free(acc);
                acc = fixed;
            }
        }
    }

    char* final = acc;
    if (ex->sheet->out_method_html) final = html_post_pass(acc);
    (void)0;
    free(first_pre);
    leptris_document_free(out);
    xslt_exec_free(ex);
    return final;
}
