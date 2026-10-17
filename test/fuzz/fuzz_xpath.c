/* test/fuzz/fuzz_xpath.c — libFuzzer harness for the XPath evaluator
 * (approach ported from teptris's fuzz_plan): the input parses into
 * a document, then a fixed ALPHABET of expressions — paths and
 * predicates that overlap the corpus shapes — evaluates against it
 * and every result accessor drains, including kind-mismatched reads
 * (the contract answers those with NULL/0/false, never a crash).
 * Invariants under ASan/UBSan: evaluator and result lifetimes, node
 * handles into a live document, no leaks. */
#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* const kExprs[] = {
    "//node()",
    "//*",
    "//@*",
    "/a/b",
    "//item[@id='1']",
    "//p[@*]",
    "count(//*)",
    "string(//a)",
    "number(//@n)",
    "boolean(//x)",
    "//a[2]/following-sibling::*",
    "//@xmlns:*/..",
    "//* | //@* | //text()",
};
#define XPATH_ALPHA ((int)(sizeof(kExprs) / sizeof(kExprs[0])))

static void drain(LeptrisXPathResult r) {
    if (!r) return;
    size_t n = leptris_xpath_result_count(r);
    for (size_t i = 0; i < n; i++) {
        LeptrisElement e = leptris_xpath_result_get(r, i);
        (void)e; /* a handle into the live document — no deref race */
        (void)leptris_xpath_result_node_kind(r, i);
    }
    LeptrisNodeRef nodes[8];
    LeptrisXPathNodeKind kinds[8];
    (void)leptris_xpath_result_get_nodes_ex(r, nodes, kinds, 8);
    char* whole = leptris_xpath_result_string(r);
    if (whole) leptris_free_string(whole);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > (1u << 20)) return 0;

    LeptrisParseOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.recover = 1;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string_ex((const char*)data, size, &opts, &st);
    if (!doc) return 0;

    for (int i = 0; i < XPATH_ALPHA; i++) {
        LeptrisXPathResult r =
            leptris_xpath_eval(doc, NULL, kExprs[i]);
        drain(r);
        if (r) leptris_xpath_result_free(r);
    }

    leptris_document_free(doc);
    return 0;
}
