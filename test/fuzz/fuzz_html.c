/* test/fuzz/fuzz_html.c — libFuzzer harness for the HTML tokenizer
 * surfaces (SOTA C1 #2051): the WHATWG HTML5 face and the legacy
 * HTML4 face over the same bytes, serialize + re-parse on whatever
 * tree comes back. The invariants under fuzz are the memory-safety
 * contracts (no crash under ASan/UBSan, no leak) — the html5lib
 * corpus owns conformance; this harness owns the tokenizer's
 * robustness over the whole state space. */
#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > (1u << 18)) return 0;

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_html_string((const char*)data, size, &st);
    if (doc) {
        char* out = leptris_document_serialize(doc, NULL);
        if (out) {
            LeptrisStatus st2 = LEPTRIS_OK;
            LeptrisDocument doc2 = leptris_parse_html_string(
                out, strlen(out), &st2);
            if (doc2) leptris_document_free(doc2);
            leptris_free_string(out);
        }
        leptris_document_free(doc);
    }

    /* The legacy HTML4 tokenizer takes the same bytes: its implied-
     * end rules and CDATA-ish quirks run a different state machine. */
    LeptrisStatus st3 = LEPTRIS_OK;
    LeptrisDocument doc3 =
        leptris_parse_html4_string((const char*)data, size, &st3);
    if (doc3) leptris_document_free(doc3);
    return 0;
}
