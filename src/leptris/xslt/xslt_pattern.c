/* xslt/xslt_pattern.c — XSLT 1.0 match patterns (TODO.transform 01).
 *
 * §5.2: a node matches a pattern when one of its alternatives
 * SELECTS it. Correct evaluation walks the ANCESTOR LADDER: the
 * alternative is evaluated with each ancestor as context, and the
 * node must appear in the result (this is what "b/c" matching a c
 * under b means — the suffix is tested at every level).
 *
 * SSOT: alternatives are ordinary compiled XPath expressions; the
 * matcher adds only the contains-the-node test. Documented v1 edge:
 * a RELATIVE single-name pattern matching the ROOT element is
 * special-cased (the evaluator's context is the root element, not a
 * document node, so child::name cannot select the root itself). */
#include "xslt_internal.h"
#include "../dom/element.h"   /* LeptrisAttributeNode */
#include <stdio.h>

/* ============================================================
 * Compiled step ladder (pattern-compiler fast path)
 *
 * A child-axis alternative like book[title] or a/b/c costs O(sib-
 * lings) per candidate on the general ladder: each ancestor rung
 * evaluates the expression as a downward XPath and membership-
 * scans the result. The compiled ladder inverts the question —
 * the LAST step is tested against the candidate directly (one
 * predicate eval on its own subtree) and the earlier steps are
 * name/kind checks walking UP the parent chain. O(depth).
 * ============================================================ */

static void pat_step_free(XsltPatStep* s) {
    if (!s) return;
    if (s->name) free(s->name);
    if (s->pred) leptris_xpath_compiled_free(s->pred);
}

void xslt_pattern_steps_free(XsltPattern* p) {
    if (!p || !p->steps) return;
    for (int i = 0; i < p->n_steps; i++) pat_step_free(&p->steps[i]);
    free(p->steps);
    p->steps = NULL;
    p->n_steps = 0;
    p->steps_valid = 0;
}

static int ncname_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

/* Parse one step at *pp (advancing past it). Returns 0 on a shape
 * the fast path does not model — the caller falls back to the
 * general ladder. Whitespace between tokens is tolerated. */
static int parse_pat_step(const char** pp, const char* end,
                          XsltPatStep* s) {
    const char* p = *pp;
    memset(s, 0, sizeof(*s));
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end) return 0;
    if (*p == '@') { s->is_attr = 1; p++; }
    if (p < end && *p == '*') {
        p++;
    } else if (p < end && ncname_char(*p) && *p != '-') {
        const char* st = p;
        while (p < end && ncname_char(*p)) p++;
        /* Prefixed name tests resolve through the template's ns set
         * — leave those to the general ladder. */
        for (const char* q = st; q < p; q++)
            if (*q == ':') return 0;
        if (p < end && *p == ':') return 0;
        size_t n = (size_t)(p - st);
        s->name = (char*)malloc(n + 1);
        if (!s->name) return 0;
        memcpy(s->name, st, n);
        s->name[n] = 0;
    } else if (p + 5 <= end && strncmp(p, "node(", 5) == 0) {
        s->kind = 1; p += 5;
    } else if (p + 5 <= end && strncmp(p, "text(", 5) == 0) {
        s->kind = 2; p += 5;
    } else if (p + 8 <= end && strncmp(p, "comment(", 8) == 0) {
        s->kind = 3; p += 8;
    } else if (p + 21 <= end &&
               strncmp(p, "processing-instruction(", 23 - 2) == 0) {
        s->kind = 4; p += 21;
    } else {
        return 0;
    }
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (s->kind) {
        if (p >= end || *p != ')') { pat_step_free(s); return 0; }
        p++;
    }
    /* Optional [pred] — balanced to the matching close. */
    if (p < end && *p == '[') {
        int depth = 0;
        const char* st = p;
        while (p < end) {
            if (*p == '[') depth++;
            else if (*p == ']') { depth--; if (!depth) break; }
            p++;
        }
        if (p >= end || depth) { pat_step_free(s); return 0; }
        /* A predicate on any step but the LAST changes ancestor
         * verification (the filter belongs to the downward step);
         * the fast path models last-step predicates only. The
         * caller re-checks position. */
        const char* body = st + 1;
        size_t blen = (size_t)(p - body);
        while (blen && (body[0] == ' ')) { body++; blen--; }
        while (blen && body[blen-1] == ' ') blen--;
        /* Pattern predicates are positional over the node-list being
         * PROCESSED (§11.5) — position()/last() here do not mean
         * the singleton context the fast path evaluates in. Any
         * mention disqualifies the fast path for this pattern. */
        for (size_t k = 0; k + 8 <= blen; k++) {
            if (strncmp(body + k, "position(", 9) == 0 ||
                strncmp(body + k, "last(", 5) == 0) {
                pat_step_free(s);
                return 0;
            }
        }
        char* dup = (char*)malloc(blen + 1);
        if (!dup) { pat_step_free(s); return 0; }
        memcpy(dup, body, blen);
        dup[blen] = 0;
        s->pred = leptris_xpath_compile(dup);
        free(dup);
        if (!s->pred) { pat_step_free(s); return 0; }
        p++;   /* past ']' */
    }
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    *pp = p;
    return 1;
}

void xslt_pattern_compile_steps(XsltPattern* p, const char* src) {
    if (!p || !src) return;
    const char* q = src;
    while (*q == ' ') q++;
    if (!*q) return;
    p->steps_absolute = (*q == '/');
    if (p->steps_absolute) q++;
    /* "//" never compiles to the ladder. */
    if (*q == '/') return;

    XsltPatStep steps_buf[32];
    int n = 0;
    const char* end = src + strlen(src);
    while (q < end) {
        if (n >= 32) goto fail;
        int last = (n > 0);
        (void)last;
        if (!parse_pat_step(&q, end, &steps_buf[n])) goto fail;
        n++;
        if (q >= end) break;
        if (*q != '/') goto fail;   /* trailing junk */
        q++;
        if (q >= end) goto fail;    /* trailing '/' */
    }
    if (n == 0) return;
    /* Predicates are only modeled on the LAST step (see
     * parse_pat_step) — earlier ones disqualify. */
    for (int i = 0; i < n - 1; i++) {
        if (steps_buf[i].pred) goto fail;
    }
    p->steps = (XsltPatStep*)calloc((size_t)n, sizeof(XsltPatStep));
    if (!p->steps) goto fail;
    memcpy(p->steps, steps_buf, (size_t)n * sizeof(XsltPatStep));
    p->n_steps = n;
    p->steps_valid = 1;
    return;
fail:
    for (int i = 0; i < n; i++) pat_step_free(&steps_buf[i]);
    p->steps_valid = 0;
}

static int step_node_matches(const XsltPatStep* s, LeptrisNodeRef n) {
    if (!n) return 0;
    int ty = leptris_node_get_type(n);
    if (s->is_attr) {
        if (ty != LEPTRIS_NODE_TYPE_ATTRIBUTE) return 0;
        if (!s->name) return 1;
        LeptrisAttributeNode* a = (LeptrisAttributeNode*)n;
        return a->name && strcmp(a->name, s->name) == 0;
    }
    switch (s->kind) {
        case 0:   /* name test (name NULL = *) — elements only */
            if (ty != LEPTRIS_NODE_TYPE_ELEMENT) return 0;
            if (!s->name) return 1;
            {
                const char* nn = leptris_element_get_name((LeptrisElement)n);
                return nn && strcmp(nn, s->name) == 0;
            }
        case 1: return 1;   /* node() */
        case 2: return ty == LEPTRIS_NODE_TYPE_TEXT ||
                       ty == LEPTRIS_NODE_TYPE_CDATA;
        case 3: return ty == LEPTRIS_NODE_TYPE_COMMENT;
        case 4: return ty == LEPTRIS_NODE_TYPE_PI;
        default: return 0;
    }
}

/* Fast-path answer for one alternative: 1 match, 0 no match,
 * -1 undetermined (fall back to the general ladder). */
static int steps_fast_match(const XsltPattern* p, LeptrisElement node,
                            LeptrisDocument doc, LeptrisXPathNsSet ns) {
    if (!p->steps_valid || p->n_steps == 0) return -1;
    LeptrisNodeRef n = (LeptrisNodeRef)node;
    const XsltPatStep* last = &p->steps[p->n_steps - 1];
    if (!step_node_matches(last, n)) return 0;
    if (last->pred) {
        struct leptris_xpath_result* r =
            ns ? leptris_xpath_compiled_eval_ns(last->pred, doc, node, ns)
               : leptris_xpath_compiled_eval(last->pred, doc, node);
        if (!r) return 0;
        /* Numeric predicates are positional (§2.4) — the fast path
         * does not model them; defer to the ladder. */
        int undet = (r->type == XPATH_RESULT_NUMBER);
        int truth = 0;
        if (!undet) {
            extern int xpath_to_boolean(struct leptris_xpath_result*);
            truth = xpath_to_boolean(r);
        }
        leptris_xpath_result_free(r);
        if (undet) return -1;
        if (!truth) return 0;
    }
    for (int i = p->n_steps - 2; i >= 0; i--) {
        LeptrisNodeRef up = (LeptrisNodeRef)leptris_node_parent(n);
        if (!up && leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_ATTRIBUTE)
            up = (LeptrisNodeRef)((LeptrisAttributeNode*)n)->owner;
        if (!up) return -1;   /* detached/doc-level: ladder decides */
        if (leptris_node_get_type(up) == LEPTRIS_NODE_TYPE_DOCUMENT) {
            /* The document rung (doc-children patterns) belongs to
             * the general matcher. */
            return -1;
        }
        if (!step_node_matches(&p->steps[i], up)) return 0;
        n = up;
    }
    if (p->steps_absolute) {
        LeptrisNodeRef up = (LeptrisNodeRef)leptris_node_parent(n);
        if (!up && leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_ATTRIBUTE)
            up = (LeptrisNodeRef)((LeptrisAttributeNode*)n)->owner;
        if (!up) {
            /* The tree root's parent is the document node — resolved
             * lazily like the general ladder's final rung. */
            up = (LeptrisNodeRef)leptris_document_get_node(
                (struct leptris_document*)doc);
        }
        if (!up || leptris_node_get_type(up) != LEPTRIS_NODE_TYPE_DOCUMENT)
            return 0;
    }
    return 1;
}


/* Node identity for pattern membership. The attribute/namespace
 * axes mint FRESH synthetic nodes on every evaluation — pointer
 * equality never holds for them. Attributes are value-objects:
 * identity is (owner, name). */
static int same_node(LeptrisNodeRef a, LeptrisNodeRef b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (leptris_node_get_type(a) != LEPTRIS_NODE_TYPE_ATTRIBUTE ||
        leptris_node_get_type(b) != LEPTRIS_NODE_TYPE_ATTRIBUTE)
        return 0;
    LeptrisAttributeNode* aa = (LeptrisAttributeNode*)a;
    LeptrisAttributeNode* ab = (LeptrisAttributeNode*)b;
    return aa->owner == ab->owner &&
           aa->name && ab->name && strcmp(aa->name, ab->name) == 0;
}

static int nodeset_contains(LeptrisXPathResult r, LeptrisElement node) {
    size_t n = leptris_xpath_result_count(r);
    for (size_t i = 0; i < n; i++) {
        if (same_node(leptris_xpath_result_get_node(r, i),
                      (LeptrisNodeRef)node))
            return 1;
    }
    return 0;
}

/* Does this alternative select `node` when evaluated from `ctx`?
 * `hook` (when set) replaces the eval route wholesale — the caller
 * owns namespace AND variable resolution then. */
static int selects_from(LeptrisXPathCompiled expr, LeptrisDocument doc,
                        LeptrisElement ctx, LeptrisElement node,
                        LeptrisXPathNsSet ns,
                        XsltPatternEvalFn hook, void* ud) {
    struct leptris_xpath_result* r;
    if (hook) {
        r = hook(ud, expr, doc, ctx);
    } else {
        r = ns ? leptris_xpath_compiled_eval_ns(expr, doc, ctx, ns)
               : leptris_xpath_compiled_eval(expr, doc, ctx);
    }
    if (!r) return 0;
    int hit = nodeset_contains(r, node);
    leptris_xpath_result_free(r);
    return hit;
}

/* "/" (the root pattern) matches ONLY the document node. */
static int alt_is_root_pattern(const XsltPattern* alt) {
    const char* e = leptris_xpath_compiled_text(alt->expr);
    if (!e) return 0;
    while (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r') e++;
    if (*e != '/') return 0;
    e++;
    while (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r') e++;
    return *e == 0;
}

int xslt_pattern_matches_ex(const XsltPattern* p, LeptrisElement node,
                            LeptrisDocument doc, LeptrisXPathNsSet ns,
                            XsltPatternEvalFn hook, void* ud) {
    if (!p || !node || !doc) return 0;

    /* A pattern matches within the tree the NODE lives in. The
     * caller passes the source document; for foreign nodes —
     * document() results, RTF fragments via exsl:node-set — the
     * source's root and document rung select nothing, so templates
     * silently never matched fragment roots (bug-65). Derive the
     * node's own document when it differs. */
    {
        int nty = leptris_node_get_type((LeptrisNodeRef)node);
        if (nty != LEPTRIS_NODE_TYPE_DOCUMENT) {
            struct leptris_document* own = NULL;
            LeptrisNodeRef top = (LeptrisNodeRef)node;
            if (nty == LEPTRIS_NODE_ATTRIBUTE)
                top = (LeptrisNodeRef)((LeptrisAttributeNode*)node)->owner;
            if (top) {
                for (;;) {
                    LeptrisElement up = leptris_node_parent(top);
                    if (!up) break;
                    top = (LeptrisNodeRef)up;
                }
                if (leptris_node_get_type(top) ==
                    LEPTRIS_NODE_TYPE_DOCUMENT)
                    own = ((LeptrisDocumentNode*)top)->doc;
            }
            /* Doc-level nodes are parentless by design (#580) and
             * result-tree elements link no doc back-pointer through
             * the climb — elements resolve their owner document.
             * The climb stops at the tree ROOT element (its parent
             * is NULL, not the document node), so first compare
             * against the caller's own root — one O(1) field read
             * versus leptris_element_get_document's full climb +
             * root-map probe, which profiled as 56% of a dispatch
             * transform (one resolution per pattern evaluation). */
            if (!own && nty == LEPTRIS_NODE_ELEMENT) {
                if (top == (LeptrisNodeRef)leptris_document_root(doc))
                    own = (struct leptris_document*)doc;
                else
                    own = (struct leptris_document*)
                        leptris_element_get_document(node);
            }
            if (own && own != (struct leptris_document*)doc)
                doc = (LeptrisDocument)own;
        }
    }

    LeptrisElement root = leptris_document_root(doc);
    int node_is_doc = leptris_node_get_type((LeptrisNodeRef)node) ==
                      LEPTRIS_NODE_TYPE_DOCUMENT;

    for (const XsltPattern* alt = p; alt; alt = alt->next) {
        if (alt_is_root_pattern(alt)) {
            if (node_is_doc) return 1;
            continue;   /* "/" never matches any other node */
        }
        if (node_is_doc) continue;   /* nothing else matches the root */

        /* Root-element special case: a single bare NAME (or *)
         * matches the root element by name — retained for documents
         * without a document node in play. */
        if (node == root && alt->expr_name_only) {
            const char* n = leptris_element_get_name(node);
            if ((alt->expr_name[0] == '*' && alt->expr_name[1] == 0) ||
                (n && strcmp(n, alt->expr_name) == 0)) {
                return 1;
            }
        }

        /* Compiled ladder first: one predicate eval plus a parent
         * walk, no downward scans. -1 = shape it does not model
         * (or an undetermined edge) — the general ladder decides. */
        int fm = steps_fast_match(alt, node, doc, ns);
        if (fm == 1) return 1;
        if (fm == 0) continue;

        /* Ancestor ladder: each ancestor as the evaluation context,
         * with the DOCUMENT node as the final rung so child-axis
         * patterns (node(), NAME, *) reach the root element. */
        for (LeptrisElement ctx = node; ctx; ) {
            if (selects_from(alt->expr, doc, ctx, node, ns, hook, ud))
                return 1;
            LeptrisElement up = leptris_node_parent((LeptrisNodeRef)ctx);
            if (!up &&
                leptris_node_get_type((LeptrisNodeRef)ctx) ==
                    LEPTRIS_NODE_TYPE_ATTRIBUTE) {
                /* Attributes hang off their owner, not the child
                 * chain — @-pattern rungs evaluate from the owner
                 * (this hop fires at the node's own rung too: an
                 * attribute's parent IS its owner). */
                up = ((LeptrisAttributeNode*)ctx)->owner;
            }
            if (!up) {
                LeptrisElement dnode = (LeptrisElement)
                    leptris_document_get_node(
                        (struct leptris_document*)doc);
                if (ctx != root) {
                    /* Detached subtree — or a document-level node
                     * (prolog/epilog comment/PI, parentless by
                     * design since #580). Offer the document rung:
                     * child-axis patterns (node(),
                     * processing-instruction()) must still match
                     * these from the document's child axis. */
                    if (dnode && dnode != (LeptrisElement)node &&
                        selects_from(alt->expr, doc, dnode, node,
                                     ns, hook, ud))
                        return 1;
                    break;
                }
                /* root's parent = the document node (final rung) */
                if (!dnode || dnode == (LeptrisElement)node) break;
                if (selects_from(alt->expr, doc, dnode, node,
                                 ns, hook, ud))
                    return 1;
                break;
            }
            ctx = up;
        }
    }
    return 0;
}

int xslt_pattern_matches(const XsltPattern* p, LeptrisElement node,
                         LeptrisDocument doc, LeptrisXPathNsSet ns) {
    return xslt_pattern_matches_ex(p, node, doc, ns, NULL, NULL);
}
