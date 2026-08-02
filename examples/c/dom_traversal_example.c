/* dom_traversal_example.c - DOM tree traversal example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates navigating and querying the DOM tree.
 */

#include <taurus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function to print element with indentation */
static void print_element(TaurusElement elem, int indent) {
    if (!elem) return;
    
    /* Print indentation */
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
    
    /* Print element name */
    const char* name = taurus_element_name(elem);
    printf("<%s", name);
    
    /* Print attributes */
    size_t attr_count = taurus_element_attribute_count(elem);
    for (size_t i = 0; i < attr_count; i++) {
        taurus_attribute* attr = taurus_element_attribute(elem, i);
        if (attr) {
            printf(" %s=\"%s\"", 
                   taurus_attribute_name(attr),
                   taurus_attribute_value(attr));
        }
    }
    printf(">\n");
    
    /* Print text content if any */
    const char* text = taurus_element_text(elem);
    if (text && strlen(text) > 0) {
        for (int i = 0; i < indent + 1; i++) {
            printf("  ");
        }
        printf("[text: \"%s\"]\n", text);
    }
}

/* Recursive tree walker */
static void walk_tree(TaurusElement elem, int depth) {
    if (!elem) return;
    
    print_element(elem, depth);
    
    /* Recursively visit children */
    size_t child_count = taurus_element_child_count(elem);
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(elem, i);
        walk_tree(child, depth + 1);
    }
}

int main(void) {
    printf("=== Taurus DOM Traversal Example ===\n\n");
    
    /* Test XML with nested structure */
    const char* xml = 
        "<library name=\"Technical Books\">"
        "  <section category=\"Programming\">"
        "    <book id=\"1\" available=\"yes\">"
        "      <title>The C Programming Language</title>"
        "      <authors>"
        "        <author>Brian Kernighan</author>"
        "        <author>Dennis Ritchie</author>"
        "      </authors>"
        "      <year>1988</year>"
        "    </book>"
        "    <book id=\"2\" available=\"no\">"
        "      <title>SICP</title>"
        "      <authors>"
        "        <author>Harold Abelson</author>"
        "      </authors>"
        "      <year>1996</year>"
        "    </book>"
        "  </section>"
        "  <section category=\"Mathematics\">"
        "    <book id=\"3\" available=\"yes\">"
        "      <title>Introduction to Algorithms</title>"
        "      <year>2009</year>"
        "    </book>"
        "  </section>"
        "</library>";
    
    printf("Parsing XML...\n");
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "Parse failed: %s\n", taurus_last_error());
        return 1;
    }
    printf("✓ Parse successful\n\n");
    
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        fprintf(stderr, "No root element\n");
        taurus_document_free(doc);
        return 1;
    }
    
    /* Example 1: Basic element information */
    printf("1. Root element information:\n");
    printf("   Name: %s\n", taurus_element_name(root));
    printf("   Children: %zu\n", taurus_element_child_count(root));
    printf("   Attributes: %zu\n", taurus_element_attribute_count(root));
    
    const char* name_attr = taurus_element_get_attribute(root, "name");
    if (name_attr) {
        printf("   'name' attribute: %s\n", name_attr);
    }
    printf("\n");
    
    /* Example 2: Complete tree traversal */
    printf("2. Complete DOM tree structure:\n");
    walk_tree(root, 0);
    printf("\n");
    
    /* Example 3: Direct child access */
    printf("3. Accessing children directly:\n");
    size_t section_count = taurus_element_child_count(root);
    printf("   Library has %zu section(s)\n", section_count);
    
    for (size_t i = 0; i < section_count; i++) {
        TaurusElement section = taurus_element_child(root, i);
        if (section) {
            const char* category = taurus_element_get_attribute(section, "category");
            size_t book_count = taurus_element_child_count(section);
            printf("   Section[%zu]: %s (%zu books)\n", 
                   i, category ? category : "(no category)", book_count);
        }
    }
    printf("\n");
    
    /* Example 4: Parent navigation */
    printf("4. Parent navigation:\n");
    if (section_count > 0) {
        TaurusElement first_section = taurus_element_child(root, 0);
        if (first_section && taurus_element_child_count(first_section) > 0) {
            TaurusElement first_book = taurus_element_child(first_section, 0);
            if (first_book) {
                printf("   Starting from first book...\n");
                const char* book_id = taurus_element_get_attribute(first_book, "id");
                printf("   Book id: %s\n", book_id ? book_id : "(none)");
                
                TaurusElement parent = taurus_element_parent(first_book);
                if (parent) {
                    printf("   Parent: %s\n", taurus_element_name(parent));
                    
                    TaurusElement grandparent = taurus_element_parent(parent);
                    if (grandparent) {
                        printf("   Grandparent: %s\n", taurus_element_name(grandparent));
                    }
                }
            }
        }
    }
    printf("\n");
    
    /* Example 5: Attribute iteration */
    printf("5. Examining all attributes:\n");
    if (section_count > 0) {
        TaurusElement first_section = taurus_element_child(root, 0);
        if (first_section && taurus_element_child_count(first_section) > 0) {
            TaurusElement first_book = taurus_element_child(first_section, 0);
            if (first_book) {
                size_t attr_count = taurus_element_attribute_count(first_book);
                printf("   First book has %zu attribute(s):\n", attr_count);
                
                for (size_t i = 0; i < attr_count; i++) {
                    taurus_attribute* attr = taurus_element_attribute(first_book, i);
                    if (attr) {
                        printf("   [%zu] %s = \"%s\"\n", i,
                               taurus_attribute_name(attr),
                               taurus_attribute_value(attr));
                    }
                }
            }
        }
    }
    printf("\n");
    
    /* Example 6: Finding specific elements */
    printf("6. Finding elements by XPath:\n");
    
    /* Find all books */
    TaurusXPathResult result = taurus_xpath_eval(doc, "//book", 6);
    if (result) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Found %zu book element(s)\n", count);
        
        for (size_t i = 0; i < count; i++) {
            TaurusElement book = taurus_xpath_result_nodeset_get(result, i);
            if (book) {
                const char* id = taurus_element_get_attribute(book, "id");
                
                /* Get title from child */
                TaurusXPathResult title_result = 
                    taurus_xpath_eval_with_context(doc, book, "./title", 7);
                char* title = NULL;
                if (title_result) {
                    title = taurus_xpath_result_as_string(title_result);
                    taurus_xpath_result_free(title_result);
                }
                
                printf("   Book %s: %s\n", 
                       id ? id : "?", 
                       title ? title : "(no title)");
                
                if (title) free(title);
            }
        }
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Example 7: Checking element properties */
    printf("7. Element property checks:\n");
    if (section_count > 0) {
        TaurusElement first_section = taurus_element_child(root, 0);
        if (first_section && taurus_element_child_count(first_section) > 0) {
            TaurusElement first_book = taurus_element_child(first_section, 0);
            if (first_book) {
                printf("   Element: <%s>\n", taurus_element_name(first_book));
                
                int has_id = taurus_element_has_attribute(first_book, "id");
                printf("   Has 'id' attribute: %s\n", has_id ? "yes" : "no");
                
                int has_price = taurus_element_has_attribute(first_book, "price");
                printf("   Has 'price' attribute: %s\n", has_price ? "yes" : "no");
                
                const char* avail = taurus_element_get_attribute(first_book, "available");
                printf("   'available' attribute: %s\n", 
                       avail ? avail : "(not present)");
            }
        }
    }
    printf("\n");
    
    /* Cleanup */
    printf("Cleaning up...\n");
    taurus_document_free(doc);
    printf("✓ Cleanup complete\n\n");
    
    printf("=== All DOM traversal examples completed! ===\n");
    return 0;
}