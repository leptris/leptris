/* evaluator_path.c - XPath path and predicate evaluation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Location path evaluation, predicates, node tests
 */

#include "evaluator_internal.h"
#include "../dom/pi.h"
#include "../dom/document_node.h"  /* LeptrisPINode for processing-instruction('target') */
#include "../leptris_internal.h"
#include "../dom/element.h"  /* For LeptrisElement structure */
#include <string.h>
#include <stdlib.h>  /* qsort (document-order merge, issue #485) */

/* Debug logging - Set to 0 to disable */
#define XPATH_DEBUG 0

#if XPATH_DEBUG
#define DEBUG_LOG(fmt, ...) fprintf(stderr, "[XPath DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) do {} while(0)
#endif

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Helper: Get element from typed node (returns NULL if not element)
 *
 * Every node begins with its type tag: real DOM nodes carry the public
 * LeptrisNodeKind (LeptrisNode.base.type), synthetic XPath nodes carry
 * LeptrisNodeType. In the unified tag space only 0 means element.
 */
static LeptrisElement node_as_element(void* node) {
    if (!node) return NULL;

    return (XPATH_NODE_TYPE(node) == LEPTRIS_NODE_ELEMENT)
        ? (LeptrisElement)node : NULL;
}

/* Helper: Get attribute node from typed node (returns NULL if not attribute) */
static LeptrisAttributeNode* node_as_attribute(void* node) {
    if (!node) return NULL;

    /* Check if first field is LEPTRIS_NODE_ATTRIBUTE */
    LeptrisNodeType first_field = *(LeptrisNodeType*)node;
    return (first_field == LEPTRIS_NODE_ATTRIBUTE) ? (LeptrisAttributeNode*)node : NULL;
}

/* ============================================================================
 * Node Test Matching
 * ============================================================================
 */

/* Helper: Parse node test name into prefix and local parts
 * Input: "ns1:element" → prefix="ns1", local="element"
 * Input: "element" → prefix=NULL, local="element"
 * Input: "ns1:*" → prefix="ns1", local="*"
 */
static void parse_node_test_name(const char* test_name,
                                   char** prefix,
                                   char** local) {
    if (!test_name) {
        *prefix = NULL;
        *local = NULL;
        return;
    }

    const char* colon = strchr(test_name, ':');
    if (colon) {
        size_t prefix_len = colon - test_name;
        *prefix = LEPTRIS_ALLOC_N(char, prefix_len + 1);
        if (*prefix) {
            memcpy(*prefix, test_name, prefix_len);
            (*prefix)[prefix_len] = '\0';
        }
        *local = leptris_strdup(colon + 1);
    } else {
        *prefix = NULL;
        *local = leptris_strdup(test_name);
    }
}

int matches_node_test(XPathContext* ctx, LeptrisNode* node, XPathASTNode* test) {
    if (!node || !test) return 1;  /* No test means match all */

    /* Name and wildcard tests only match element nodes (TODO 109). */
    if (node->type != LEPTRIS_NODE_TYPE_ELEMENT) {
        if (test->type == XPATH_AST_NODE_TEST_TYPE && test->value) {
            if (strcmp(test->value, "node") == 0) return 1;
            if (strcmp(test->value, "text") == 0)
                return node->type == LEPTRIS_NODE_TYPE_TEXT ||
                       node->type == LEPTRIS_NODE_TYPE_CDATA;
            if (strcmp(test->value, "comment") == 0)
                return node->type == LEPTRIS_NODE_TYPE_COMMENT;
            if (strcmp(test->value, "processing-instruction") == 0) {
                if (node->type != LEPTRIS_NODE_TYPE_PI) return 0;
                /* Optional target argument: processing-instruction('xml-stylesheet') */
                if (test->local_name) {
                    LeptrisPINode* pi = (LeptrisPINode*)node;
                    return pi->target && strcmp(pi->target, test->local_name) == 0;
                }
                return 1;
            }
        }
        return 0;
    }

    LeptrisElement elem = (LeptrisElement)node;

    switch (test->type) {
        case XPATH_AST_NODE_TEST_NAME: {
            /* Match specific name - namespace-aware */
            const char* node_name = leptris_element_get_name(elem);
            if (!test->value || !node_name) return 0;

            /* Fast path: No colon means no namespace prefix */
            const char* colon = strchr(test->value, ':');

            if (!colon) {
                /* Simple name match - no prefix in test */
                /* Issue #525 (XPath 1.0 §2.3): an unprefixed test
                 * matches only NO-namespace elements. A prefix-less
                 * element under a default xmlns IS namespaced —
                 * "no prefix" is the wrong proxy. */
                if (strcmp(node_name, test->value) != 0) return 0;
                if (!ctx || !ctx->document || !ctx->document->has_namespaces)
                    return 1;   /* namespace-free document: no gate */
                const char* uri = leptris_element_get_namespace_uri(elem);
                return !uri || !uri[0];
            }

            /* Has prefix - need namespace-aware matching */
            size_t prefix_len = colon - test->value;
            const char* test_local = colon + 1;

            /* URI-aware matching when the external bindings carry
             * the test prefix (XPointer xmlns()): p:e matches any e
             * in the bound namespace, including elements carrying
             * that namespace via a different prefix or the default
             * namespace. Unbound prefix falls through to the
             * historic literal-prefix comparison. */
            const char* test_uri = ctx
                ? leptris_xpath_ns_lookup(
                      (const struct leptris_xpath_ns_map*)ctx->ns_set,
                      test->value, prefix_len)
                : NULL;
            if (test_uri) {
                const char* node_uri = leptris_element_get_namespace_uri(elem);
                return node_uri && strcmp(node_uri, test_uri) == 0 &&
                       strcmp(node_name, test_local) == 0;
            }

            /* Get node prefix */
            const char* node_prefix = leptris_element_get_prefix(elem);
            if (!node_prefix) return 0;  /* Test has prefix, node doesn't */

            /* Match prefix (compare up to prefix_len) */
            if (strncmp(test->value, node_prefix, prefix_len) != 0 ||
                node_prefix[prefix_len] != '\0') {
                return 0;  /* Prefix mismatch or node prefix longer */
            }

            /* Match local name */
            return (strcmp(node_name, test_local) == 0);
        }

        case XPATH_AST_NODE_TEST_ALL: {
            /* prefix:* — namespace-scoped wildcard (the parser stores
             * the prefix in test->prefix with value "*"). This was
             * previously ignored: //t:* matched EVERY element. */
            if (test->prefix) {
                size_t prefix_len = strlen(test->prefix);
                const char* test_uri = ctx
                    ? leptris_xpath_ns_lookup(
                          (const struct leptris_xpath_ns_map*)ctx->ns_set,
                          test->prefix, prefix_len)
                    : NULL;
                if (test_uri) {
                    const char* node_uri =
                        leptris_element_get_namespace_uri(elem);
                    return node_uri && strcmp(node_uri, test_uri) == 0;
                }
                const char* node_prefix = leptris_element_get_prefix(elem);
                if (!node_prefix) return 0;
                return strncmp(test->prefix, node_prefix, prefix_len) == 0 &&
                       node_prefix[prefix_len] == '\0';
            }
            /* Wildcard - if test has prefix, match namespace */
            if (test->value) {
                /* Fast path: No colon means match all */
                const char* colon = strchr(test->value, ':');
                if (!colon) return 1;  /* Pure "*" matches all elements */

                /* Has prefix (e.g., "ns1:*") - match namespace */
                size_t prefix_len = colon - test->value;

                const char* test_uri = ctx
                    ? leptris_xpath_ns_lookup(
                          (const struct leptris_xpath_ns_map*)ctx->ns_set,
                          test->value, prefix_len)
                    : NULL;
                if (test_uri) {
                    const char* node_uri =
                        leptris_element_get_namespace_uri(elem);
                    return node_uri && strcmp(node_uri, test_uri) == 0;
                }

                const char* node_prefix = leptris_element_get_prefix(elem);

                if (!node_prefix) return 0;

                /* Match prefix */
                return (strncmp(test->value, node_prefix, prefix_len) == 0 &&
                        node_prefix[prefix_len] == '\0');
            }
            /* No prefix - match all elements */
            return 1;
        }

        case XPATH_AST_NODE_TEST_TYPE:
            /* Node type tests (node(), text(), comment(), etc.) */
            if (test->value) {
                if (strcmp(test->value, "node") == 0) return 1;
                /* text()/comment()/processing-instruction() never
                 * match elements — the non-element branch above tests
                 * real text/cdata/comment/pi nodes. The legacy
                 * "text() matches elements with text content" rule
                 * double-counted //text() (element + its text node). */
                if (strcmp(test->value, "text") == 0) return 0;
                if (strcmp(test->value, "comment") == 0) return 0;
                if (strcmp(test->value, "processing-instruction") == 0) return 0;
            }
            return 0;

        default:
            return 0;
    }
}

/* ============================================================================
 * Predicate Evaluation
 * ============================================================================ */

/* Helper: Evaluate a single predicate for a node in-place
 * Returns 1 if node matches predicate, 0 otherwise
 */
static int evaluate_predicate_for_node(XPathContext* ctx,
                                       void* node,
                                       XPathASTNode* predicate,
                                       size_t proximity_position,
                                       size_t context_size) {
    /* For predicates, we need element context - attributes predicate on their owner */
    LeptrisElement context_elem = node_as_element(node);
    if (!context_elem) {
        LeptrisAttributeNode* attr_node = node_as_attribute(node);
        if (attr_node) {
            context_elem = attr_node->owner;
        }
    }

    if (!context_elem) {
        return 0; /* Skip if no valid context */
    }

    /* Save context */
    LeptrisElement old_node = ctx->context_node;
    size_t old_pos = ctx->context_position;
    size_t old_size = ctx->context_size;
    void* old_predicate_node = ctx->current_predicate_node;

    /* Set context ONCE for this evaluation
     * IMPORTANT: context_node should be the actual node being tested (element or attribute)
     * This allows functions like name() to work correctly on attributes */
    ctx->context_node = (LeptrisElement)node;  /* Use the actual node, not just element */
    ctx->context_position = proximity_position;  /* 1-based position in candidate set */
    ctx->context_size = context_size;
    ctx->current_predicate_node = node;  /* The actual node (can be attribute) */

    /* Evaluate predicate */
    struct leptris_xpath_result* pred_result = evaluate_expr(ctx, predicate);
    int matches = 0;

    if (pred_result) {
        /* Numeric predicate: matches position */
        if (pred_result->type == XPATH_RESULT_NUMBER) {
            if ((size_t)pred_result->value.number_value == proximity_position) {
                matches = 1;
            }
        }
        /* Boolean predicate */
        else if (xpath_to_boolean(pred_result)) {
            matches = 1;
        }
        xpath_result_free(pred_result);
    }

    /* Restore context */
    ctx->context_node = old_node;
    ctx->context_position = old_pos;
    ctx->context_size = old_size;
    ctx->current_predicate_node = old_predicate_node;

    return matches;
}

/* Apply predicates using in-place filtering (libxml2 algorithm)
 * This is 100-200x faster than creating new arrays
 *
 * Algorithm:
 *   - Use two-pointer technique (read/write positions)
 *   - Track matched_position separately from iteration
 *   - Set contextSize once, not per evaluation
 *   - Early termination for position predicates
 */
XPathNodeSet* apply_predicates(XPathContext* ctx, XPathNodeSet* nodes,
                                XPathASTNode** predicates, size_t pred_count) {
    if (!nodes || pred_count == 0) return nodes;

    DEBUG_LOG("    === apply_predicates: pred_count=%zu, nodeset size=%zu ===",
             pred_count, xpath_nodeset_count(nodes));
    /* Apply each predicate in sequence, filtering in-place */
    for (size_t p = 0; p < pred_count; p++) {
        DEBUG_LOG("      Processing predicate %zu", p);

        size_t current_size = xpath_nodeset_count(nodes);
        if (current_size == 0) {
            break;  /* No nodes left to filter */
        }

        /* Two-pointer in-place filtering algorithm */
        size_t read_pos = 0;      /* Reading from here */
        size_t write_pos = 0;     /* Writing to here */

        DEBUG_LOG("      Filtering %zu nodes", current_size);

        /* Process all candidates */
        for (read_pos = 0; read_pos < current_size; read_pos++) {
            void* node = xpath_nodeset_get(nodes, read_pos);
            size_t proximity_position = read_pos + 1;  /* 1-based position in candidate set */

            DEBUG_LOG("        Node[%zu]: proximity_pos=%zu",
                     read_pos, proximity_position);

            /* Evaluate predicate for this node */
            int matches = evaluate_predicate_for_node(ctx, node, predicates[p],
                                                     proximity_position, current_size);

            if (matches) {
                DEBUG_LOG("          MATCH! Writing to pos %zu", write_pos);

                /* Keep this node - move it to write position if needed */
                if (read_pos != write_pos) {
                    /* Move node pointer from read to write position */
                    nodes->nodes[write_pos] = node;
                }
                write_pos++;

                /* Early termination optimization:
                 * If predicate is position-based and we've found enough matches, stop
                 * Example: [position() < 3] stops after finding 2 matches
                 *
                 * TODO: Implement maxPos detection from predicate AST
                 * For now, continue to ensure correctness
                 */
            } else {
                DEBUG_LOG("          NO MATCH, skipping");
                /* Don't increment write_pos - effectively deletes this node */
            }
        }

        /* Update nodeset size to reflect filtered results */
        nodes->count = write_pos;

        DEBUG_LOG("      After predicate %zu: %zu nodes remain", p, write_pos);
    }

    DEBUG_LOG("    === apply_predicates END: result size=%zu ===",
             xpath_nodeset_count(nodes));
    return nodes;
}

/* ============================================================================
 * Path Expression Evaluation
 * ============================================================================ */

/* ---- Document-order rank sort (issue #485) ------------------------------
 *
 * Comparing two nodes' document order directly costs O(depth +
 * siblings) — the sibling-chain walk makes a qsort over a wide nodeset
 * quadratic in practice (e.g. //text() on a 20k-child document).
 * Instead we assign every node an integer rank with one preorder walk
 * of the covering subtrees and sort by rank: O(tree) + O(n log n)
 * integer comparisons.
 *
 * Walked nodes get rank 4*seq; a synthetic namespace/attribute/
 * xpath-text node owned by a walked element ranks at 4*seq+1/+2/+3 —
 * between the element and its first child, per XPath 1.0 document
 * order. Nodes outside the walked region (doctype, cross-document)
 * sort last, pointer-ordered, for determinism.
 * ------------------------------------------------------------------------ */

typedef struct {
    void** keys;
    int64_t* ranks;
    size_t count;
    size_t mask;  /* capacity - 1, power of two */
    unsigned built_version;  /* document mutation_version at build */
} DocOrderRankTable;

static int doc_rank_table_init(DocOrderRankTable* t, size_t expected) {
    size_t cap = 16;
    while (cap < expected * 2) cap <<= 1;
    t->keys = (void**)calloc(cap, sizeof(void*));
    t->ranks = (int64_t*)malloc(cap * sizeof(int64_t));
    if (!t->keys || !t->ranks) {
        free(t->keys);
        free(t->ranks);
        t->keys = NULL;
        t->ranks = NULL;
        return 0;
    }
    t->count = 0;
    t->mask = cap - 1;
    return 1;
}

static void doc_rank_table_free(DocOrderRankTable* t) {
    free(t->keys);
    free(t->ranks);
}

/* Grow at 70% load so probe chains stay short. */
static int doc_rank_table_grow(DocOrderRankTable* t) {
    size_t new_cap = (t->mask + 1) << 1;
    void** new_keys = (void**)calloc(new_cap, sizeof(void*));
    int64_t* new_ranks = (int64_t*)malloc(new_cap * sizeof(int64_t));
    if (!new_keys || !new_ranks) {
        free(new_keys);
        free(new_ranks);
        return 0;
    }
    size_t new_mask = new_cap - 1;
    for (size_t i = 0; i <= t->mask; i++) {
        if (!t->keys[i]) continue;
        size_t j = (size_t)(((uintptr_t)t->keys[i] >> 4) & new_mask);
        while (new_keys[j]) j = (j + 1) & new_mask;
        new_keys[j] = t->keys[i];
        new_ranks[j] = t->ranks[i];
    }
    free(t->keys);
    free(t->ranks);
    t->keys = new_keys;
    t->ranks = new_ranks;
    t->mask = new_mask;
    return 1;
}

static int doc_rank_table_put(DocOrderRankTable* t, void* key, int64_t rank) {
    if ((t->count + 1) * 10 >= (t->mask + 1) * 7) {
        if (!doc_rank_table_grow(t)) return 0;
    }
    size_t i = (size_t)(((uintptr_t)key >> 4) & t->mask);
    while (t->keys[i]) {
        if (t->keys[i] == key) return 1;  /* first visit wins */
        i = (i + 1) & t->mask;
    }
    t->keys[i] = key;
    t->ranks[i] = rank;
    t->count++;
    return 1;
}

static int64_t doc_rank_table_get(const DocOrderRankTable* t, void* key) {
    size_t i = (size_t)(((uintptr_t)key >> 4) & t->mask);
    while (t->keys[i]) {
        if (t->keys[i] == key) return t->ranks[i];
        i = (i + 1) & t->mask;
    }
    return INT64_MAX;
}

/* Preorder walk of elem's subtree, ranking every node. */
static void doc_rank_walk(DocOrderRankTable* t, LeptrisElement elem,
                          int64_t* seq) {
    if (!elem) return;
    doc_rank_table_put(t, elem, (*seq)++ * 4);
    LeptrisNode* child = leptris_elem_first_child(elem);
    while (child) {
        if (child->type == LEPTRIS_NODE_TYPE_ELEMENT) {
            doc_rank_walk(t, (LeptrisElement)child, seq);
        } else {
            doc_rank_table_put(t, child, (*seq)++ * 4);
        }
        child = leptris_node_get_next_sibling(child);
    }
}

/* Free a cached rank table (called from document teardown). */
void leptris_doc_order_index_free(void* table) {
    if (!table) return;
    doc_rank_table_free((DocOrderRankTable*)table);
    free(table);
}

/* Sort ns in document order (descending if reverse). Returns 0 on
 * success, -1 on allocation failure (ns left unsorted — callers treat
 * order as best-effort for exotic nodes).
 *
 * The rank table is cached on the document and keyed on
 * mutation_version so repeated queries don't rewalk the tree
 * (issue #485: a per-call walk made union-heavy benchmark loops
 * quadratic). */
int xpath_nodeset_sort_doc_order(XPathContext* ctx, XPathNodeSet* ns,
                                 int reverse) {
    if (!ns || ns->count < 2) return 0;
    if (!ctx || !ctx->document || !ctx->document->new_dom_root) return 0;

    DocOrderRankTable* t = (DocOrderRankTable*)ctx->document->doc_order_index;
    if (t && t->built_version != ctx->document->mutation_version) {
        leptris_doc_order_index_free(t);
        t = NULL;
        ctx->document->doc_order_index = NULL;
    }
    if (!t) {
        t = (DocOrderRankTable*)malloc(sizeof(DocOrderRankTable));
        if (!t) return -1;
        if (!doc_rank_table_init(t, 256)) {
            free(t);
            return -1;
        }
        int64_t seq = 0;
        doc_rank_walk(t, (LeptrisElement)ctx->document->new_dom_root, &seq);
        t->built_version = ctx->document->mutation_version;
        ctx->document->doc_order_index = t;
    }
    const DocOrderRankTable ct = *t;

    /* Rank every entry: synthetic nodes via their owner's rank. */
    size_t n = ns->count;
    int64_t* rank = (int64_t*)malloc(n * sizeof(int64_t));
    if (!rank) {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        void* node = ns->nodes[i];
        int64_t r = doc_rank_table_get(&ct, node);
        if (r == INT64_MAX) {
            int tag = (int)XPATH_NODE_TYPE(node);
            if (tag == LEPTRIS_NODE_ATTRIBUTE) {
                int64_t o = doc_rank_table_get(
                    &ct, ((LeptrisAttributeNode*)node)->owner);
                r = (o == INT64_MAX) ? INT64_MAX : o + 2;
            } else if (tag == LEPTRIS_NODE_NAMESPACE) {
                int64_t o = doc_rank_table_get(
                    &ct, ((LeptrisNamespaceNode*)node)->owner);
                r = (o == INT64_MAX) ? INT64_MAX : o + 1;
            } else if (tag == LEPTRIS_NODE_TEXT) {
                int64_t o = doc_rank_table_get(
                    &ct, ((XPathTextNode*)node)->owner);
                r = (o == INT64_MAX) ? INT64_MAX : o + 3;
            } else if (r == INT64_MAX) {
                /* Outside the tree (doctype, cross-document): last,
                 * pointer-ordered among themselves. */
                r = INT64_MAX - (int64_t)(((uintptr_t)node >> 4) & 0x3FFFFFFF);
            }
        }
        rank[i] = r;
    }

    /* Stable bottom-up merge sort on an index array keyed by rank
     * (qsort_r is not portable C99; plain qsort cannot see the rank
     * array). O(n log n) int64 compares. */
    size_t* idx = (size_t*)malloc(n * sizeof(size_t));
    if (!idx) {
        free(rank);
        return -1;
    }
    for (size_t i = 0; i < n; i++) idx[i] = i;
    {
        /* Bottom-up merge sort, stable, O(n log n) compares on int64. */
        size_t* tmp = (size_t*)malloc(n * sizeof(size_t));
        if (!tmp) {
            free(idx);
            free(rank);
            return -1;
        }
        for (size_t width = 1; width < n; width <<= 1) {
            for (size_t lo = 0; lo < n; lo += width << 1) {
                size_t mid = lo + width < n ? lo + width : n;
                size_t hi = lo + (width << 1) < n ? lo + (width << 1) : n;
                size_t a = lo, b = mid, k = lo;
                while (a < mid && b < hi)
                    tmp[k++] = (rank[idx[a]] <= rank[idx[b]]) ? idx[a++] : idx[b++];
                while (a < mid) tmp[k++] = idx[a++];
                while (b < hi) tmp[k++] = idx[b++];
            }
            memcpy(idx, tmp, n * sizeof(size_t));
        }
        free(tmp);
    }

    void** sorted = (void**)malloc(n * sizeof(void*));
    if (!sorted) {
        free(idx);
        free(rank);
        return -1;
    }
    if (!reverse) {
        for (size_t i = 0; i < n; i++) sorted[i] = ns->nodes[idx[i]];
    } else {
        for (size_t i = 0; i < n; i++) sorted[n - 1 - i] = ns->nodes[idx[i]];
    }
    memcpy(ns->nodes, sorted, n * sizeof(void*));

    /* Sort groups duplicate pointers adjacently — compact them in one
     * pass. O(n) after the O(n log n) sort, versus the O(n^2) linear
     * duplicate scan the union operator used to run per merge. */
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (i == 0 || ns->nodes[i] != ns->nodes[i - 1]) {
            ns->nodes[w++] = ns->nodes[i];
        }
    }
    ns->count = w;

    free(sorted);
    free(idx);
    free(rank);
    return 0;
}

/* Shared tail of evaluate_step's per-input loop: apply the step's
 * predicates to one input's axis result, then merge into the step
 * result. The document-node branch and the element branch MUST flow
 * through here together — a doc-branch shortcut that skipped
 * predicates silently dropped them (the double-wildcard-with-
 * predicate query, bug-16- follow-up). */
static void merge_step_axis_result(XPathContext* ctx,
                                   XPathNodeSet* result,
                                   XPathNodeSet* axis_result,
                                   const char* axis_name,
                                   XPathASTNode* step) {
    if (!axis_result) return;
    if (step->child_count > 1)
        apply_predicates(ctx, axis_result,
                         &step->children[1], step->child_count - 1);

    /* Ownership is determined by axis type, not by node kind: sets
     * from the attribute/namespace axes carry synthetic nodes the
     * result must own (frees only tag-matched entries). */
    if (strcmp(axis_name, "attribute") == 0)
        result->owns_attributes = 1;
    else if (strcmp(axis_name, "namespace") == 0)
        result->owns_namespaces = 1;

    for (size_t j = 0; j < xpath_nodeset_count(axis_result); j++)
        xpath_nodeset_add(result, xpath_nodeset_get(axis_result, j));

    axis_result->owns_attributes = 0;
    axis_result->owns_namespaces = 0;
    xpath_nodeset_free(axis_result);
}

struct leptris_xpath_result* evaluate_step(XPathContext* ctx,
                                          XPathASTNode* step,
                                          XPathNodeSet* input) {
    DEBUG_LOG("  === evaluate_step START ===");
    if (!step || step->type != XPATH_AST_STEP || !input) {
        DEBUG_LOG("    Invalid parameters: step=%p, type=%d, input=%p",
                 (void*)step, step ? step->type : -1, (void*)input);
        return NULL;
    }

    const char* axis_name = step->value ? step->value : "child";
    XPathASTNode* node_test = (step->child_count > 0) ? step->children[0] : NULL;
    DEBUG_LOG("    axis_name = %s", axis_name);
    DEBUG_LOG("    node_test = %p (type=%d)", (void*)node_test, node_test ? node_test->type : -1);
    if (node_test && node_test->value) {
        DEBUG_LOG("    node_test->value = %s", node_test->value);
    }
    DEBUG_LOG("    input nodeset count = %zu", xpath_nodeset_count(input));

    XPathNodeSet* result = xpath_nodeset_new();
    if (!result) {
        DEBUG_LOG("    FAILED to create result nodeset");
        return NULL;
    }

    /* Apply axis to each input node (must be elements) */
    for (size_t i = 0; i < xpath_nodeset_count(input); i++) {
        void* node_ptr = xpath_nodeset_get(input, i);
        LeptrisElement node = node_as_element(node_ptr);
        DEBUG_LOG("    Processing input[%zu]: node=%p", i, (void*)node);

        /* Document-node contexts (XSLT "/" initial context):
         * child = [root element]; self = the document itself;
         * descendant(-or-self) = the root subtree; parent/
         * ancestor = nothing. Results flow through the SAME
         * predicate + merge tail as element inputs. */
        if (node_ptr && ((LeptrisNode*)node_ptr)->type ==
                             LEPTRIS_NODE_TYPE_DOCUMENT) {
            struct leptris_document* dd =
                ((LeptrisDocumentNode*)node_ptr)->doc;
            LeptrisElement doc_root = (LeptrisElement)dd->new_dom_root;
            if (!doc_root) doc_root = dd->root;
            XPathNodeSet* axis_result = xpath_nodeset_new();
            if (!axis_result) continue;
            if (strcmp(axis_name, "child") == 0) {
                for (LeptrisNode* c = (LeptrisNode*)doc_root; c;
                     c = leptris_node_get_next_sibling(c))
                    if (matches_node_test(ctx, c, node_test))
                        xpath_nodeset_add(axis_result, c);
            } else if (strcmp(axis_name, "self") == 0) {
                if (matches_node_test(ctx, (LeptrisNode*)node_ptr,
                                      node_test))
                    xpath_nodeset_add(axis_result,
                                      (LeptrisNode*)node_ptr);
            } else if (strcmp(axis_name, "descendant") == 0 ||
                       strcmp(axis_name, "descendant-or-self") == 0) {
                /* bug-16-: two document-node rules. (1) '-' sits at
                 * index 10 of "descendant-or-self", not 11 — the
                 * self-add never fired, so //NAME's first step lost
                 * the document node. (2) The root ELEMENT is itself
                 * a descendant of the document: from here the root
                 * subtree walks with -or-self semantics either way. */
                int or_self = axis_name[10] == '-';
                if (or_self &&
                    matches_node_test(ctx, (LeptrisNode*)node_ptr,
                                      node_test))
                    xpath_nodeset_add(axis_result,
                                      (LeptrisNode*)node_ptr);
                if (doc_root) {
                    XPathNodeSet* sub = apply_axis(
                        ctx, (LeptrisNode*)doc_root,
                        "descendant-or-self", node_test);
                    if (sub) {
                        for (size_t j = 0;
                             j < xpath_nodeset_count(sub); j++)
                            xpath_nodeset_add(
                                axis_result,
                                xpath_nodeset_get(sub, j));
                        xpath_nodeset_free(sub);
                    }
                }
            }
            /* parent/ancestor/etc. from the document: empty */
            merge_step_axis_result(ctx, result, axis_result,
                                   axis_name, step);
            continue;
        }

        /* Handle attribute nodes for certain axes */
        if (!node) {
            LeptrisAttributeNode* attr_node = node_as_attribute(node_ptr);
            if (attr_node) {
                DEBUG_LOG("      Input is an attribute node, name=%s", attr_node->name);

                /* Special case: parent axis from attribute should return owner directly */
                if (strcmp(axis_name, "parent") == 0) {
                    if (attr_node->owner && matches_node_test(ctx, (LeptrisNode*)attr_node->owner, node_test)) {
                        xpath_nodeset_add(result, (LeptrisNode*)attr_node->owner);
                    }
                    continue;  /* Skip normal axis processing */
                }

                /* ancestor(-or-self) from an attribute walks the
                 * owner's chain; self::node() on an attribute is the
                 * ATTRIBUTE itself (XPath §2.2 — attributes are
                 * nodes in their own right). */
                if (strcmp(axis_name, "self") == 0) {
                    if (matches_node_test(ctx, (LeptrisNode*)node_ptr,
                                          node_test))
                        xpath_nodeset_add(result, node_ptr);
                    continue;
                }
                if (strcmp(axis_name, "ancestor") == 0 ||
                    strcmp(axis_name, "ancestor-or-self") == 0) {

                    /* Use the owner element as context */
                    node = attr_node->owner;
                    DEBUG_LOG("      Using owner element as context: %p (name=%s)",
                             (void*)node, node ? leptris_element_get_name(node) : "(null)");
                } else {
                    DEBUG_LOG("      Axis '%s' cannot operate on attribute nodes, skipping", axis_name);
                    continue;  /* Skip non-element nodes for other axes */
                }
            } else if (node_ptr && ((LeptrisNode*)node_ptr)->type !=
                       LEPTRIS_NODE_TYPE_ATTRIBUTE) {
                /* Real DOM non-element (text/comment/cdata/pi):
                 * self and parent still work; other axes are empty
                 * from these nodes. */
                if (strcmp(axis_name, "self") == 0) {
                    if (matches_node_test(ctx, (LeptrisNode*)node_ptr,
                                          node_test))
                        xpath_nodeset_add(result, (LeptrisNode*)node_ptr);
                } else if (strcmp(axis_name, "parent") == 0) {
                    /* leptris_node_parent returns LeptrisElement —
                     * musl GCC rejects the mismatched store (#582). */
                    LeptrisElement up = leptris_node_parent(
                        (LeptrisNodeRef)node_ptr);
                    if (up &&
                        matches_node_test(ctx, (LeptrisNode*)up, node_test))
                        xpath_nodeset_add(result, (LeptrisNode*)up);
                }
                continue;
            } else {
                DEBUG_LOG("      Skipping non-element, non-attribute node");
                continue; /* Skip non-element, non-attribute nodes */
            }
        }

        if (!node) {
            DEBUG_LOG("      No valid node context, skipping");
            continue;
        }

        DEBUG_LOG("      node->name = %s",
                 leptris_element_get_name(node) ? leptris_element_get_name(node) : "(null)");

        XPathNodeSet* axis_result = apply_axis(ctx, (LeptrisNode*)node, axis_name, node_test);
        DEBUG_LOG("      axis_result count = %zu", axis_result ? xpath_nodeset_count(axis_result) : 0);

        merge_step_axis_result(ctx, result, axis_result,
                               axis_name, step);
    }

    DEBUG_LOG("    Final result count = %zu", xpath_nodeset_count(result));

    /* Multi-context steps interleave: children of an early context
     * node must precede a later context node even when they were
     * appended after it (issue #485). Sort the merged result into
     * document order — reverse axes descending, per XPath 1.0.
     * Single-context steps come out of the axis walks already
     * ordered. */
    if (xpath_nodeset_count(input) > 1 && result->count > 1) {
        int reverse = (strcmp(axis_name, "ancestor") == 0 ||
                       strcmp(axis_name, "ancestor-or-self") == 0 ||
                       strcmp(axis_name, "preceding") == 0 ||
                       strcmp(axis_name, "preceding-sibling") == 0);
        xpath_nodeset_sort_doc_order(ctx, result, reverse);
    }

    DEBUG_LOG("  === evaluate_step END ===");

    struct leptris_xpath_result* res = xpath_result_new(XPATH_RESULT_NODESET);
    if (res) res->value.nodeset_value = result;
    return res;
}

struct leptris_xpath_result* evaluate_location_path(XPathContext* ctx,
                                                    XPathASTNode* path) {
    XPathNodeSet* current = xpath_nodeset_new();
    if (!current) return NULL;

    /* PATH_EXPR: a filter-expression head feeding steps —
     * document('')//x, $v/a, key('k','v')/b, (@attr)/.. — the head
     * result is the starting nodeset. Previously the head was
     * IGNORED and the steps ran from the context node (every
     * foreign-document and variable path silently misresolved). */
    if (path->type == XPATH_AST_PATH_EXPR && path->child_count >= 1 &&
        path->children[0]->type != XPATH_AST_STEP &&
        path->children[0]->type != XPATH_AST_RELATIVE_PATH) {
        struct leptris_xpath_result* head =
            evaluate_expr(ctx, path->children[0]);
        if (!head) {
            xpath_nodeset_free(current);
            return NULL;
        }
        if (head->type != XPATH_RESULT_NODESET) {
            /* A non-nodeset head: per XPath 1.0 the path is applied
             * to the head's VALUE as the context (rare; treat as
             * empty — the common forms are nodeset heads). */
            xpath_result_free(head);
            xpath_nodeset_free(current);
            return xpath_result_new(XPATH_RESULT_NODESET);
        }
        if (head->value.nodeset_value) {
            for (size_t i = 0; i < head->value.nodeset_value->count; i++)
                xpath_nodeset_add(current,
                                  head->value.nodeset_value->nodes[i]);
        }
        xpath_result_free(head);
        /* Process remaining children as steps. */
        for (size_t i = 1; i < path->child_count; i++) {
            XPathASTNode* child = path->children[i];
            if (child->type == XPATH_AST_STEP) {
                struct leptris_xpath_result* step_result =
                    evaluate_step(ctx, child, current);
                if (!step_result) {
                    xpath_nodeset_free(current);
                    return NULL;
                }
                xpath_nodeset_free(current);
                current = step_result->value.nodeset_value;
                step_result->value.nodeset_value = NULL;
                xpath_result_free(step_result);
            } else if (child->type == XPATH_AST_RELATIVE_PATH) {
                for (size_t j = 0; j < child->child_count; j++) {
                    XPathASTNode* step = child->children[j];
                    struct leptris_xpath_result* step_result =
                        evaluate_step(ctx, step, current);
                    if (!step_result) {
                        xpath_nodeset_free(current);
                        return NULL;
                    }
                    xpath_nodeset_free(current);
                    current = step_result->value.nodeset_value;
                    step_result->value.nodeset_value = NULL;
                    xpath_result_free(step_result);
                }
            }
        }
        struct leptris_xpath_result* result =
            xpath_result_new(XPATH_RESULT_NODESET);
        if (result) result->value.nodeset_value = current;
        else xpath_nodeset_free(current);
        return result;
    }

    /* Starting nodeset */
    if (path->type == XPATH_AST_ABSOLUTE_PATH) {
        /* Special case: Absolute path with element name as first step
         * XPath "/root" means "child of document node named root"
         * Since we don't have a document node, check if root matches and use it */
        int is_root_match = 0;
        LeptrisElement root = (LeptrisElement)ctx->document->new_dom_root;

        DEBUG_LOG("  Checking for special case: child_count=%zu, root=%p",
                 (size_t)path->child_count, (void*)root);

        if (path->child_count > 0 && root) {
            const char* root_name = leptris_element_get_name(root);
            if (!root_name) {
                /* No name means something is wrong, skip special case */
                goto normal_absolute_path;
            }

            XPathASTNode* first_child = path->children[0];

            DEBUG_LOG("  First child type=%d (RELATIVE_PATH=%d, STEP=%d)",
                     first_child->type, XPATH_AST_RELATIVE_PATH, XPATH_AST_STEP);

            /* The first child might be RELATIVE_PATH containing steps, or a direct STEP */
            XPathASTNode* first_step = NULL;
            if (first_child->type == XPATH_AST_RELATIVE_PATH && first_child->child_count > 0) {
                first_step = first_child->children[0];
                DEBUG_LOG("  Found RELATIVE_PATH, extracting first step");
            } else if (first_child->type == XPATH_AST_STEP) {
                first_step = first_child;
                DEBUG_LOG("  Found direct STEP");
            }

            /* Check if first step is a simple child axis with element name */
            if (first_step && first_step->type == XPATH_AST_STEP) {
                const char* axis = first_step->value ? first_step->value : "child";
                DEBUG_LOG("  Axis=%s, child_count=%zu", axis, (size_t)first_step->child_count);

                if (strcmp(axis, "child") == 0 && first_step->child_count > 0) {
                    XPathASTNode* node_test = first_step->children[0];
                    DEBUG_LOG("  Node test type=%d, value=%s",
                             node_test->type, node_test->value ? node_test->value : "(null)");

                    if (node_test->type == XPATH_AST_NODE_TEST_NAME && node_test->value) {
                        /* Parse test value to get local name (in case it has prefix) */
                        char* test_prefix = NULL;
                        char* test_local = NULL;
                        parse_node_test_name(node_test->value, &test_prefix, &test_local);

                        if (test_local) {
                            /* root_name is already the local name (split-name architecture) */
                            const char* root_local = root_name;

                            DEBUG_LOG("  Comparing root_local='%s' with test_local='%s'",
                                     root_local, test_local);

                            /* Check if root element name matches */
                            int names_match = (strcmp(root_local, test_local) == 0);

                            /* If test has prefix: resolve through the
                             * context bindings — the root may carry the
                             * namespace under a different prefix (or the
                             * default). Unbound prefix falls back to the
                             * literal comparison. */
                            if (names_match && test_prefix) {
                                const char* test_uri = ctx
                                    ? leptris_xpath_ns_lookup(
                                          (const struct leptris_xpath_ns_map*)ctx->ns_set,
                                          test_prefix, strlen(test_prefix))
                                    : NULL;
                                if (test_uri) {
                                    const char* root_uri =
                                        leptris_element_get_namespace_uri(root);
                                    names_match = root_uri &&
                                        strcmp(root_uri, test_uri) == 0;
                                } else {
                                    const char* root_prefix =
                                        leptris_element_get_prefix(root);
                                    names_match = (root_prefix &&
                                        strcmp(root_prefix, test_prefix) == 0);
                                }
                            }

                            if (names_match) {
                                is_root_match = 1;
                                DEBUG_LOG("  ✓ Special case: /root matches document root");
                            }

                            LEPTRIS_FREE(test_local);
                            if (test_prefix) LEPTRIS_FREE(test_prefix);
                        }
                    }
                }
            }
        }

normal_absolute_path:
        DEBUG_LOG("  is_root_match=%d", is_root_match);

        if (is_root_match) {
            /* Root matches - add it and process remaining steps */
            xpath_nodeset_add(current, root);
            DEBUG_LOG("  Added root to nodeset, processing remaining steps");

            /* Get the RELATIVE_PATH (first child of ABSOLUTE_PATH) */
            XPathASTNode* rel_path = path->children[0];
            if (rel_path && rel_path->type == XPATH_AST_RELATIVE_PATH && rel_path->child_count > 1) {
                /* Process steps starting from index 1 (skip first step which matched root) */
                for (size_t j = 1; j < rel_path->child_count; j++) {
                    XPathASTNode* step = rel_path->children[j];
                    if (step->type == XPATH_AST_STEP) {
                        DEBUG_LOG("    Processing remaining step %zu", j);
                        struct leptris_xpath_result* step_result = evaluate_step(ctx, step, current);
                        if (!step_result) {
                            xpath_nodeset_free(current);
                            return NULL;
                        }
                        xpath_nodeset_free(current);
                        current = step_result->value.nodeset_value;
                        step_result->value.nodeset_value = NULL;
                        xpath_result_free(step_result);
                    }
                }
            }
            /* If rel_path has only 1 child (the step that matched), we're done - just return root */
        } else {
            /* Normal absolute path - start from root and process ALL steps */
            DEBUG_LOG("  Adding root to initial nodeset for absolute path");

            // Special case: /* (child::* from document node) should return root element directly.
            // In XPath, the document node has one child: the root element.
            // So /* means "select all children of document node" which is just the root element.
            int is_wildcard_only = (path->child_count == 1);
            if (is_wildcard_only) {
                XPathASTNode* first_child = path->children[0];
                /* Check if it's a RELATIVE_PATH with single STEP that is child::* */
                if (first_child->type == XPATH_AST_RELATIVE_PATH && first_child->child_count == 1) {
                    XPathASTNode* step = first_child->children[0];
                    if (step->type == XPATH_AST_STEP) {
                        const char* axis = step->value ? step->value : "child";
                        if (strcmp(axis, "child") == 0 && step->child_count > 0) {
                            XPathASTNode* node_test =step->children[0];
                            if (node_test->type == XPATH_AST_NODE_TEST_ALL) {
                                /* This is / * - just return root element (space so the
                                 * slash-star sequence doesn't look like a nested comment). */
                                DEBUG_LOG("  Special case: /* returns document root element");
                                xpath_nodeset_add(current, (LeptrisElement)ctx->document->new_dom_root);
                                goto done_evaluating;
                            }
                        }
                    }
                }
            }

            /* Seed the DOCUMENT node, not the root element (bug-16-):
             * an absolute path's first step runs against the document
             * — child:: yields the root element, descendant(-or-self)
             * must offer the root element itself. The doc-branch in
             * apply_axis handles every axis from this node type. */
            xpath_nodeset_add(current,
                (LeptrisElement)leptris_document_get_node(ctx->document));
            DEBUG_LOG("  Nodeset count after adding document node: %zu",
                      xpath_nodeset_count(current));

            /* Process steps - handle both direct steps and those in RELATIVE_PATH */
            DEBUG_LOG("  Processing %zu children", (size_t)path->child_count);
            for (size_t i = 0; i < path->child_count; i++) {
                XPathASTNode* child = path->children[i];
                DEBUG_LOG("  Child[%zu]: type=%d", i, child->type);

                if (child->type == XPATH_AST_STEP) {
                    struct leptris_xpath_result* step_result = evaluate_step(ctx, child, current);
                    if (!step_result) {
                        DEBUG_LOG("    STEP evaluation FAILED");
                        xpath_nodeset_free(current);
                        return NULL;
                    }
                    DEBUG_LOG("    STEP result nodeset count: %zu",
                             xpath_nodeset_count(step_result->value.nodeset_value));

                    xpath_nodeset_free(current);
                    current = step_result->value.nodeset_value;
                    step_result->value.nodeset_value = NULL;
                    xpath_result_free(step_result);
                }
                else if (child->type == XPATH_AST_RELATIVE_PATH) {
                    /* RELATIVE_PATH container - process its step children */
                    for (size_t j = 0; j < child->child_count; j++) {
                        XPathASTNode* step = child->children[j];

                        if (step->type == XPATH_AST_STEP) {
                            struct leptris_xpath_result* step_result = evaluate_step(ctx, step, current);
                            if (!step_result) {
                                xpath_nodeset_free(current);
                                return NULL;
                            }

                            xpath_nodeset_free(current);
                            current = step_result->value.nodeset_value;
                            step_result->value.nodeset_value = NULL;
                            xpath_result_free(step_result);
                        }
                    }
                }
            }
        }
    }  /* End of if (path->type == XPATH_AST_ABSOLUTE_PATH) */
    else {
        /* Relative path - start from context node */
        xpath_nodeset_add(current, ctx->context_node);

        /* Process steps - handle both direct steps and those in RELATIVE_PATH */
        for (size_t i = 0; i < path->child_count; i++) {
            XPathASTNode* child = path->children[i];

            if (child->type == XPATH_AST_STEP) {
                /* Direct step child - process it */
                struct leptris_xpath_result* step_result = evaluate_step(ctx, child, current);
                if (!step_result) {
                    xpath_nodeset_free(current);
                    return NULL;
                }

                xpath_nodeset_free(current);
                current = step_result->value.nodeset_value;
                step_result->value.nodeset_value = NULL;
                xpath_result_free(step_result);
            }
            else if (child->type == XPATH_AST_RELATIVE_PATH) {
                /* RELATIVE_PATH container - process its step children */
                for (size_t j = 0; j < child->child_count; j++) {
                    XPathASTNode* step = child->children[j];

                    if (step->type == XPATH_AST_STEP) {
                        struct leptris_xpath_result* step_result = evaluate_step(ctx, step, current);
                        if (!step_result) {
                            xpath_nodeset_free(current);
                            return NULL;
                        }

                        xpath_nodeset_free(current);
                        current = step_result->value.nodeset_value;
                        step_result->value.nodeset_value = NULL;
                        xpath_result_free(step_result);
                    }
                }
            }
        }
    }

done_evaluating:
    DEBUG_LOG("  Final nodeset count: %zu", xpath_nodeset_count(current));
    DEBUG_LOG("=== evaluate_location_path END ===");

    struct leptris_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
    if (result) result->value.nodeset_value = current;
    return result;
}