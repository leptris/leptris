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
    if (!node->open_any || !node->oc_mode) return;
    nfa_trans(n, state, 0, node->open_any, state);
}

static void cm_build_children(CmNfa* n, XsdCm* node, int from, int to) {
    XsdCm* c = node->first_child;
    if (!c) {
        nfa_trans(n, from, -1, NULL, to);
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
                nfa_trans(n, from, -1, NULL, to);
                return;
            }
            for (; c; c = c->next) cm_build_occurrence(n, c, from, to);
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
            if (p->name && strcmp(p->name, names[i]) == 0 &&
                (!p->ns || (uri && strcmp(uri, p->ns) == 0)))
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
            if (p->name && strcmp(p->name, names[i]) == 0 &&
                (!p->ns || (uri && strcmp(uri, p->ns) == 0)))
                matched = 1;
        }
        if (!matched) return 0;
    }
    (void)target_ns;
    return 1;
}

int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                      const char* target_ns, const char* const* names,
                      const char* const* ns_uris, size_t count) {
    if (!model) return -1;
    if (model->kind == XSD_CM_ALL && model->first_child)
        return all_root_valid(model, target_ns, names, ns_uris, count);
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
                    int named = (strcmp(p->name, names[c]) == 0) ||
                                (n.s && xsd_is_substitute(
                                            n.s, names[c], p->name));
                    if (named &&
                        (!p->ns || (uri && strcmp(uri, p->ns) == 0)))
                        ss_add(&next, tr->to);
                } else if (p->kind == XSD_CM_ANY) {
                    /* #1735: ##targetNamespace/##other are relative
                     * to the DECLARING unit's tns */
                    if (!any_ns_matches(p, uri,
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
