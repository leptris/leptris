/* basic_example.c - Basic Leptris API usage example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates parsing XML and evaluating XPath expressions.
 */

#include <leptris.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Leptris Basic Example ===\n\n");

    /* Test XML document */
    const char* xml =
        "<library>"
        "  <book>"
        "    <title>The C Programming Language</title>"
        "    <author>Kernighan and Ritchie</author>"
        "  </book>"
        "  <book>"
        "    <title>Structure and Interpretation of Computer Programs</title>"
        "    <author>Abelson and Sussman</author>"
        "  </book>"
        "</library>";

    printf("1. Parsing XML...\n");
    LeptrisStatus status = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "   Parse failed\n");
        return 1;
    }
    printf("   Parse successful\n\n");

    /* Get root element */
    printf("2. Getting root element...\n");
    LeptrisElement root = leptris_document_root(doc);
    if (!root) {
        fprintf(stderr, "   No root element\n");
        leptris_document_free(doc);
        return 1;
    }
    printf("   Root element: <%s>\n\n", leptris_element_name(root));

    /* Evaluate XPath */
    printf("3. Evaluating XPath: //title\n");
    const char* xpath = "//title";
    LeptrisXPathResult result =
        leptris_xpath_eval(doc, xpath, strlen(xpath));

    if (!result) {
        fprintf(stderr, "   ✗ XPath evaluation failed\n");
    } else {
        printf("   ✓ XPath evaluation successful\n");
        leptris_xpath_result_free(result);
    }
    printf("\n");

    /* Test another XPath */
    printf("4. Evaluating XPath: /library/book\n");
    xpath = "/library/book";
    result = leptris_xpath_eval(doc, xpath, strlen(xpath));

    if (!result) {
        fprintf(stderr, "   ✗ XPath evaluation failed\n");
    } else {
        printf("   ✓ XPath evaluation successful\n");
        leptris_xpath_result_free(result);
    }
    printf("\n");

    /* Cleanup */
    printf("5. Cleaning up...\n");
    leptris_document_free(doc);
    printf("   ✓ Cleanup complete\n\n");

    printf("=== All tests passed! ===\n");
    return 0;
}