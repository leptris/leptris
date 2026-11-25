/* xsd/content_model.c — #1075 slice 3: content-model evaluation.
 *
 * The compiled model tree lowers into a Thompson NFA (epsilon +
 * symbol transitions, occurrence bounds via counting copies and a
 * loop for unbounded) and the child sequence is consumed by
 * simulating the live state set — linear in children x states, no
 * backtracking blowup on nested choices.
 *
 * Namespace filters (xs:any/@namespace): "##any" (default),
 * "##other" (not the target namespace, not unqualified),
 * "##targetNamespace", "##local", or a space-separated URI list.
 */
#include <stdlib.h>
#include <string.h>

#include "xsd_internal.h"

/* validate.c-facing global lookup (compile.c): an unqualified
 * particle may leniently admit a namespaced child only when no
 * global declaration in the child's namespace could type it
 * (elemZ020 vs the OOXML-style wp.xsd corpus). */
extern const struct xsd_element_decl* xsd_find_element_child_ns(
    struct leptris_xsd_schema* s, const char* name, const char* uri);

typedef struct {
    int from;
    int symbol;      /* -1 = epsilon; else a particle transition */
    XsdCm* particle; /* symbol owner (element ref or wildcard) */
    int to;
} CmTrans;

typedef struct {
    CmTrans* trans;
    size_t count, cap;
    int state_count; /* explicit: state ids are NOT transition ids */
    int start, accept;
    struct leptris_xsd_schema* s; /* group-ref resolution */
    int depth;                    /* cyclic-group guard */
    int overflow; /* #1894: crossed XSD_CM_NFA_MAX — abort the build */
    int no_oc;    /* #2151: particle-only build (open-content loops
                     suppressed — the greedy-attribution trace) */
} CmNfa;

/* leptris#1894: a counted particle multiplies the expanded NFA (finite
 * maxOccurs copies the subtree per copy), so nested large bounds grow
 * the automaton exponentially and a huge finite bound walks the copy
 * loop until the process dies (jetsam/OOM, no diagnostic). Crossing
 * this bound aborts the build; xsd_content_valid reports -2 and the
 * callers surface a loud structured row. Generous: legitimate suite
 * content models stay orders of magnitude below it. */
#define XSD_CM_NFA_MAX 1000000

static int nfa_state(CmNfa* n) {
    if (n->overflow) return 0;
    if (n->state_count >= XSD_CM_NFA_MAX) {
        n->overflow = 1;
        return 0;
    }
    return n->state_count++;
}

static void nfa_trans(CmNfa* n, int from, int symbol, XsdCm* particle,
                      int to) {
    if (n->overflow) return;
    if (n->count >= XSD_CM_NFA_MAX) {
        n->overflow = 1;
        return;
    }
    if (n->count == n->cap) {
        size_t cap = n->cap ? n->cap * 2 : 16;
        CmTrans* grown = (CmTrans*)realloc(n->trans, cap * sizeof(*grown));
        if (!grown) return; /* OOM: walk treats the NFA as non-matching */
        n->trans = grown;
        n->cap = cap;
    }
    n->trans[n->count].from = from;
    n->trans[n->count].symbol = symbol;
    n->trans[n->count].particle = particle;
    n->trans[n->count].to = to;
    n->count++;
}

static void cm_build(CmNfa* n, XsdCm* node, int from, int to);
static void cm_build_occurrence(CmNfa* n, XsdCm* node,
                               int from, int to);

/* chain the group children: first reads `from`, last writes `to`.
 * xs:all is evaluated in sequence order for now (its order-free
 * semantics arrive with slice 4's all-semantics pass). */
/* XSD 1.1 open content: a wildcard self-loop at a junction lets
 * any-namespace elements (per the wildcard's grammar) consume a
 * child without advancing the model. */
static void oc_loop(CmNfa* n, XsdCm* node, int state) {
    if (!node->open_any || !node->oc_mode || n->no_oc) return;
    nfa_trans(n, state, 0, node->open_any, state);
}

static void cm_build_children(CmNfa* n, XsdCm* node, int from, int to) {
    XsdCm* c = node->first_child;
    if (!c) {
        /* an empty model still carries open content (§3.4.2.3.3
         * cl. 6.2 synthesizes the empty sequence particle and the
         * wildcard — open010's suffix self-loops must survive) */
        if (node->open_any && node->oc_mode == 2)
            oc_loop(n, node, from);
        nfa_trans(n, from, -1, NULL, to);
        if (node->open_any && node->oc_mode)
            oc_loop(n, node, to);
        return;
    }
    int interleave =
        (node->open_any && node->oc_mode == 2) ? 1 : 0;
    int cursor = from;
    if (interleave) oc_loop(n, node, cursor);
    while (c->next) {
        int mid = nfa_state(n);
        cm_build_occurrence(n, c, cursor, mid);
        cursor = mid;
        if (interleave) oc_loop(n, node, cursor);
        c = c->next;
    }
    cm_build_occurrence(n, c, cursor, to);
    /* suffix (and interleave) wildcards may follow the sequence */
    if (node->open_any && node->oc_mode)
        oc_loop(n, node, to);
}

static void cm_build(CmNfa* n, XsdCm* node, int from, int to) {
    switch (node->kind) {
        case XSD_CM_ELEMENT:
        case XSD_CM_ANY:
            nfa_trans(n, from, 0, node, to);
            return;
        case XSD_CM_SEQ:
        case XSD_CM_ALL:
            cm_build_children(n, node, from, to);
            return;
        case XSD_CM_GROUP_REF: {
            XsdGroupDef* g =
                n->s ? xsd_find_group(n->s, node->name) : NULL;
            if (!g || !g->model || n->depth > 64) {
                /* unresolved (or cyclic) ref: splice nothing */
                nfa_trans(n, from, -1, NULL, to);
                return;
            }
            n->depth++;
            cm_build(n, g->model, from, to);
            n->depth--;
            return;
        }
        case XSD_CM_CHOICE: {
            XsdCm* c = node->first_child;
            if (!c) {
                if (node->open_any && node->oc_mode == 2)
                    oc_loop(n, node, from);
                nfa_trans(n, from, -1, NULL, to);
                if (node->open_any && node->oc_mode)
                    oc_loop(n, node, to);
                return;
            }
            /* open content rides a choice's junctions: interleave
             * absorbs before and after any branch (ibm S3_4_1 v02's
             * choice models), suffix only trails */
            if (node->open_any && node->oc_mode == 2)
                oc_loop(n, node, from);
            for (; c; c = c->next) cm_build_occurrence(n, c, from, to);
            if (node->open_any && node->oc_mode)
                oc_loop(n, node, to);
            return;
        }
    }
}

/* occurrence bounds: min copies, then (max-min) optionals, or an
 * epsilon self-loop region for unbounded. */
static void cm_build_occurrence(CmNfa* n, XsdCm* node, int from, int to) {
    if (n->overflow) return;
    for (int i = 0; i < node->min; i++) {
        if (n->overflow) return;
        int mid = nfa_state(n);
        cm_build(n, node, from, mid);
        from = mid;
    }
    if (node->max < 0) {
        int loop = nfa_state(n);
        nfa_trans(n, from, -1, NULL, loop);
        cm_build(n, node, loop, loop);
        nfa_trans(n, loop, -1, NULL, to);
        return;
    }
    for (int i = node->min; i < node->max; i++) {
        if (n->overflow) return;
        nfa_trans(n, from, -1, NULL, to);
        int mid = nfa_state(n);
        cm_build(n, node, from, mid);
        from = mid;
    }
    nfa_trans(n, from, -1, NULL, to);
}

/* ---- #1894 analytic product bound (pre-build rejection) ----------- */

/* Saturating upper bound on the states+transitions one subtree
 * expands, honoring the same occurrence lowering cm_build_occurrence
 * performs: finite max copies the subtree max times; unbounded lowers
 * to ONE loop region (min + 1 copies). Groups follow like cm_build
 * (depth-capped; unresolved/cyclic refs count as 1 — the expansion
 * cap remains the backstop for anything the walk cannot see). Returns
 * nonzero when the bound crosses XSD_CM_NFA_MAX. */
static int cm_bound_over(struct leptris_xsd_schema* s,
                         const XsdCm* node, size_t* bound, int depth) {
    if (!node || depth > 64) return 0;
    size_t sub;
    switch (node->kind) {
        case XSD_CM_ELEMENT:
        case XSD_CM_ANY:
            sub = 1;
            break;
        case XSD_CM_GROUP_REF: {
            XsdGroupDef* g = s ? xsd_find_group(s, node->name) : NULL;
            if (!g || !g->model ||
                cm_bound_over(s, g->model, &sub, depth + 1))
                sub = 1;
            break;
        }
        case XSD_CM_SEQ:
        case XSD_CM_ALL:
        case XSD_CM_CHOICE: {
            sub = 0;
            for (const XsdCm* c = node->first_child; c; c = c->next) {
                size_t cb = 0;
                if (cm_bound_over(s, c, &cb, depth + 1)) return 1;
                if (sub > XSD_CM_NFA_MAX) return 1;
                sub += cb;
            }
            break;
        }
        default:
            sub = 1;
            break;
    }
    /* occurrence multiplier: copies of the subtree */
    size_t copies;
    if (node->max < 0)
        copies = (size_t)(node->min > 0 ? node->min : 0) + 1;
    else
        copies = (size_t)(node->max > 0 ? node->max : 0);
    if (copies == 0) {
        *bound = 0;
        return 0;
    }
    if (sub != 0 && copies > XSD_CM_NFA_MAX / sub) return 1;
    *bound = sub * copies + 1;
    return *bound > XSD_CM_NFA_MAX;
}

/* ---- namespace filter (xs:any/@namespace) ------------------------ */

/* XSD 1.1 notQName hit test (moxml#460): ##defined matches a name
 * the schema declares globally; prefix-less literals match
 * no-namespace names; prefixed QNames are not resolved here. */
static int cm_not_qname_hits(const char* tokens,
                             struct leptris_xsd_schema* s,
                             const char* local, const char* uri) {
    const char* p = tokens;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
               *p != '\r')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (len == 9 && strncmp(start, "##defined", 9) == 0) {
            if (xsd_element_name_defined(s, local, uri)) return 1;
            continue;
        }
        /* #2151: a PREFIX-RESOLVED token ("{uri}local") — the open
         * content capture resolved the prefix against the declaring
         * element's in-scope bindings (s3_4_1ii04). */
        if (start[0] == '{') {
            const char* close = memchr(start, '}', len);
            if (close) {
                size_t ulen = (size_t)(close - start - 1);
                size_t llen = len - ulen - 2;
                if (uri && strlen(uri) == ulen &&
                    strncmp(start + 1, uri, ulen) == 0 &&
                    strlen(local) == llen &&
                    strncmp(close + 1, local, llen) == 0)
                    return 1;
            }
            continue;
        }
        if (memchr(start, ':', len)) continue; /* prefixed: deferred */
        if (uri == NULL && strlen(local) == len &&
            strncmp(local, start, len) == 0)
            return 1;
    }
    return 0;
}

static int any_ns_matches(XsdCm* any, const char* uri,
                          const char* target_ns) {
    const char* f = any->any_ns ? any->any_ns : "##any";
    if (strcmp(f, "##any") == 0) return 1;
    if (strcmp(f, "##local") == 0) return uri == NULL;
    if (strcmp(f, "##targetNamespace") == 0)
        return target_ns && uri && strcmp(uri, target_ns) == 0;
    if (strcmp(f, "##other") == 0)
        return uri != NULL &&
               !(target_ns && strcmp(uri, target_ns) == 0);
    /* whitespace-separated URI list (+##local): the attribute value
     * may span lines (pretty-printed schemas indent the continuation),
     * so token separators are all XML whitespace, not just spaces. */
    const char* p = f;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (strncmp(start, "##local", len) == 0 && len == 7) {
            if (uri == NULL) return 1;
            continue;
        }
        if (strncmp(start, "##targetNamespace", len) == 0 && len == 17) {
            if (target_ns && uri && strcmp(uri, target_ns) == 0) return 1;
            continue;
        }
        if (strncmp(start, "##other", len) == 0 && len == 7) {
            if (uri != NULL &&
                !(target_ns && strcmp(uri, target_ns) == 0))
                return 1;
            continue;
        }
        if (uri && strlen(uri) == len && strncmp(start, uri, len) == 0)
            return 1;
    }
    return 0;
}

/* #2151: the @notNamespace exclusion (open047) — a hit excludes the
 * name regardless of the positive filter. ##targetNamespace resolves
 * against the wildcard's declaring unit tns. */
static int cm_not_ns_hits(XsdCm* any, const char* uri,
                          const char* target_ns) {
    if (!any->not_ns) return 0;
    const char* p = any->not_ns;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (strncmp(start, "##local", len) == 0 && len == 7) {
            if (uri == NULL) return 1;
            continue;
        }
        if (strncmp(start, "##targetNamespace", len) == 0 && len == 17) {
            if (target_ns && uri && strcmp(uri, target_ns) == 0)
                return 1;
            continue;
        }
        if (uri && strlen(uri) == len && strncmp(start, uri, len) == 0)
            return 1;
    }
    return 0;
}

/* The open-content wildcard's admission for one child (positive
 * filter + notNamespace + notQName). */
static int oc_wildcard_admits(struct leptris_xsd_schema* s, XsdCm* w,
                              const char* uri, const char* names_c,
                              const char* tns) {
    if (!any_ns_matches(w, uri, w->any_unit_tns ? w->any_unit_tns
                                                : tns))
        return 0;
    if (cm_not_ns_hits(w, uri,
                       w->any_unit_tns ? w->any_unit_tns : tns))
        return 0;
    if (w->not_qname &&
        cm_not_qname_hits(w->not_qname, s, names_c, uri))
        return 0;
    return 1;
}

/* ---- simulation --------------------------------------------------- */

typedef struct {
    int* states;
    size_t count, cap;
} StateSet;

static void ss_add(StateSet* ss, int st) {
    for (size_t i = 0; i < ss->count; i++)
        if (ss->states[i] == st) return;
    if (ss->count == ss->cap) {
        size_t cap = ss->cap ? ss->cap * 2 : 16;
        int* grown = (int*)realloc(ss->states, cap * sizeof(int));
        if (!grown) return;
        ss->states = grown;
        ss->cap = cap;
    }
    ss->states[ss->count++] = st;
}

static void ss_close(StateSet* ss, CmNfa* n) {
    /* fixpoint over epsilon edges */
    size_t i = 0;
    while (i < ss->count) {
        int st = ss->states[i];
        for (size_t t = 0; t < n->count; t++)
            if (n->trans[t].from == st && n->trans[t].symbol == -1)
                ss_add(ss, n->trans[t].to);
        i++;
    }
}

/* XSD xs:all at the model root: order-free, each particle at
 * most maxOccurs times, minOccurs floors — a counting check over
 * the particle list (XSD 1.0 restricts xs:all to the model root,
 * each child a particle, maxOccurs <= 1). */
static int all_root_valid(XsdCm* all, const char* target_ns,
                          const char* const* names,
                          const char* const* ns_uris, size_t count) {
    /* the whole all is ABSENT (moxml#466, mgAa003/mgD013): with
     * minOccurs="0" an empty instance validates even when the
     * particles carry min=1 — their bounds apply only when the all
     * is present */
    if (count == 0 && all->min == 0) return 1;
    for (XsdCm* p = all->first_child; p; p = p->next) {
        size_t hits = 0;
        for (size_t i = 0; i < count; i++) {
            const char* uri = ns_uris ? ns_uris[i] : NULL;
            int child_qualified = uri && uri[0];
            int ns_ok = p->ns ? (!child_qualified ||
                                 strcmp(uri, p->ns) == 0)
                              : !child_qualified;
            if (p->name && strcmp(p->name, names[i]) == 0 && ns_ok)
                hits++;
        }
        if ((long)hits < p->min) return 0;
        if (p->max >= 0 && (long)hits > p->max) return 0;
    }
    /* every instance child must belong to a particle */
    for (size_t i = 0; i < count; i++) {
        int matched = 0;
        const char* uri = ns_uris ? ns_uris[i] : NULL;
        for (XsdCm* p = all->first_child; p && !matched; p = p->next) {
            int child_qualified2 = uri && uri[0];
            int ns_ok2 = p->ns ? (!child_qualified2 ||
                                  strcmp(uri, p->ns) == 0)
                               : !child_qualified2;
            if (p->name && strcmp(p->name, names[i]) == 0 && ns_ok2)
                matched = 1;
        }
        if (!matched) return 0;
    }
    (void)target_ns;
    return 1;
}

/* ---- xs:all with open content (XSD 1.1 §3.4.4.3) ------------------ */

/* Suffix over an all: S = S1 + S2 where S1 is valid against the all
 * and every S2 element matches the wildcard (open009 — the boundary
 * is a prefix split). */
static int all_oc_suffix(XsdCm* all, XsdCm* w,
                         struct leptris_xsd_schema* s,
                         const char* target_ns,
                         const char* const* names,
                         const char* const* ns_uris, size_t count) {
    /* the all is order-free, but S1 must be a PREFIX: try every
     * split (instances are small; cap the quadratic walk) */
    if (count > 512) return 0;
    for (size_t k = 0; k <= count; k++) {
        if (!all_root_valid(all, target_ns, names, ns_uris, k))
            continue;
        int ok = 1;
        for (size_t i = k; i < count && ok; i++) {
            const char* uri = ns_uris ? ns_uris[i] : NULL;
            if (!oc_wildcard_admits(s, w, uri, names[i], target_ns))
                ok = 0;
        }
        if (ok) return 1;
    }
    return 0;
}

/* Interleave over an all: S ∈ S1 × S2 — some subset goes to the
 * wildcard and the REST (order-free) satisfies the all (open008).
 * Subset enumeration with a size cap. */
static int all_oc_interleave(XsdCm* all, XsdCm* w,
                             struct leptris_xsd_schema* s,
                             const char* target_ns,
                             const char* const* names,
                             const char* const* ns_uris, size_t count) {
    if (count > 24) return 0;
    /* taken bit i: element i goes to the wildcard */
    unsigned long long mask = 0;
    unsigned long long limit = 1ULL << count;
    for (; mask < limit; mask++) {
        const char* rn[24];
        const char* ru[24];
        size_t m = 0;
        int ok = 1;
        for (size_t i = 0; i < count && ok; i++) {
            const char* uri = ns_uris ? ns_uris[i] : NULL;
            if ((mask >> i) & 1ULL) {
                if (!oc_wildcard_admits(s, w, uri, names[i],
                                        target_ns))
                    ok = 0;
            } else {
                rn[m] = names[i];
                ru[m] = uri;
                m++;
            }
        }
        if (!ok || m > 24) continue;
        if (all_root_valid(all, target_ns, rn, ru, m)) return 1;
    }
    return 0;
}

int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                      const char* target_ns, const char* const* names,
                      const char* const* ns_uris, size_t count,
                      int* oc_from) {
    if (!model) return -1;
    if (model->kind == XSD_CM_ALL && model->first_child) {
        /* #2151: an open content over an xs:all model — the all's
         * counting walk takes the wildcards into account */
        if (model->open_any && model->oc_mode) {
            return model->oc_mode == 2
                       ? all_oc_interleave(model, model->open_any, s,
                                           target_ns, names, ns_uris,
                                           count)
                       : all_oc_suffix(model, model->open_any, s,
                                       target_ns, names, ns_uris,
                                       count);
        }
        return all_root_valid(model, target_ns, names, ns_uris, count);
    }
    CmNfa n;
    memset(&n, 0, sizeof(n));
    n.s = s;
    n.start = nfa_state(&n);
    n.accept = nfa_state(&n);
    /* #1894: bound the expansion analytically BEFORE building — a
     * pathological occurrence product (nested finite maxOccurs)
     * rejects in microseconds instead of growing the automaton until
     * the process dies. */
    {
        size_t bound = 0;
        if (cm_bound_over(s, model, &bound, 0)) return -2;
    }
    cm_build_occurrence(&n, model, n.start, n.accept);
    if (n.overflow) {
        free(n.trans);
        return -2;
    }
    /* XSD 1.1 interleave open content (§3.4.4.3 cl. 3): S is an
     * interleave of the model-valid subsequence and the wildcard
     * span — after an absorbed span the model must RESUME wherever
     * it was, so every state with an outgoing symbol transition
     * carries a wildcard self-loop (the junction loops in
     * cm_build_children alone cannot re-enter a half-consumed
     * particle group: a/x1/b against (a+, b?) must accept). */
    if (model->open_any && model->oc_mode == 2 && !n.overflow) {
        size_t base = n.count;
        for (size_t i = 0; i < base; i++)
            if (n.trans[i].symbol >= 0)
                oc_loop(&n, model, n.trans[i].from);
        if (n.overflow) {
            free(n.trans);
            return -2;
        }
    }

    /* #2151: the greedy-attribution trace (§3.4.4.3 cl. 2.3/3.3).
     * Children before `oc_from` are attributed to the model's
     * particles; children at/after it may be attributed to the open
     * content — a particle-only re-run (oc loops suppressed) finds
     * where the model stops matching: the first index its live set
     * dies. -1 = the model alone accepts everything (no divergence). */
        if (oc_from) {
        *oc_from = -1;
        if (model->open_any && model->oc_mode) {
            CmNfa p;
            memset(&p, 0, sizeof(p));
            p.s = s;
            p.no_oc = 1;
            p.start = nfa_state(&p);
            p.accept = nfa_state(&p);
            {
                size_t bound = 0;
                if (cm_bound_over(s, model, &bound, 0)) {
                    free(n.trans);
                    return -2;
                }
            }
            cm_build_occurrence(&p, model, p.start, p.accept);
            if (p.overflow) {
                free(n.trans);
                free(p.trans);
                return -2;
            }
            StateSet plive;
            memset(&plive, 0, sizeof(plive));
            ss_add(&plive, p.start);
            ss_close(&plive, &p);
            int died = -1;
            for (size_t c = 0; c < count && died < 0; c++) {
                StateSet next;
                memset(&next, 0, sizeof(next));
                const char* uri = ns_uris ? ns_uris[c] : NULL;
                for (size_t i = 0; i < plive.count; i++) {
                    for (size_t t = 0; t < p.count; t++) {
                        CmTrans* tr = &p.trans[t];
                        if (tr->from != plive.states[i] ||
                            tr->symbol == -1)
                            continue;
                        XsdCm* q = tr->particle;
                        int hit = 0;
                        if (q->kind == XSD_CM_ELEMENT) {
                            const char* pns = q->ns;
                            int named =
                                (strcmp(q->name, names[c]) == 0);
                            if (!named && s) {
                                const XsdElementDecl* m =
                                    xsd_substitute_member(
                                        s, names[c], q->name);
                                if (m) {
                                    named = 1;
                                    if (m->ns) pns = m->ns;
                                }
                            }
                            int child_ns = uri && uri[0];
                            /* #2151: mirror the main run's gate —
                             * the lenient no-global fallback narrows
                             * when the model carries open content */
                            int oc_open =
                                model->open_any && model->oc_mode;
                            int pns_ok =
                                pns ? (!child_ns ||
                                       (uri && strcmp(uri, pns) == 0))
                                    : (!child_ns ||
                                       (s && !oc_open &&
                                        !xsd_find_element_child_ns(
                                            s, names[c], uri)));
                            hit = named && pns_ok;
                        } else if (q->kind == XSD_CM_ANY) {
                            if (any_ns_matches(q, uri,
                                               q->any_unit_tns
                                                   ? q->any_unit_tns
                                                   : target_ns) &&
                                !cm_not_ns_hits(q, uri,
                                                q->any_unit_tns
                                                    ? q->any_unit_tns
                                                    : target_ns) &&
                                !(q->not_qname &&
                                  cm_not_qname_hits(q->not_qname, s,
                                                    names[c], uri)))
                                hit = 1;
                        }
                        if (hit) ss_add(&next, tr->to);
                    }
                }
                free(plive.states);
                plive = next;
                ss_close(&plive, &p);
                if (plive.count == 0) died = (int)c;
            }
            if (died < 0) {
                int paccept = 0;
                for (size_t i = 0; i < plive.count; i++)
                    if (plive.states[i] == p.accept) paccept = 1;
                if (!paccept) died = (int)count;
            }
            free(plive.states);
            free(p.trans);
            if (died >= 0) *oc_from = died;
        }
    }

    StateSet live;
    memset(&live, 0, sizeof(live));
    ss_add(&live, n.start);
    ss_close(&live, &n);

    for (size_t c = 0; c < count; c++) {
        StateSet next;
        memset(&next, 0, sizeof(next));
        const char* uri = ns_uris ? ns_uris[c] : NULL;
        for (size_t i = 0; i < live.count; i++) {
            for (size_t t = 0; t < n.count; t++) {
                CmTrans* tr = &n.trans[t];
                if (tr->from != live.states[i] || tr->symbol == -1)
                    continue;
                XsdCm* p = tr->particle;
                if (p->kind == XSD_CM_ELEMENT) {
                    /* elemZ020: the ns gate is asymmetric — a
                     * qualified particle matches its own ns but
                     * leniently admits a no-ns child (the pinned
                     * #1871 corpus posture); an unqualified
                     * particle (ns NULL) is strict: no-ns children
                     * only. A substitute hit rides the MEMBER's
                     * own ns (cross-ns groups, #1881 sets). */
                    const char* pns = p->ns;
                    int named = (strcmp(p->name, names[c]) == 0);
                    if (!named && n.s) {
                        const XsdElementDecl* m =
                            xsd_substitute_member(n.s, names[c],
                                                  p->name);
                        if (m) {
                            named = 1;
                            if (m->ns) pns = m->ns;
                        }
                    }
                    int child_ns = uri && uri[0];
                    /* #2151: the lenient no-global fallback narrows
                     * when the model carries an open content
                     * wildcard — the wildcard's namespace set is the
                     * openness boundary, and the particle's leniency
                     * would silently widen past it (open047.n1: the
                     * unqualified <b> must not swallow the
                     * disallowed.com <b> the union excludes). */
                    int oc_open = model->open_any && model->oc_mode;
                    int pns_ok =
                        pns ? (!child_ns ||
                               (uri && strcmp(uri, pns) == 0))
                            : (!child_ns ||
                               (n.s && !oc_open &&
                                !xsd_find_element_child_ns(
                                    n.s, names[c], uri)));
                    if (named && pns_ok) ss_add(&next, tr->to);
                } else if (p->kind == XSD_CM_ANY) {
                    /* #1735: ##targetNamespace/##other are relative
                     * to the DECLARING unit's tns */
                    if (!any_ns_matches(p, uri,
                                        p->any_unit_tns
                                            ? p->any_unit_tns
                                            : target_ns))
                        continue;
                    /* #2151: the @notNamespace exclusion */
                    if (cm_not_ns_hits(p, uri,
                                       p->any_unit_tns
                                           ? p->any_unit_tns
                                           : target_ns))
                        continue;
                    /* XSD 1.1 notQName (moxml#460): a hit on the
                     * negative list excludes the name regardless of
                     * the positive filter. ##defined resolves against
                     * the schema's global element declarations;
                     * prefix-less literals compare no-namespace
                     * names; prefixed QNames are deferred (they need
                     * instance-side prefix resolution). */
                    if (p->not_qname &&
                        cm_not_qname_hits(p->not_qname, n.s,
                                          names[c], uri))
                        continue;
                    ss_add(&next, tr->to);
                }
            }
        }
        free(live.states);
        live = next;
        ss_close(&live, &n);
        if (live.count == 0) {
            free(n.trans);
            return 0;
        }
    }

    int accept = 0;
    for (size_t i = 0; i < live.count; i++)
        if (live.states[i] == n.accept) accept = 1;
    free(live.states);
    free(n.trans);
    return accept;
}
