/* test/fuzz/fuzz_parse.c — libFuzzer harness for taurus_parse_string.
 *
 * Run:
 *   cmake -B build -S . -DTAURUS_ENABLE_FUZZING=ON
 *   cmake --build build --target fuzz_parse
 *   mkdir -p corpus && cp test/fixtures/*.xml corpus/
 *   ./build/fuzz_parse -max_total_time=600 corpus/
 *
 * Crashes are written to crash-*; add them as regression tests.
 *
 * See TODO 40.
 */

#include "taurus.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    /* Avoid pathological inputs that immediately exceed the depth limit
     * (already covered by tests).  Cap input size for speed. */
    if (size > 1024 * 1024) return 0;

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string((const char*)data, size, &st);

    if (doc) {
        /* Exercise serialize + free to catch use-after-free in cleanup. */
        char* out = taurus_document_serialize(doc, NULL);
        if (out) taurus_free_string(out);

        /* Also exercise XPath on the parsed document — catches bugs
         * in the evaluator's interaction with the DOM. */
        TaurusXPathResult r = taurus_xpath_eval(doc, NULL, "//node()");
        if (r) taurus_xpath_result_free(r);

        taurus_document_free(doc);
    }
    return 0;
}
