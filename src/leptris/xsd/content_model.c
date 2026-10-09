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
} CmNfa;

static int nfa_state(CmNfa* n) { return n->state_count++; }

static void nfa_trans(CmNfa* n, int from, int symbol, XsdCm* particle,
                      int to) {
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
    for (int i = 0; i < node->min; i++) {
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
        nfa_trans(n, from, -1, NULL, to);
        int mid = nfa_state(n);
        cm_build(n, node, from, mid);
        from = mid;
    }
    nfa_trans(n, from, -1, NULL, to);
}

/* ---- namespace filter (xs:any/@namespace) ------------------------ */

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
    /* space-separated URI list (+##local) */
    const char* p = f;
    while (*p) {
        while (*p == ' ') p++;
        const char* start = p;
        while (*p && *p != ' ') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) break;
        if (strncmp(start, "##local", len) == 0 && len == 7) {
            if (uri == NULL) return 1;
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

int xsd_content_valid(struct leptris_xsd_schema* s, XsdCm* model,
                      const char* target_ns, const char* const* names,
                      const char* const* ns_uris, size_t count) {
    if (!model) return -1;
    CmNfa n;
    memset(&n, 0, sizeof(n));
    n.s = s;
    n.start = nfa_state(&n);
    n.accept = nfa_state(&n);
    cm_build_occurrence(&n, model, n.start, n.accept);

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
                    if (any_ns_matches(p, uri, target_ns))
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
