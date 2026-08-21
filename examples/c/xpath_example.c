/* xpath_example.c - XPath evaluation example.
 *
 * Demonstrates the public XPath API: evaluate queries and access
 * results of each type (nodeset, string, number, boolean).
 */

#include <leptris.h>
#include <stdio.h>
#include <string.h>

static const char* RESULT_TYPE_NAMES[] = {
    "nodeset", "boolean", "number", "string"
};

static void show_result(LeptrisDocument doc, const char* expr) {
    printf("  %-50s => ", expr);
    LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, expr);
    if (!r) {
        printf("(error)\n");
        return;
    }

    LeptrisXPathResultType t = leptris_xpath_result_type(r);
    switch (t) {
        case LEPTRIS_XPATH_NODESET: {
            size_t n = leptris_xpath_result_count(r);
            printf("nodeset[%zu]\n", n);
            break;
        }
        case LEPTRIS_XPATH_BOOLEAN:
            printf("boolean(%s)\n", leptris_xpath_result_boolean(r) ? "true" : "false");
            break;
        case LEPTRIS_XPATH_NUMBER:
            printf("number(%g)\n", leptris_xpath_result_number(r));
            break;
        case LEPTRIS_XPATH_STRING: {
            char* s = leptris_xpath_result_string(r);
            printf("string(\"%s\")\n", s ? s : "");
            if (s) leptris_free_string(s);
            break;
        }
        default:
            printf("unknown(%d)\n", (int)t);
            break;
    }
    leptris_xpath_result_free(r);
}

int main(void) {
    const char* xml =
        "<library>"
        "  <book id='b1'><title>C</title><price>29</price></book>"
        "  <book id='b2'><title>Lisp</title><price>39</price></book>"
        "  <book id='b3'><title>Prolog</title><price>49</price></book>"
        "</library>";

    LeptrisStatus status = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "parse failed (status=%d)\n", status);
        return 1;
    }

    printf("=== Leptris XPath Example ===\n\n");
    show_result(doc, "count(//book)");
    show_result(doc, "//book[@price > 35]/title");
    show_result(doc, "string(//book[1]/title)");
    show_result(doc, "//book[last()]/title");
    show_result(doc, "sum(//price)");
    show_result(doc, "//book[contains(title, 'C')]");

    leptris_document_free(doc);
    return 0;
}
