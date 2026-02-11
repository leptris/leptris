/* error_handling_example.c - Error handling example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates proper error handling with the Taurus API.
 */

#include <taurus/taurus.h>
#include <stdio.h>
#include <string.h>

static void print_separator(void) {
    printf("----------------------------------------\n");
}

int main(void) {
    printf("=== Taurus Error Handling Example ===\n\n");
    
    /* Example 1: NULL input */
    print_separator();
    printf("Example 1: NULL input\n");
    taurus_document* doc = taurus_parse(NULL, 0);
    if (!doc) {
        printf("✓ Correctly rejected NULL input\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error message: %s\n", error);
        }
        taurus_error_code code = taurus_last_error_code();
        printf("  Error code: %d (%s)\n", code, taurus_error_string(code));
    } else {
        printf("✗ Should have rejected NULL input\n");
        taurus_document_free(doc);
    }
    printf("\n");
    
    /* Example 2: Empty input */
    print_separator();
    printf("Example 2: Empty input\n");
    doc = taurus_parse("", 0);
    if (!doc) {
        printf("✓ Correctly rejected empty input\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error message: %s\n", error);
        }
        taurus_error_code code = taurus_last_error_code();
        printf("  Error code: %d (%s)\n", code, taurus_error_string(code));
    } else {
        printf("✗ Should have rejected empty input\n");
        taurus_document_free(doc);
    }
    printf("\n");
    
    /* Example 3: Malformed XML - unclosed tag */
    print_separator();
    printf("Example 3: Malformed XML (unclosed tag)\n");
    const char* bad_xml1 = "<root><child>text</root>";
    doc = taurus_parse(bad_xml1, strlen(bad_xml1));
    if (!doc) {
        printf("✓ Correctly rejected malformed XML\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error: %s\n", error);
        }
        
        /* Show position information if available */
        int line = taurus_parse_error_line();
        int column = taurus_parse_error_column();
        if (line > 0) {
            printf("  Location: line %d, column %d\n", line, column);
        }
    } else {
        printf("✗ Should have rejected malformed XML\n");
        taurus_document_free(doc);
    }
    printf("\n");
    
    /* Example 4: Malformed XML - missing closing bracket */
    print_separator();
    printf("Example 4: Malformed XML (missing bracket)\n");
    const char* bad_xml2 = "<root><child>text</child";
    doc = taurus_parse(bad_xml2, strlen(bad_xml2));
    if (!doc) {
        printf("✓ Correctly rejected malformed XML\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error: %s\n", error);
        }
    } else {
        printf("✗ Should have rejected malformed XML\n");
        taurus_document_free(doc);
    }
    printf("\n");
    
    /* Example 5: Valid XML, invalid XPath */
    print_separator();
    printf("Example 5: Invalid XPath syntax\n");
    const char* valid_xml = "<root><child>text</child></root>";
    doc = taurus_parse(valid_xml, strlen(valid_xml));
    if (!doc) {
        printf("✗ Valid XML should parse\n");
        return 1;
    }
    printf("✓ XML parsed successfully\n");
    
    /* Try invalid XPath */
    const char* bad_xpath = "//[invalid";
    taurus_xpath_result* result = taurus_xpath_eval(doc, bad_xpath, strlen(bad_xpath));
    if (!result) {
        printf("✓ Correctly rejected invalid XPath\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error: %s\n", error);
        }
    } else {
        printf("✗ Should have rejected invalid XPath\n");
        taurus_xpath_result_free(result);
    }
    
    taurus_document_free(doc);
    printf("\n");
    
    /* Example 6: Error clearing */
    print_separator();
    printf("Example 6: Error state management\n");
    
    /* Cause an error */
    doc = taurus_parse(NULL, 0);
    if (!doc) {
        printf("✓ Error occurred (as expected)\n");
        const char* error = taurus_last_error();
        printf("  Error before clear: %s\n", error ? error : "(none)");
    }
    
    /* Clear error */
    taurus_clear_error();
    const char* error_after = taurus_last_error();
    printf("  Error after clear: %s\n", error_after ? error_after : "(none)");
    
    if (!error_after) {
        printf("✓ Error successfully cleared\n");
    } else {
        printf("✗ Error not cleared properly\n");
    }
    printf("\n");
    
    /* Example 7: Custom parse options with error tracking */
    print_separator();
    printf("Example 7: Parse options with position tracking\n");
    
    taurus_parse_options opts;
    taurus_parse_options_init(&opts);
    opts.track_positions = 1;  /* Enable position tracking */
    opts.strict = 1;            /* Strict validation */
    
    const char* bad_xml3 = 
        "<root>\n"
        "  <child>\n"
        "    <broken\n"       /* Error on line 3 */
        "  </child>\n"
        "</root>";
    
    doc = taurus_parse_with_options(bad_xml3, strlen(bad_xml3), &opts);
    if (!doc) {
        printf("✓ Detected error with position tracking\n");
        const char* error = taurus_last_error();
        if (error) {
            printf("  Error: %s\n", error);
        }
        
        int line = taurus_parse_error_line();
        int column = taurus_parse_error_column();
        if (line > 0) {
            printf("  Location: line %d, column %d\n", line, column);
        }
    } else {
        printf("✗ Should have detected malformed XML\n");
        taurus_document_free(doc);
    }
    printf("\n");
    
    /* Example 8: Safe NULL handling */
    print_separator();
    printf("Example 8: Safe NULL handling in API\n");
    
    /* API should handle NULL pointers safely */
    printf("  Testing taurus_document_free(NULL)...\n");
    taurus_document_free(NULL);
    printf("  ✓ No crash with NULL document\n");
    
    printf("  Testing taurus_xpath_result_free(NULL)...\n");
    taurus_xpath_result_free(NULL);
    printf("  ✓ No crash with NULL result\n");
    
    printf("  Testing taurus_clear_error() (no error)...\n");
    taurus_clear_error();
    printf("  ✓ No crash clearing when no error\n");
    printf("\n");
    
    print_separator();
    printf("\n=== All error handling examples completed! ===\n");
    printf("\nKey takeaways:\n");
    printf("1. Always check return values (NULL = error)\n");
    printf("2. Use taurus_last_error() to get error details\n");
    printf("3. Use taurus_last_error_code() for programmatic error handling\n");
    printf("4. Enable track_positions for detailed error locations\n");
    printf("5. Call taurus_clear_error() to reset error state\n");
    printf("6. API is safe with NULL pointers in cleanup functions\n");
    
    return 0;
}