/* test/fuzz/fuzz_xslt.c — libFuzzer harness for the XSLT compile
 * surface (SOTA C1 #2051: the grammar-aware family growth). The
 * stylesheet input goes through leptris_xslt_parse; a compiled
 * stylesheet (if any) is applied to a fixed minimal document so the
 * transform execution path — template matching, output serialization
 * — runs over attacker-controlled stylesheets too. The invariants
 * under fuzz are the memory-safety contracts (no crash under
 * ASan/UBSan, no leak — every compile/apply path frees or transfers),
 * not transform correctness — the libxslt suite owns that. */
#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* Cap input size for speed; the compile/recursion caps are
     * tested elsewhere. */
    if (size == 0 || size > (1u << 18)) return 0;

    LeptrisXslt xslt = leptris_xslt_parse((const char*)data, size);
    if (!xslt) return 0;

    static const char SEED[] =
        "<r><item>a</item><item>b</item></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string(SEED, sizeof(SEED) - 1, &st);
    if (doc) {
        LeptrisDocument out = leptris_xslt_apply(xslt, doc);
        if (out) leptris_document_free(out);
        leptris_document_free(doc);
    }
    leptris_xslt_free(xslt);
    return 0;
}
