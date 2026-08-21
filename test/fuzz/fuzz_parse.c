/* test/fuzz/fuzz_parse.c — libFuzzer harness for leptris_parse_string.
 *
 * Run:
 *   cmake -B build -S . -DLEPTRIS_ENABLE_FUZZING=ON
 *   cmake --build build --target fuzz_parse
 *   mkdir -p corpus && cp test/fixtures/*.xml corpus/
 *   ./build/fuzz_parse -max_total_time=600 corpus/
 *
 * Crashes are written to crash-*; add them as regression tests.
 *
 * See TODO 40.
 */

#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* Avoid pathological inputs that immediately exceed the depth limit
     * (already covered by tests).  Cap input size for speed. */
    if (size > 1024 * 1024) return 0;

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string((const char*)data, size, &st);

    if (doc) {
        /* Exercise serialize + free to catch use-after-free in cleanup. */
        char* out = leptris_document_serialize(doc, NULL);
        if (out) leptris_free_string(out);

        /* Also exercise XPath on the parsed document — catches bugs
         * in the evaluator's interaction with the DOM. */
        LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, "//node()");
        if (r) leptris_xpath_result_free(r);

        leptris_document_free(doc);
    }
    return 0;
}
