/* xinclude/xinclude.c — XInclude 1.0 support.
 *
 * Implements XInclude 1.0 processing for both parse="text" and
 * parse="xml" modes.  Walks the document tree, finds xi:include
 * elements, and substitutes them with the included content.
 *
 *   parse="text" — load the file as plain text, splice a text node
 *                  in place of the xi:include element.
 *   parse="xml"  — load+parse the file as XML, deep-copy the root
 *                  of the included document into the parent document's
 *                  pool, splice the copy in place of the xi:include.
 *                  xpointer fragment selection is TODO 92 Phase 3.
 *
 * XPointer forms (TODO.remaining/04): the xpointer attribute and
 * href fragment identifiers support bare shorthand (an NCName,
 * selecting the element with that ID), the element() scheme
 * (element(name/1/2) child sequences), and the xpointer() scheme
 * (a full XPath 1.0 expression).  Multiple space-separated schemes
 * are tried left to right; the first that selects an element wins.
 * xmlns(prefix=uri) components bind expression prefixes to
 * namespace URIs for subsequent xpointer() bodies (prefix tests
 * match by NAMESPACE, whatever prefix the included document
 * declared).  Non-element selections are treated as
 * no match (the move path needs an element).
 *
 * xi:fallback is honored in both modes.  Element-classification
 * helpers (is_include_element, etc.) are fully implemented and
 * documented in leptris.h.
 */

#include "../../include/leptris.h"
#include "../leptris_internal.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../dom/text.h"
#include "../dom/cdata.h"
#include "../dom/comment.h"
#include "../dom/pi.h"
#include "../memory/pool.h"
#include "../encoding/encoding.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define XINCLUDE_NAMESPACE "http://www.w3.org/2001/XInclude"

static int element_is_in_xinclude_namespace(LeptrisElement elem) {
    if (!elem) return 0;
    const char* ns_uri = leptris_element_get_namespace_uri(elem);
    if (!ns_uri) return 0;
    return strcmp(ns_uri, XINCLUDE_NAMESPACE) == 0;
}

static const char* get_attr_value(LeptrisElement elem, const char* name) {
    if (!elem || !name) return NULL;
    struct leptris_attribute* attr = leptris_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;
    return attr_cvalue(attr);
}

LEPTRIS_API int leptris_xinclude_is_include_element(LeptrisElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = leptris_element_get_name(elem);
    return name && strcmp(name, "include") == 0;
}

LEPTRIS_API int leptris_xinclude_is_fallback_element(LeptrisElement elem) {
    if (!element_is_in_xinclude_namespace(elem)) return 0;
    const char* name = leptris_element_get_name(elem);
    return name && strcmp(name, "fallback") == 0;
}

LEPTRIS_API const char* leptris_xinclude_get_href(LeptrisElement include_elem) {
    if (!leptris_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "href");
}

LEPTRIS_API const char* leptris_xinclude_get_parse(LeptrisElement include_elem) {
    if (!leptris_xinclude_is_include_element(include_elem)) return NULL;
    const char* parse = get_attr_value(include_elem, "parse");
    return (parse && parse[0]) ? parse : "xml";
}

LEPTRIS_API const char* leptris_xinclude_get_xpointer(LeptrisElement include_elem) {
    if (!leptris_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "xpointer");
}

LEPTRIS_API const char* leptris_xinclude_get_encoding(LeptrisElement include_elem) {
    if (!leptris_xinclude_is_include_element(include_elem)) return NULL;
    return get_attr_value(include_elem, "encoding");
}

/* ---- parse="text" processor ---- */

/* Load a file into a malloc'd buffer. Returns NULL on failure.
 * Caller must free the returned buffer. */
static char* load_file_content(const char* path, size_t* out_len) {
    if (!path) return NULL;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

/* ---- cross-document deep copy ---- */

/* Deep-copy a node subtree into `target_pool`, rewriting every StringView
 * and char* into freshly pool-owned storage.  The returned subtree is
 * self-contained — none of its pointers reference the source document.
 *
 * Returns NULL on allocation failure (partial copies are abandoned
 * along with the pool, which is freed document-scoped). */
static LeptrisNode* deep_copy_node(const LeptrisNode* src,
                                   LeptrisMemoryPool* target_pool,
                                   struct leptris_document* target_doc);

static LeptrisElement deep_copy_element(const LeptrisElement src,
                                        LeptrisMemoryPool* target_pool,
                                        struct leptris_document* target_doc) {
    const char* name = leptris_element_get_name(src);
    LeptrisElement dst = leptris_element_create_pooled(name, target_pool);
    if (!dst) return NULL;

    /* Copy attributes. */
    for (struct leptris_attribute* a = leptris_elem_first_attribute(src); a; a = leptris_attr_next(a)) {
        const char* a_name = attr_cname(a);
        const char* a_value = attr_cvalue(a);
        leptris_element_add_attribute_pooled(dst, a_name, a_value, target_pool);
    }

    /* Copy children (any type).  We can't use leptris_element_append_child
     * because it only links elements — mixed content needs the generic
     * set_next_sibling + parent.first_child / last_child wiring. */
    LeptrisNode* prev_copy = NULL;
    for (LeptrisNode* child = leptris_elem_first_child(src);
         child;
         child = leptris_node_get_next_sibling(child)) {

        LeptrisNode* child_copy = deep_copy_node(child, target_pool, target_doc);
        if (!child_copy) return NULL;

        if (prev_copy) {
            leptris_node_set_next_sibling(prev_copy, child_copy);
        } else {
            leptris_elem_set_first_child(dst, child_copy);
        }
        prev_copy = child_copy;
    }
    if (prev_copy) {
        leptris_elem_set_last_child(dst, prev_copy);
    }
    return dst;
}

static LeptrisNode* deep_copy_node(const LeptrisNode* src,
                                   LeptrisMemoryPool* target_pool,
                                   struct leptris_document* target_doc) {
    if (!src) return NULL;

    switch (src->type) {
        case LEPTRIS_NODE_TYPE_ELEMENT:
            return (LeptrisNode*)deep_copy_element((LeptrisElement)src, target_pool, target_doc);

        case LEPTRIS_NODE_TYPE_TEXT: {
            const LeptrisTextNode* t = (const LeptrisTextNode*)src;
            /* #1285 slice 4: runs are always NUL-terminated;
             * content_len is authoritative. TODO 115 Phase B. */
            return (LeptrisNode*)leptris_text_create(leptris_textnode_content(t),
                                                     t->content_len, target_pool,
                                                     target_doc);
        }

        case LEPTRIS_NODE_TYPE_CDATA: {
            const LeptrisCDATANode* c = (const LeptrisCDATANode*)src;
            const char* content = c->content ? c->content : "";
            return (LeptrisNode*)leptris_cdata_create(content, strlen(content), target_pool);
        }

        case LEPTRIS_NODE_TYPE_COMMENT: {
            const LeptrisCommentNode* cm = (const LeptrisCommentNode*)src;
            const char* content = cm->content ? cm->content : "";
            return (LeptrisNode*)leptris_comment_create(content, strlen(content), target_pool);
        }

        case LEPTRIS_NODE_TYPE_PI: {
            const LeptrisPINode* pi = (const LeptrisPINode*)src;
            const char* tgt = pi->target ? pi->target : "";
            const char* data = pi->data ? pi->data : "";
            return (LeptrisNode*)leptris_pi_create(tgt, strlen(tgt), data, strlen(data), target_pool);
        }

        default:
            /* DOCTYPE and other specialized nodes don't appear in element
             * content; nothing to copy. */
            return NULL;
    }
}

/* Replace one child of `parent` with `new_node`.
 * Walks parent->first_child to find the node before `old_node`,
 * then splices new_node in. */
static void replace_child_node(LeptrisElement parent,
                                LeptrisNode* old_node,
                                LeptrisNode* new_node) {
    if (!parent || !old_node || !new_node) return;

    LeptrisNode* prev = NULL;
    LeptrisNode* child = leptris_elem_first_child(parent);
    while (child && child != old_node) {
        prev = child;
        child = leptris_node_get_next_sibling(child);
    }
    if (child != old_node) return;  /* old_node not found */

    /* Splice new_node in place of old_node. */
    LeptrisNode* next = leptris_node_get_next_sibling(old_node);
    leptris_node_set_next_sibling(new_node, next);

    if (prev) {
        leptris_node_set_next_sibling(prev, new_node);
    } else {
        /* old_node was first_child */
        leptris_elem_set_first_child(parent, new_node);
    }

    /* Update last_child if old_node was last. */
    if (leptris_elem_last_child(parent) == old_node) {
        leptris_elem_set_last_child(parent, new_node);
    }
}

/* Build the full path to an xi:include href relative to base_url.
 * Writes into out (fixed buffer) and returns out on success, NULL on
 * overflow.  No dynamic allocation — keeps the call site simple. */
static char* join_path(char* out, size_t out_size,
                       const char* base_url, const char* href) {
    if (!href) return NULL;
    int n;
    if (base_url && base_url[0]) {
        n = snprintf(out, out_size, "%s/%s", base_url, href);
    } else {
        n = snprintf(out, out_size, "%s", href);
    }
    return (n < 0 || (size_t)n >= out_size) ? NULL : out;
}

/* Find the xi:fallback child of an xi:include element, if any. */
static LeptrisElement find_fallback(LeptrisElement include_elem) {
    for (LeptrisNodeRef fb = leptris_node_first_child((LeptrisNodeRef)include_elem);
         fb;
         fb = leptris_node_next_sibling(fb)) {
        LeptrisElement fe = leptris_node_as_element(fb);
        if (fe && leptris_xinclude_is_fallback_element(fe)) return fe;
    }
    return NULL;
}

/* Recursion depth cap. Per XInclude spec, the recursion limit is
 * impl-defined but must be bounded to prevent infinite loops on
 * mutually-recursive xi:include. 32 is generous; most real-world
 * inclusion chains are < 5 deep. */
#define XINCLUDE_MAX_DEPTH 32

/* ---- XPointer evaluation (TODO.remaining/04) ------------------------ */

typedef struct {
    const char* name;   size_t name_len;  /* scheme name or shorthand token */
    const char* body;   size_t body_len;  /* inside the parens; empty when none */
    int  has_parens;
} XptrComponent;

/* Split an xpointer value into scheme components. Whitespace
 * separates shorthand tokens; scheme(...) bodies are scanned with
 * paren-depth so nested parentheses inside xpointer() survive.
 * Components point into `value` (no allocation). Returns the count
 * (0 for NULL/empty), or -1 on malformed nesting. */
static int xptr_parse_components(const char* value, XptrComponent* out,
                                 int max) {
    if (!value) return 0;
    int count = 0;
    const char* p = value;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != '(' && *p != ' ' && *p != '\t') p++;
        size_t name_len = (size_t)(p - start);
        if (name_len == 0) return -1;
        if (*p == '(') {
            int depth = 1;
            p++;
            const char* body = p;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            if (depth != 0) return -1;   /* unbalanced */
            size_t body_len = (size_t)(p - 1 - body);
            if (count < max) {
                out[count].name = start; out[count].name_len = name_len;
                out[count].body = body;  out[count].body_len = body_len;
                out[count].has_parens = 1;
            }
            count++;
        } else {
            /* Bare token: shorthand pointer (an NCName). */
            if (count < max) {
                out[count].name = start; out[count].name_len = name_len;
                out[count].body = NULL;  out[count].body_len = 0;
                out[count].has_parens = 0;
            }
            count++;
        }
    }
    return count;
}

/* element() scheme body: [NCName]? ('/' integer)+ — walk child
 * ELEMENTS by 1-based position, starting from the element named by
 * the shorthand (via id()) or the document element when no name. */
static LeptrisElement xptr_element_scheme(LeptrisDocument doc,
                                          const char* body, size_t len) {
    char* buf = (char*)malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, body, len);
    buf[len] = '\0';

    LeptrisElement cur = NULL;
    const char* p = buf;
    const char* slash = strchr(p, '/');
    int first_step_is_document = 0;

    if (slash && slash != p) {
        /* NCName prefix: shorthand select, then child sequence. */
        size_t nlen = (size_t)(slash - p);
        char* name = (char*)malloc(nlen + 32);
        if (name) {
            snprintf(name, nlen + 32, "id('%.*s')", (int)nlen, p);
            LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, name);
            cur = r ? leptris_xpath_result_get(r, 0) : NULL;
            if (r) leptris_xpath_result_free(r);
            free(name);
        }
        p = slash;
    } else if (!slash) {
        /* element(name): shorthand select, no child sequence. */
        size_t nlen = len;
        char* name = (char*)malloc(nlen + 32);
        if (name) {
            snprintf(name, nlen + 32, "id('%.*s')", (int)nlen, p);
            LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, name);
            cur = r ? leptris_xpath_result_get(r, 0) : NULL;
            if (r) leptris_xpath_result_free(r);
            free(name);
        }
        free(buf);
        return cur;
    } else {
        /* element(/1/...): the first number selects among the
         * DOCUMENT's element children -- /1 is the document element
         * itself; further numbers step into element children. */
        first_step_is_document = 1;
    }

    /* Child sequence: /n1/n2/... selecting nth ELEMENT children. */
    while (p < buf + len) {
        if (*p != '/') { cur = NULL; break; }
        p++;
        if (p >= buf + len || *p < '0' || *p > '9') { cur = NULL; break; }
        long n = strtol(p, (char**)&p, 10);
        if (n < 1) { cur = NULL; break; }
        if (first_step_is_document) {
            /* The document has exactly one element child; the XPointer
             * framework only defines /1 at this level. */
            if (n != 1) { cur = NULL; break; }
            cur = leptris_document_root(doc);
            first_step_is_document = 0;
            continue;
        }
        if (!cur) break;
        LeptrisElement child = leptris_element_first_child_any(cur);
        long seen = 0;
        while (child) {
            seen++;
            if (seen == n) break;
            child = leptris_element_next_sibling_any(child);
        }
        cur = child;
    }
    free(buf);
    return cur;
}

/* Parse an xmlns() scheme body: prefix=URI (EscapedURI per the
 * framework; this subset takes it verbatim). Returns 1 and fills
 * prefix/uri (malloc'd) on success. */
static int xptr_xmlns_binding(const char* body, size_t len,
                              char** prefix, char** uri) {
    /* Skip leading/trailing whitespace. */
    while (len > 0 && (body[0] == ' ' || body[0] == '\t')) { body++; len--; }
    while (len > 0 && (body[len - 1] == ' ' || body[len - 1] == '\t')) len--;
    const char* eq = memchr(body, '=', len);
    if (!eq || eq == body) return 0;
    size_t plen = (size_t)(eq - body);
    char* p = (char*)malloc(plen + 1);
    if (!p) return 0;
    memcpy(p, body, plen);
    p[plen] = '\0';
    char* u = (char*)malloc(len - plen);
    if (!u) { free(p); return 0; }
    memcpy(u, eq + 1, len - plen - 1);
    u[len - plen - 1] = '\0';
    *prefix = p;
    *uri = u;
    return 1;
}

/* Evaluate an xpointer value against the included document.
 * Returns the first selected ELEMENT, or NULL when nothing (or only
 * non-elements) matched. Tries schemes left to right per XPointer.
 * xmlns(prefix=uri) components accumulate bindings (left to right,
 * per the XPointer framework) that resolve prefixed name tests in
 * subsequent xpointer() bodies by NAMESPACE, not literal prefix. */
static LeptrisElement xptr_select(LeptrisDocument doc, const char* value) {
    XptrComponent comps[8];
    int n = xptr_parse_components(value, comps, 8);
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    LeptrisElement found = NULL;
    for (int i = 0; i < n && i < 8 && !found; i++) {
        const XptrComponent* c = &comps[i];
        if (c->has_parens && c->name_len == 5 &&
            strncmp(c->name, "xmlns", 5) == 0) {
            char *pfx, *u;
            if (xptr_xmlns_binding(c->body, c->body_len, &pfx, &u)) {
                leptris_xpath_ns_set_add(ns, pfx, u);
                free(pfx);
                free(u);
            }
            continue;
        }
        if (c->has_parens && c->name_len == 8 &&
            strncmp(c->name, "xpointer", 8) == 0) {
            char* expr = (char*)malloc(c->body_len + 1);
            if (!expr) continue;
            memcpy(expr, c->body, c->body_len);
            expr[c->body_len] = '\0';
            LeptrisXPathResult r = leptris_xpath_eval_ns(doc, NULL, expr, ns);
            free(expr);
            if (!r) continue;
            LeptrisElement e = leptris_xpath_result_get(r, 0);
            /* Elements only: the ownership-transfer path needs one. */
            if (e) {
                /* The element belongs to `doc` (freed by the caller
                 * after the splice), so freeing the result here is
                 * safe. */
                leptris_xpath_result_free(r);
                found = e;
                break;
            }
            leptris_xpath_result_free(r);
        } else if (c->has_parens && c->name_len == 7 &&
                   strncmp(c->name, "element", 7) == 0) {
            LeptrisElement e = xptr_element_scheme(doc, c->body, c->body_len);
            if (e) { found = e; break; }
        } else if (!c->has_parens) {
            /* Bare token. When it is an NCName it is a shorthand
             * pointer (the element with that ID). Tokens that are
             * not NCNames are legacy raw XPath -- the pre-scheme
             * best-effort behavior, kept for compatibility. */
            int is_ncname = 1;
            for (size_t k = 0; k < c->name_len; k++) {
                char ch = c->name[k];
                if (ch == '/' || ch == '[' || ch == ']' || ch == '@' ||
                    ch == '(' || ch == ')' || ch == '=' || ch == '\'' ||
                    ch == '"' || ch == '*' || ch == '.' || ch == ' ' ||
                    ch == ':') {
                    is_ncname = 0;
                    break;
                }
            }
            char* expr;
            if (is_ncname) {
                expr = (char*)malloc(c->name_len + 32);
                if (!expr) continue;
                snprintf(expr, c->name_len + 32, "id('%.*s')",
                         (int)c->name_len, c->name);
            } else {
                expr = (char*)malloc(c->name_len + 1);
                if (!expr) continue;
                memcpy(expr, c->name, c->name_len);
                expr[c->name_len] = '\0';
            }
            LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, expr);
            free(expr);
            if (!r) continue;
            LeptrisElement e = leptris_xpath_result_get(r, 0);
            leptris_xpath_result_free(r);
            if (e) { found = e; break; }
        }
        /* Unknown schemes: skipped (lenient subset). */
    }
    leptris_xpath_ns_set_free(ns);
    return found;
}

/* Forward decls: internal recursion variant + catalog lookup. */
static LeptrisStatus xinclude_process_internal(struct leptris_document* doc,
                                              const char* base_url,
                                              int depth,
                                              const leptris_xinclude_opts* opts);
static void xinclude_catalog_lookup(const char* catalog_path,
                                    const char* uri, char* out,
                                    size_t out_size);

/* TODO 117 Phase C: cycle detection.
 *
 * Each include directive resolves to an absolute path.  We thread a
 * singly-linked list of `CycleNode` paths through the recursion so we
 * can reject A -> B -> A without going through the depth guard (which
 * protects against unbounded recursion, not cycles specifically).
 *
 * Memory: each entry is a malloc'd string, freed when the scope ends
 * (matching push/pop pairing in the recursion). */
typedef struct CycleNode {
    char* path;                 /* heap-allocated absolute path */
    struct CycleNode* next;
} CycleNode;

static int cycle_contains(CycleNode* ancestors, const char* path) {
    for (CycleNode* n = ancestors; n; n = n->next) {
        if (strcmp(n->path, path) == 0) return 1;
    }
    return 0;
}

static CycleNode* cycle_push(CycleNode* ancestors, const char* path) {
    CycleNode* n = (CycleNode*)malloc(sizeof(CycleNode));
    if (!n) return ancestors;
    n->path = strdup(path);
    n->next = ancestors;
    if (!n->path) { free(n); return ancestors; }
    return n;
}

static CycleNode* cycle_pop(CycleNode* ancestors) {
    if (!ancestors) return NULL;
    CycleNode* next = ancestors->next;
    free(ancestors->path);
    free(ancestors);
    return next;
}

/* Recursive walker. Bottom-up so that included content can itself
 * contain nested xi:include elements.  Returns 0 on first hard failure
 * (parse error, alloc failure), 1 otherwise. The `depth` parameter
 * (TODO 92 Phase 5) tracks how many xi:include layers we've descended
 * through; XINCLUDE_MAX_DEPTH stops mutual-recursion attacks. */
/* ---- TODO.assemble 03: xml:base (RFC 3986) ------------------------------
 *
 * The effective base of an element = merge(inherited_base, this
 * element's xml:base). merge_uri implements RFC 3986 §5.2 for the
 * cases hrefs actually exercise: absolute refs replace; scheme-
 * relative ("//host/x") keeps the scheme; root-relative ("/x")
 * keeps scheme+authority; otherwise the reference rides the base's
 * path directory with dot-segments removed. */
static int uri_is_absolute(const char* s) {
    for (const char* p = s; *p; p++) {
        if (*p == ':') return p != s;
        if (*p == '/' || *p == '?' || *p == '#') return 0;
    }
    return 0;
}

static void copy_dir_of(char* out, size_t out_size, const char* uri) {
    const char* slash = strrchr(uri, '/');
    if (!slash) {
        out[0] = 0;
        return;
    }
    size_t bl = (size_t)(slash - uri);
    if (bl >= out_size) bl = out_size - 1;
    memcpy(out, uri, bl);
    out[bl] = 0;
}

static void remove_dot_segments(char* path) {
    /* Rebuild the path segment by segment, dropping "." segments and
     * popping ".." against the last emitted one (RFC 3986 §5.2.4 for
     * the path-only form — no query/fragment here). */
    char tmp[8192];
    size_t tl = strlen(path);
    if (tl >= sizeof(tmp)) return; /* oversized: leave as-is */
    memcpy(tmp, path, tl + 1);

    char* out = path;
    size_t out_len = 0;
    const char* r = tmp;
    int absolute = (*r == '/');
    if (absolute) r++;

    while (*r) {
        const char* seg = r;
        while (*r && *r != '/') r++;
        size_t sl = (size_t)(r - seg);
        int had_slash = (*r == '/');
        if (had_slash) r++;

        if (sl == 1 && seg[0] == '.') continue;
        if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            if (out_len > 0) {
                while (out_len > 0 && out[out_len - 1] != '/') out_len--;
                if (out_len > 0) out_len--; /* the separator itself */
            }
            out[out_len] = 0;
            continue;
        }
        if (out_len == 0) {
            if (absolute) out[out_len++] = '/';
        } else if (out[out_len - 1] != '/') {
            out[out_len++] = '/';
        }
        memcpy(out + out_len, seg, sl);
        out_len += sl;
        out[out_len] = 0;
    }
    if (tl > 0 && tmp[tl - 1] == '/' &&
        !(out_len > 0 && out[out_len - 1] == '/')) {
        if (out_len == 0) {
            if (absolute) out[out_len++] = '/';
        } else if (out[out_len - 1] != '/') {
            out[out_len++] = '/';
        }
        out[out_len] = 0;
    }
}

static int merge_uri(const char* base, const char* ref, char* out,
                     size_t out_size) {
    if (!ref || !ref[0]) {
        snprintf(out, out_size, "%s", base ? base : "");
        return 0;
    }
    if (!base || !base[0] || uri_is_absolute(ref)) {
        snprintf(out, out_size, "%s", ref);
        return 0;
    }
    /* Filesystem-absolute reference: used as-is (the processor's
     * base is a plain directory prefix, not a URI with authority). */
    if (ref[0] == '/') {
        snprintf(out, out_size, "%s", ref);
        return 0;
    }
    if (strncmp(ref, "//", 2) == 0) {
        /* scheme-relative: reuse the base's scheme */
        const char* colon = strchr(base, ':');
        if (colon) {
            int n = snprintf(out, out_size, "%.*s:%s",
                             (int)(colon - base), base, ref);
            return (n < 0 || (size_t)n >= out_size);
        }
        snprintf(out, out_size, "%s", ref);
        return 0;
    }
    /* strip query/fragment from the base for the directory merge */
    char base_path[4096];
    snprintf(base_path, sizeof(base_path), "%s", base);
    char* cut = strpbrk(base_path, "?#");
    if (cut) *cut = 0;
    char dir[4096];
    copy_dir_of(dir, sizeof(dir), base_path);
    char joined[8192];
    if (dir[0])
        snprintf(joined, sizeof(joined), "%s/%s", dir, ref);
    else
        snprintf(joined, sizeof(joined), "%s", ref);
    remove_dot_segments(joined);
    snprintf(out, out_size, "%s", joined);
    return 0;
}

/* The element's effective base: nearest xml:base (this element or
 * nearest ancestor carrying one), merged over the inherited base. */
static void effective_xml_base(LeptrisElement elem, const char* inherited,
                               char* out, size_t out_size) {
    const char* xb = get_attr_value(elem, "xml:base");
    if (xb && xb[0] && inherited && inherited[0] &&
        !uri_is_absolute(xb)) {
        merge_uri(inherited, xb, out, out_size);
    } else if (xb && xb[0]) {
        snprintf(out, out_size, "%s", xb);
    } else {
        snprintf(out, out_size, "%s", inherited ? inherited : "");
    }
}

static int process_element_xinclude(LeptrisElement elem,
                                     struct leptris_document* doc,
                                     const char* base_url,
                                     const leptris_xinclude_opts* opts,
                                     int depth,
                                     CycleNode* ancestors) {
    if (!elem) return 1;

    char subtree_base[4096];
    effective_xml_base(elem, base_url, subtree_base, sizeof(subtree_base));
    base_url = subtree_base;

    LeptrisNodeRef child = leptris_node_first_child((LeptrisNodeRef)elem);
    while (child) {
        LeptrisNodeRef next = leptris_node_next_sibling(child);
        if (leptris_node_get_type(child) == LEPTRIS_NODE_TYPE_ELEMENT) {
            int rc = process_element_xinclude((LeptrisElement)child, doc,
                                              base_url, opts, depth,
                                              ancestors);
            if (!rc) return 0;
        }
        child = next;
    }

    if (!leptris_xinclude_is_include_element(elem)) return 1;

    const char* parse = leptris_xinclude_get_parse(elem);
    const char* href = leptris_xinclude_get_href(elem);
    if (!href || !href[0]) return 1;
    const char* xptr_attr = leptris_xinclude_get_xpointer(elem);

    /* XInclude §4: split a fragment identifier off the href. The
     * fragment (when present) is a shorthand pointer for parse="xml".
     * Resource errors per spec: fragment + explicit xpointer attr
     * together, any fragment on parse="text", or an xpointer attr on
     * parse="text". Resource error -> fallback, like a load failure. */
    const char* frag = strchr(href, '#');
    if (frag && !frag[1]) frag = NULL;   /* bare trailing '#' = none */
    char href_base[4096];
    size_t base_len = frag ? (size_t)(frag - href) : strlen(href);
    if (base_len >= sizeof(href_base)) return 1;
    memcpy(href_base, href, base_len);
    href_base[base_len] = '\0';

    int is_xml = (!parse || strcmp(parse, "xml") == 0);
    /* Resource errors per §4 fall through to the fallback handling
     * below -- an early return here would skip xi:fallback entirely. */
    int resource_error = 0;
    if (frag && xptr_attr && xptr_attr[0]) resource_error = 1;
    if (!is_xml && (frag || (xptr_attr && xptr_attr[0]))) resource_error = 1;

    char full_path[4096];
    if (!resource_error) {
        int rv;
        if (base_url && base_url[0])
            rv = merge_uri(base_url, href_base, full_path, sizeof(full_path));
        else
            rv = merge_uri(NULL, href_base, full_path, sizeof(full_path));
        if (rv) return 1;
    }
    /* XML Catalog lookup (exact <uri name=...> mappings) rewrites
     * the URI before the policy gate — catalogs exist precisely to
     * map remote names onto local resources. */
    char catalog_hit[4096];
    catalog_hit[0] = 0;
    if (!resource_error && opts && opts->catalog_path && opts->catalog_path[0]) {
        xinclude_catalog_lookup(opts->catalog_path, full_path, catalog_hit,
                                sizeof(catalog_hit));
    }
    const char* fetch_uri =
        catalog_hit[0] ? catalog_hit : full_path;
    /* TODO.assemble 04: network policy — remote schemes are resource
     * errors unless the caller explicitly enables them; the gate runs
     * on the POST-catalog URI (a catalog mapping may localize it). */
    int no_network = opts ? opts->no_network : 1;
    if (!resource_error && no_network && uri_is_absolute(fetch_uri) &&
        (strncmp(fetch_uri, "http://", 7) == 0 ||
         strncmp(fetch_uri, "https://", 8) == 0 ||
         strncmp(fetch_uri, "ftp://", 6) == 0 ||
         strncmp(fetch_uri, "ftps://", 7) == 0))
        resource_error = 1;

    /* TODO 117 Phase C: cycle detection.  If `full_path` is already in
     * the ancestor chain, we'd be re-including something we're
     * currently processing -- treat as failure (fallback below). */
    if (cycle_contains(ancestors, full_path)) {
        return 0;  /* signal failure so caller (or fallback) handles it */
    }

    size_t content_len = 0;
    char* content = NULL;
    int resolver_handled = 0;
    if (!resource_error && opts && opts->resolver) {
        int rr = opts->resolver(fetch_uri, opts->resolver_user, &content,
                                &content_len);
        if (rr == 1) {
            resolver_handled = 1;
        } else if (rr < 0) {
            content = NULL; /* forced resource error -> fallback */
        } else {
            content = NULL;
        }
    }
    if (!resource_error && !resolver_handled && !content)
        content = load_file_content(fetch_uri, &content_len);

    /* The substitute node — element for parse="xml", text for parse="text",
     * NULL on failure (fallback path below). */
    LeptrisNode* substitute = NULL;

    if (content) {
        if (is_xml) {
            /* TODO 117 Phase C: push current URI onto ancestor stack so
             * nested xi:include can detect cycles.  Pop after we're
             * done with this include (pushed node is the new head;
             * cycle_pop(ancestors) pops the head). */
            ancestors = cycle_push(ancestors, full_path);

            LeptrisStatus st = LEPTRIS_OK;
            LeptrisDocument included_doc = leptris_parse_string(content, content_len, &st);
            if (included_doc && st == LEPTRIS_OK) {
                /* Resolve any xi:include nested inside the included doc
                 * before splicing — XInclude processes recursively.
                 * Bump depth so mutual-recursion stops at MAX_DEPTH. */
                /* TODO.assemble 03: nested hrefs inside the included
                 * document resolve against the INCLUDED document's
                 * own location, not the includer's base. */
                {
                    char inc_dir[4096];
                    copy_dir_of(inc_dir, sizeof(inc_dir), full_path);
                    size_t dl = strlen(inc_dir);
                    if (dl + 1 < sizeof(inc_dir) && (dl == 0 ||
                                                     inc_dir[dl-1] != '/')) {
                        inc_dir[dl++] = '/';
                        inc_dir[dl] = 0;
                    }
                    xinclude_process_internal(included_doc, inc_dir,
                                              depth + 1, opts);
                }
                LeptrisElement inc_root = leptris_document_root(included_doc);
                if (inc_root) {
                    /* Phase 4 of TODO 92: xpointer attribute selects
                     * fragment from the included document. The value
                     * is evaluated as an XPath expression against the
                     * included doc. If it returns a nodeset, the
                     * resulting nodes are spliced into the parent
                     * (replacing the xi:include). Non-nodeset results
                     * or missing xpointer fall back to the root element.
                     *
                     * The XPointer framework also defines scheme-
                     * qualified forms (xpointer(...), element(/1/2),
                     * xmlns(...)) which we don't support here — the
                     * raw attribute value is passed straight to the
                     * XPath engine as a best-effort implementation. */
                    /* TODO.remaining/04: the pointer is the xpointer
                     * attribute (scheme forms) or the href fragment
                     * (a bare shorthand). The scheme evaluator tries
                     * xpointer()/element()/shorthand left to right and
                     * returns an ELEMENT for the move path; a match of
                     * only non-elements counts as no match. */
                    const char* xp = (xptr_attr && xptr_attr[0])
                                         ? xptr_attr
                                         : (frag ? frag + 1 : NULL);
                    if (xp && xp[0]) {
                        LeptrisElement first = xptr_select(included_doc, xp);
                        if (first) {
                            /* Move: the fragment still lives in
                             * included_doc's pool, so we adopt
                             * included_doc into our lifecycle to keep
                             * the pool alive. */
                            leptris_element_set_document_tree(first, doc);
                            leptris_document_adopt_child(doc, included_doc);
                            substitute = (LeptrisNode*)first;
                            included_doc = NULL;
                        }
                    }
                    if (!substitute) {
                        /* TODO 117 Phase A: pool ownership transfer.
                         *
                         * Move (don't copy) the included root into our
                         * tree.  The child's pool still owns the
                         * memory, so adopt the child document into
                         * our lifecycle -- it will be freed when we
                         * are.  Set included_doc = NULL below the
                         * adoption to skip the early-exit free. */
                        included_doc->root = NULL;
                        if (included_doc->new_dom_root ==
                            (void*)inc_root) {
                            included_doc->new_dom_root = NULL;
                        }
                        leptris_element_set_document_tree(inc_root, doc);
                        leptris_document_adopt_child(doc, included_doc);
                        substitute = (LeptrisNode*)inc_root;
                        included_doc = NULL;  /* now owned by `doc` */
                    }
                }
            }
            if (included_doc) leptris_document_free(included_doc);
            /* TODO 117 Phase C: pop the URI we pushed at the start.
             * cycle_pop pops the head (the pushed node) and returns
             * the previous head, restoring ancestors to its pre-push
             * value. */
            ancestors = cycle_pop(ancestors);
        } else if (strcmp(parse, "text") == 0) {
            /* TODO.remaining/04: the encoding attribute names the
             * source character set; convert to UTF-8 before splicing.
             * Only available with iconv; without it the attribute is
             * ignored (bytes passed through). Failed conversion is a
             * resource error -> fallback. */
            const char* enc = get_attr_value(elem, "encoding");
            if (enc && enc[0] &&
                strcmp(enc, "UTF-8") != 0 && strcmp(enc, "utf-8") != 0) {
#if defined(LEPTRIS_HAS_ICONV)
                size_t conv_len = 0;
                char* conv = leptris_encoding_convert(
                    enc, "UTF-8", content, content_len, &conv_len);
                if (!conv) {
                    free(content);
                    content = NULL;   /* resource error -> fallback */
                    substitute = NULL;
                    goto fallback;
                }
                free(content);
                content = conv;
                content_len = conv_len;
#endif
            }
            substitute = (LeptrisNode*)leptris_text_create(content, content_len, doc->pool, doc);
            /* parse="text" doesn't recurse into another file's body
             * (no risk of A -> B -> A via text), so no push/pop here. */
        }
        /* Unknown parse= value: silently skip, per XInclude spec. */
        free(content);
    }

#if defined(LEPTRIS_HAS_ICONV)
    /* Only referenced by the conversion-failure goto above; without
     * iconv the goto is compiled out and the label would dangle. */
fallback:
#endif
    if (!substitute) {
        /* Resource error: try xi:fallback before giving up. */
        LeptrisElement fb = find_fallback(elem);
        if (fb) {
            /* XInclude §4.5: the fallback's CHILDREN replace the
             * include — element children splice as-is (same pool,
             * moved, not copied; ruby#478's <offline/> shape), and
             * a text-only fallback keeps the historical single
             * text node. The old path read only TEXT content, so
             * an element fallback silently vanished together with
             * the include. */
            LeptrisNodeRef fbchild =
                leptris_node_first_child(leptris_element_as_node(fb));
            LeptrisNodeRef fb_elem_child = NULL;
            int fb_elem_count = 0;
            for (LeptrisNodeRef c = fbchild; c;
                 c = leptris_node_next_sibling(c)) {
                if (leptris_node_get_type(c) == LEPTRIS_NODE_TYPE_ELEMENT) {
                    if (!fb_elem_child) fb_elem_child = c;
                    fb_elem_count++;
                }
            }
            if (fb_elem_child) {
                LeptrisElement parent = leptris_element_get_parent(elem);
                if (parent) {
                    LeptrisNodeRef c = fbchild;
                    while (c) {
                        LeptrisNodeRef next =
                            leptris_node_next_sibling(c);
                        if (leptris_node_get_type(c) ==
                            LEPTRIS_NODE_TYPE_ELEMENT)
                            leptris_element_insert_before(
                                elem, (LeptrisElement)c);
                        c = next;
                    }
                    leptris_node_unlink((LeptrisNodeRef)elem);
                    leptris_element_set_document_tree(
                        (LeptrisElement)fb_elem_child, doc);
                    return 1;
                }
            }
            char* fb_text = leptris_element_get_text_content(fb);
            if (fb_text) {
                substitute = (LeptrisNode*)leptris_text_create(fb_text, strlen(fb_text), doc->pool, doc);
                leptris_free(fb_text);
            }
        }
        /* No fallback: spec says resource error SHOULD be reported; we're
         * lenient and leave the xi:include in place. */
    }

    if (substitute) {
        LeptrisElement parent = leptris_element_get_parent(elem);
        if (parent) {
            replace_child_node(parent, (LeptrisNode*)elem, substitute);

            /* The substitute was allocated from doc->pool but its
             * document pointer (and the document pointers of every
             * descendant) is still NULL.  Re-stamp them so future
             * callers can reach the pool through leptris_element_get_document(element). */
            if (substitute->type == LEPTRIS_NODE_TYPE_ELEMENT) {
                leptris_element_set_document_tree((LeptrisElement)substitute, doc);
            }
        }
    }

    return 1;
}

/* ---- TODO.assemble 04: XML Catalog (minimal) ----------------------------
 *
 * Parses an XML Catalog file and answers EXACT <uri name="..." uri=
 * "..."/> mappings. The file is re-read per lookup (catalogs are
 * small, XInclude counts are depth-bounded); a missing or malformed
 * catalog simply yields no rewrite. */
static void xinclude_catalog_lookup(const char* catalog_path,
                                    const char* uri, char* out,
                                    size_t out_size) {
    out[0] = 0;
    size_t clen = 0;
    char* text = load_file_content(catalog_path, &clen);
    if (!text) return;
    /* Exact <uri name="..." uri="..."/> mappings; attribute order is
     * free. Malformed input simply yields no rewrite. */
    const char* p = text;
    while ((p = strchr(p, '<')) != NULL) {
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (strncmp(p, "uri", 3) != 0 ||
            (p[3] != ' ' && p[3] != '\t' && p[3] != '\n' && p[3] != '\r' &&
             p[3] != '/' && p[3] != '>')) {
            continue;
        }
        const char* tag_end = strchr(p, '>');
        if (!tag_end) break;
        const char* name_v = NULL;
        const char* uri_v = NULL;
        const char* uri_v_end = NULL;
        const char* q = p + 3;
        while (q < tag_end) {
            const char* eq = strchr(q, '=');
            if (!eq || eq >= tag_end) break;
            const char* key = eq;
            while (key > p && key[-1] != ' ' && key[-1] != '\t' &&
                   key[-1] != '\n' && key[-1] != '\r')
                key--;
            size_t kl = (size_t)(eq - key);
            const char* vq = eq + 1;
            while (vq < tag_end && (*vq == ' ' || *vq == '\t'))
                vq++;
            if (vq >= tag_end || (*vq != '"' && *vq != '\'')) {
                q = eq + 1;
                continue;
            }
            char quote = *vq++;
            const char* vend = memchr(vq, quote, (size_t)(tag_end - vq));
            if (!vend) break;
            if ((kl == 4 && strncmp(key, "name", 4) == 0) &&
                (size_t)(vend - vq) == strlen(uri) &&
                strncmp(vq, uri, vend - vq) == 0)
                name_v = vq;
            if (kl == 3 && strncmp(key, "uri", 3) == 0) {
                uri_v = vq;
                uri_v_end = vend;
            }
            if (name_v && uri_v) {
                size_t urlen = (size_t)(uri_v_end - uri_v);
                if (urlen >= out_size) urlen = out_size - 1;
                memcpy(out, uri_v, urlen);
                out[urlen] = 0;
                free(text);
                return;
            }
            q = vend + 1;
        }
        p = tag_end + 1;
    }
    free(text);
}

/* Internal recursion-aware variant. Aborts with LEPTRIS_ERROR_INVALID_ARG
 * if depth exceeds XINCLUDE_MAX_DEPTH (TODO 92 Phase 5). */
static LeptrisStatus xinclude_process_internal(struct leptris_document* doc,
                                              const char* base_url,
                                              int depth,
                                              const leptris_xinclude_opts* opts) {
    if (!doc) return LEPTRIS_ERROR_NULL_ARG;
    if (depth > XINCLUDE_MAX_DEPTH) return LEPTRIS_ERROR_INVALID_ARG;

    LeptrisElement root = leptris_document_root(doc);
    if (!root) return LEPTRIS_OK;

    /* TODO 117 Phase C: thread empty ancestor list into the walk.
     * The first include pushes its own URI, so the second visit to the
     * same URI is detected as a cycle. */
    int rc = process_element_xinclude(root, doc, base_url, opts, depth,
                                      NULL);
    if (!rc) return LEPTRIS_ERROR_IO;
    return LEPTRIS_OK;
}

LEPTRIS_API LeptrisStatus leptris_xinclude_process(LeptrisDocument doc, const char* base_url) {
    /* no_network defaults to 1 for the legacy entry too — the remote
     * refusal is the documented posture (TODO.assemble 04). */
    leptris_xinclude_opts defaults;
    memset(&defaults, 0, sizeof(defaults));
    defaults.no_network = 1;
    return xinclude_process_internal(doc, base_url, 0, &defaults);
}

LEPTRIS_API LeptrisStatus leptris_xinclude_process_opts(
    LeptrisDocument doc, const char* base_url,
    const leptris_xinclude_opts* opts) {
    leptris_xinclude_opts defaults;
    if (!opts) {
        memset(&defaults, 0, sizeof(defaults));
        defaults.no_network = 1;
        opts = &defaults;
    }
    return xinclude_process_internal(doc, base_url, 0, opts);
}
