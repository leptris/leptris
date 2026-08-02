/* namespace_example.c - Namespace-aware parsing and XPath example.
 *
 * Demonstrates the public API for working with namespaced documents.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char* xml =
        "<catalog xmlns:b='http://books' xmlns:c='http://cds'>"
        "  <b:book id='b1'><b:title>First</b:title></b:book>"
        "  <c:cd id='c1'><c:title>Album</c:title></c:cd>"
        "</catalog>";

    TaurusStatus status = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "parse failed (status=%d)\n", status);
        return 1;
    }

    printf("=== Taurus Namespace Example ===\n\n");

    TaurusElement root = taurus_document_root(doc);
    printf("root element: %s\n", taurus_element_name(root));

    TaurusElement child = taurus_element_first_child_any(root);
    while (child) {
        const char* name = taurus_element_name(child);
        const char* ns    = taurus_element_namespace(child);
        const char* id    = taurus_element_attribute(child, "id");
        printf("  <%s> ns='%s' id='%s'\n",
               name ? name : "?",
               ns    ? ns    : "(none)",
               id    ? id    : "(none)");
        child = taurus_element_next_sibling_any(child);
    }

    TaurusXPathResult r = taurus_xpath_eval(doc, NULL, "//b:title");
    if (r) {
        printf("\nb: titles found: %zu\n", taurus_xpath_result_count(r));
        taurus_xpath_result_free(r);
    }

    taurus_document_free(doc);
    return 0;
}
