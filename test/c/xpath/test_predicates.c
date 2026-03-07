/* test_predicates.c - XPath predicates test
 * Tests for XPath predicate evaluation
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Include public API */
#include "../../../src/include/taurus.h"

/* Include internal XPath interfaces for testing */
#include "../../../src/taurus/xpath/lexer.h"
#include "../../../src/taurus/xpath/parser.h"
#include "../../../src/taurus/xpath/evaluator.h"

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Helper: Run test */
#define RUN_TEST(name) \
    do { \
        printf("Running %s... ", #name); \
        tests_run++; \
        if (name()) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
        } \
    } while(0)

/* Helper: Parse example.xml content */
static struct taurus_document* create_bookstore_document(void) {
    const char* xml =
        "<bookstore>"
        "<book id=\"1\">"
        "<title>XML Basics</title>"
        "<author>John Doe</author>"
        "<price>29.99</price>"
        "</book>"
        "<book id=\"2\">"
        "<title>XPath Guide</title>"
        "<author>Jane Smith</author>"
        "<price>34.99</price>"
        "</book>"
        "</bookstore>";

    return taurus_parse(xml, strlen(xml));
}

/* ============================================================================
 * Numeric Comparison Predicate Tests
 * ============================================================================ */

/* Test 1: Greater-than comparison (BUG: Returns empty, should return 1 node) */
static int test_numeric_greater_than(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) {
        printf("Failed to parse document! ");
        return 0;
    }

    const char* xpath = "//book[price > 30.0]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match book with id="2" (price=34.99) */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    } else {
        printf("Invalid result type or NULL ");
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 2: Greater-than-or-equal comparison */
static int test_numeric_greater_equal(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[price >= 30]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match both books (29.99 rounds up, 34.99 > 30) or just second book */
        /* Actually 29.99 is NOT >= 30, so should be 1 */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 3: Less-than comparison */
static int test_numeric_less_than(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[price < 30]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match book with id="1" (price=29.99) */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 4: Equality comparison with number */
static int test_numeric_equality(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[price = 34.99]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match book with id="2" */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* ============================================================================
 * Attribute Predicate Tests
 * ============================================================================ */

/* Test 5: Attribute equality */
static int test_attribute_equality(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[@id=\"2\"]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match second book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 6: Attribute existence */
static int test_attribute_existence(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[@id]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match both books (both have id attribute) */
        success = (count == 2);
        if (!success) {
            printf("Expected 2 nodes, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* ============================================================================
 * Position Predicate Tests
 * ============================================================================ */

/* Test 7: Numeric position predicate [1] */
static int test_position_numeric(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[1]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match first book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 8: position() function */
static int test_position_function(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[position()=2]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match second book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 9: last() function */
static int test_last_function(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[last()]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match last book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* ============================================================================
 * Boolean Predicate Tests
 * ============================================================================ */

/* Test 10: Element existence predicate */
static int test_element_existence(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[title]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match both books (both have title child) */
        success = (count == 2);
        if (!success) {
            printf("Expected 2 nodes, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 11: String equality in predicate */
static int test_string_equality(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[author=\"John Doe\"]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match first book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* ============================================================================
 * Complex Predicate Tests
 * ============================================================================ */

/* Test 12: Multiple predicates */
static int test_multiple_predicates(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[@id=\"1\"][price < 30]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match first book (has id=1 AND price < 30) */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* Test 13: AND operator in predicate */
static int test_and_in_predicate(void) {
    struct taurus_document* doc = create_bookstore_document();
    if (!doc) return 0;

    const char* xpath = "//book[@id=\"1\" and price < 30]";
    struct taurus_xpath_result* result = taurus_xpath_eval(doc, xpath, strlen(xpath));

    int success = 0;
    if (result && result->type == TAURUS_XPATH_NODESET) {
        size_t count = taurus_xpath_result_nodeset_size(result);
        /* Should match first book */
        success = (count == 1);
        if (!success) {
            printf("Expected 1 node, got %zu ", count);
        }
    }

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
    return success;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running XPath Predicate Tests\n");
    printf("==============================\n\n");

    /* Numeric comparison predicates */
    printf("Numeric Comparison Predicates:\n");
    RUN_TEST(test_numeric_greater_than);
    RUN_TEST(test_numeric_greater_equal);
    RUN_TEST(test_numeric_less_than);
    RUN_TEST(test_numeric_equality);
    printf("\n");

    /* Attribute predicates */
    printf("Attribute Predicates:\n");
    RUN_TEST(test_attribute_equality);
    RUN_TEST(test_attribute_existence);
    printf("\n");

    /* Position predicates */
    printf("Position Predicates:\n");
    RUN_TEST(test_position_numeric);
    RUN_TEST(test_position_function);
    RUN_TEST(test_last_function);
    printf("\n");

    /* Boolean predicates */
    printf("Boolean Predicates:\n");
    RUN_TEST(test_element_existence);
    RUN_TEST(test_string_equality);
    printf("\n");

    /* Complex predicates */
    printf("Complex Predicates:\n");
    RUN_TEST(test_multiple_predicates);
    RUN_TEST(test_and_in_predicate);
    printf("\n");

    printf("==============================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}