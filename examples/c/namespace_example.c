/* namespace_example.c - Namespace-aware parsing and XPath example.
 *
 * Demonstrates the public API for working with namespaced documents.
 */

#include <leptris.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char* xml =
        "<catalog xmlns:b='http://books' xmlns:c='http://cds'>"
        "  <b:book id='b1'><b:title>First</b:title></b:book>"
        "  <c:cd id='c1'><c:title>Album</c:title></c:cd>"
        "</catalog>";

    LeptrisStatus status = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "parse failed (status=%d)\n", status);
        return 1;
    }

    printf("=== Leptris Namespace Example ===\n\n");

    LeptrisElement root = leptris_document_root(doc);
    printf("root element: %s\n", leptris_element_name(root));

    LeptrisElement child = leptris_element_first_child_any(root);
    while (child) {
        const char* name = leptris_element_name(child);
        const char* ns    = leptris_element_namespace(child);
        const char* id    = leptris_element_attribute(child, "id");
        printf("  <%s> ns='%s' id='%s'\n",
               name ? name : "?",
               ns    ? ns    : "(none)",
               id    ? id    : "(none)");
        child = leptris_element_next_sibling_any(child);
    }

    LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, "//b:title");
    if (r) {
        printf("\nb: titles found: %zu\n", leptris_xpath_result_count(r));
        leptris_xpath_result_free(r);
    }

    leptris_document_free(doc);
    return 0;
}
