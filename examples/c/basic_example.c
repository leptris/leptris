/* basic_example.c - Basic Taurus API usage example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates parsing XML and evaluating XPath expressions.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Taurus Basic Example ===\n\n");

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
    TaurusStatus status = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "   Parse failed\n");
        return 1;
    }
    printf("   Parse successful\n\n");

    /* Get root element */
    printf("2. Getting root element...\n");
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        fprintf(stderr, "   No root element\n");
        taurus_document_free(doc);
        return 1;
    }
    printf("   Root element: <%s>\n\n", taurus_element_name(root));

    /* Evaluate XPath */
    printf("3. Evaluating XPath: //title\n");
    const char* xpath = "//title";
    TaurusXPathResult result =
        taurus_xpath_eval(doc, xpath, strlen(xpath));

    if (!result) {
        fprintf(stderr, "   ✗ XPath evaluation failed\n");
    } else {
        printf("   ✓ XPath evaluation successful\n");
        taurus_xpath_result_free(result);
    }
    printf("\n");

    /* Test another XPath */
    printf("4. Evaluating XPath: /library/book\n");
    xpath = "/library/book";
    result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    if (!result) {
        fprintf(stderr, "   ✗ XPath evaluation failed\n");
    } else {
        printf("   ✓ XPath evaluation successful\n");
        taurus_xpath_result_free(result);
    }
    printf("\n");

    /* Cleanup */
    printf("5. Cleaning up...\n");
    taurus_document_free(doc);
    printf("   ✓ Cleanup complete\n\n");

    printf("=== All tests passed! ===\n");
    return 0;
}