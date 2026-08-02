/* error_handling_example.c - Error handling example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates proper error handling with the Taurus API.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

static void print_separator(void) {
    printf("----------------------------------------\n");
}

/* Stubbed — the public API has a per-thread error getter, but this
 * example illustrates intended usage.  These functions may not exist
 * in the current build; the example is illustrative. */
extern const char* taurus_last_error(void);
extern int taurus_last_error_code(void);
extern const char* taurus_error_string(int code);
extern int taurus_parse_error_line(void);
extern int taurus_parse_error_column(void);
extern void taurus_clear_error(void);

int main(void) {
    printf("=== Taurus Error Handling Example ===\n\n");

    /* Example 1: NULL input */
    print_separator();
    printf("Example 1: NULL input\n");
    TaurusStatus status = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(NULL, 0, &status);
    if (!doc) {
        printf("Correctly rejected NULL input (status=%d)\n", status);
    } else {
        printf("Should have rejected NULL input\n");
        taurus_document_free(doc);
    }
    printf("\n");

    /* Example 2: Empty input */
    print_separator();
    printf("Example 2: Empty input\n");
    doc = taurus_parse_string("", 0, &status);
    if (!doc) {
        printf("Correctly rejected empty input (status=%d)\n", status);
    } else {
        printf("Should have rejected empty input\n");
        taurus_document_free(doc);
    }
    printf("\n");

    /* Example 3: Malformed XML - unclosed tag */
    print_separator();
    printf("Example 3: Malformed XML (unclosed tag)\n");
    const char* bad_xml1 = "<root><child>text</root>";
    doc = taurus_parse_string(bad_xml1, strlen(bad_xml1), &status);
    if (!doc) {
        printf("Correctly rejected malformed XML (status=%d)\n", status);
    } else {
        printf("Should have rejected malformed XML\n");
        taurus_document_free(doc);
    }
    printf("\n");

    /* Example 4: Malformed XML - missing closing bracket */
    print_separator();
    printf("Example 4: Malformed XML (missing bracket)\n");
    const char* bad_xml2 = "<root><child>text</child";
    doc = taurus_parse_string(bad_xml2, strlen(bad_xml2), &status);
    if (!doc) {
        printf("Correctly rejected malformed XML (status=%d)\n", status);
    } else {
        printf("Should have rejected malformed XML\n");
        taurus_document_free(doc);
    }
    printf("\n");

    /* Example 5: Valid XML, invalid XPath */
    print_separator();
    printf("Example 5: Invalid XPath syntax\n");
    const char* valid_xml = "<root><child>text</child></root>";
    doc = taurus_parse_string(valid_xml, strlen(valid_xml), &status);
    if (!doc) {
        printf("Valid XML should parse (status=%d)\n", status);
        return 1;
    }
    printf("XML parsed successfully\n");

    /* Try invalid XPath (note: new API is taurus_xpath_eval(doc, ctx, expr) */
    TaurusXPathResult result = taurus_xpath_eval(doc, NULL, "//[invalid");
    if (!result) {
        printf("Correctly rejected invalid XPath\n");
    } else {
        printf("Should have rejected invalid XPath\n");
        taurus_xpath_result_free(result);
    }

    taurus_document_free(doc);
    printf("\n");

    /* Example 6: NULL handling in cleanup */
    print_separator();
    printf("Example 6: Safe NULL handling in API\n");
    printf("  Testing taurus_document_free(NULL)...\n");
    taurus_document_free(NULL);
    printf("  No crash with NULL document\n");
    printf("  Testing taurus_xpath_result_free(NULL)...\n");
    taurus_xpath_result_free(NULL);
    printf("  No crash with NULL result\n");
    printf("\n");

    print_separator();
    printf("\n=== All error handling examples completed! ===\n");
    printf("\nKey takeaways:\n");
    printf("1. Always check return values (NULL = error)\n");
    printf("2. Use TaurusStatus from taurus_parse_string to get error details\n");
    printf("3. API is safe with NULL pointers in cleanup functions\n");

    return 0;
}
