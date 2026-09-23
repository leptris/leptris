/* descriptor/plan.c — tree-shaped schema-descriptor materialization
 * (issue #1039).
 *
 * leptris_plan_build deep-copies a POD spec into an engine-owned pool
 * (strings included — the caller's arrays may die immediately).
 * leptris_plan_walk then materializes a whole subtree against the
 * plan tree in one native pass: no per-element host calls, the
 * per-node branching the wrapper layers used to do happens here.
 *
 * The result tree is standalone: every string is copied, so it
 * outlives the document. Nodes are plain malloc'd structs freed
 * recursively by leptris_plan_result_free. */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../common/port.h"
#include "leptris.h"
#include "leptris/descriptor.h"

struct leptris_plan_result {
    LeptrisPlanValueKind kind;
    char* name;        /* wire_name copy; NULL when none */
    uint8_t type_tag;
    char* str;         /* SCALAR/RAW/CALLBACK value; NULL otherwise */
    size_t length;
    size_t position;   /* byte offset of source node (0 unknown) */
    uint8_t node_kind; /* #1273: source LEPTRIS_NODE_TYPE_* (0 none) */
    /* #1273: dense sibling rank inside the producing element (0
     * unranked). Stable across iterations regardless of byte
     * offsets. */
    uint32_t order_index;
    /* ELEMENT child values / COLLECTION items */
    struct leptris_plan_result** kids;
    size_t kid_count;
    size_t kid_cap;
    /* ELEMENT attributes (parallel arrays; small) */
    char** attr_names;
    char** attr_values;
    size_t attr_count;
    /* #1269a in-pass type execution: when `type_tag` mapped to an
     * executable type (1=int, 2=float, 3=bool) AND the parsed string
     * succeeded, `typed_ok` is 1 and the typed fields are valid.
     * The str field is ALWAYS populated (backward compatibility). */
    uint8_t typed_ok;
    int64_t typed_i;
    double typed_f;
    uint8_t typed_b;
};

/* ---- string + node helpers ------------------------------------- */

static char* dp_strdup_n(const char* s, size_t n) {
    char* p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char* dp_strdup(const char* s) {
    return s ? dp_strdup_n(s, strlen(s)) : NULL;
}

static struct leptris_plan_result* dp_value_new(LeptrisPlanValueKind k) {
    struct leptris_plan_result* v =
        (struct leptris_plan_result*)calloc(1, sizeof(*v));
    return v ? (v->kind = k, v) : NULL;
}

static int dp_value_push(struct leptris_plan_result* parent,
                         struct leptris_plan_result* kid) {
    if (parent->kid_count == parent->kid_cap) {
        size_t cap = parent->kid_cap ? parent->kid_cap * 2 : 4;
        struct leptris_plan_result** grown = (struct leptris_plan_result**)
            realloc(parent->kids, cap * sizeof(*grown));
        if (!grown) return 0;
        parent->kids = grown;
        parent->kid_cap = cap;
    }
    parent->kids[parent->kid_count++] = kid;
    return 1;
}

static int dp_value_set_str(struct leptris_plan_result* v,
                            const char* s, size_t n) {
    v->str = dp_strdup_n(s, n);
    v->length = n;
    return v->str != NULL || n == 0;
}

static void dp_result_free_rec(struct leptris_plan_result* v) {
    if (!v) return;
    for (size_t i = 0; i < v->kid_count; i++) dp_result_free_rec(v->kids[i]);
    free(v->kids);
    for (size_t i = 0; i < v->attr_count; i++) {
        free(v->attr_names[i]);
        free(v->attr_values[i]);
    }
    free(v->attr_names);
    free(v->attr_values);
    free(v->name);
    free(v->str);
    free(v);
}

/* ---- predicate + type-exec helpers (#1272, #1269a) -------------- */

/* AND across every (attr_name, expected_value) pair. Empty list
 * = match (no filter). Missing or non-equal pair = no match.
 * expected_value NULL is a degenerate predicate; rejected at
 * build time? — accepted here as no-match for safety. */
static int dp_predicates_satisfied(LeptrisElement elem,
                                   const leptris_attr_predicate* preds,
                                   uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        const char* want = preds[i].expected_value;
        const char* attr_name = preds[i].wire_name;
        if (!want || !attr_name) return 0;
        const char* actual = leptris_element_attribute(elem, attr_name);
        if (!actual || strcmp(actual, want) != 0) return 0;
    }
    return 1;
}

/* Stamp node identity (node_kind, byte position) onto a freshly
 * built SCALAR / RAW / CALLBACK / NESTED value. Unified so every
 * emission path tags the value identically (#1273). */
static void dp_value_stamp(struct leptris_plan_result* v,
                           LeptrisNodeRef src_node) {
    if (!v || !src_node) return;
    v->position = leptris_node_byte_offset(src_node);
    v->node_kind = (uint8_t)leptris_node_get_type(src_node);
}

/* #1269a in-pass type execution. Maps the documented tags:
 *   0 = string (default, no typed payload filled)
 *   1 = integer (signed decimal; overflow leaves typed_ok = 0)
 *   2 = float (XSD lexical; INF/NaN pass through)
 *   3 = boolean ("true"/"1" → 1, "false"/"0" → 0, case-insensitive)
 * The string form is ALWAYS retained in `str` so legacy hosts that
 * only read strings keep working. */
static void dp_execute_type(struct leptris_plan_result* v,
                             uint8_t type_tag) {
    v->typed_ok = 0;
    if (!v->str) return;
    switch (type_tag) {
        case 0: /* string — pass-through */
            break;
        case 1: {
            /* Integer: scan signed decimal; use strtoll for ranges
             * but reject partial parse (trailing garbage). */
            const char* s = v->str;
            while (*s == ' ' || *s == '\t') s++;
            if (*s == '\0') return;
            char* end = NULL;
            errno = 0;
            long long vll = strtoll(s, &end, 10);
            if (errno == ERANGE || end == s || (end && *end != '\0'))
                return;
            v->typed_i = (int64_t)vll;
            v->typed_ok = 1;
            break;
        }
        case 2: {
            /* Float: strtod, INF/NaN allowed. */
            const char* s = v->str;
            char* end = NULL;
            errno = 0;
            double d = strtod(s, &end);
            if (errno == ERANGE || end == s || (end && *end != '\0'))
                return;
            v->typed_f = d;
            v->typed_ok = 1;
            break;
        }
        case 3: {
            /* Boolean: tolerant lexical match. */
            const char* s = v->str;
            while (*s == ' ' || *s == '\t') s++;
            int n = 0;
            if (strncasecmp(s, "true", 4) == 0) { n = 4; }
            else if (strncasecmp(s, "false", 5) == 0) { n = 5; }
            else if (*s == '1') { n = 1; }
            else if (*s == '0') { n = 1; }
            else return;
            /* trailing must be whitespace or end-of-string */
            while (s[n] == ' ' || s[n] == '\t') n++;
            if (s[n] != '\0') return;
            v->typed_b = (strncasecmp(s, "false", 5) != 0 &&
                          *s != '0');
            v->typed_ok = 1;
            break;
        }
        default:
            break;
    }
}

/* ---- compiled plan pool ----------------------------------------- */

/* Internal mirror of the spec: everything copied, arrays flattened
 * per plan. */
typedef struct {
    /* Predicate pairs (AND across pairs). #1272: when non-empty,
     * the row matches a child only when every pair is satisfied. */
    uint16_t predicate_count;
    uint16_t pad_pred;
    leptris_attr_predicate* predicates; /* deep-copied; strings owned */
} dp_predicate_list;

/* Pre-flattened predicate storage so the walk hot path doesn't
 * walk two levels of indirection. */
typedef struct {
    const char* attr_name;
    const char* expected_value;
} dp_pred;

/* Re-pack a pointer list into a dense array at build time. */
typedef struct {
    dp_pred* items;
    size_t count;
} dp_pred_array;

typedef struct {
    char* element_name;
    uint8_t ns_form;
    char* ns_uri;
    uint32_t attribute_count;
    leptris_attr_plan* attribute_plans;  /* wire_name copied in place */
    uint32_t child_count;
    leptris_child_plan* child_plans;     /* wire_name copied in place */
    uint16_t flags;
} dp_plan;

struct leptris_plan {
    uint32_t plan_count;
    dp_plan plans[];
};

static void dp_plan_free(LeptrisPlan p) {
    if (!p) return;
    for (uint32_t i = 0; i < p->plan_count; i++) {
        dp_plan* d = &p->plans[i];
        for (uint32_t a = 0; a < d->attribute_count; a++) {
            free((char *)d->attribute_plans[a].wire_name);
            for (uint16_t pi = 0;
                 pi < d->attribute_plans[a].predicate_count; pi++) {
                free((char*)d->attribute_plans[a]
                         .predicates[pi].wire_name);
                free((char*)d->attribute_plans[a]
                         .predicates[pi].expected_value);
            }
            free((leptris_attr_predicate*)d->attribute_plans[a].predicates);
        }
        for (uint32_t c = 0; c < d->child_count; c++) {
            free((char *)d->child_plans[c].wire_name);
            for (uint16_t pi = 0;
                 pi < d->child_plans[c].predicate_count; pi++) {
                free((char*)d->child_plans[c]
                         .predicates[pi].wire_name);
                free((char*)d->child_plans[c]
                         .predicates[pi].expected_value);
            }
            free((leptris_attr_predicate*)d->child_plans[c].predicates);
        }
        free(d->attribute_plans);
        free(d->child_plans);
        free(d->element_name);
        free(d->ns_uri);
    }
    free(p);
}

LEPTRIS_API uint32_t leptris_plan_abi_version(void) {
    return LEPTRIS_PLAN_ABI_VERSION;
}


LEPTRIS_API LeptrisPlan leptris_plan_build(const leptris_plan_spec* spec,
                               LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!spec || !spec->plans || spec->plan_count == 0) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    if (spec->abi_version != LEPTRIS_PLAN_ABI_VERSION) {
        if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
        return NULL;
    }
    struct leptris_plan* p = (struct leptris_plan*)calloc(
        1, sizeof(*p) + spec->plan_count * sizeof(dp_plan));
    if (!p) goto oom;
    p->plan_count = spec->plan_count;

    for (uint32_t i = 0; i < spec->plan_count; i++) {
        const leptris_element_plan* s = &spec->plans[i];
        dp_plan* d = &p->plans[i];
        if (s->element_name) {
            d->element_name = dp_strdup(s->element_name);
            if (!d->element_name) goto oom;
        }
        d->ns_form = s->ns_form;
        if (s->ns_uri) {
            d->ns_uri = dp_strdup(s->ns_uri);
            if (!d->ns_uri) goto oom;
        }
        d->flags = s->flags;

        if (s->attribute_count) {
            if (!s->attribute_plans) {
                if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
                dp_plan_free(p);
                return NULL;
            }
            d->attribute_plans = (leptris_attr_plan*)calloc(
                s->attribute_count, sizeof(*d->attribute_plans));
            if (!d->attribute_plans) goto oom;
            d->attribute_count = s->attribute_count;
            for (uint32_t a = 0; a < s->attribute_count; a++) {
                d->attribute_plans[a] = s->attribute_plans[a];
                /* Additive-ABI contract (#1272): a host that
                 * mallocs (not value-initializes) the spec leaves
                 * the new trailing fields garbage — zero them here
                 * so `predicate_count = 0` really means "no filter"
                 * and free never derefs an uninitialized pointer. */
                if (!s->attribute_plans[a].predicates ||
                    !s->attribute_plans[a].predicate_count) {
                    d->attribute_plans[a].predicates = NULL;
                    d->attribute_plans[a].predicate_count = 0;
                    d->attribute_plans[a].pad_pred = 0;
                }
                if (s->attribute_plans[a].wire_name) {
                    d->attribute_plans[a].wire_name =
                        dp_strdup(s->attribute_plans[a].wire_name);
                    if (!d->attribute_plans[a].wire_name) goto oom;
                }
                uint16_t pc = s->attribute_plans[a].predicate_count;
                if (pc) {
                    if (!s->attribute_plans[a].predicates) goto oom;
                    leptris_attr_predicate* cp =
                        (leptris_attr_predicate*)calloc(
                            pc, sizeof(leptris_attr_predicate));
                    if (!cp) goto oom;
                    for (uint16_t pi = 0; pi < pc; pi++) {
                        const leptris_attr_predicate* pp =
                            &s->attribute_plans[a].predicates[pi];
                        cp[pi].wire_name = pp->wire_name
                                                ? dp_strdup(pp->wire_name)
                                                : NULL;
                        cp[pi].expected_value = pp->expected_value
                                                   ? dp_strdup(
                                                       pp->expected_value)
                                                   : NULL;
                        if (pp->wire_name && !cp[pi].wire_name)
                            goto oom;
                        if (pp->expected_value &&
                            !cp[pi].expected_value)
                            goto oom;
                    }
                    d->attribute_plans[a].predicates = cp;
                    d->attribute_plans[a].predicate_count = pc;
                }
            }
        }

        if (s->child_count) {
            if (!s->child_plans) {
                if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
                dp_plan_free(p);
                return NULL;
            }
            d->child_plans = (leptris_child_plan*)calloc(
                s->child_count, sizeof(*d->child_plans));
            if (!d->child_plans) goto oom;
            d->child_count = s->child_count;
            for (uint32_t c = 0; c < s->child_count; c++) {
                const leptris_child_plan* sc = &s->child_plans[c];
                if ((sc->kind == LEPTRIS_PLAN_KIND_NESTED &&
                     (sc->child_plan_index < 0 ||
                      (uint32_t)sc->child_plan_index >= spec->plan_count))) {
                    if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
                    dp_plan_free(p);
                    return NULL;
                }
                d->child_plans[c] = *sc;
                /* Additive-ABI contract (#1272): zero the new
                 * trailing fields when the spec left them unset —
                 * malloc-initialized hosts get "no filter" and a
                 * safe free. */
                if (!sc->predicates || !sc->predicate_count) {
                    d->child_plans[c].predicates = NULL;
                    d->child_plans[c].predicate_count = 0;
                    d->child_plans[c].pad_pred = 0;
                }
                if (sc->wire_name) {
                    d->child_plans[c].wire_name = dp_strdup(sc->wire_name);
                    if (!d->child_plans[c].wire_name) goto oom;
                }
                uint16_t pc = sc->predicate_count;
                if (pc) {
                    if (!sc->predicates) goto oom;
                    leptris_attr_predicate* cp =
                        (leptris_attr_predicate*)calloc(
                            pc, sizeof(leptris_attr_predicate));
                    if (!cp) goto oom;
                    for (uint16_t pi = 0; pi < pc; pi++) {
                        const leptris_attr_predicate* pp =
                            &sc->predicates[pi];
                        cp[pi].wire_name = pp->wire_name
                                                ? dp_strdup(pp->wire_name)
                                                : NULL;
                        cp[pi].expected_value = pp->expected_value
                                                   ? dp_strdup(
                                                       pp->expected_value)
                                                   : NULL;
                        if (pp->wire_name && !cp[pi].wire_name)
                            goto oom;
                        if (pp->expected_value &&
                            !cp[pi].expected_value)
                            goto oom;
                    }
                    d->child_plans[c].predicates = cp;
                    d->child_plans[c].predicate_count = pc;
                }
            }
        }
    }
    return p;

oom:
    if (status) *status = LEPTRIS_ERROR_MEMORY;
    dp_plan_free(p);
    return NULL;
}

LEPTRIS_API void leptris_plan_free(LeptrisPlan plan) { dp_plan_free(plan); }

/* ---- walk ------------------------------------------------------- */

/* Namespace binding for a child element against a row's target form. */
static int dp_ns_binds(LeptrisElement elem, uint8_t ns_form,
                       const char* ns_uri, int lenient) {
    const char* local = NULL;
    const char* prefix = NULL;
    const char* uri = NULL;
    leptris_element_expanded_name(elem, &local, &prefix, &uri);
    int has_ns = prefix != NULL || uri != NULL;
    switch (ns_form) {
        case LEPTRIS_PLAN_NS_NONE:
            return lenient ? 1 : !has_ns;
        case LEPTRIS_PLAN_NS_EXACT:
            if (lenient) return 1;
            return uri && ns_uri && strcmp(uri, ns_uri) == 0;
        case LEPTRIS_PLAN_NS_ANY:
        default:
            return 1;
    }
}

static struct leptris_plan_result* dp_walk_element(LeptrisElement elem,
                                                   const dp_plan* plan,
                                                   const LeptrisPlan pool);

static int dp_walk_children(LeptrisElement elem, const dp_plan* plan,
                            const LeptrisPlan pool,
                            struct leptris_plan_result* out);

static struct leptris_plan_result* dp_walk_element(LeptrisElement elem,
                                                   const dp_plan* plan,
                                                   const LeptrisPlan pool) {
    struct leptris_plan_result* v = dp_value_new(LEPTRIS_PLAN_VALUE_ELEMENT);
    if (!v) return NULL;

    /* Attribute channel */
    if (plan->attribute_count) {
        v->attr_names = (char**)calloc(plan->attribute_count,
                                       sizeof(char*));
        v->attr_values = (char**)calloc(plan->attribute_count,
                                        sizeof(char*));
        if (!v->attr_names || !v->attr_values) {
            dp_result_free_rec(v);
            return NULL;
        }
        for (uint32_t a = 0; a < plan->attribute_count; a++) {
            const leptris_attr_plan* ap = &plan->attribute_plans[a];
            if (!ap->wire_name) continue;
            const char* val = leptris_element_attribute(elem, ap->wire_name);
            if (!val) continue;
            v->attr_names[v->attr_count] = dp_strdup(ap->wire_name);
            v->attr_values[v->attr_count] = dp_strdup(val);
            if (!v->attr_names[v->attr_count] ||
                !v->attr_values[v->attr_count]) {
                dp_result_free_rec(v);
                return NULL;
            }
            v->attr_count++;
        }
    }

    if (!dp_walk_children(elem, plan, pool, v)) {
        dp_result_free_rec(v);
        return NULL;
    }
    return v;
}

static int dp_walk_children(LeptrisElement elem, const dp_plan* plan,
                            const LeptrisPlan pool,
                            struct leptris_plan_result* out) {
    int lenient = (plan->flags & LEPTRIS_PLAN_FLAG_NS_LENIENT) != 0;
    int spine = (plan->flags & LEPTRIS_PLAN_FLAG_EMIT_ORDER_SPINE) != 0;

    /* #1272 same-wire-name partition claim: when a child element
     * is consumed by a row with predicates, no later same-wire-name
     * row (with or without predicates) may claim the same node. We
     * use a flat bitmap (one bit per matching child element by
     * sibling rank) sized to the element's child count. The match
     * attempt marks the bit so an earlier row "wins". */
    int child_count = 0;
    {
        LeptrisNodeRef probe = leptris_node_first_child(
            leptris_element_as_node(elem));
        for (; probe; probe = leptris_node_next_sibling(probe))
            child_count++;
    }
    /* Restrict to element children for the claim bitmap (only those
     * can be matched; comments/PI/text are spine-emitted only). */
    int element_count = 0;
    for (LeptrisNodeRef probe = leptris_node_first_child(
             leptris_element_as_node(elem));
         probe; probe = leptris_node_next_sibling(probe)) {
        if (leptris_node_get_type(probe) == LEPTRIS_NODE_TYPE_ELEMENT)
            element_count++;
    }
    unsigned char* claimed = NULL;
    if (element_count > 0 && (plan->child_count > 0)) {
        claimed = (unsigned char*)calloc(
            element_count, 1);
        if (!claimed) return 0;
    }
    /* Map from sibling node → element-rank index (0-based) so we
     * can flip the right bit. */
    int* elem_rank = NULL;
    if (element_count > 0) {
        elem_rank = (int*)malloc(element_count * sizeof(int));
        if (!elem_rank) { free(claimed); return 0; }
        int rank = 0;
        for (LeptrisNodeRef probe = leptris_node_first_child(
                 leptris_element_as_node(elem));
             probe; probe = leptris_node_next_sibling(probe)) {
            if (leptris_node_get_type(probe) == LEPTRIS_NODE_TYPE_ELEMENT)
                elem_rank[rank++] = (int)(intptr_t)probe;
        }
    }

    /* Per-walk document-order counter (#1273): incremented across
     * all rows so children of one element are globally rankable
     * even when emitted by different rows. Reset per parent call. */
    uint32_t walk_order = 0;

    for (uint32_t c = 0; c < plan->child_count; c++) {
        const leptris_child_plan* row = &plan->child_plans[c];
        if (!row->wire_name) continue;

        if (row->kind == LEPTRIS_PLAN_KIND_CONTENT) {
            /* Mixed-content text runs, document order. */
            if (!(plan->flags & LEPTRIS_PLAN_FLAG_MIXED_CONTENT)) continue;
            struct leptris_plan_result* coll =
                dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
            if (!coll) { free(claimed); free(elem_rank); return 0; }
            int want_cdata = (plan->flags & LEPTRIS_PLAN_FLAG_CDATA) != 0;
            /* order comes from outer walk_order */
            for (LeptrisNodeRef n = leptris_node_first_child(
                     leptris_element_as_node(elem));
                 n; n = leptris_node_next_sibling(n)) {
                int t = leptris_node_get_type(n);
                if (t != LEPTRIS_NODE_TYPE_TEXT &&
                    !(want_cdata && t == LEPTRIS_NODE_TYPE_CDATA))
                    continue;
                const char* text = leptris_text_node_get_content(n);
                if (!text) continue;
                struct leptris_plan_result* run =
                    dp_value_new(LEPTRIS_PLAN_VALUE_SCALAR);
                if (!run) {
                    dp_result_free_rec(coll);
                    free(claimed); free(elem_rank); return 0;
                }
                if (!dp_value_set_str(run, text, strlen(text))) {
                    dp_result_free_rec(run);
                    dp_result_free_rec(coll);
                    free(claimed); free(elem_rank); return 0;
                }
                dp_value_stamp(run, n);
                run->order_index = walk_order++;
                if (!dp_value_push(coll, run)) {
                    dp_result_free_rec(run);
                    dp_result_free_rec(coll);
                    free(claimed); free(elem_rank); return 0;
                }
            }
            if (!dp_value_push(out, coll)) {
                dp_result_free_rec(coll);
                free(claimed); free(elem_rank); return 0;
            }
            continue;
        }

        if (row->kind == LEPTRIS_PLAN_KIND_NESTED) {
            const dp_plan* target =
                &pool->plans[(uint32_t)row->child_plan_index];
            /* order comes from outer walk_order */
            for (LeptrisNodeRef n = leptris_node_first_child(
                     leptris_element_as_node(elem));
                 n; n = leptris_node_next_sibling(n)) {
                if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                    continue;
                LeptrisElement child = (LeptrisElement)n;
                const char* local = NULL;
                leptris_element_expanded_name(child, &local, NULL, NULL);
                if (!local || strcmp(local, row->wire_name) != 0) continue;
                if (!dp_ns_binds(child, target->ns_form, target->ns_uri,
                                 lenient))
                    continue;
                /* Claim the element against later same-wire rows
                 * (predicate partitioning). */
                int rank = -1;
                if (elem_rank)
                    for (int i = 0; i < element_count; i++)
                        if (elem_rank[i] == (int)(intptr_t)n) {
                            rank = i; break;
                        }
                if (rank >= 0 && claimed && claimed[rank]) continue;
                struct leptris_plan_result* v =
                    dp_walk_element(child, target, pool);
                if (!v) {
                    free(claimed); free(elem_rank); return 0;
                }
                v->name = dp_strdup(row->wire_name);
                v->type_tag = row->type_tag;
                dp_value_stamp(v, n);
                v->order_index = walk_order++;
                if (!v->name || !dp_value_push(out, v)) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
                /* Predicate-partitioning exclusive claim (#1272):
                 * only rows WITH predicates reserve the element;
                 * otherwise the multi-ns siblings in #1115 would
                 * lock each other out. */
                if (row->predicate_count > 0 && rank >= 0 && claimed)
                    claimed[rank] = 1;
            }
            continue;
        }

        /* SCALAR | COLLECTION | RAW | CALLBACK: bind matching child
         * elements by name; non-nested rows bind no-namespace
         * elements (NS_LENIENT relaxes). */
        int emitted = 0;
        /* order comes from outer walk_order */
        for (LeptrisNodeRef n = leptris_node_first_child(
                 leptris_element_as_node(elem));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement child = (LeptrisElement)n;
            const char* local = NULL;
            leptris_element_expanded_name(child, &local, NULL, NULL);
            if (!local || strcmp(local, row->wire_name) != 0) continue;
            /* #1272: same-wire-name claim — if a previous row bound
             * this node, skip it. */
            int rank = -1;
            if (elem_rank)
                for (int i = 0; i < element_count; i++)
                    if (elem_rank[i] == (int)(intptr_t)n) {
                        rank = i; break;
                    }
            if (rank >= 0 && claimed && claimed[rank]) continue;

            /* #1115: a row-level ns form owns the match; unset
             * rows keep the historical no-namespace (+LENIENT)
             * behavior. */
            if (row->ns_form) {
                if (!dp_ns_binds(child, row->ns_form, row->ns_uri, 0))
                    continue;
            } else if (!dp_ns_binds(child, LEPTRIS_PLAN_NS_NONE, NULL,
                                    lenient)) {
                continue;
            }
            /* #1272: predicate filter — require every pair. Empty
             * list is a no-op (match every element). */
            if (!dp_predicates_satisfied(child, row->predicates,
                                          row->predicate_count))
                continue;

            size_t node_off = leptris_node_byte_offset(n);
            struct leptris_plan_result* v = NULL;
            if (row->kind == LEPTRIS_PLAN_KIND_RAW) {
                char* ser = leptris_element_serialize(child, NULL);
                if (!ser) {
                    free(claimed); free(elem_rank); return 0;
                }
                v = dp_value_new(LEPTRIS_PLAN_VALUE_RAW);
                if (!v || !dp_value_set_str(v, ser, strlen(ser))) {
                    leptris_free_string(ser);
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
                leptris_free_string(ser);
            } else if (row->kind == LEPTRIS_PLAN_KIND_CALLBACK) {
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_CALLBACK);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
            } else {
                /* SCALAR item or COLLECTION item: text content. */
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_SCALAR);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
            }
            v->name = dp_strdup(row->wire_name);
            v->type_tag = row->type_tag;
            if (!v->name) {
                dp_result_free_rec(v);
                free(claimed); free(elem_rank); return 0;
            }
            /* #1269a: in-pass type execution. Strings stay populated;
             * `typed_ok` is the failure soft-fallback gate. */
            if (row->kind != LEPTRIS_PLAN_KIND_RAW)
                dp_execute_type(v, row->type_tag);
            dp_value_stamp(v, n);
            v->order_index = walk_order++;

            if (row->kind == LEPTRIS_PLAN_KIND_COLLECTION) {
                /* First match materializes the collection; subsequent
                 * matches append. */
                if (!emitted) {
                    struct leptris_plan_result* coll =
                        dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
                    if (!coll || !dp_value_push(out, coll)) {
                        dp_result_free_rec(coll);
                        dp_result_free_rec(v);
                        free(claimed); free(elem_rank); return 0;
                    }
                    /* #1113: the collection echoes its producing
                     * row's wire_name/type_tag — the documented
                     * contract, same as scalar/nested rows, so a
                     * consumer can attribute it among multiple
                     * collection rows. #1115: it also carries the
                     * first item's node offset + node_kind. */
                    coll->name = dp_strdup(row->wire_name);
                    coll->type_tag = row->type_tag;
                    coll->position = node_off;
                    coll->node_kind = LEPTRIS_NODE_TYPE_ELEMENT;
                    emitted = 1;
                }
                struct leptris_plan_result* coll =
                    out->kids[out->kid_count - 1];
                if (!dp_value_push(coll, v)) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
            } else if (!dp_value_push(out, v)) {
                dp_result_free_rec(v);
                free(claimed); free(elem_rank); return 0;
            }
            /* Predicate-partitioning exclusive claim (#1272): only
             * rows with predicates reserve; the #1115 multi-ns
             * sibling case stays independent. */
            if (row->predicate_count > 0 && rank >= 0 && claimed)
                claimed[rank] = 1;
        }
    }

    /* #1273 EMIT_ORDER_SPINE: after plan-row processing, append
     * unmatched sibling non-element nodes (text / comment / PI) as
     * SCALAR values so ordered/mixed hosts can rebuild the full
     * document order without re-parsing. Plan-matched elements
     * are already emitted at their row positions; the spine
     * fills the gaps. */
    if (spine) {
        /* order comes from outer walk_order */
        /* Reuse order_index counter so spine positions follow the
         * last matched row's count. We restart from 0 because
         * order_index is per-value within the parent context. */
        for (LeptrisNodeRef n = leptris_node_first_child(
                 leptris_element_as_node(elem));
             n; n = leptris_node_next_sibling(n)) {
            int t = leptris_node_get_type(n);
            if (t == LEPTRIS_NODE_TYPE_ELEMENT) continue; /* row channel */
            if (t == LEPTRIS_NODE_TYPE_TEXT) {
                const char* text = leptris_text_node_get_content(n);
                if (!text) continue;
                struct leptris_plan_result* v =
                    dp_value_new(LEPTRIS_PLAN_VALUE_SCALAR);
                if (!v || !dp_value_set_str(v, text, strlen(text))) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
                dp_value_stamp(v, n);
                v->order_index = walk_order++;
                if (!dp_value_push(out, v)) {
                    dp_result_free_rec(v);
                    free(claimed); free(elem_rank); return 0;
                }
            }
            /* comment / PI: not currently emitted as values (no
             * established channel); future work. Spine opts in
             * even if comment/PI are no-ops. */
        }
    }

    free(claimed);
    free(elem_rank);
    return 1;
}

LEPTRIS_API LeptrisPlanResult leptris_plan_walk(LeptrisDocument doc, LeptrisElement ctx,
                                    LeptrisPlan plan, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!doc || !ctx || !plan) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    struct leptris_plan_result* r =
        dp_walk_element(ctx, &plan->plans[0], plan);
    if (!r) {
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    return r;
}

LEPTRIS_API void leptris_plan_result_free(LeptrisPlanResult result) {
    dp_result_free_rec((struct leptris_plan_result*)result);
}

/* ---- accessors --------------------------------------------------- */

LEPTRIS_API LeptrisPlanValueKind leptris_plan_value_kind(const LeptrisPlanResult v) {
    return v ? v->kind : LEPTRIS_PLAN_VALUE_ELEMENT;
}

LEPTRIS_API const char* leptris_plan_value_name(const LeptrisPlanResult v) {
    return v ? v->name : NULL;
}

LEPTRIS_API uint8_t leptris_plan_value_type_tag(const LeptrisPlanResult v) {
    return v ? v->type_tag : 0;
}

LEPTRIS_API const char* leptris_plan_value_string(const LeptrisPlanResult v) {
    return v ? v->str : NULL;
}

LEPTRIS_API size_t leptris_plan_value_length(const LeptrisPlanResult v) {
    return v ? v->length : 0;
}

LEPTRIS_API size_t leptris_plan_value_position(const LeptrisPlanResult v) {
    return v ? v->position : 0;
}

LEPTRIS_API uint8_t leptris_plan_value_node_kind(const LeptrisPlanResult v) {
    return v ? v->node_kind : 0;
}

LEPTRIS_API uint32_t leptris_plan_value_order_index(const LeptrisPlanResult v) {
    return v ? v->order_index : 0u;
}

LEPTRIS_API size_t leptris_plan_value_count(const LeptrisPlanResult v) {
    return v ? v->kid_count : 0;
}

LEPTRIS_API LeptrisPlanResult leptris_plan_value_at(const LeptrisPlanResult v, size_t i) {
    if (!v || i >= v->kid_count) return NULL;
    return v->kids[i];
}

LEPTRIS_API const char* leptris_plan_value_attribute(const LeptrisPlanResult v,
                                         const char* wire_name) {
    if (!v || !wire_name) return NULL;
    for (size_t i = 0; i < v->attr_count; i++)
        if (strcmp(v->attr_names[i], wire_name) == 0)
            return v->attr_values[i];
    return NULL;
}

/* ---- #1269a typed accessors ------------------------------------- */

LEPTRIS_API int leptris_plan_value_int(const LeptrisPlanResult v,
                                       int64_t* out) {
    if (!v || !out) return 1;
    if (!v->typed_ok || v->type_tag != 1) return 1;
    *out = v->typed_i;
    return 0;
}

LEPTRIS_API int leptris_plan_value_float(const LeptrisPlanResult v,
                                         double* out) {
    if (!v || !out) return 1;
    if (!v->typed_ok || v->type_tag != 2) return 1;
    *out = v->typed_f;
    return 0;
}

LEPTRIS_API int leptris_plan_value_bool(const LeptrisPlanResult v, int* out) {
    if (!v || !out) return 1;
    if (!v->typed_ok || v->type_tag != 3) return 1;
    *out = v->typed_b ? 1 : 0;
    return 0;
}

/* ---- #1269b fused parse+walk ------------------------------------ */

LEPTRIS_API LeptrisPlanResult leptris_plan_materialize(
    const char* source, size_t source_len,
    LeptrisPlan plan, LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!source || !plan) {
        if (status) *status = LEPTRIS_ERROR_NULL_ARG;
        return NULL;
    }
    LeptrisDocument doc = leptris_parse_string(source, source_len, status);
    if (!doc) return NULL;
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
        leptris_document_free(doc);
        return NULL;
    }
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, status);
    leptris_document_free(doc);
    return r;
}
