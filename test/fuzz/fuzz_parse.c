/* test/fuzz/fuzz_parse.c — libFuzzer harness for the parse surface
 * (approach ported from teptris test/fuzz): parse -> serialize ->
 * re-parse, under both the default and the RECOVER parse mode. The
 * invariants under fuzz are the memory-safety contracts (no crash
 * under ASan/UBSan, no leak — every parse path frees or transfers
 * its document), not semantic correctness — the conformance suites
 * own that. The serialize output is engine-owned memory; re-parsing
 * it exercises the serializer's guarantee that its bytes parse
 * back. */
#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void round_trip(const char* data, size_t size, int recover) {
    LeptrisParseOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.recover = recover;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_ex(data, size, &opts, &st);
    if (st != LEPTRIS_OK && doc == NULL) return;

    /* exercise XPath against whatever tree came back */
    LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, "//node()");
    if (r) leptris_xpath_result_free(r);

    char* out = leptris_document_serialize(doc, NULL);
    if (out) {
        LeptrisParseOptions opts2;
        memset(&opts2, 0, sizeof(opts2));
        opts2.recover = recover;
        LeptrisStatus st2 = LEPTRIS_OK;
        LeptrisDocument doc2 = leptris_parse_string_ex(
            out, strlen(out), &opts2, &st2);
        if (doc2) leptris_document_free(doc2);
        leptris_free_string(out);
    }
    leptris_document_free(doc);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* Cap input size for speed; the depth cap is tested elsewhere. */
    if (size == 0 || size > (1u << 20)) return 0;

    round_trip((const char*)data, size, 0);
    round_trip((const char*)data, size, 1);
    return 0;
}
