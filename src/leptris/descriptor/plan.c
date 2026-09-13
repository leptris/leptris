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
#include <stdlib.h>
#include <string.h>

#include "leptris.h"
#include "leptris/descriptor.h"

struct leptris_plan_result {
    LeptrisPlanValueKind kind;
    char* name;        /* wire_name copy; NULL when none */
    uint8_t type_tag;
    char* str;         /* SCALAR/RAW/CALLBACK value; NULL otherwise */
    size_t length;
    size_t position;   /* CALLBACK */
    /* ELEMENT child values / COLLECTION items */
    struct leptris_plan_result** kids;
    size_t kid_count;
    size_t kid_cap;
    /* ELEMENT attributes (parallel arrays; small) */
    char** attr_names;
    char** attr_values;
    size_t attr_count;
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

/* ---- compiled plan pool ----------------------------------------- */

/* Internal mirror of the spec: everything copied, arrays flattened
 * per plan. */
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
        for (uint32_t a = 0; a < d->attribute_count; a++)
            free(d->attribute_plans[a].wire_name);
        for (uint32_t c = 0; c < d->child_count; c++)
            free(d->child_plans[c].wire_name);
        free(d->attribute_plans);
        free(d->child_plans);
        free(d->element_name);
        free(d->ns_uri);
    }
    free(p);
}

LEPTRIS_DESCRIPTOR_API uint32_t leptris_plan_abi_version(void) {
    return LEPTRIS_PLAN_ABI_VERSION;
}


LEPTRIS_DESCRIPTOR_API LeptrisPlan leptris_plan_build(const leptris_plan_spec* spec,
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
                if (s->attribute_plans[a].wire_name) {
                    d->attribute_plans[a].wire_name =
                        dp_strdup(s->attribute_plans[a].wire_name);
                    if (!d->attribute_plans[a].wire_name) goto oom;
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
                if (sc->wire_name) {
                    d->child_plans[c].wire_name = dp_strdup(sc->wire_name);
                    if (!d->child_plans[c].wire_name) goto oom;
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

LEPTRIS_DESCRIPTOR_API void leptris_plan_free(LeptrisPlan plan) { dp_plan_free(plan); }

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

    for (uint32_t c = 0; c < plan->child_count; c++) {
        const leptris_child_plan* row = &plan->child_plans[c];
        if (!row->wire_name) continue;

        if (row->kind == LEPTRIS_PLAN_KIND_CONTENT) {
            /* Mixed-content text runs, document order. */
            if (!(plan->flags & LEPTRIS_PLAN_FLAG_MIXED_CONTENT)) continue;
            struct leptris_plan_result* coll =
                dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
            if (!coll) return 0;
            int want_cdata = (plan->flags & LEPTRIS_PLAN_FLAG_CDATA) != 0;
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
                if (!run || !dp_value_set_str(run, text, strlen(text)) ||
                    !dp_value_push(coll, run)) {
                    dp_result_free_rec(run);
                    dp_result_free_rec(coll);
                    return 0;
                }
            }
            if (!dp_value_push(out, coll)) {
                dp_result_free_rec(coll);
                return 0;
            }
            continue;
        }

        if (row->kind == LEPTRIS_PLAN_KIND_NESTED) {
            const dp_plan* target =
                &pool->plans[(uint32_t)row->child_plan_index];
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
                struct leptris_plan_result* v =
                    dp_walk_element(child, target, pool);
                if (!v) return 0;
                v->name = dp_strdup(row->wire_name);
                v->type_tag = row->type_tag;
                if (!v->name || !dp_value_push(out, v)) {
                    dp_result_free_rec(v);
                    return 0;
                }
            }
            continue;
        }

        /* SCALAR | COLLECTION | RAW | CALLBACK: bind matching child
         * elements by name; non-nested rows bind no-namespace
         * elements (NS_LENIENT relaxes). */
        int emitted = 0;
        for (LeptrisNodeRef n = leptris_node_first_child(
                 leptris_element_as_node(elem));
             n; n = leptris_node_next_sibling(n)) {
            if (leptris_node_get_type(n) != LEPTRIS_NODE_TYPE_ELEMENT)
                continue;
            LeptrisElement child = (LeptrisElement)n;
            const char* local = NULL;
            leptris_element_expanded_name(child, &local, NULL, NULL);
            if (!local || strcmp(local, row->wire_name) != 0) continue;
            if (!dp_ns_binds(child, LEPTRIS_PLAN_NS_NONE, NULL, lenient))
                continue;

            struct leptris_plan_result* v = NULL;
            if (row->kind == LEPTRIS_PLAN_KIND_RAW) {
                char* ser = leptris_element_serialize(child, NULL);
                if (!ser) return 0;
                v = dp_value_new(LEPTRIS_PLAN_VALUE_RAW);
                if (!v || !dp_value_set_str(v, ser, strlen(ser))) {
                    leptris_free_string(ser);
                    dp_result_free_rec(v);
                    return 0;
                }
                leptris_free_string(ser);
            } else if (row->kind == LEPTRIS_PLAN_KIND_CALLBACK) {
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_CALLBACK);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    return 0;
                }
                /* Parse-created nodes carry byteOffset+1 (the
                 * issue #223 lazy scheme); 0 = unknown. */
                v->position = leptris_node_byte_offset(n);
            } else {
                /* SCALAR item or COLLECTION item: text content. */
                const char* text = leptris_element_text(child);
                v = dp_value_new(LEPTRIS_PLAN_VALUE_SCALAR);
                if (!v ||
                    !dp_value_set_str(v, text ? text : "",
                                      text ? strlen(text) : 0)) {
                    dp_result_free_rec(v);
                    return 0;
                }
            }
            v->name = dp_strdup(row->wire_name);
            v->type_tag = row->type_tag;
            if (!v->name) {
                dp_result_free_rec(v);
                return 0;
            }

            if (row->kind == LEPTRIS_PLAN_KIND_COLLECTION) {
                /* First match materializes the collection; subsequent
                 * matches append. */
                if (!emitted) {
                    struct leptris_plan_result* coll =
                        dp_value_new(LEPTRIS_PLAN_VALUE_COLLECTION);
                    if (!coll || !dp_value_push(out, coll)) {
                        dp_result_free_rec(coll);
                        dp_result_free_rec(v);
                        return 0;
                    }
                    emitted = 1;
                }
                struct leptris_plan_result* coll =
                    out->kids[out->kid_count - 1];
                if (!dp_value_push(coll, v)) {
                    dp_result_free_rec(v);
                    return 0;
                }
            } else if (!dp_value_push(out, v)) {
                dp_result_free_rec(v);
                return 0;
            }
        }
    }
    return out;
}

LEPTRIS_DESCRIPTOR_API LeptrisPlanResult leptris_plan_walk(LeptrisDocument doc, LeptrisElement ctx,
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

LEPTRIS_DESCRIPTOR_API void leptris_plan_result_free(LeptrisPlanResult result) {
    dp_result_free_rec((struct leptris_plan_result*)result);
}

/* ---- accessors --------------------------------------------------- */

LEPTRIS_DESCRIPTOR_API LeptrisPlanValueKind leptris_plan_value_kind(const LeptrisPlanResult v) {
    return v ? v->kind : LEPTRIS_PLAN_VALUE_ELEMENT;
}

LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_name(const LeptrisPlanResult v) {
    return v ? v->name : NULL;
}

LEPTRIS_DESCRIPTOR_API uint8_t leptris_plan_value_type_tag(const LeptrisPlanResult v) {
    return v ? v->type_tag : 0;
}

LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_string(const LeptrisPlanResult v) {
    return v ? v->str : NULL;
}

LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_length(const LeptrisPlanResult v) {
    return v ? v->length : 0;
}

LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_position(const LeptrisPlanResult v) {
    return v ? v->position : 0;
}

LEPTRIS_DESCRIPTOR_API size_t leptris_plan_value_count(const LeptrisPlanResult v) {
    return v ? v->kid_count : 0;
}

LEPTRIS_DESCRIPTOR_API LeptrisPlanResult leptris_plan_value_at(const LeptrisPlanResult v, size_t i) {
    if (!v || i >= v->kid_count) return NULL;
    return v->kids[i];
}

LEPTRIS_DESCRIPTOR_API const char* leptris_plan_value_attribute(const LeptrisPlanResult v,
                                         const char* wire_name) {
    if (!v || !wire_name) return NULL;
    for (size_t i = 0; i < v->attr_count; i++)
        if (strcmp(v->attr_names[i], wire_name) == 0)
            return v->attr_values[i];
    return NULL;
}
