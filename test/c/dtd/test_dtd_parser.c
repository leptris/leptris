/**
 * test_dtd_parser.c - DTD parser tests
 */

#include "../../../src/include/taurus/dtd.h"
#include "../../../src/taurus/dtd/model.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_parse_element_empty(void) {
    printf("\n=== Test: Parse ELEMENT with EMPTY ===\n");

    const char* dtd = "<!ELEMENT br EMPTY>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->element_count == 1);
    assert(strcmp(dtd_obj->elements[0]->name, "br") == 0);
    assert(dtd_obj->elements[0]->content_type == DTD_CONTENT_EMPTY);

    taurus_dtd_free(dtd_obj);
    printf("✓ ELEMENT EMPTY test passed\n");
}

void test_parse_element_any(void) {
    printf("\n=== Test: Parse ELEMENT with ANY ===\n");

    const char* dtd = "<!ELEMENT div ANY>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->element_count == 1);
    assert(strcmp(dtd_obj->elements[0]->name, "div") == 0);
    assert(dtd_obj->elements[0]->content_type == DTD_CONTENT_ANY);

    taurus_dtd_free(dtd_obj);
    printf("✓ ELEMENT ANY test passed\n");
}

void test_parse_element_children(void) {
    printf("\n=== Test: Parse ELEMENT with children ===\n");

    const char* dtd = "<!ELEMENT book (title, author+, isbn?)>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->element_count == 1);
    assert(strcmp(dtd_obj->elements[0]->name, "book") == 0);
    assert(dtd_obj->elements[0]->content_type == DTD_CONTENT_CHILDREN);
    assert(dtd_obj->elements[0]->content_model != NULL);

    taurus_dtd_free(dtd_obj);
    printf("✓ ELEMENT children test passed\n");
}

void test_parse_element_mixed(void) {
    printf("\n=== Test: Parse ELEMENT with mixed content ===\n");

    const char* dtd = "<!ELEMENT p (#PCDATA | em | strong)*>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->element_count == 1);
    assert(strcmp(dtd_obj->elements[0]->name, "p") == 0);
    assert(dtd_obj->elements[0]->content_type == DTD_CONTENT_MIXED);

    taurus_dtd_free(dtd_obj);
    printf("✓ ELEMENT mixed content test passed\n");
}

void test_parse_attlist_required(void) {
    printf("\n=== Test: Parse ATTLIST with REQUIRED ===\n");

    const char* dtd = "<!ATTLIST book id ID #REQUIRED>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->attribute_count == 1);
    assert(strcmp(dtd_obj->attributes[0]->element_name, "book") == 0);
    assert(strcmp(dtd_obj->attributes[0]->attr_name, "id") == 0);
    assert(strcmp(dtd_obj->attributes[0]->attr_type, "ID") == 0);
    assert(dtd_obj->attributes[0]->default_type == DTD_ATTR_REQUIRED);

    taurus_dtd_free(dtd_obj);
    printf("✓ ATTLIST REQUIRED test passed\n");
}

void test_parse_attlist_implied(void) {
    printf("\n=== Test: Parse ATTLIST with IMPLIED ===\n");

    const char* dtd = "<!ATTLIST book title CDATA #IMPLIED>";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->attribute_count == 1);
    assert(dtd_obj->attributes[0]->default_type == DTD_ATTR_IMPLIED);

    taurus_dtd_free(dtd_obj);
    printf("✓ ATTLIST IMPLIED test passed\n");
}

void test_parse_attlist_fixed(void) {
    printf("\n=== Test: Parse ATTLIST with FIXED ===\n");

    const char* dtd = "<!ATTLIST book version CDATA #FIXED \"1.0\">";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->attribute_count == 1);
    assert(dtd_obj->attributes[0]->default_type == DTD_ATTR_FIXED);
    assert(strcmp(dtd_obj->attributes[0]->default_value, "1.0") == 0);

    taurus_dtd_free(dtd_obj);
    printf("✓ ATTLIST FIXED test passed\n");
}

void test_parse_attlist_default(void) {
    printf("\n=== Test: Parse ATTLIST with default value ===\n");

    const char* dtd = "<!ATTLIST book lang CDATA \"en\">";
    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->attribute_count == 1);
    assert(dtd_obj->attributes[0]->default_type == DTD_ATTR_DEFAULT);
    assert(strcmp(dtd_obj->attributes[0]->default_value, "en") == 0);

    taurus_dtd_free(dtd_obj);
    printf("✓ ATTLIST default value test passed\n");
}

void test_parse_multiple_declarations(void) {
    printf("\n=== Test: Parse multiple declarations ===\n");

    const char* dtd =
        "<!ELEMENT book (title, author+)>"
        "<!ATTLIST book id ID #REQUIRED>"
        "<!ELEMENT title (#PCDATA)>"
        "<!ELEMENT author (#PCDATA)>";

    TaurusDTD* dtd_obj = taurus_dtd_parse(dtd, strlen(dtd));

    assert(dtd_obj != NULL);
    assert(dtd_obj->element_count == 3);
    assert(dtd_obj->attribute_count == 1);

    taurus_dtd_free(dtd_obj);
    printf("✓ Multiple declarations test passed\n");
}

int main(void) {
    printf("Running DTD Parser Tests\n");
    printf("========================\n");

    test_parse_element_empty();
    test_parse_element_any();
    test_parse_element_children();
    test_parse_element_mixed();
    test_parse_attlist_required();
    test_parse_attlist_implied();
    test_parse_attlist_fixed();
    test_parse_attlist_default();
    test_parse_multiple_declarations();

    printf("\n========================\n");
    printf("All DTD parser tests passed! ✓\n");

    return 0;
}