/* dom_traversal_example.c - DOM tree traversal example.
 *
 * Demonstrates the public API for walking a parsed document:
 * root, children, siblings, attributes.
 */

#include <leptris.h>
#include <stdio.h>
#include <string.h>

static void walk(LeptrisElement elem, int depth) {
    if (!elem) return;

    for (int i = 0; i < depth; i++) printf("  ");

    const char* name = leptris_element_name(elem);
    printf("<%s", name ? name : "?");

    const char* id = leptris_element_attribute(elem, "id");
    if (id) printf(" id='%s'", id);
    printf(">\n");

    LeptrisElement child = leptris_element_first_child_any(elem);
    while (child) {
        walk(child, depth + 1);
        child = leptris_element_next_sibling_any(child);
    }
}

int main(void) {
    const char* xml =
        "<root id='r'>"
        "  <section id='s1'><title>Intro</title><p>Hello</p></section>"
        "  <section id='s2'><title>Body</title><p>World</p></section>"
        "</root>";

    LeptrisStatus status = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "parse failed (status=%d)\n", status);
        return 1;
    }

    printf("=== Leptris DOM Traversal Example ===\n\n");
    walk(leptris_document_root(doc), 0);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement child = leptris_element_first_child_any(root);
    int n = 0;
    while (child) {
        n++;
        child = leptris_element_next_sibling_any(child);
    }
    printf("\nroot children: %d\n", n);

    leptris_document_free(doc);
    return 0;
}
