/* dom_traversal_example.c - DOM tree traversal example.
 *
 * Demonstrates the public API for walking a parsed document:
 * root, children, siblings, attributes.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

static void walk(TaurusElement elem, int depth) {
    if (!elem) return;

    for (int i = 0; i < depth; i++) printf("  ");

    const char* name = taurus_element_name(elem);
    printf("<%s", name ? name : "?");

    const char* id = taurus_element_attribute(elem, "id");
    if (id) printf(" id='%s'", id);
    printf(">\n");

    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        walk(child, depth + 1);
        child = taurus_element_next_sibling_any(child);
    }
}

int main(void) {
    const char* xml =
        "<root id='r'>"
        "  <section id='s1'><title>Intro</title><p>Hello</p></section>"
        "  <section id='s2'><title>Body</title><p>World</p></section>"
        "</root>";

    TaurusStatus status = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "parse failed (status=%d)\n", status);
        return 1;
    }

    printf("=== Taurus DOM Traversal Example ===\n\n");
    walk(taurus_document_root(doc), 0);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child_any(root);
    int n = 0;
    while (child) {
        n++;
        child = taurus_element_next_sibling_any(child);
    }
    printf("\nroot children: %d\n", n);

    taurus_document_free(doc);
    return 0;
}
