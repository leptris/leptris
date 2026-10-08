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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
    /* #1551 emission routing: which plan row produced this value
     * (row_index == UINT32_MAX = no row: the walk root itself, or
     * an unmatched spine run), plus the row's ns binding, so the
     * serializer can rebuild element wrappers and prefixed
     * names deterministically. */
    uint32_t container_plan;
    uint32_t row_index;
    char* ns_prefix;
    char* ns_uri;
    /* attr-channel bindings, parallel to attr_names (entries or
     * NULL; arrays NULL when the element has no attribute plan). */
    char** attr_ns_prefixes;
    char** attr_ns_uris;
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
    if (v->attr_ns_prefixes) {
        for (size_t i = 0; i < v->attr_count; i++)
            free(v->attr_ns_prefixes[i]);
        free(v->attr_ns_prefixes);
    }
    if (v->attr_ns_uris) {
        for (size_t i = 0; i < v->attr_count; i++)
            free(v->attr_ns_uris[i]);
        free(v->attr_ns_uris);
    }
    free(v->attr_names);
    free(v->attr_values);
    free(v->name);
    free(v->str);
    free(v->ns_prefix);
    free(v->ns_uri);
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
    char* ns_prefix; /* #1551 emission binding */
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
            free((char *)d->attribute_plans[a].ns_uri);
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
            free((char *)d->child_plans[c].ns_prefix);
            free((char *)d->child_plans[c].ns_uri);
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
        free(d->ns_prefix);
    }
    free(p);
}

LEPTRIS_API uint32_t leptris_plan_abi_version(void) {
    return LEPTRIS_PLAN_ABI_VERSION;
}

LEPTRIS_API size_t leptris_plan_spec_struct_size(void) {
    return sizeof(leptris_plan_spec);
}
LEPTRIS_API size_t leptris_plan_element_row_size(void) {
    return sizeof(leptris_element_plan);
}
LEPTRIS_API size_t leptris_plan_child_row_size(void) {
    return sizeof(leptris_child_plan);
}
LEPTRIS_API size_t leptris_plan_attr_row_size(void) {
    return sizeof(leptris_attr_plan);
}
LEPTRIS_API size_t leptris_plan_predicate_row_size(void) {
    return sizeof(leptris_attr_predicate);
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
        d->ns_prefix = s->ns_prefix ? dp_strdup(s->ns_prefix) : NULL;
        if (s->ns_prefix && !d->ns_prefix) goto oom;
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
                /* #1486 additive-ABI guard (the #1272 lesson): a
                 * host that mallocs the spec leaves the trailing
                 * ns fields garbage — normalize unset forms so the
                 * walker never reads a garbage pointer, and deep-
                 * copy EXACT uris. EXACT without a uri cannot
                 * express an identity: reject the spec. */
                if (!s->attribute_plans[a].ns_form) {
                    d->attribute_plans[a].ns_form = 0;
                    d->attribute_plans[a].pad_ns = 0;
                    d->attribute_plans[a].ns_uri = NULL;
                } else {
                    d->attribute_plans[a].pad_ns = 0;
                    if (s->attribute_plans[a].ns_form ==
                            LEPTRIS_PLAN_NS_EXACT) {
                        if (!s->attribute_plans[a].ns_uri) {
                            if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
                            dp_plan_free(p);
                            return NULL;
                        }
                        d->attribute_plans[a].ns_uri =
                            dp_strdup(s->attribute_plans[a].ns_uri);
                        if (!d->attribute_plans[a].ns_uri) goto oom;
                    } else {
                        d->attribute_plans[a].ns_uri = NULL;
                    }
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
                      (uint32_t)sc->child_plan_index >= spec->plan_count)) ||
                    (sc->kind == LEPTRIS_PLAN_KIND_WILDCARD &&
                     (sc->child_plan_index < -1 ||
                      (sc->child_plan_index >= 0 &&
                       (uint32_t)sc->child_plan_index >=
                           spec->plan_count)))) {
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
                d->child_plans[c].ns_prefix =
                    sc->ns_prefix ? dp_strdup(sc->ns_prefix) : NULL;
                if (sc->ns_prefix && !d->child_plans[c].ns_prefix)
                    goto oom;
                /* #1585: ns_uri rides the struct copy otherwise —
                 * a dangling pointer into the caller's build
                 * buffers once they are reclaimed (bindings GC
                 * their anchors; the descriptor must outlive
                 * them). Same retention as attr rows (#1486). */
                d->child_plans[c].ns_uri =
                    sc->ns_uri ? dp_strdup(sc->ns_uri) : NULL;
                if (sc->ns_uri && !d->child_plans[c].ns_uri)
                    goto oom;
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
/* Child-row wire-name match. The ABI documents wire_name as "the
 * XML element name as it appears on the wire": a row may carry the
 * PREFIXED form ("w:b") or the bare local name ("b"). Rows with a
 * colon bind prefix+local (literal prefix, per the wire contract);
 * bare rows keep the historical local-name match with the row's
 * ns_form doing the namespace work. The old matcher compared the
 * local name only, so every prefixed row — the shape hosts
 * compiling from xsd:QName schemas emit — silently bound nothing
 * and whole subtrees hydrated empty (XSD-002 fallout). */
static int dp_wire_name_matches(LeptrisElement child,
                                const char* wire_name) {
    const char* local = NULL;
    const char* prefix = NULL;
    leptris_element_expanded_name(child, &local, &prefix, NULL);
    if (!wire_name || !local) return 0;
    const char* colon = strchr(wire_name, ':');
    if (!colon) return strcmp(local, wire_name) == 0;
    size_t plen = (size_t)(colon - wire_name);
    return prefix != NULL && strlen(prefix) == plen &&
           strncmp(prefix, wire_name, plen) == 0 &&
           strcmp(local, colon + 1) == 0;
}

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
        case LEPTRIS_PLAN_NS_UNQUALIFIED:
            /* #1560: the WRITTEN spelling — no prefix, any
             * effective URI (default-xmlns elements included);
             * prefixed spellings never bind. */
            return prefix == NULL;
    }
}

/* #1551: "prefix:local" join for emission spellings. */
static char* dp_join_colon(const char* pfx, const char* local) {
    size_t pl = strlen(pfx), ll = strlen(local);
    char* j = (char*)malloc(pl + 1 + ll + 1);
    if (!j) return NULL;
    memcpy(j, pfx, pl);
    j[pl] = ':';
    memcpy(j + pl + 1, local, ll + 1);
    return j;
}

static struct leptris_plan_result* dp_walk_element(LeptrisElement elem,
                                                   const dp_plan* plan,
                                                   const LeptrisPlan pool);

static int dp_walk_children(LeptrisElement elem, const dp_plan* plan,
                            const LeptrisPlan pool,
                            struct leptris_plan_result* out);

/* #1486 attribute-row binding. ns_form 0 = the historical
 * wire-name lookup (the string exactly as it appears on the
 * wire). A set form matches (namespace, local): the row's
 * wire_name is the LOCAL name; each attribute's prefix resolves
 * through the ELEMENT's in-scope bindings, so the identity holds
 * across prefix spellings. Unprefixed attributes have no
 * namespace (XML namespaces §5.2) — EXACT never binds them. */
static const char* dp_attr_value(LeptrisElement elem,
                                 const leptris_attr_plan* ap) {
    if (!ap->ns_form) {
        /* #1586, the plan-path mirror of #758: the exact wire
         * spelling first, then any qualification by local name —
         * plain rows are lenient exactly like element rows under
         * ns_lenient. Exact-URI rows keep their #1486 semantics. */
        const char* exact =
            leptris_element_attribute(elem, ap->wire_name);
        if (exact) return exact;
        for (LeptrisAttribute at = leptris_element_first_attribute(elem);
             at; at = leptris_attribute_next(at)) {
            const char* qn = leptris_attribute_get_name(at);
            if (!qn) continue;
            const char* colon = strchr(qn, ':');
            const char* local = colon ? colon + 1 : qn;
            if (strcmp(local, ap->wire_name) == 0)
                return leptris_attribute_get_value(elem, at);
        }
        return NULL;
    }
    for (LeptrisAttribute at = leptris_element_first_attribute(elem);
         at; at = leptris_attribute_next(at)) {
        const char* qn = leptris_attribute_get_name(at);
        if (!qn) continue;
        const char* colon = strchr(qn, ':');
        const char* local = colon ? colon + 1 : qn;
        if (strcmp(local, ap->wire_name) != 0) continue;
        const char* uri = NULL;
        if (colon) {
            char prefix[128];
            size_t plen = (size_t)(colon - qn);
            if (plen >= sizeof(prefix)) continue;
            memcpy(prefix, qn, plen);
            prefix[plen] = 0;
            uri = leptris_element_namespace_for_prefix(elem, prefix);
        }
        switch (ap->ns_form) {
            case LEPTRIS_PLAN_NS_NONE:
                if (uri) continue;
                break;
            case LEPTRIS_PLAN_NS_EXACT:
                if (!uri || !ap->ns_uri ||
                    strcmp(uri, ap->ns_uri) != 0)
                    continue;
                break;
            default: /* ANY */
                break;
        }
        return leptris_attribute_get_value(elem, at);
    }
    return NULL;
}

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
        v->attr_ns_prefixes = (char**)calloc(plan->attribute_count,
                                             sizeof(char*));
        v->attr_ns_uris = (char**)calloc(plan->attribute_count,
                                         sizeof(char*));
        if (!v->attr_names || !v->attr_values || !v->attr_ns_prefixes ||
            !v->attr_ns_uris) {
            dp_result_free_rec(v);
            return NULL;
        }
        for (uint32_t a = 0; a < plan->attribute_count; a++) {
            const leptris_attr_plan* ap = &plan->attribute_plans[a];
            if (!ap->wire_name) continue;
            const char* val = dp_attr_value(elem, ap);
            if (!val) continue;
            /* #1551: a prefixed attr row emits "prefix:local" —
             * the plan-chosen spelling, not the wire's. */
            if (ap->ns_prefix)
                v->attr_names[v->attr_count] =
                    dp_join_colon(ap->ns_prefix, ap->wire_name);
            else
                v->attr_names[v->attr_count] = dp_strdup(ap->wire_name);
            v->attr_values[v->attr_count] = dp_strdup(val);
            if (ap->ns_prefix) {
                v->attr_ns_prefixes[v->attr_count] =
                    dp_strdup(ap->ns_prefix);
                v->attr_ns_uris[v->attr_count] =
                    ap->ns_uri ? dp_strdup(ap->ns_uri) : NULL;
            }
            if (!v->attr_names[v->attr_count] ||
                !v->attr_values[v->attr_count] ||
                (ap->ns_prefix && (!v->attr_ns_prefixes[v->attr_count] ||
                                   !v->attr_ns_uris[v->attr_count]))) {
                dp_result_free_rec(v);
                return NULL;
            }
            v->attr_count++;
        }
    }

    /* #1551: the element's own binding (root plan or nested /
     * wildcard walk member) — prefixed emission + declaration. */
    if (plan->ns_prefix && plan->ns_form == LEPTRIS_PLAN_NS_EXACT) {
        v->container_plan = (uint32_t)(plan - pool->plans);
        v->row_index = UINT32_MAX;
        v->ns_prefix = dp_strdup(plan->ns_prefix);
        v->ns_uri = dp_strdup(plan->ns_uri);
        if (!v->ns_prefix || !v->ns_uri) {
            dp_result_free_rec(v);
            return NULL;
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
    /* #1551: rows stamp their producer so the serializer can
     * rebuild wrappers (container plan + row index). */
    uint32_t self_plan = (uint32_t)(plan - pool->plans);

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
    /* #1552: bound[] marks every element a named row actually
     * bound (claimed[] is the #1272 predicate-partition subset).
     * Wildcard rows consult it after all named rows ran. */
    unsigned char* bound = NULL;
    if (element_count > 0 && (plan->child_count > 0)) {
        claimed = (unsigned char*)calloc(
            element_count, 1);
        if (!claimed) return 0;
        bound = (unsigned char*)calloc(
            element_count, 1);
        if (!bound) { free(claimed); return 0; }
    }
    /* Map from sibling node → element-rank index (0-based) so we
     * can flip the right bit. */
    int* elem_rank = NULL;
    if (element_count > 0) {
        elem_rank = (int*)malloc(element_count * sizeof(int));
        if (!elem_rank) { free(bound); free(claimed); return 0; }
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
        /* #1552: wildcard rows run in a second pass, after every
         * named row has bound — named rows take precedence. */
        if (row->kind == LEPTRIS_PLAN_KIND_WILDCARD) continue;

        if (row->kind == LEPTRIS_PLAN_KIND_CONTENT) {
            /* Mixed-content text runs, document order. */
            if (!(plan->flags & LEPTRIS_PLAN_FLAG_MIXED_CONTENT)) continue;
            struct leptris_plan_result* coll =
                dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
            if (!coll) { free(bound); free(claimed); free(elem_rank); return 0; }
            coll->container_plan = self_plan;
            coll->row_index = c;
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
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                if (!dp_value_set_str(run, text, strlen(text))) {
                    dp_result_free_rec(run);
                    dp_result_free_rec(coll);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                dp_value_stamp(run, n);
                run->order_index = walk_order++;
                if (!dp_value_push(coll, run)) {
                    dp_result_free_rec(run);
                    dp_result_free_rec(coll);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
            }
            if (!dp_value_push(out, coll)) {
                dp_result_free_rec(coll);
                free(bound); free(claimed); free(elem_rank); return 0;
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
                if (!dp_wire_name_matches(child, row->wire_name))
                    continue;
                /* The TARGET plan's form governs (#1115) — so its
                 * lenient flag governs the leniency, not the outer
                 * plan's (#1586: a namespaced child under a plan
                 * that declares ns_lenient binds by local name). */
                if (!dp_ns_binds(child, target->ns_form, target->ns_uri,
                                 (target->flags &
                                  LEPTRIS_PLAN_FLAG_NS_LENIENT) != 0))
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
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                v->name = dp_strdup(row->wire_name);
                v->type_tag = row->type_tag;
                v->container_plan = self_plan;
                v->row_index = c;
                /* #1551: the row's emission binding; the URI falls
                 * back to the target plan's (NESTED matches with
                 * the target's form, #1115). */
                if (row->ns_prefix) {
                    /* The inner walk's own #1551 stamp may already
                     * hold this pair — free before overwriting or
                     * the first strdup orphans. */
                    free(v->ns_prefix);
                    free(v->ns_uri);
                    v->ns_prefix = dp_strdup(row->ns_prefix);
                    v->ns_uri = dp_strdup(row->ns_uri ? row->ns_uri
                                                      : target->ns_uri);
                    if (!v->ns_prefix || !v->ns_uri) {
                        dp_result_free_rec(v);
				free(claimed); free(elem_rank); return 0;
                        return 0;
                    }
                }
                dp_value_stamp(v, n);
                v->order_index = walk_order++;
                if (!v->name || !dp_value_push(out, v)) {
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                /* Predicate-partitioning exclusive claim (#1272):
                 * only rows WITH predicates reserve the element;
                 * otherwise the multi-ns siblings in #1115 would
                 * lock each other out. */
                if (row->predicate_count > 0 && rank >= 0 && claimed)
                    claimed[rank] = 1;
                /* #1552: named-row bind — invisible to later
                 * same-wire rows (unchanged #1272 semantics) but
                 * claimed against the wildcard remainder. */
                if (rank >= 0 && bound)
                    bound[rank] = 1;
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
            if (!dp_wire_name_matches(child, row->wire_name)) continue;
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
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                v = dp_value_new(LEPTRIS_PLAN_VALUE_RAW);
                if (!v || !dp_value_set_str(v, ser, strlen(ser))) {
                    leptris_free_string(ser);
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                leptris_free_string(ser);
            } else if (row->kind == LEPTRIS_PLAN_KIND_CALLBACK) {
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_CALLBACK);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
            } else {
                /* SCALAR item or COLLECTION item: text content. */
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_SCALAR);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
            }
            v->name = dp_strdup(row->wire_name);
            v->type_tag = row->type_tag;
            v->container_plan = self_plan;
            v->row_index = c;
            /* #1551: scalar/collection rows still serialize as
             * ELEMENTS — the row is the wrapper. */
            if (row->ns_prefix) {
                v->ns_prefix = dp_strdup(row->ns_prefix);
                v->ns_uri = dp_strdup(row->ns_uri);
                if (!v->ns_prefix || !v->ns_uri) {
                    dp_result_free_rec(v);
			free(claimed); free(elem_rank); return 0;
                }
            }
            if (!v->name) {
                dp_result_free_rec(v);
                free(bound); free(claimed); free(elem_rank); return 0;
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
                        free(bound); free(claimed); free(elem_rank); return 0;
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
                    coll->container_plan = self_plan;
                    coll->row_index = c;
                    if (row->ns_prefix) {
                        coll->ns_prefix = dp_strdup(row->ns_prefix);
                        coll->ns_uri = dp_strdup(row->ns_uri);
                    }
                    emitted = 1;
                }
                struct leptris_plan_result* coll =
                    out->kids[out->kid_count - 1];
                if (!dp_value_push(coll, v)) {
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
            } else if (!dp_value_push(out, v)) {
                dp_result_free_rec(v);
                free(bound); free(claimed); free(elem_rank); return 0;
            }
            /* Predicate-partitioning exclusive claim (#1272): only
             * rows with predicates reserve; the #1115 multi-ns
             * sibling case stays independent. */
            if (row->predicate_count > 0 && rank >= 0 && claimed)
                claimed[rank] = 1;
            /* #1552: named-row bind — claimed against the
             * wildcard remainder. */
            if (rank >= 0 && bound)
                bound[rank] = 1;
        }
    }

    /* #1552 pass 2: WILDCARD rows. bound[] is complete — every
     * element a named row bound is marked, so the catch-all sees
     * exactly the remainder (elements a named row rejected by
     * ns form or predicate are still remainder). One COLLECTION
     * per row, always emitted (empty bucket = routing signal),
     * members in document order, each echoing the row type_tag. */
    for (uint32_t c = 0; c < plan->child_count; c++) {
        const leptris_child_plan* row = &plan->child_plans[c];
        if (!row->wire_name) continue;
        if (row->kind != LEPTRIS_PLAN_KIND_WILDCARD) continue;

        struct leptris_plan_result* coll =
            dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
        if (!coll) {
            free(bound); free(bound); free(claimed); free(elem_rank); return 0;
        }
        coll->name = dp_strdup(row->wire_name);
        coll->type_tag = row->type_tag;
        coll->node_kind = LEPTRIS_NODE_TYPE_ELEMENT;
        if (!coll->name || !dp_value_push(out, coll)) {
            dp_result_free_rec(coll);
            free(bound); free(bound); free(claimed); free(elem_rank); return 0;
        }
        for (LeptrisNodeRef n = leptris_node_first_child(
                 leptris_element_as_node(elem));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement child = (LeptrisElement)n;
            int rank = -1;
            if (elem_rank)
                for (int i = 0; i < element_count; i++)
                    if (elem_rank[i] == (int)(intptr_t)n) {
                        rank = i; break;
                    }
            if (rank >= 0 && bound && bound[rank]) continue;
            /* pad0 set = explicit form; unset = ANY namespace, the
             * catch-all default (ns_form 0 alone can't distinguish
             * explicit NONE from unset). */
            if (row->pad0 &&
                !dp_ns_binds(child, row->ns_form, row->ns_uri, 0))
                continue;
            if (!dp_predicates_satisfied(child, row->predicates,
                                          row->predicate_count))
                continue;

            struct leptris_plan_result* v;
            if (row->child_plan_index >= 0) {
                const dp_plan* target =
                    &pool->plans[(uint32_t)row->child_plan_index];
                v = dp_walk_element(child, target, pool);
            } else {
                char* ser = leptris_element_serialize(child, NULL);
                if (!ser) {
                    free(bound); free(claimed); free(elem_rank);
                    return 0;
                }
                v = dp_value_new(LEPTRIS_PLAN_VALUE_RAW);
                if (!v || !dp_value_set_str(v, ser, strlen(ser))) {
                    leptris_free_string(ser);
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank);
                    return 0;
                }
                leptris_free_string(ser);
            }
            if (!v) {
                free(bound); free(bound); free(claimed); free(elem_rank); return 0;
            }
            const char* local = NULL;
            leptris_element_expanded_name(child, &local, NULL, NULL);
            v->name = local ? dp_strdup(local) : NULL;
            v->type_tag = row->type_tag;
            dp_value_stamp(v, n);
            v->order_index = walk_order++;
            if (!dp_value_push(coll, v)) {
                dp_result_free_rec(v);
                free(bound); free(bound); free(claimed); free(elem_rank); return 0;
            }
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
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
                v->container_plan = self_plan;
                v->row_index = UINT32_MAX; /* spine: bare text */
                dp_value_stamp(v, n);
                v->order_index = walk_order++;
                if (!dp_value_push(out, v)) {
                    dp_result_free_rec(v);
                    free(bound); free(claimed); free(elem_rank); return 0;
                }
            }
            /* comment / PI: not currently emitted as values (no
             * established channel); future work. Spine opts in
             * even if comment/PI are no-ops. */
        }
    }

    free(bound);
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

/* ---- #1551: result -> XML serialization ------------------------- */
typedef struct {
    char* s;
    size_t len, cap;
} dp_sb;

static int dp_sb_putn(dp_sb* b, const char* s, size_t n) {
    if (!n) return 1;
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        while (b->len + n + 1 > nc) nc *= 2;
        char* g = (char*)realloc(b->s, nc);
        if (!g) return 0;
        b->s = g;
        b->cap = nc;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
    return 1;
}

static int dp_sb_puts(dp_sb* b, const char* s) {
    return dp_sb_putn(b, s, strlen(s));
}

static int dp_sb_escape(dp_sb* b, const char* s, int is_attr) {
    for (; s && *s; s++) {
        if (*s == '&') {
            if (!dp_sb_puts(b, "&amp;")) return 0;
        } else if (*s == '<') {
            if (!dp_sb_puts(b, "&lt;")) return 0;
        } else if (*s == '>') {
            if (!dp_sb_puts(b, "&gt;")) return 0;
        } else if (is_attr && *s == '"') {
            if (!dp_sb_puts(b, "&quot;")) return 0;
        } else if (!dp_sb_putn(b, s, 1)) {
            return 0;
        }
    }
    return 1;
}

/* Distinct prefix bindings, first-encounter order; same prefix
 * keeps its first URI (deterministic). */
typedef struct {
    char** p;
    char** u;
    size_t n, cap;
} dp_decls;

static int dp_decls_seen(dp_decls* d, const char* p, const char* u) {
    for (size_t i = 0; i < d->n; i++)
        if (strcmp(d->p[i], p) == 0) return 1;
    if (d->n == d->cap) {
        size_t nc = d->cap ? d->cap * 2 : 8;
        char** gp = (char**)realloc(d->p, nc * sizeof(char*));
        if (!gp) return 0;
        d->p = gp;
        char** gu = (char**)realloc(d->u, nc * sizeof(char*));
        if (!gu) return 0;
        d->u = gu;
        d->cap = nc;
    }
    d->p[d->n] = dp_strdup(p);
    d->u[d->n] = dp_strdup(u);
    if (!d->p[d->n] || !d->u[d->n]) return 0;
    d->n++;
    return 1;
}

static void dp_decls_free(dp_decls* d) {
    for (size_t i = 0; i < d->n; i++) {
        free(d->p[i]);
        free(d->u[i]);
    }
    free(d->p);
    free(d->u);
}

static int dp_collect_decls(const struct leptris_plan_result* v,
                            dp_decls* d) {
    if (v->ns_prefix && v->ns_uri &&
        !dp_decls_seen(d, v->ns_prefix, v->ns_uri))
        return 0;
    for (size_t a = 0; a < v->attr_count; a++)
        if (v->attr_ns_prefixes && v->attr_ns_prefixes[a] &&
            v->attr_ns_uris && v->attr_ns_uris[a] &&
            !dp_decls_seen(d, v->attr_ns_prefixes[a],
                           v->attr_ns_uris[a]))
            return 0;
    for (size_t i = 0; i < v->kid_count; i++)
        if (!dp_collect_decls(v->kids[i], d)) return 0;
    return 1;
}

/* Flatten one element's children: COLLECTION rows are transparent
 * (members are the siblings). */
static int dp_flatten_kids(const struct leptris_plan_result* v,
                           const struct leptris_plan_result*** arr,
                           size_t* n, size_t* cap) {
    for (size_t i = 0; i < v->kid_count; i++) {
        const struct leptris_plan_result* k = v->kids[i];
        if (k->kind == LEPTRIS_PLAN_VALUE_COLLECTION) {
            if (!dp_flatten_kids(k, arr, n, cap)) return 0;
            continue;
        }
        if (*n == *cap) {
            size_t nc = *cap ? *cap * 2 : 16;
            const struct leptris_plan_result** g =
                (const struct leptris_plan_result**)realloc(
                    (void*)*arr, nc * sizeof(*g));
            if (!g) return 0;
            *arr = g;
            *cap = nc;
        }
        (*arr)[(*n)++] = k;
    }
    return 1;
}

static int dp_kid_pos_cmp(const void* a, const void* b) {
    const struct leptris_plan_result* const* ka =
        (const struct leptris_plan_result* const*)a;
    const struct leptris_plan_result* const* kb =
        (const struct leptris_plan_result* const*)b;
    if ((*ka)->position < (*kb)->position) return -1;
    if ((*ka)->position > (*kb)->position) return 1;
    return 0;
}

/* The wrapper spelling for a routed value: the row's wire_name,
 * prefixed by the row binding when present. */
static char* dp_wrapper_name(const struct leptris_plan_result* v) {
    const char* local = v->name;
    const char* colon = local ? strchr(local, ':') : NULL;
    if (colon) local = colon + 1;
    if (v->ns_prefix) return dp_join_colon(v->ns_prefix, local ? local : "");
    return dp_strdup(local ? local : "");
}

static int dp_emit_element_open(const struct leptris_plan_result* v,
                                const char* name, dp_sb* b,
                                const dp_decls* decls, int is_root) {
    if (!dp_sb_putn(b, "<", 1) || !dp_sb_puts(b, name)) return 0;
    if (is_root && decls) {
        for (size_t i = 0; i < decls->n; i++) {
            if (!dp_sb_puts(b, " xmlns:") || !dp_sb_puts(b, decls->p[i]) ||
                !dp_sb_puts(b, "=\"") || !dp_sb_puts(b, decls->u[i]) ||
                !dp_sb_putn(b, "\"", 1))
                return 0;
        }
    }
    for (size_t a = 0; a < v->attr_count; a++) {
        if (!dp_sb_putn(b, " ", 1) || !dp_sb_puts(b, v->attr_names[a]) ||
            !dp_sb_puts(b, "=\"") ||
            !dp_sb_escape(b, v->attr_values[a], 1) ||
            !dp_sb_putn(b, "\"", 1))
            return 0;
    }
    return 1;
}

static int dp_emit_value(const struct leptris_plan_result* v,
                         const dp_plan* container, const LeptrisPlan pool,
                         dp_sb* b, const dp_decls* decls, int is_root);

static int dp_emit_element(const struct leptris_plan_result* v,
                           const char* name, const LeptrisPlan pool,
                           const dp_plan* kids_container, dp_sb* b,
                           const dp_decls* decls, int is_root) {
    if (!dp_emit_element_open(v, name, b, decls, is_root)) return 0;
    const struct leptris_plan_result** kids = NULL;
    size_t nk = 0, cap = 0;
    if (!dp_flatten_kids(v, &kids, &nk, &cap)) {
        free((void*)kids);
        return 0;
    }
    if (!nk) {
        free((void*)kids);
        return dp_sb_puts(b, "/>");
    }
    if (!dp_sb_putn(b, ">", 1)) {
        free((void*)kids);
        return 0;
    }
    qsort((void*)kids, nk, sizeof(*kids), dp_kid_pos_cmp);
    /* #1565: kid ROWS live in the plan that WALKED this element —
     * container_plan is the plan whose row CAPTURED it (the
     * parent), so a nested member resolved its kids' rows against
     * the parent's rows and a CONTENT row serialized as an
     * empty-named wrapper. */
    int ok = 1;
    for (size_t i = 0; i < nk && ok; i++)
        ok = dp_emit_value(kids[i], kids_container, pool, b, NULL, 0);
    free((void*)kids);
    if (!ok) return 0;
    return dp_sb_puts(b, "</") && dp_sb_puts(b, name) &&
           dp_sb_putn(b, ">", 1);
}

static int dp_emit_value(const struct leptris_plan_result* v,
                         const dp_plan* container, const LeptrisPlan pool,
                         dp_sb* b, const dp_decls* decls, int is_root) {
    if (v->kind == LEPTRIS_PLAN_VALUE_RAW)
        return dp_sb_puts(b, v->str ? v->str : "");
    if (v->kind == LEPTRIS_PLAN_VALUE_ELEMENT) {
        /* #1565: a walk member captured by a CONTENT row is an
         * unnamed element — the row lookup must precede the name
         * resolution, or the empty wire name becomes the wrapper
         * (<w:item><>text</></w:item>). Content emits inline text
         * at every nesting level. */
        if (container && v->row_index != UINT32_MAX &&
            v->row_index < container->child_count &&
            container->child_plans[v->row_index].kind ==
                LEPTRIS_PLAN_KIND_CONTENT)
            return dp_sb_escape(b, v->str ? v->str : "", 0);
        /* The element's own binding was stamped at walk time; a
         * walk member without one falls back to its plan's. */
        char* name = NULL;
        const dp_plan* self =
            &((const struct leptris_plan*)pool)
                ->plans[v->container_plan];
        /* Local name: the value's echoed wire name, or the plan's
         * element_name for a walk root (which carries no name). */
        const char* local = NULL;
        if (v->name) {
            const char* colon = strchr(v->name, ':');
            local = colon ? colon + 1 : v->name;
        } else {
            local = self->element_name;
        }
        if (v->ns_prefix)
            name = dp_join_colon(v->ns_prefix, local ? local : "");
        else
            name = dp_strdup(local ? local : "");
        if (!name) return 0;
        /* Kids resolve their rows against the plan that WALKED this
         * element: the capturing row's child_plan_index when nested,
         * else container_plan (the walk root is its own plan). */
        const struct leptris_plan* poolp =
            (const struct leptris_plan*)pool;
        const dp_plan* kids_container =
            &poolp->plans[v->container_plan];
        if (container && v->row_index != UINT32_MAX &&
            v->row_index < container->child_count) {
            const leptris_child_plan* cap =
                &container->child_plans[v->row_index];
            if (cap->child_plan_index >= 0 &&
                (uint32_t)cap->child_plan_index < poolp->plan_count)
                kids_container =
                    &poolp->plans[cap->child_plan_index];
        }
        int ok = dp_emit_element(v, name, pool, kids_container, b,
                                 decls, is_root);
        free(name);
        return ok;
    }
    /* SCALAR / CALLBACK / CONTENT runs. */
    const leptris_child_plan* row = NULL;
    if (container && v->row_index != UINT32_MAX &&
        v->row_index < container->child_count)
        row = &container->child_plans[v->row_index];
    const char* text = v->str ? v->str : "";
    if (row && row->kind != LEPTRIS_PLAN_KIND_CONTENT) {
        /* The row is the wrapper element around this text. */
        char* name = dp_wrapper_name(v);
        if (!name) return 0;
        int ok = dp_sb_putn(b, "<", 1) && dp_sb_puts(b, name) &&
                 dp_sb_putn(b, ">", 1) && dp_sb_escape(b, text, 0) &&
                 dp_sb_puts(b, "</") && dp_sb_puts(b, name) &&
                 dp_sb_putn(b, ">", 1);
        free(name);
        return ok;
    }
    /* Spine runs and CONTENT rows: bare text. */
    return dp_sb_escape(b, text, 0);
}

LEPTRIS_API char* leptris_plan_serialize(LeptrisPlan plan,
                                         const LeptrisPlanResult result,
                                         LeptrisStatus* status) {
    if (status) *status = LEPTRIS_OK;
    if (!plan || !result) {
        if (status) *status = LEPTRIS_ERROR_INVALID_ARG;
        return NULL;
    }
    const struct leptris_plan_result* rv =
        (const struct leptris_plan_result*)result;
    dp_decls decls = {0};
    if (!dp_collect_decls(rv, &decls)) {
        dp_decls_free(&decls);
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    dp_sb b = {0};
    int ok = dp_emit_value(rv, NULL, (const LeptrisPlan)plan, &b,
                           &decls, 1);
    dp_decls_free(&decls);
    if (!ok) {
        free(b.s);
        if (status) *status = LEPTRIS_ERROR_MEMORY;
        return NULL;
    }
    if (!b.s) {
        b.s = (char*)calloc(1, 1);
        if (!b.s) {
            if (status) *status = LEPTRIS_ERROR_MEMORY;
            return NULL;
        }
    }
    return b.s;
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

LEPTRIS_API size_t leptris_plan_value_children_snapshot(
    const LeptrisPlanResult v,
    char* names_blob, size_t blob_cap,
    size_t* name_offsets, uint8_t* type_tags,
    LeptrisPlanResult* child_handles) {
    if (!v) return 0;
    size_t need = 0;
    for (size_t i = 0; i < v->kid_count; i++) {
        const char* n = v->kids[i]->name;
        need += (n ? strlen(n) : 0) + 1;
    }
    if (need > blob_cap || !names_blob || !name_offsets || !type_tags ||
        !child_handles)
        return need;
    size_t off = 0;
    for (size_t i = 0; i < v->kid_count; i++) {
        const LeptrisPlanResult kid = v->kids[i];
        const char* n = kid->name;
        if (n) {
            size_t len = strlen(n);
            memcpy(names_blob + off, n, len);
            names_blob[off + len] = '\0';
            name_offsets[i] = off;
            off += len + 1;
        } else {
            name_offsets[i] = (size_t)-1;
        }
        type_tags[i] = kid->type_tag;
        child_handles[i] = kid;
    }
    name_offsets[v->kid_count] = off;
    return 0;
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
