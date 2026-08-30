/* xslt/xslt_public.c — public XSLT 1.0 API (TODO.transform).
 *
 * leptris_xslt_transform: compile-once handle, apply-many. The
 * stylesheet document is parsed by the caller (or from a string via
 * the convenience entry), compiled once, and applied per source
 * document. Output honors xsl:output (method xml|text, indent,
 * omit-xml-declaration, encoding declaration). */
#include "xslt_internal.h"
#include "../dom/text.h"
#include "../dom/pi.h"
#include "../serialize/serialize.h"
#include "../encoding/encoding.h"   /* LeptrisSerializeExtended (issue #568) */
#include <stdlib.h>
#ifdef _MSC_VER
#include <string.h>   /* _stricmp/_strnicmp (no strings.h on MSVC) */
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
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
    /* Document child chain (issue #580): prolog PIs are the head of
     * the chain, before the root element. */
    for (LeptrisNode* n =
             (LeptrisNode*)((struct leptris_document*)doc)->doc_children_head;
         n; n = leptris_node_get_next_sibling(n)) {
        if (n->type == LEPTRIS_NODE_TYPE_ELEMENT) break;
        if (n->type != LEPTRIS_NODE_TYPE_PI) continue;
        LeptrisPINode* pi = (LeptrisPINode*)n;
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
    if (ex->eval_error) { xslt_exec_free(ex); return NULL; }
    LeptrisDocument out = ex->result;
    ex->result = NULL;    /* ownership moved */
    xslt_exec_free(ex);
    return out;
}

/* One fragment node by kind: element -> subtree serialize;
 * comment/PI/text -> their literal serialization. Under method=html
 * a PI closes SGML-style — `<?target data>`, no `?` (libxml2's
 * html serializer; libxslt bug-11-). Elements serialize through the
 * EXTENDED entry — fragment top-level elements must keep
 * cdata-section-elements and html/xhtml semantics (bug-90: every
 * element after the first lost the CDATA wrap). */
static char* serialize_frag_node_text(
        LeptrisNodeRef n, int html,
        const LeptrisSerializeOptions* opts,
        const LeptrisSerializeExtended* ext) {
    int ty = leptris_node_get_type(n);
    if (ty == LEPTRIS_NODE_TYPE_ELEMENT)
        return ext ? leptris_element_serialize_ex((LeptrisElement)n,
                                                  opts, ext)
                   : leptris_element_serialize((LeptrisElement)n, opts);
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
        snprintf(out, cap + 8, "<?%s%s%s%s",
                 target ? target : "", body && *body ? " " : "",
                 body ? body : "", html ? ">" : "?>");
    else if (ty == LEPTRIS_NODE_TYPE_COMMENT)
        snprintf(out, cap + 8, "<!--%s-->", body ? body : "");
    else
        snprintf(out, cap + 8, "%s", body ? body : "");
    return out;
}

/* §16.1 default output method: when xsl:output names no method, an
 * UNNAMESPACED html result root selects the html method (libxslt
 * parity — the exact condition also requires the root to be the
 * first non-blank result node; a namespaced xhtml root stays xml). */
static int effective_html_method(const XsltStylesheet* sheet,
                                 LeptrisElement root) {
    if (sheet->out_method_html) return 1;
    if (sheet->out_method_set) return 0;
    if (root && leptris_element_get_namespace_uri(root) == NULL) {
        const char* n = leptris_element_name(root);
        if (n) {
            size_t l = strlen(n);
            if ((l == 4 && (n[3] == 'l' || n[3] == 'L')) &&
                tolower((unsigned char)n[0]) == 'h' &&
                tolower((unsigned char)n[1]) == 't' &&
                tolower((unsigned char)n[2]) == 'm')
                return 1;
        }
    }
    return 0;
}

/* §16.1 default indent: html method indents yes unless the sheet
 * said otherwise; xml defaults no. */
static int effective_indent(const XsltStylesheet* sheet, int html) {
    if (sheet->out_indent >= 0) return sheet->out_indent;
    return html;
}

/* libxml2 xmlIsXHTML: EXACT matches against the XHTML 1.0 DTD ids
 * switch the xml-method serializer to XHTML mode (meta injection +
 * HTML-style minimized empty elements). */
static int xhtml_doctype(const char* pub, const char* sys) {
    static const char* const pubs[] = {
        "-//W3C//DTD XHTML 1.0 Strict//EN",
        "-//W3C//DTD XHTML 1.0 Transitional//EN",
        "-//W3C//DTD XHTML 1.0 Frameset//EN",
    };
    static const char* const syss[] = {
        "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd",
        "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd",
        "http://www.w3.org/TR/xhtml1/DTD/xhtml1-frameset.dtd",
    };
    for (int i = 0; i < 3; i++) {
        if (pub && strcmp(pub, pubs[i]) == 0) return 1;
        if (sys && strcmp(sys, syss[i]) == 0) return 1;
    }
    return 0;
}

/* §16.2 meta charset injection (libxslt htmlSetMetaEncoding
 * parity): a head with no encoding-bearing meta gets
 * <meta charset="ENC"> prepended as its first child — at the DOM
 * level, BEFORE serialization, so the html indent rules treat it
 * like any other child. */
static void inject_html_meta(LeptrisDocument out, const char* enc) {
    if (!out || !enc || !*enc) return;
    LeptrisElement root = leptris_document_root(out);
    if (!root) return;
    LeptrisElement head = NULL;
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(root));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) {
            const char* n = leptris_element_name((LeptrisElement)c);
            if (n && strcmp(n, "head") == 0) { head = (LeptrisElement)c; break; }
        }
    }
    if (!head) return;
    /* An author-provided meta wins — charset attr or http-equiv. */
    for (LeptrisNodeRef c =
             leptris_node_first_child(leptris_element_as_node(head));
         c; c = leptris_node_next_sibling(c)) {
        if (leptris_node_get_type(c) != LEPTRIS_NODE_TYPE_ELEMENT) continue;
        const char* n = leptris_element_name((LeptrisElement)c);
        if (!n || strcmp(n, "meta") != 0) continue;
        if (leptris_element_attribute((LeptrisElement)c, "charset") ||
            leptris_element_attribute((LeptrisElement)c, "http-equiv"))
            return;
    }
    LeptrisElement m = leptris_element_create(out, "meta");
    if (!m) return;
    /* libxslt's injected meta rides the html tree's namespace — a
     * namespace-less element under a default-namespaced root would
     * serialize with a spurious xmlns="" (bug-130). */
    const char* root_ns = leptris_element_get_namespace_uri(root);
    if (root_ns && *root_ns)
        leptris_element_set_namespace_uri(m, root_ns);
    if (leptris_element_set_attribute(m, "charset", enc) != LEPTRIS_OK) {
        /* The pool owns m; a failed attr leaves it detached and
         * document-free — safe to leak-free via the doc. */
        return;
    }
    leptris_node_prepend_child((LeptrisNodeRef)head, (LeptrisNodeRef)m);
}

#ifndef LEPTRIS_HAS_ICONV
/* Minimal UTF-8 -> ISO-8859-1 transcoder for builds without iconv
 * (CI runs LEPTRIS_ENABLE_ICONV=OFF): the Latin-1 subset needs no
 * conversion table — every codepoint <= U+00FF maps 1:1 — so Western
 * output encodings stay byte-faithful. Returns NULL for any other
 * encoding name (unknown encodings pass the body through). */
static char* latin1_from_utf8(const char* enc, const char* s,
                              size_t* out_len) {
    if (strcasecmp(enc, "ISO-8859-1") != 0 &&
        strcasecmp(enc, "ISO8859-1") != 0 &&
        strcasecmp(enc, "LATIN1") != 0 &&
        strcasecmp(enc, "LATIN-1") != 0)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        unsigned cp = c;
        size_t len = 1;
        if (c >= 0xC2 && c <= 0xDF) len = 2;
        else if (c >= 0xE0 && c <= 0xEF) len = 3;
        else if (c >= 0xF0 && c <= 0xF4) len = 4;
        if (len > 1) {
            int ok = 1;
            cp = c & (unsigned)(0x7Fu >> len);
            for (size_t k = 1; k < len; k++) {
                if (i + k >= n ||
                    ((unsigned char)s[i + k] & 0xC0) != 0x80) {
                    ok = 0;
                    break;
                }
                cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3Fu);
            }
            /* Stray high byte (latin-1 source parsed byte-passthrough
             * on no-iconv builds): emit it unchanged. */
            if (!ok) { cp = c; len = 1; }
        }
        i += len;
        out[w++] = (cp <= 0xFF) ? (char)cp : '?';
    }
    out[w] = 0;
    *out_len = w;
    return out;
}
#endif

LEPTRIS_API char* leptris_xslt_apply_string(LeptrisXslt xslt,
                                            LeptrisDocument source) {
    if (!xslt || !source) return NULL;
    XsltExec* ex = xslt_transform_doc(xslt->compiled,
                                     xslt->sheet_doc, source);
    if (!ex) return NULL;
    if (ex->eval_error) { xslt_exec_free(ex); return NULL; }
    LeptrisDocument out = ex->result;
    ex->result = NULL;

    LeptrisElement root = out ? leptris_document_root(out) : NULL;
    int html_method = effective_html_method(ex->sheet, root);

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
         * the result — text/comment/PI in emitted order. §16.1: the
         * declaration still leads unless omitted or text-method
         * (libxslt parity — bug-31-). */
        size_t cap = 64, len = 0;
        char* acc = (char*)malloc(cap);
        if (!acc) { xslt_exec_free(ex); return NULL; }
        acc[0] = 0;
        if (!html_method && !ex->sheet->out_method_text &&
            !ex->sheet->out_omit_decl) {
            const char* decl = "<?xml version=\"1.0\"?>\n";
            size_t dl = strlen(decl);
            memcpy(acc, decl, dl + 1);
            len = dl;
        }
        LeptrisSerializeOptions frag_opts = {0};
        frag_opts.indent = effective_indent(ex->sheet, html_method) ? 2 : 0;
        for (XsltFragNode* f = (XsltFragNode*)ex->frag_nodes; f;
             f = f->next) {
            char* piece = serialize_frag_node_text(f->node, html_method,
                                                   &frag_opts, NULL);
            if (!piece) continue;
            size_t pl = strlen(piece);
            while (len + pl + 1 > cap) cap *= 2;
            acc = (char*)realloc(acc, cap);
            memcpy(acc + len, piece, pl + 1);
            len += pl;
            free(piece);
        }
        if (out) leptris_document_free(out);
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
    /* §16.2: the html method never writes a declaration — the
     * serializer owns every HTML semantic now (layout, voids,
     * raw text, attr escaping, PI form). */
    opts.xml_declaration =
        (!ex->sheet->out_method_text && !html_method &&
         !ex->sheet->out_omit_decl) ? 1 : 0;
    /* §16.1 indent="yes": 2-space pretty-print via the shared
     * serializer (text-only elements stay inline — libxslt rule).
     * The cdata/html-method settings ride the extended-options
     * entry — the public struct is ABI-frozen (issue #568). */
    opts.indent = effective_indent(ex->sheet, html_method) ? 2 : 0;
    LeptrisSerializeExtended ext = {0};
    ext.html_method = html_method;
    /* libxslt serialize semantics: ws-only text children count as
     * mixed (the formatter stops below them, bug-98). */
    if (!html_method && opts.indent) ext.ws_mixed = 1;
    /* libxml2 xmlIsXHTML: the doctype ids select XHTML serialization
     * for the xml output method. */
    if (!html_method && !ex->sheet->out_method_text)
        ext.xhtml = xhtml_doctype(ex->sheet->out_doctype_public,
                                  ex->sheet->out_doctype_system);
    /* §16.1: xsl:output encoding names the output declaration's
     * encoding (bug-132) — verbatim, because this layer transcodes
     * the body to the same encoding after serialization (bug-140). */
    opts.encoding = ex->sheet->out_encoding;
    ext.decl_encoding_verbatim = 1;
    /* musl GCC rejects the qualified-pointer store (#582). */
    ext.cdata_elements = (const char* const*)ex->sheet->out_cdata;
    ext.cdata_element_count = ex->sheet->out_cdata_count;
    if (html_method)
        inject_html_meta(out, ex->sheet->out_encoding
                                ? ex->sheet->out_encoding : "UTF-8");
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
                char* piece = serialize_frag_node_text(f->node, html_method,
                                                       &opts, &ext);
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
    char* first = leptris_document_serialize_ex(out, &opts, &ext);
    /* Declaration FIRST, then the pre-root fragment nodes, then the
     * document body (top-level comments/PIs precede the root
     * element in document order). */
    size_t decl_len = 0;
    if (first && strncmp(first, "<?xml", 5) == 0) {
        const char* end = strstr(first, "?>");
        if (end) decl_len = (size_t)(end - first) + 2;
    }
    /* §16.2 doctype-system/doctype-public: `<!DOCTYPE name ...>`
     * right after the declaration. The html method's version="5"
     * shorthand is the bare HTML5 doctype; html with only a public
     * id omits the system id (bug-175). */
    char doctype[512];
    size_t doctype_len = 0;
    doctype[0] = '\0';
    if (root) {
        const char* pub = ex->sheet->out_doctype_public;
        const char* sys = ex->sheet->out_doctype_system;
        int html5 = html_method && ex->sheet->out_version &&
                    ex->sheet->out_version[0] == '5';
        const char* name = leptris_element_name(root);
        if (!name) name = "doc";
        if (html5) {
            doctype_len = (size_t)snprintf(doctype, sizeof doctype,
                                           "<!DOCTYPE html>\n");
        } else if (pub && sys) {
            doctype_len = (size_t)snprintf(
                doctype, sizeof doctype,
                "<!DOCTYPE %s PUBLIC \"%s\" \"%s\">\n", name, pub, sys);
        } else if (pub) {
            doctype_len = (size_t)snprintf(
                doctype, sizeof doctype,
                "<!DOCTYPE %s PUBLIC \"%s\">\n", name, pub);
        } else if (sys) {
            doctype_len = (size_t)snprintf(
                doctype, sizeof doctype,
                "<!DOCTYPE %s SYSTEM \"%s\">\n", name, sys);
        } else {
            doctype_len = 0;
        }
        if (doctype_len >= sizeof doctype) doctype_len = sizeof doctype - 1;
    }
    size_t cap = doctype_len + pre_len + (first ? strlen(first) : 0) + 64,
           total = 0;
    char* acc = (char*)malloc(cap);
    if (!acc) {
        free(first_pre);
        leptris_free_string(first);
        leptris_document_free(out);
        xslt_exec_free(ex);
        return NULL;
    }
    if (decl_len) {
        memcpy(acc, first, decl_len);
        total = decl_len;
    }
    if (doctype_len) {
        memcpy(acc + total, doctype, doctype_len);
        total += doctype_len;
        /* The serializer breaks the line after the declaration; the
         * doctype carries its own newline — drop the duplicate. */
        if (first && first[decl_len] == '\n') decl_len++;
    }
    if (pre_len) {
        /* The serializer's post-declaration newline belongs to the
         * DECLARATION — libxslt writes decl+\n as a unit, then the
         * pre-root nodes. Splicing the pre-root text between them
         * turned the first element's line into "  \n<elem>" instead
         * of "\n  <elem>" (bug-90). */
        if (first && decl_len && first[decl_len] == '\n') {
            acc[total++] = '\n';
            decl_len++;
        }
        memcpy(acc + total, first_pre, pre_len);
        total += pre_len;
    }
    if (first) {
        size_t fl = strlen(first);
        while (total + fl + 1 > cap) cap *= 2;
        acc = (char*)realloc(acc, cap);
        if (acc) {
            memcpy(acc + total, first + decl_len,
                   fl - decl_len + 1);
            total += fl - decl_len;
        }
        leptris_free_string(first);
    }
    for (LeptrisNodeRef sib = leptris_node_next_sibling(
             leptris_element_as_node(root));
         sib; sib = leptris_node_next_sibling(sib)) {
        char* piece = serialize_frag_node_text(sib, html_method,
                                               &opts, &ext);
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

    /* libxslt writes ONE final newline after the last top-level node
     * when the output indents (xsltSaveResultTo's closing write). */
    if (acc && opts.indent) {
        while (total + 2 > cap) {
            cap *= 2;
            acc = (char*)realloc(acc, cap);
        }
        if (acc) {
            acc[total++] = '\n';
            acc[total] = '\0';
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
    /* §16.1 output encoding: us-ascii escapes non-ASCII as numeric
     * character references; single-byte legacy encodings transcode
     * the UTF-8 result via iconv (libxslt behavior, bugs 159/95). */
    if (final && ex->sheet->out_encoding && *ex->sheet->out_encoding) {
        const char* enc = ex->sheet->out_encoding;
        if (strcasecmp(enc, "us-ascii") == 0 ||
            strcasecmp(enc, "ascii") == 0) {
            size_t need = 0;
            for (const unsigned char* q = (const unsigned char*)final;
                 *q; q++)
                need += (*q >= 0x80) ? 8 + 1 : 1;
            char* esc = (char*)malloc(need + 1);
            if (esc) {
                char* w = esc;
                const unsigned char* q = (const unsigned char*)final;
                while (*q) {
                    if (*q < 0x80) {
                        *w++ = (char)*q++;
                    } else {
                        /* Decode one UTF-8 codepoint. */
                        unsigned cp;
                        int n;
                        if ((*q & 0xE0) == 0xC0) {
                            cp = (*q & 0x1F); n = 1;
                        } else if ((*q & 0xF0) == 0xE0) {
                            cp = (*q & 0x0F); n = 2;
                        } else {
                            cp = (*q & 0x07); n = 3;
                        }
                        q++;
                        for (int k = 0; k < n && *q; k++, q++)
                            cp = cp << 6 | (*q & 0x3F);
                        w += sprintf(w, "&#%u;", cp);
                    }
                }
                *w = 0;
                free(final);
                final = esc;
            }
        } else if (strcasecmp(enc, "utf-8") != 0 &&
                   strcasecmp(enc, "utf8") != 0) {
            char* conv = NULL;
            size_t outlen = 0;
#ifdef LEPTRIS_HAS_ICONV
            conv = leptris_encoding_convert(
                "UTF-8", enc, final, strlen(final), &outlen);
#else
            conv = latin1_from_utf8(enc, final, &outlen);
#endif
            if (conv && outlen) {
                conv[outlen] = 0;
                free(final);
                final = conv;
            }
        }
    }
    free(first_pre);
    leptris_document_free(out);
    xslt_exec_free(ex);
    return final;
}
