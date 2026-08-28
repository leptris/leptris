/* xslt/xslt_functions.c — XSLT/EXSLT context function bridge
 * (TODO.transform phases 04 + 05).
 *
 * The board's design contract: "context-function bridge … injected
 * through the per-eval registry copy — the same path as custom
 * functions (SSOT)." Handlers reach this exec via
 * ctx->current_fn_user_data (set per-dispatch by the evaluator) so
 * the source document stays read-only.
 *
 * Scope (v1):
 *   XSLT core: current, generate-id, system-property, key,
 *              format-number, document.
 *   EXSLT:     exslt:node-set (and bare node-set), date:date-time,
 *              regexp:test.
 *
 * Deferred to follow-ups (documented inline):
 *   regexp:match / regexp:replace — POSIX regmatch_t-then-rebuild
 *                                    buffering is straightforward but
 *                                    out of scope for the initial
 *                                    convergence.
 */
#include "xslt_internal.h"
#include "../xpath/evaluator_internal.h"
#include "../xpath/functions.h"
#include "../dom/element.h"
#include "../dtd/model.h"
#include <stdio.h>
#include <time.h>
/* POSIX ERE for the EXSLT regexp handlers. MSVC has no <regex.h>;
 * Windows builds register no-op stubs until a portable engine
 * lands (documented limitation — tracked with TODO.xpath2's
 * expression work, which brings its own regex layer). */
#ifndef _WIN32
#include <regex.h>
#define LEPTRIS_XSLT_HAVE_REGEX 1
#endif
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* The next-document-order walker lives in xslt_exec.c. Lift it
 * here as a non-static helper so key() construction can iterate
 * every element in document order. */
LeptrisElement xslt_next_doc_order(LeptrisElement e);

/* ============================================================
 * Result + argument helpers
 * ============================================================ */

static struct leptris_xpath_result* res_string(const char* s) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_STRING);
    if (!r) return NULL;
    r->value.string_value = s ? leptris_strdup(s) : leptris_strdup("");
    if (!r->value.string_value) { xpath_result_free(r); return NULL; }
    return r;
}
static struct leptris_xpath_result* res_empty_ns(void) {
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_NODESET);
    if (!r) return NULL;
    r->value.nodeset_value = xpath_nodeset_new();
    if (!r->value.nodeset_value) { xpath_result_free(r); return NULL; }
    return r;
}
static struct leptris_xpath_result* res_nodeset_copy(const XPathNodeSet* in) {
    struct leptris_xpath_result* r = res_empty_ns();
    if (!r) return NULL;
    if (!in) return r;
    for (size_t i = 0; i < in->count; i++)
        xpath_nodeset_add(r->value.nodeset_value, in->nodes[i]);
    return r;
}

static XsltExec* exec_from(XPathContext* ctx) {
    return (XsltExec*)ctx->current_fn_user_data;
}

static char* arg_string(XPathContext* ctx, XPathASTNode** args,
                        size_t n, size_t i) {
    if (i >= n) return leptris_strdup("");
    struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
    if (!r) return leptris_strdup("");
    char* s = leptris_xpath_result_string(r);
    leptris_xpath_result_free(r);
    return s ? s : leptris_strdup("");
}
static double arg_number(XPathContext* ctx, XPathASTNode** args,
                         size_t n, size_t i) {
    if (i >= n) return 0;
    struct leptris_xpath_result* r = evaluate_expr(ctx, args[i]);
    if (!r) return 0;
    double d = leptris_xpath_result_number(r);
    leptris_xpath_result_free(r);
    return d;
}

/* ============================================================
 * XSLT core (§12): current, generate-id, system-property
 * ============================================================ */

/* current() — the node being processed by the surrounding template
 * rule or for-each (§12.4). The actual handler registered in the
 * bridge is xslt_fn_current_real (below) — it must read the exec
 * here since user_data wiring happens during registry init. */

/* generate-id numbering (bug-224): libxslt's deterministic
 * per-transform counter — "id" + 1-based sequence assigned in
 * first-request order; the same node re-queried returns the same
 * id. The map is exec-owned (freed with the transform). */
typedef struct {
    const void** nodes;
    size_t n, cap;
} XsltGidMap;

static size_t gid_for(XsltExec* ex, const void* node) {
    XsltGidMap* m = (XsltGidMap*)ex->gids;
    if (!m) {
        m = (XsltGidMap*)calloc(1, sizeof(*m));
        if (!m) return 0;
        ex->gids = m;
    }
    for (size_t i = 0; i < m->n; i++)
        if (m->nodes[i] == node) return i + 1;
    if (m->n == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 16;
        const void** grown =
            (const void**)realloc(m->nodes, cap * sizeof(*grown));
        if (!grown) return 0;
        m->nodes = grown;
        m->cap = cap;
    }
    m->nodes[m->n++] = node;
    return m->n;
}

void xslt_gids_free(XsltExec* ex) {
    XsltGidMap* m = ex ? (XsltGidMap*)ex->gids : NULL;
    if (!m) return;
    free(m->nodes);
    free(m);
    ex->gids = NULL;
}

static struct leptris_xpath_result* xslt_fn_generate_id(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    XsltExec* ex = exec_from(ctx);
    LeptrisElement target = NULL;
    if (n >= 1) {
        struct leptris_xpath_result* r = evaluate_expr(ctx, args[0]);
        if (r && r->type == XPATH_RESULT_NODESET &&
            r->value.nodeset_value && r->value.nodeset_value->count)
            target = (LeptrisElement)r->value.nodeset_value->nodes[0];
        if (r) leptris_xpath_result_free(r);
    } else {
        target = ex ? ex->current_node : NULL;
    }
    char buf[32];
    if (!target || !ex) buf[0] = '\0';
    else snprintf(buf, sizeof(buf), "id%zu", gid_for(ex, target));
    return res_string(buf);
}

static struct leptris_xpath_result* xslt_fn_system_property(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* name = arg_string(ctx, args, n, 0);
    const char* val = "";
    if (name) {
        if (strcmp(name, "xsl:version") == 0) val = "1.0";
        else if (strcmp(name, "xsl:vendor") == 0) val = "leptris";
        else if (strcmp(name, "xsl:vendor-url") == 0) val = "https://leptris.dev";
    }
    free(name);
    return res_string(val);
}

/* Patch the "current" nodeset shape — read the active template
 * / for-each node from the exec. Centralized so handlers don't
 * need to know the bridge mechanics. */
static struct leptris_xpath_result* xslt_fn_current_real(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)args; (void)n;
    XsltExec* ex = exec_from(ctx);
    struct leptris_xpath_result* r = res_empty_ns();
    if (ex && ex->current_node && r)
        xpath_nodeset_add(r->value.nodeset_value,
                          (LeptrisNodeRef)ex->current_node);
    return r;
}

/* ============================================================
 * xsl:key + key() (§12.2) — lazy per-(name) index
 * ============================================================ */

typedef struct xslt_key_bucket {
    char* value;
    XPathNodeSet* nodes;
    struct xslt_key_bucket* next;
} XsltKeyBucket;

typedef struct xslt_key_index {
    char* name;
    XsltKeyBucket** buckets;
    size_t cap;
    struct xslt_key_index* next;
} XsltKeyIndex;

static size_t str_hash(const char* s) {
    size_t h = 5381;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
        h = ((h << 5) + h) + *p;
    return h;
}

static XsltKeyIndex* key_index_for(const XsltExec* ex, const char* name) {
    for (XsltKeyIndex* k = (XsltKeyIndex*)ex->keys; k; k = k->next)
        if (strcmp(k->name, name) == 0) return k;
    return NULL;
}

static XsltKeyIndex* key_index_new(const char* name) {
    XsltKeyIndex* k = (XsltKeyIndex*)calloc(1, sizeof(*k));
    if (!k) return NULL;
    k->name = leptris_strdup(name ? name : "");
    k->cap = 64;
    k->buckets = (XsltKeyBucket**)calloc(k->cap, sizeof(XsltKeyBucket*));
    if (!k->buckets || !k->name) {
        free(k->buckets); free(k->name); free(k); return NULL;
    }
    return k;
}

static void key_index_put(XsltKeyIndex* k, const char* val, LeptrisNode* node) {
    if (!val || !node) return;
    size_t h = str_hash(val);
    XsltKeyBucket* b = k->buckets[h % k->cap];
    while (b && strcmp(b->value, val) != 0) b = b->next;
    if (!b) {
        b = (XsltKeyBucket*)calloc(1, sizeof(*b));
        if (!b) return;
        b->value = leptris_strdup(val);
        if (!b->value) { free(b); return; }
        b->nodes = xpath_nodeset_new();
        if (!b->nodes) { free(b->value); free(b); return; }
        b->next = k->buckets[h % k->cap];
        k->buckets[h % k->cap] = b;
    }
    xpath_nodeset_add(b->nodes, node);
}

static void key_index_free(XsltKeyIndex* k) {
    if (!k) return;
    for (size_t i = 0; i < k->cap; i++) {
        XsltKeyBucket* b = k->buckets[i];
        while (b) {
            XsltKeyBucket* nx = b->next;
            free(b->value);
            xpath_nodeset_free(b->nodes);
            free(b);
            b = nx;
        }
    }
    free(k->buckets); free(k->name); free(k);
}

static LeptrisNodeRef xslt_any_next_doc_order(LeptrisNodeRef n) {
    LeptrisNodeRef c = leptris_node_first_child(n);
    if (c) return c;
    while (n) {
        LeptrisNodeRef s = leptris_node_next_sibling(n);
        if (s) return s;
        n = leptris_node_parent(n);
    }
    return NULL;
}

static int xslt_keys_build(XsltExec* ex, const char* name) {
    if (!ex || !ex->sheet || !ex->source) return -1;
    int any = 0;
    for (XsltKeyDef* kd = ex->sheet->keys; kd; kd = kd->next)
        if (kd->name && kd->match && kd->use &&
            strcmp(kd->name, name) == 0) any = 1;
    if (!any) return 0;
    XsltKeyIndex* idx = key_index_new(name);
    if (!idx) return -1;
    /* Insert first so a concurrent lookup sees it. */
    XsltKeyIndex** tail = (XsltKeyIndex**)&ex->keys;
    while (*tail) tail = &(*tail)->next;
    *tail = idx;

    /* Every node KIND in document order — match="node()" /
     * match="text()" must index text and comment nodes too (the
     * elements-only walker dropped them, bug-133). */
    struct leptris_document* sdoc = (struct leptris_document*)ex->source;
    LeptrisNodeRef start =
        (LeptrisNodeRef)sdoc->doc_children_head;
    if (!start)
        start = (LeptrisNodeRef)leptris_document_root(ex->source);
    for (LeptrisNodeRef e = start; e; e = xslt_any_next_doc_order(e)) {
        for (XsltKeyDef* kd = ex->sheet->keys; kd; kd = kd->next) {
            if (!kd->name || strcmp(kd->name, name) != 0) continue;
            XsltPattern pat; memset(&pat, 0, sizeof(pat));
            pat.expr = kd->match;
            int m = xslt_pattern_matches(&pat, (LeptrisElement)e, ex->source,
                                       NULL);
            if (!m) continue;
            LeptrisElement saved_cur = ex->current_node;
            ex->current_node = (LeptrisElement)e;
            struct leptris_xpath_result* r =
                leptris_xpath_compiled_eval(kd->use, ex->source,
                                            (LeptrisElement)e);
            ex->current_node = saved_cur;
            if (!r) continue;
            char* sv = leptris_xpath_result_string(r);
            leptris_xpath_result_free(r);
            if (sv) {
                key_index_put(idx, sv, e);
                free(sv);
            }
        }
    }
    return 0;
}

/* Append every node in bucket `val` to the result nodeset. */
static void key_bucket_add(struct leptris_xpath_result* r,
                           XsltKeyIndex* k, const char* val) {
    size_t h = str_hash(val);
    XsltKeyBucket* b = k->buckets[h % k->cap];
    while (b && strcmp(b->value, val) != 0) b = b->next;
    if (!b) return;
    for (size_t i = 0; i < b->nodes->count; i++)
        xpath_nodeset_add(r->value.nodeset_value, b->nodes->nodes[i]);
}

static struct leptris_xpath_result* xslt_fn_key(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    XsltExec* ex = exec_from(ctx);
    if (!ex || n < 2) return res_empty_ns();
    char* name = arg_string(ctx, args, n, 0);
    struct leptris_xpath_result* r = res_empty_ns();
    if (!r) { free(name); return NULL; }
    if (!name || !*name) { free(name); return r; }
    XsltKeyIndex* k = key_index_for(ex, name);
    if (!k && xslt_keys_build(ex, name) == 0)
        k = key_index_for(ex, name);
    free(name);
    if (!k) return r;

    /* §12.2: the second argument converts via string() — a node-set
     * unions the buckets of EVERY node's string-value. */
    struct leptris_xpath_result* vr = evaluate_expr(ctx, args[1]);
    if (!vr) return r;
    if (vr->type == XPATH_RESULT_NODESET && vr->value.nodeset_value) {
        for (size_t i = 0; i < vr->value.nodeset_value->count; i++) {
            LeptrisNodeRef nd = vr->value.nodeset_value->nodes[i];
            XPathNodeSet* one = xpath_nodeset_new();
            if (!one) continue;
            xpath_nodeset_add(one, nd);
            struct leptris_xpath_result tmp;
            memset(&tmp, 0, sizeof(tmp));
            tmp.type = XPATH_RESULT_NODESET;
            tmp.value.nodeset_value = one;
            char* sv = leptris_xpath_result_string(&tmp);
            xpath_nodeset_free(one);
            if (sv) {
                key_bucket_add(r, k, sv);
                free(sv);
            }
        }
    } else {
        char* val = leptris_xpath_result_string(vr);
        if (val) {
            key_bucket_add(r, k, val);
            free(val);
        }
    }
    leptris_xpath_result_free(vr);
    return r;
}

void xslt_keys_free(XsltExec* ex) {
    if (!ex || !ex->keys) return;
    XsltKeyIndex* k = (XsltKeyIndex*)ex->keys;
    while (k) { XsltKeyIndex* nx = k->next; key_index_free(k); k = nx; }
    ex->keys = NULL;
}

/* ============================================================
 * document() (§12.1) — lazy per-exec cache
 * ============================================================ */

typedef struct xslt_doc_cache_entry {
    char* href;
    LeptrisDocument doc;
    struct xslt_doc_cache_entry* next;
} XsltDocEntry;

static LeptrisDocument xslt_doc_cache_get(XsltExec* ex, const char* href) {
    if (!ex || !href || !*href) return NULL;
    for (XsltDocEntry* e = (XsltDocEntry*)ex->docs; e; e = e->next)
        if (strcmp(e->href, href) == 0) return e->doc;
    LeptrisDocument d = leptris_parse_file(href, NULL);
    if (!d) return NULL;
    XsltDocEntry* ent = (XsltDocEntry*)calloc(1, sizeof(*ent));
    if (!ent) { leptris_document_free(d); return NULL; }
    ent->href = leptris_strdup(href);
    ent->doc = d;
    ent->next = (XsltDocEntry*)ex->docs;
    ex->docs = ent;
    return d;
}

void xslt_docs_free(XsltExec* ex) {
    if (!ex || !ex->docs) return;
    XsltDocEntry* e = (XsltDocEntry*)ex->docs;
    while (e) {
        XsltDocEntry* nx = e->next;
        free(e->href);
        if (e->doc) leptris_document_free(e->doc);
        free(e);
        e = nx;
    }
    ex->docs = NULL;
}

static struct leptris_xpath_result* xslt_fn_document(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    XsltExec* ex = exec_from(ctx);
    if (!ex || n < 1) return res_empty_ns();
    char* href = arg_string(ctx, args, n, 0);
    if (!href) return res_empty_ns();
    /* §12.1: the empty string references the stylesheet document
     * itself (the common lookup-table idiom). */
    if (!*href) {
        free(href);
        struct leptris_xpath_result* r = res_empty_ns();
        LeptrisNodeRef sd = ex->sheet_doc
            ? (LeptrisNodeRef)leptris_document_get_node(
                  (struct leptris_document*)ex->sheet_doc)
            : NULL;
        if (r && sd)
            xpath_nodeset_add(r->value.nodeset_value, sd);
        return r;
    }
    LeptrisDocument d = xslt_doc_cache_get(ex, href);
    free(href);
    struct leptris_xpath_result* r = res_empty_ns();
    if (!r) return NULL;
    /* §12.1: document() yields the DOCUMENT node — /ch selects the
     * root element itself, not its children. */
    LeptrisNodeRef dn = d ? (LeptrisNodeRef)leptris_document_get_node(d)
                          : NULL;
    if (dn) xpath_nodeset_add(r->value.nodeset_value, dn);
    return r;
}

/* ============================================================
 * format-number() (§12.3) — JDK1.1 pattern subset
 * ============================================================ */

static const XsltDecimalFormat* find_decformat(
        const XsltStylesheet* s, const char* name,
        const struct leptris_xpath_ns_map* ns) {
    if (!s || !s->decformats) return NULL;
    if (!name || !*name) return s->decformats;
    /* Namespace-expanded match: resolve the CALL's prefix through
     * the expression's ns context, then compare URI + local. */
    const char* colon = strchr(name, ':');
    char uri[256] = "";
    const char* local = name;
    if (colon) {
        local = colon + 1;
        const char* u = leptris_xpath_ns_lookup(
            (const struct leptris_xpath_ns_map*)ns, name,
            (size_t)(colon - name));
        snprintf(uri, sizeof(uri), "%s", u ? u : "");
    }
    for (XsltDecimalFormat* d = s->decformats; d; d = d->next) {
        if (!d->name) continue;
        if (strcmp(d->name, name) == 0) return d;   /* literal */
        if (d->local && d->uri && *d->uri && strcmp(d->local, local) == 0 &&
            strcmp(d->uri, uri) == 0)
            return d;
    }
    return NULL;
}

typedef struct {
    const char* prefix; size_t prefix_len;
    const char* suffix; size_t suffix_len;
    int min_int, max_int;     /* leading '0's and total digits */
    int min_frac, max_frac;
    int has_decimal;
    int has_grouping;
    int group_size;            /* digit slots after the LAST grouping
                                * separator (JDK semantics: '#,#0' = 2) */
    int multiplier;            /* 1, 100, 1000 */
} PatternInfo;

static const char* find_split(const char* s, const char* end) {
    int depth = 0;
    for (const char* p = s; p < end; p++) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        else if (*p == ';' && depth == 0) return p;
    }
    return end;
}

/* Does the byte at p start the multi-byte-safe separator sep? */
static int sep_at(const char* p, const char* end, const char* sep) {
    if (!sep) return 0;
    size_t sl = strlen(sep);
    if ((size_t)(end - p) < sl) return 0;
    return memcmp(p, sep, sl) == 0;
}

static void parse_one(const char* s, const char* end, PatternInfo* pi,
                       const char* dsep, const char* gsep) {
    /* Single pass: classify each char into prefix/int/digit/frac/
     * suffix; %/mille/./0-#/, are tokens. The cursor lets the
     * outer loop know we belong to the int vs frac part. */
    int phase = 0;   /* 0=prefix, 1=int, 2=frac, 3=suffix */
    pi->prefix = s; pi->prefix_len = 0;
    pi->suffix = end; pi->suffix_len = 0;
    pi->min_int = 0; pi->max_int = 0;
    pi->min_frac = 0; pi->max_frac = 0;
    pi->has_decimal = 0;
    pi->has_grouping = 0;
    pi->group_size = 0;
    pi->multiplier = 1;
    /* Walk: count contiguous prefix chars; first '0' or '#' begins
     * the int part; '.' moves to frac; '%' (and ‰ — v1 aliased to
     * '%' since per-mille is stored as ASCII; multi-byte ‰ is
     * treated as a multiplier trigger in the more general form) set
     * multiplier; other chars before/after turn into prefix/suffix. */
    const char* p = s;
    while (p < end) {
        unsigned char uc = (unsigned char)*p;
        char c = (char)uc;
        if (phase == 0) {
            if (c == '0' || c == '#') { phase = 1; continue; }
            pi->prefix_len++;
            if (c == '%') pi->multiplier = 100;
            else if (uc == 0xE2 &&
                     p + 2 < end && p[1] == (char)0x80 &&
                     p[2] == (char)0xB0) {  /* UTF-8 ‰ */
                pi->multiplier = 1000; p += 2;
            } else if (sep_at(p, end, dsep)) { pi->has_decimal = 1; }
            p++; continue;
        }
        if (phase == 1) {
            if (c == '0') {
                pi->min_int++; pi->max_int++;
                if (pi->has_grouping) pi->group_size++;
                p++; continue;
            }
            if (c == '#') {
                pi->max_int++;
                if (pi->has_grouping) pi->group_size++;
                p++; continue;
            }
            /* When both separators are the same string, the
             * DECIMAL reading wins (§12.3 ambiguity — libxslt). */
            if (sep_at(p, end, dsep)) {
                pi->has_decimal = 1;
                phase = 2; p += strlen(dsep); continue;
            }
            if (sep_at(p, end, gsep)) {
                pi->has_grouping = 1;
                /* JDK grouping size: digit slots after the LAST
                 * separator — reset the count at each separator. */
                pi->group_size = 0;
                p += strlen(gsep); continue;
            }

            if (c == '%') { pi->multiplier = 100; pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break; }
            if (uc == 0xE2 && p + 2 < end && p[1] == (char)0x80 &&
                p[2] == (char)0xB0) {
                pi->multiplier = 1000; p += 2;
                pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
            }
            pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
        }
        if (phase == 2) {
            if (c == '0') { pi->min_frac++; pi->max_frac++; p++; continue; }
            if (c == '#') { pi->max_frac++; p++; continue; }
            pi->suffix = p; pi->suffix_len = (size_t)(end - p); phase = 3; break;
        }
        break;
    }
}

/* Returns 1 when an explicit negative subpattern was present. */
static int parse_pattern(const char* s, size_t len,
                          PatternInfo* pos, PatternInfo* neg,
                          const char* dsep, const char* gsep) {
    const char* end = s + len;
    const char* split = find_split(s, end);
    parse_one(s, split, pos, dsep, gsep);
    if (split < end) {
        parse_one(split + 1, end, neg, dsep, gsep);
        return 1;
    }
    *neg = *pos;
    return 0;
}

/* Format |abs_v| per the parsed pattern; returns OWNED string.
 * Separators are full strings (any UTF-8 character). */
static char* format_value(const PatternInfo* p, double abs_v,
                           const char* decimal_sep,
                           const char* grouping_sep,
                           char zero_digit) {
    size_t dsl = strlen(decimal_sep);
    size_t gsl = strlen(grouping_sep);
    int prec = p->max_frac;
    char body[256];
    if (prec > 0) snprintf(body, sizeof(body), "%.*f", prec, abs_v);
    else snprintf(body, sizeof(body), "%.0f", abs_v);

    char intpart[128] = "", fracpart[128] = "";
    const char* dot = strchr(body, '.');
    if (dot) {
        size_t il = (size_t)(dot - body);
        memcpy(intpart, body, il); intpart[il] = 0;
        snprintf(fracpart, sizeof(fracpart), "%s", dot + 1);
    } else {
        snprintf(intpart, sizeof(intpart), "%s", body);
    }
    /* Right-pad frac with zeros up to min_frac, then truncate to max_frac. */
    {
        size_t fl = strlen(fracpart);
        if ((int)fl < p->min_frac) {
            for (; (int)fl < p->min_frac && fl + 1 < sizeof(fracpart); fl++)
                fracpart[fl] = zero_digit;
            fracpart[fl] = 0;
        }
        if ((int)fl > p->max_frac) fracpart[p->max_frac] = 0;
        /* Optional fraction digits: trailing zeros beyond min_frac
         * are not shown ('#,##0.##' of an integral value has no
         * decimal part). */
        while (fl > 0 && (int)fl > p->min_frac &&
               fracpart[fl - 1] == zero_digit)
            fracpart[--fl] = 0;
    }
    /* Left-pad int with zeros to min_int. */
    if ((int)strlen(intpart) < p->min_int) {
        char tmp[128]; size_t t = 0;
        for (int i = 0; i < p->min_int - (int)strlen(intpart) && t < sizeof(tmp) - 1; i++)
            tmp[t++] = zero_digit;
        snprintf(tmp + t, sizeof(tmp) - t, "%s", intpart);
        snprintf(intpart, sizeof(intpart), "%s", tmp);
    }
    /* Insert the grouping separator every group_size digits from
     * the right — the size the PATTERN defines (digit slots after
     * the last separator; '#,#0' groups by 2, '#,##0' by 3). */
    int gsz = p->group_size > 0 ? p->group_size : 3;
    if (p->has_grouping) {
        size_t gl = strlen(intpart);
        char rev[320]; size_t ri = 0, cnt = 0;
        for (size_t i = gl; i-- > 0 && ri + gsl < sizeof(rev); ) {
            rev[ri++] = intpart[i];
            if (++cnt % (size_t)gsz == 0 && i > 0 &&
                ri + gsl < sizeof(rev)) {
                /* Store the separator REVERSED — the whole rev
                 * buffer is byte-reversed below, which restores
                 * multi-byte separators in order. */
                for (size_t k = gsl; k-- > 0; )
                    rev[ri++] = grouping_sep[k];
            }
        }
        /* reverse rev → intpart */
        for (size_t i = 0; i < ri && i + 1 < sizeof(intpart); i++)
            intpart[i] = rev[ri - 1 - i];
        intpart[ri < sizeof(intpart) ? ri : sizeof(intpart) - 1] = 0;
    }
    char out[512]; size_t o = 0;
    for (size_t i = 0; i < p->prefix_len && o + 1 < sizeof(out); i++)
        out[o++] = p->prefix[i];
    if (p->prefix_len && p->multiplier != 1 && p->multiplier == 100) {
        /* JDK1.1: % is appended at the start of the prefix segment
         * (rightmost prefix char), but we already absorbed it as a
         * prefix byte (kept verbatim). No further change. */
    }
    for (size_t i = 0; intpart[i] && o + 1 < sizeof(out); i++) out[o++] = intpart[i];
    if (p->has_decimal && fracpart[0] && o + dsl < sizeof(out)) {
        memcpy(out + o, decimal_sep, dsl);
        o += dsl;
    }
    for (size_t i = 0; fracpart[i] && o + 1 < sizeof(out); i++) out[o++] = fracpart[i];
    for (size_t i = 0; i < p->suffix_len && o + 1 < sizeof(out); i++)
        out[o++] = p->suffix[i];
    out[o] = 0;
    return leptris_strdup(out);
}

char* xslt_format_number(const XsltStylesheet* sheet, double value,
                         const char* pattern, const char* df_name,
                         const struct leptris_xpath_ns_map* ns) {
    if (!pattern || !*pattern) pattern = "0";
    const XsltDecimalFormat* df = find_decformat(sheet, df_name, ns);
    if (!df) df = find_decformat(sheet, NULL, NULL);
    const char* decimal_sep  = df ? df->decimal_sep   : ".";
    const char* grouping_sep = df ? df->grouping_sep  : ",";
    char zero_digit   = df ? df->zero_digit    : '0';
    const char* nan_str  = df && df->nan       ? df->nan       : "NaN";
    const char* inf_str  = df && df->infinity ? df->infinity : "Infinity";
    char minus_sign    = df ? df->minus_sign   : '-';
    (void)minus_sign;

    if (value != value) return leptris_strdup(nan_str);
    int neg = (value < 0);
    PatternInfo pos = {0}, negi = {0};
    int has_neg = parse_pattern(pattern, strlen(pattern), &pos, &negi,
                                  decimal_sep, grouping_sep);
    PatternInfo* use = neg ? (has_neg ? &negi : &pos) : &pos;
    if (neg && !has_neg) {   /* negate with explicit minus prefix */
        char minus = df ? df->minus_sign : '-';
        size_t pl = use->prefix_len;   /* NOT strlen — prefix points
                                          into the pattern string */
        char* nv = (char*)malloc(pl + 2);
        if (nv) { memcpy(nv, use->prefix, pl); nv[pl] = minus; nv[pl+1] = 0;
                  use->prefix = nv; use->prefix_len = pl + 1; }
    }
    double av = neg ? -value : value;
    if (use->multiplier != 1) av *= use->multiplier;
    if (av >= HUGE_VAL || av <= -HUGE_VAL) {
        if (neg && use->prefix_len) {
            char* s = (char*)malloc(use->prefix_len + strlen(inf_str) + 1);
            if (s) { memcpy(s, use->prefix, use->prefix_len);
                      memcpy(s + use->prefix_len, inf_str, strlen(inf_str) + 1);
                      free((void*)use->prefix);
                      return s; }
        }
        return leptris_strdup(inf_str);
    }
    char* out = format_value(use, av, decimal_sep, grouping_sep, zero_digit);
    if (neg && use->prefix_len && !has_neg) free((void*)use->prefix);
    return out;
}

static struct leptris_xpath_result* xslt_fn_format_number(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    XsltExec* ex = exec_from(ctx);
    double v = arg_number(ctx, args, n, 0);
    char* pat = arg_string(ctx, args, n, 1);
    char* df  = (n >= 3) ? arg_string(ctx, args, n, 2) : leptris_strdup("");
    char* out = xslt_format_number(ex ? ex->sheet : NULL, v, pat, df,
                                ctx->ns_set);
    free(pat); free(df);
    struct leptris_xpath_result* r = res_string(out ? out : "");
    free(out);   /* res_string copies — the local is ours to free */
    return r;
}

/* ============================================================
 * EXSLT pack (subset)
 * ============================================================ */

static struct leptris_xpath_result* xslt_fn_node_set(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    if (n < 1) return res_empty_ns();
    struct leptris_xpath_result* in = evaluate_expr(ctx, args[0]);
    if (!in) return res_empty_ns();
    if (in->type == XPATH_RESULT_NODESET) {
        struct leptris_xpath_result* r = res_nodeset_copy(in->value.nodeset_value);
        leptris_xpath_result_free(in);
        return r;
    }
    /* Non-nodeset input: nothing to attach to without owning a
     * scratch doc here — return empty. Producers should ensure
     * variables hold RTF nodesets, which they do in this engine. */
    char* s = leptris_xpath_result_string(in);
    leptris_xpath_result_free(in);
    free(s);
    return res_empty_ns();
}

static struct leptris_xpath_result* xslt_fn_regexp_test(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* src = arg_string(ctx, args, n, 0);
    char* patstr = arg_string(ctx, args, n, 1);
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!r) { free(src); free(patstr); return NULL; }
    int matched = 0;
#ifdef LEPTRIS_XSLT_HAVE_REGEX
    if (src && patstr) {
        regex_t rx;
        if (regcomp(&rx, patstr, REG_EXTENDED) == 0) {
            matched = (regexec(&rx, src, 0, NULL, 0) == 0);
            regfree(&rx);
        }
    }
#else
    (void)src; (void)patstr;
#endif
    r->value.boolean_value = matched;
    free(src); free(patstr);
    return r;
}

/* regexp:match / regexp:replace — TODO. Full implementation lands
 * with TODO.engine extension work; v1 builders can use exslt:node-
 * set + per-call `=~`-style scripting or wait. The stubs return
 * identity / empty so callers have well-defined responses. */
static struct leptris_xpath_result* xslt_fn_regexp_match_v1(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* src = arg_string(ctx, args, n, 0);
    char* patstr = arg_string(ctx, args, n, 1);
    char* flags = (n >= 3) ? arg_string(ctx, args, n, 2) : leptris_strdup("");
    struct leptris_xpath_result* r = res_empty_ns();
    XsltExec* ex = exec_from(ctx);
    if (!r || !src || !patstr) { free(src); free(patstr); free(flags); return r; }
#ifdef LEPTRIS_XSLT_HAVE_REGEX
    regex_t rx;
    if (regcomp(&rx, patstr, REG_EXTENDED) == 0) {
        int global = flags && strchr(flags, 'g');
        LeptrisDocument host = (ex && ex->result) ? ex->result : NULL;
        size_t pos = 0;
        while (pos <= strlen(src)) {
            regmatch_t m;
            if (regexec(&rx, src + pos, 1, &m, 0) != 0) break;
            if (host) {
                size_t len = (size_t)(m.rm_eo - m.rm_so);
                char* sub = (char*)malloc(len + 1);
                if (sub) {
                    memcpy(sub, src + pos + m.rm_so, len);
                    sub[len] = 0;
                    LeptrisNodeRef t = leptris_text_node_create(host, sub);
                    free(sub);
                    if (t) xpath_nodeset_add(r->value.nodeset_value, t);
                }
            }
            size_t adv = (m.rm_eo > m.rm_so)
                           ? (size_t)(m.rm_eo - m.rm_so) : 1;
            pos += (size_t)m.rm_so + adv;
            if (!global) break;
        }
        regfree(&rx);
    }
#else
    (void)ex;
#endif
    free(src); free(patstr); free(flags);
    return r;
}

static struct leptris_xpath_result* xslt_fn_regexp_replace_v1(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    if (n < 3) return res_string("");
    char* src = arg_string(ctx, args, n, 0);
    char* patstr = arg_string(ctx, args, n, 1);
    char* repl = arg_string(ctx, args, n, 2);
    char* flags = (n >= 4) ? arg_string(ctx, args, n, 3) : leptris_strdup("");
    if (!src || !patstr || !repl) {
        free(src); free(patstr); free(repl); free(flags);
        return res_string("");
    }
    struct leptris_xpath_result* r = NULL;
#ifdef LEPTRIS_XSLT_HAVE_REGEX
    regex_t rx;
    if (regcomp(&rx, patstr, REG_EXTENDED) != 0) {
        free(src); free(patstr); free(repl); free(flags);
        return res_string("");
    }
    int global = flags && strchr(flags, 'g');
    size_t srclen = strlen(src);
    size_t cap = srclen + 64, o = 0;
    char* out = (char*)malloc(cap);
    size_t pos = 0;
    regmatch_t m[11];
    while (pos <= srclen &&
           regexec(&rx, src + pos, 10, m, 0) == 0) {
        /* Leading non-matching segment. */
        size_t head = (size_t)m[0].rm_so;
        while (o + head + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        memcpy(out + o, src + pos, head); o += head;
        /* Replacement with $1..$9 (and $0 = whole match). */
        for (const char* p = repl; *p; p++) {
            if (*p == '$' && p[1] >= '0' && p[1] <= '9') {
                int gi = p[1] - '0';
                p++;
                if (m[gi].rm_so < 0) continue;
                size_t gl = (size_t)(m[gi].rm_eo - m[gi].rm_so);
                while (o + gl + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                memcpy(out + o, src + pos + m[gi].rm_so, gl);
                o += gl;
            } else {
                if (o + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
                out[o++] = *p;
            }
        }
        size_t adv = (m[0].rm_eo > m[0].rm_so)
                         ? (size_t)m[0].rm_eo : 1;
        pos += (size_t)m[0].rm_so + adv;
        if (!global) break;
    }
    if (pos <= srclen) {
        size_t tail = srclen - pos;
        while (o + tail + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); }
        memcpy(out + o, src + pos, tail); o += tail;
    }
    out[o] = 0;
    r = res_string(out);
    free(out);
    regfree(&rx);
#else
    (void)flags;
    r = res_string(src);
#endif
    free(src); free(patstr); free(repl); free(flags);
    return r;
}

static struct leptris_xpath_result* xslt_fn_date_datetime(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)args; (void)n; (void)ctx;
    time_t t = time(NULL);
    struct tm* tm = gmtime(&t);
    char buf[40];
    if (tm)
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    else snprintf(buf, sizeof(buf), "1970-01-01T00:00:00Z");
    return res_string(buf);
}


/* §12.4 unparsed-entity-uri(name): the system identifier of the
 * unparsed entity declared in the source document's DTD. */
static struct leptris_xpath_result* xslt_fn_unparsed_entity_uri(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    XsltExec* ex = exec_from(ctx);
    char* name = arg_string(ctx, args, n, 0);
    if (!ex || !name || !ex->source || !ex->source) { free(name); return res_string(""); }
    struct leptris_document* d = (struct leptris_document*)ex->source;
    if (!d->dtd) { free(name); return res_string(""); }
    DTDEntityDecl* ent = ttdtd_lookup_entity(
        (const LeptrisDTD*)d->dtd, name);
    free(name);
    if (!ent || ent->type != DTD_ENTITY_EXTERNAL || !ent->system_id)
        return res_string("");
    return res_string(ent->system_id);
}

/* §14.2 element-available(name): true for the XSLT 1.0
 * instruction set. */
static struct leptris_xpath_result* xslt_fn_element_available(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* name = arg_string(ctx, args, n, 0);
    static const char* kInstrs[] = {
        "apply-imports", "apply-templates", "attribute",
        "call-template", "choose", "comment", "copy", "copy-of",
        "element", "fallback", "for-each", "if", "message", "number",
        "otherwise", "processing-instruction", "text", "value-of",
        "when", NULL };
    int avail = 0;
    if (name) {
        const char* bare = strncmp(name, "xsl:", 4) == 0
                               ? name + 4 : name;
        for (size_t i = 0; kInstrs[i]; i++) {
            if (strcmp(bare, kInstrs[i]) == 0) { avail = 1; break; }
        }
    }
    free(name);
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (r) r->value.boolean_value = avail;
    return r;
}

/* §14.2 function-available(name): the XPath 1.0 core + the XSLT
 * and EXSLT bridge surface. */
static struct leptris_xpath_result* xslt_fn_function_available(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    char* name = arg_string(ctx, args, n, 0);
    int avail = 0;
    if (name && ctx) {
        if (ctx->function_registry) {
            avail = xpath_function_registry_get(
                (XPathFunctionRegistry*)ctx->function_registry, name) != NULL;
        } else {
            extern XPathFunctionRegistry* xpath_function_registry_get_standard(void);
            avail = xpath_function_registry_get(
                xpath_function_registry_get_standard(), name) != NULL;
        }
        if (!avail) {
            static const char* kXslt[] = {
                "current", "generate-id", "system-property", "key",
                "format-number", "document", "unparsed-entity-uri",
                "element-available", "function-available",
                "exslt:node-set", "node-set", "regexp:test",
                "regexp:match", "regexp:replace", "date:date-time",
                NULL };
            for (size_t i = 0; kXslt[i]; i++) {
                if (strcmp(name, kXslt[i]) == 0) { avail = 1; break; }
            }
        }
    }
    free(name);
    struct leptris_xpath_result* r = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (r) r->value.boolean_value = avail;
    return r;
}

/* ============================================================
 * EXSLT func:function — stylesheet-defined XPath functions.
 *
 * Each definition is registered in the bridge registry under its
 * raw qname; the registry entry's user_data is a BINDING (exec-
 * owned array) carrying both the exec and the definition, because
 * handlers receive only ctx->current_fn_user_data.
 * ============================================================ */

typedef struct xslt_ufn_binding {
    XsltExec* ex;
    const XsltUserFunc* fn;
} XsltUfnBinding;

extern int xslt_exec_instrs(XsltExec*, const XsltInstr*, LeptrisElement);

static struct leptris_xpath_result* xslt_fn_user_func(
        XPathContext* ctx, XPathASTNode** args, size_t n) {
    (void)args; (void)n;
    XsltUfnBinding* b = (XsltUfnBinding*)ctx->current_fn_user_data;
    if (!b || !b->ex || !b->fn || !b->fn->body) {
        struct leptris_xpath_result* r =
            xpath_result_new(XPATH_RESULT_STRING);
        if (r) r->value.string_value = leptris_strdup("");
        return r;
    }
    XsltExec* ex = b->ex;
    const XsltUserFunc* fn = b->fn;
    LeptrisElement ctxnode = (LeptrisElement)ctx->context_node;

    /* Function scope (EXSLT): globals + own locals — the caller's
     * local frames are not visible. The body emits ONLY through
     * func:result; switch the output doc so stray text lands in a
     * scratch fragment instead of the result tree. */
    XsltVar* saved_vars = ex->vars;
    ex->vars = ex->global_vars;
    struct leptris_xpath_result* saved_result = ex->fn_result;
    int saved_yield = ex->fn_yield;
    LeptrisDocument main_result = ex->result;
    LeptrisElement saved_parent = ex->pending_parent;
    ex->result = leptris_document_create();
    ex->pending_parent = NULL;
    ex->fn_result = NULL;
    ex->fn_yield = 0;

    /* Argument binding (func:param): declaration-order params pair
     * with the call's argument expressions, evaluated in the
     * CALLER's context. Unbound params fall to their select
     * default (or empty). */
    {
        size_t ai = 0;
        for (const XsltInstr* p = fn->body; p; p = p->next) {
            if (p->kind != XSLT_INSTR_VARIABLE || !p->is_param ||
                !p->name)
                continue;
            struct leptris_xpath_result* v =
                (ai < n) ? evaluate_expr(ctx, args[ai++]) : NULL;
            if (!v && p->select) v = xslt_eval(ex, p->select, ctxnode);
            xslt_push_var(ex, p->name, v);
        }
    }
    XsltVar* fn_mark = ex->vars;

    xslt_exec_instrs(ex, fn->body, ctxnode);

    struct leptris_xpath_result* got = ex->fn_result;
    ex->fn_result = NULL;
    ex->fn_yield = saved_yield;
    xslt_pop_vars_to(ex, fn_mark);

    leptris_document_free(ex->result);
    ex->result = main_result;
    ex->pending_parent = saved_parent;
    ex->vars = saved_vars;
    if (saved_result) leptris_xpath_result_free(saved_result);

    if (!got) {
        got = xpath_result_new(XPATH_RESULT_STRING);
        if (got) got->value.string_value = leptris_strdup("");
    }
    return got;
}

/* Bindings live on the exec (registry entries point into them; the
 * registry is rebuilt per eval, the bindings are not). */
static XsltUfnBinding* ufn_bindings(XsltExec* ex) {
    if (ex->ufn) return (XsltUfnBinding*)ex->ufn;
    size_t n = 0;
    for (const XsltUserFunc* f = ex->sheet->funcs; f; f = f->next) n++;
    if (!n) return NULL;
    XsltUfnBinding* arr = (XsltUfnBinding*)calloc(n, sizeof(*arr));
    if (!arr) return NULL;
    size_t i = 0;
    for (const XsltUserFunc* f = ex->sheet->funcs; f; f = f->next, i++) {
        arr[i].ex = ex;
        arr[i].fn = f;
    }
    ex->ufn = arr;
    return arr;
}

void xslt_ufn_free(XsltExec* ex) {
    free(ex->ufn);
    ex->ufn = NULL;
}

/* ============================================================
 * Registry handler installation
 *
 * Per-eval path: xslt_exec.c calls xslt_register_bridge_handlers
 * with a freshly-built registry (cleanup frees it). Each entry's
 * user_data carries THIS exec so handlers reach keys/document
 * cache/current_node via the per-dispatch TLS slot.
 *
 * Caching note: the prototype cached this per-exec. Per-eval build
 * is correct but trades ~12 register calls for ownership
 * simplicity; revisit once the hot-path is benchmarked.
 * ============================================================ */

static void xslt_register_handler(XPathFunctionRegistry* r, const char* name,
                                   XPathFunctionHandler h, int min_args,
                                   int max_args, void* user_data) {
    xpath_function_registry_register(r, name, h, min_args, max_args);
    if (r->count > 0)
        r->functions[r->count - 1].user_data = user_data;
}

void xslt_register_bridge_handlers(XPathFunctionRegistry* r, void* exec) {
    if (!r) return;
    xslt_register_handler(r, "current", xslt_fn_current_real, 0, 0, exec);
    xslt_register_handler(r, "generate-id", xslt_fn_generate_id, 0, 1, exec);
    xslt_register_handler(r, "system-property", xslt_fn_system_property, 1, 1, exec);
    xslt_register_handler(r, "key", xslt_fn_key, 2, 2, exec);
    xslt_register_handler(r, "format-number", xslt_fn_format_number, 2, 3, exec);
    xslt_register_handler(r, "document", xslt_fn_document, 1, 2, exec);
    xslt_register_handler(r, "exslt:node-set", xslt_fn_node_set, 1, 1, exec);
    xslt_register_handler(r, "node-set", xslt_fn_node_set, 1, 1, exec);
    xslt_register_handler(r, "regexp:test", xslt_fn_regexp_test, 2, 2, exec);
    xslt_register_handler(r, "regexp:match", xslt_fn_regexp_match_v1, 2, 2, exec);
    xslt_register_handler(r, "regexp:replace", xslt_fn_regexp_replace_v1, 3, 3, exec);
    xslt_register_handler(r, "date:date-time", xslt_fn_date_datetime, 0, 0, exec);
    xslt_register_handler(r, "unparsed-entity-uri", xslt_fn_unparsed_entity_uri, 1, 1, exec);
    xslt_register_handler(r, "element-available", xslt_fn_element_available, 1, 1, exec);
    xslt_register_handler(r, "function-available", xslt_fn_function_available, 1, 1, exec);

    /* The first-party EXSLT pack rides along: str:/set:/math:
     * handlers are native (TODO.concurrency/06) and the libxslt
     * suite exercises them through XSLT sheets (set:distinct,
     * str:tokenize…). Their entries get the exec as user_data so
     * handlers needing context resolve it. */
    {
        extern void leptris_exslt_register(XPathFunctionRegistry*);
        size_t before = r->count;
        leptris_exslt_register(r);
        for (size_t i = before; i < r->count; i++)
            r->functions[i].user_data = exec;
    }

    /* Stylesheet-defined EXSLT functions (func:function): one
     * registry entry per definition, user_data = the binding. */
    if (exec) {
        XsltExec* ex = (XsltExec*)exec;
        XsltUfnBinding* arr = ufn_bindings(ex);
        if (arr) {
            size_t i = 0;
            for (const XsltUserFunc* f = ex->sheet->funcs; f;
                 f = f->next, i++)
                xslt_register_handler(r, f->name, xslt_fn_user_func,
                                      0, 8, &arr[i]);
        }
    }
}

/* The original exec-scoped builder is kept for the future cache
 * optimization; per-eval allocates a fresh registry above. */

XPathFunctionRegistry* xslt_bridge_registry(XsltExec* ex) {
    if (!ex) return NULL;
    if (ex->bridge) return (XPathFunctionRegistry*)ex->bridge;
    XPathFunctionRegistry* r = xpath_function_registry_new();
    if (!r) return NULL;
    xpath_function_registry_init_standard(r);
    xslt_register_bridge_handlers(r, ex);
    ex->bridge = r;
    return r;
}

void xslt_bridge_free(XsltExec* ex) {
    if (!ex || !ex->bridge) return;
    xpath_function_registry_free((XPathFunctionRegistry*)ex->bridge);
    ex->bridge = NULL;
}
