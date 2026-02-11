/* xpath_example.c - XPath API usage example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates various XPath 1.0 queries and result types.
 */

#include <taurus/taurus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("=== Taurus XPath Example ===\n\n");
    
    /* Test XML with various content types */
    const char* xml = 
        "<bookstore>"
        "  <book price=\"29.99\" category=\"programming\">"
        "    <title>The C Programming Language</title>"
        "    <author>Kernighan</author>"
        "    <author>Ritchie</author>"
        "    <year>1988</year>"
        "  </book>"
        "  <book price=\"39.95\" category=\"programming\">"
        "    <title>SICP</title>"
        "    <author>Abelson</author>"
        "    <year>1996</year>"
        "  </book>"
        "  <book price=\"49.99\" category=\"fiction\">"
        "    <title>The Art of Computer Programming</title>"
        "    <author>Knuth</author>"
        "    <year>1968</year>"
        "  </book>"
        "</bookstore>";
    
    printf("Parsing XML...\n");
    taurus_document* doc = taurus_parse(xml, strlen(xml));
    if (!doc) {
        fprintf(stderr, "Parse failed: %s\n", taurus_last_error());
        return 1;
    }
    printf("✓ Parse successful\n\n");
    
    /* Example 1: Node-set query */
    printf("1. Node-set query: //book\n");
    taurus_xpath_result* result = taurus_xpath_eval(doc, "//book", 6);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Found %zu book(s)\n", count);
        
        for (size_t i = 0; i < count; i++) {
            taurus_element* book = taurus_xpath_result_nodeset_get(result, i);
            const char* title = taurus_element_get_attribute(book, "category");
            printf("   - Book %zu: category=%s\n", i + 1, 
                   title ? title : "(none)");
        }
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 2: Predicate with position */
    printf("2. Position predicate: //book[1]\n");
    result = taurus_xpath_eval(doc, "//book[1]", 10);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Found %zu node(s) (first book)\n", count);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 3: Attribute query */
    printf("3. Attribute query: //book/@price\n");
    result = taurus_xpath_eval(doc, "//book/@price", 14);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Found %zu price attribute(s)\n", count);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 4: Boolean query */
    printf("4. Boolean query: //book[@category='fiction']\n");
    result = taurus_xpath_eval(doc, "//book[@category='fiction']", 28);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        int exists = taurus_xpath_result_as_boolean(result);
        printf("   Fiction books exist: %s\n", exists ? "true" : "false");
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 5: Count function */
    printf("5. Function query: count(//book)\n");
    result = taurus_xpath_eval(doc, "count(//book)", 13);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        double count = taurus_xpath_result_as_number(result);
        printf("   Total books: %.0f\n", count);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 6: String query */
    printf("6. String query: //book[1]/title\n");
    result = taurus_xpath_eval(doc, "//book[1]/title", 15);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        char* title = taurus_xpath_result_as_string(result);
        printf("   First book title: \"%s\"\n", title);
        free(title);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 7: Complex predicate */
    printf("7. Complex predicate: //book[year > 1990]\n");
    result = taurus_xpath_eval(doc, "//book[year > 1990]", 19);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Books published after 1990: %zu\n", count);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 8: Multiple predicates */
    printf("8. Multiple predicates: //book[@category='programming'][1]\n");
    result = taurus_xpath_eval(doc, "//book[@category='programming'][1]", 35);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   First programming book: %zu node(s)\n", count);
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Cleanup */
    printf("Cleaning up...\n");
    taurus_document_free(doc);
    printf("✓ Cleanup complete\n\n");
    
    printf("=== All XPath examples completed! ===\n");
    return 0;
}