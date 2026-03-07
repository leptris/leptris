/**
 * test_dtd_validation.c - DTD validation tests
 */

#include "../../../src/include/taurus/dtd.h"
#include "../../../src/include/taurus.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void test_validate_required_attribute_present(void) {
    printf("\n=== Test: Validate required attribute present ===\n");

    const char* xml = "<book id=\"123\"><title>Test</title></book>";
    const char* dtd_str = "<!ATTLIST book id ID #REQUIRED>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 1); /* Valid */

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ Required attribute present test passed\n");
}

void test_validate_required_attribute_missing(void) {
    printf("\n=== Test: Validate required attribute missing ===\n");

    const char* xml = "<book><title>Test</title></book>";
    const char* dtd_str = "<!ATTLIST book id ID #REQUIRED>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 0); /* Invalid */
    assert(error.message != NULL);
    printf("  Error: %s\n", error.message);

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ Required attribute missing test passed\n");
}

void test_validate_empty_element_valid(void) {
    printf("\n=== Test: Validate EMPTY element (valid) ===\n");

    const char* xml = "<br/>";
    const char* dtd_str = "<!ELEMENT br EMPTY>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 1); /* Valid */

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ EMPTY element valid test passed\n");
}

void test_validate_empty_element_invalid(void) {
    printf("\n=== Test: Validate EMPTY element (invalid) ===\n");

    const char* xml = "<br><text>Content</text></br>";
    const char* dtd_str = "<!ELEMENT br EMPTY>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 0); /* Invalid */
    assert(error.message != NULL);
    printf("  Error: %s\n", error.message);

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ EMPTY element invalid test passed\n");
}

void test_validate_nested_elements(void) {
    printf("\n=== Test: Validate nested elements ===\n");

    const char* xml =
        "<book id=\"1\">"
        "  <title>XML Guide</title>"
        "  <author>John Doe</author>"
        "</book>";

    const char* dtd_str =
        "<!ELEMENT book (title, author)>"
        "<!ATTLIST book id ID #REQUIRED>"
        "<!ELEMENT title (#PCDATA)>"
        "<!ELEMENT author (#PCDATA)>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 1); /* Valid */

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ Nested elements test passed\n");
}

void test_validate_multiple_attributes(void) {
    printf("\n=== Test: Validate multiple attributes ===\n");

    const char* xml = "<book id=\"1\" lang=\"en\"><title>Test</title></book>";
    const char* dtd_str =
        "<!ATTLIST book id ID #REQUIRED>"
        "<!ATTLIST book lang CDATA #IMPLIED>";

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusDTD* dtd = taurus_dtd_parse(dtd_str, strlen(dtd_str));

    assert(doc != NULL);
    assert(dtd != NULL);

    TaurusDTDError error = {0};
    int result = taurus_dtd_validate(doc, dtd, &error);

    assert(result == 1); /* Valid */

    taurus_dtd_error_free(&error);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);

    printf("✓ Multiple attributes test passed\n");
}

int main(void) {
    printf("Running DTD Validation Tests\n");
    printf("============================\n");

    test_validate_required_attribute_present();
    test_validate_required_attribute_missing();
    test_validate_empty_element_valid();
    test_validate_empty_element_invalid();
    test_validate_nested_elements();
    test_validate_multiple_attributes();

    printf("\n============================\n");
    printf("All DTD validation tests passed! ✓\n");

    return 0;
}