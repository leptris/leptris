/* namespace_example.c - XML Namespace handling example
 * Copyright (c) 2024, Ribose Inc.
 *
 * Demonstrates XML Namespaces 1.0 support.
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== Taurus Namespace Example ===\n\n");
    
    /* Test XML with namespaces */
    const char* xml = 
        "<root xmlns=\"http://example.com/default\" "
        "      xmlns:app=\"http://example.com/app\">"
        "  <element>Default namespace</element>"
        "  <app:element>App namespace</app:element>"
        "  <nested xmlns:lib=\"http://example.com/lib\">"
        "    <lib:item id=\"1\">Library item</lib:item>"
        "  </nested>"
        "</root>";
    
    printf("Parsing XML with namespaces...\n");
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    if (!doc) {
        fprintf(stderr, "Parse failed: %s\n", taurus_last_error());
        return 1;
    }
    printf("✓ Parse successful\n\n");
    
    /* Get root element */
    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        fprintf(stderr, "No root element\n");
        taurus_document_free(doc);
        return 1;
    }
    
    /* Example 1: Element namespace information */
    printf("1. Root element namespace information:\n");
    printf("   Name: %s\n", taurus_element_name(root));
    
    const char* ns_uri = taurus_element_namespace(root);
    printf("   Namespace URI: %s\n", ns_uri ? ns_uri : "(none)");
    
    const char* prefix = taurus_element_prefix(root);
    printf("   Prefix: %s\n", prefix ? prefix : "(none)");
    printf("\n");
    
    /* Example 2: Namespace declarations */
    printf("2. Namespace declarations on root:\n");
    size_t ns_count = taurus_element_namespace_count(root);
    printf("   Total declarations: %zu\n", ns_count);
    
    for (size_t i = 0; i < ns_count; i++) {
        taurus_namespace* ns = taurus_element_namespace_decl(root, i);
        if (ns) {
            const char* ns_prefix = taurus_namespace_prefix(ns);
            const char* uri = taurus_namespace_uri(ns);
            printf("   [%zu] xmlns", i);
            if (ns_prefix) {
                printf(":%s", ns_prefix);
            }
            printf(" = \"%s\"\n", uri);
        }
    }
    printf("\n");
    
    /* Example 3: Namespace resolution */
    printf("3. Namespace resolution:\n");
    
    const char* default_ns = taurus_element_resolve_namespace(root, NULL);
    printf("   Default namespace: %s\n", 
           default_ns ? default_ns : "(not defined)");
    
    const char* app_ns = taurus_element_resolve_namespace(root, "app");
    printf("   'app' prefix: %s\n", 
           app_ns ? app_ns : "(not defined)");
    
    /* Try to resolve undefined prefix */
    const char* undefined = taurus_element_resolve_namespace(root, "undefined");
    printf("   'undefined' prefix: %s\n", 
           undefined ? undefined : "(not defined)");
    printf("\n");
    
    /* Example 4: Child element namespaces */
    printf("4. Examining child elements:\n");
    size_t child_count = taurus_element_child_count(root);
    printf("   Root has %zu children\n", child_count);
    
    for (size_t i = 0; i < child_count; i++) {
        TaurusElement child = taurus_element_child(root, i);
        if (child) {
            const char* name = taurus_element_name(child);
            const char* child_ns = taurus_element_namespace(child);
            const char* child_prefix = taurus_element_prefix(child);
            
            printf("   Child[%zu]: <%s", i, name);
            if (child_prefix) {
                printf(" (prefix: %s)", child_prefix);
            }
            printf(">\n");
            printf("            Namespace: %s\n", 
                   child_ns ? child_ns : "(none)");
        }
    }
    printf("\n");
    
    /* Example 5: Namespace inheritance */
    printf("5. Namespace inheritance:\n");
    TaurusElement nested = taurus_element_child(root, 2);
    if (nested) {
        printf("   Element: %s\n", taurus_element_name(nested));
        
        /* Check if 'app' namespace is inherited */
        const char* inherited_app = 
            taurus_element_resolve_namespace(nested, "app");
        printf("   Inherited 'app' prefix: %s\n", 
               inherited_app ? inherited_app : "(not inherited)");
        
        /* Check for local 'lib' namespace */
        const char* lib_ns = 
            taurus_element_resolve_namespace(nested, "lib");
        printf("   Local 'lib' prefix: %s\n", 
               lib_ns ? lib_ns : "(not defined)");
        
        /* Check nested element */
        if (taurus_element_child_count(nested) > 0) {
            TaurusElement item = taurus_element_child(nested, 0);
            if (item) {
                const char* item_name = taurus_element_name(item);
                const char* item_ns = taurus_element_namespace(item);
                printf("   Nested item: <%s> in namespace %s\n", 
                       item_name, item_ns ? item_ns : "(none)");
            }
        }
    }
    printf("\n");
    
    /* Example 6: XPath with default namespace */
    printf("6. XPath query with default namespace:\n");
    TaurusXPathResult result = taurus_xpath_eval(doc, "//element", 9);
    if (!result) {
        fprintf(stderr, "   ✗ Query failed: %s\n", taurus_last_error());
    } else {
        size_t count = taurus_xpath_result_nodeset_size(result);
        printf("   Found %zu element(s) with name 'element'\n", count);
        
        /* Note: This finds elements by local name, regardless of namespace */
        for (size_t i = 0; i < count; i++) {
            TaurusElement elem = taurus_xpath_result_nodeset_get(result, i);
            if (elem) {
                const char* elem_ns = taurus_element_namespace(elem);
                printf("   [%zu] Namespace: %s\n", i, 
                       elem_ns ? elem_ns : "(none)");
            }
        }
        taurus_xpath_result_free(result);
    }
    printf("\n");
    
    /* Cleanup */
    printf("Cleaning up...\n");
    taurus_document_free(doc);
    printf("✓ Cleanup complete\n\n");
    
    printf("=== All namespace examples completed! ===\n");
    return 0;
}