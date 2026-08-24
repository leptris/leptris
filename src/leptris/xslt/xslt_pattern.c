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

static int nodeset_contains(LeptrisXPathResult r, LeptrisElement node) {
    size_t n = leptris_xpath_result_count(r);
    for (size_t i = 0; i < n; i++) {
        if (leptris_xpath_result_get_node(r, i) == (LeptrisNodeRef)node)
            return 1;
    }
    return 0;
}

/* Does this alternative select `node` when evaluated from `ctx`? */
static int selects_from(LeptrisXPathCompiled expr, LeptrisDocument doc,
                        LeptrisElement ctx, LeptrisElement node) {
    LeptrisXPathResult r = leptris_xpath_compiled_eval(expr, doc, ctx);
    if (!r) return 0;
    int hit = nodeset_contains(r, node);
    leptris_xpath_result_free(r);
    return hit;
}

int xslt_pattern_matches(const XsltPattern* p, LeptrisElement node,
                         LeptrisDocument doc) {
    if (!p || !node || !doc) return 0;

    LeptrisElement root = leptris_document_root(doc);

    for (const XsltPattern* alt = p; alt; alt = alt->next) {
        /* Root-element special case: a single bare NAME (or *)
         * matches the root element by name — child:: cannot reach
         * it without a document node. */
        if (node == root && alt->expr_name_only) {
            const char* n = leptris_element_get_name(node);
            if ((alt->expr_name[0] == '*' && alt->expr_name[1] == 0) ||
                (n && strcmp(n, alt->expr_name) == 0)) {
                return 1;
            }
        }

        /* Ancestor ladder: each ancestor as the evaluation context. */
        for (LeptrisElement ctx = node; ctx; ) {
            if (selects_from(alt->expr, doc, ctx, node)) return 1;
            LeptrisElement up = leptris_node_parent((LeptrisNodeRef)ctx);
            if (!up && ctx == root) break;      /* ladder exhausted */
            ctx = up;
            if (!ctx) break;
        }
    }
    return 0;
}
