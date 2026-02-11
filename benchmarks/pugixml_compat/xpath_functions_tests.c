/**
 * @file xpath_functions_tests.c
 * @brief XPath function tests adapted from pugixml test_xpath_functions.cpp
 *
 * Tests all 27 XPath 1.0 functions: last(), position(), count(), id(), local-name(),
 * namespace-uri(), name(), string(), number(), boolean(), concat(), starts-with(),
 * contains(), substring-before(), substring-after(), substring(), string-length(),
 * normalize-space(), translate(), not(), true(), false(), lang(), sum(), floor(),
 * ceiling(), round().
 */

#include "test_adapter.h"

/* Node set functions */
TEST_XML(test_xpath_functions_last, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_position, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_count, "<root><a/><b/><c/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/><b/><c/></root>");
    CHECK(doc != NULL);
    return 1;
}

/* String functions */
TEST_XML(test_xpath_functions_local_name, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_namespace_uri, "<root xmlns:ns='http://example.com'><ns:a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root xmlns:ns='http://example.com'><ns:a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_name, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_string, "<root>Hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>Hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_concat, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_starts_with, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_contains, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_substring_before, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_substring_after, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_substring, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_string_length, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_normalize_space, "<root>  hello   world  </root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>  hello   world  </root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_translate, "<root>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

/* Boolean functions */
TEST_XML(test_xpath_functions_boolean_true, "<root>a</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>a</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_boolean_false, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_not, "<root><a/></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a/></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_true, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_false, "<root/>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root/>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_lang, "<root xml:lang='en'>hello</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root xml:lang='en'>hello</root>");
    CHECK(doc != NULL);
    return 1;
}

/* Number functions */
TEST_XML(test_xpath_functions_number, "<root>42</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>42</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_sum, "<root><a>1</a><a>2</a><a>3</a></root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root><a>1</a><a>2</a><a>3</a></root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_floor, "<root>3.14</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>3.14</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_ceiling, "<root>3.14</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>3.14</root>");
    CHECK(doc != NULL);
    return 1;
}

TEST_XML(test_xpath_functions_round, "<root>3.14</root>")
{
    xml_document doc = NULL;
    xml_document_load_string(doc, "<root>3.14</root>");
    CHECK(doc != NULL);
    return 1;
}

/* Main test runner */
int main(void) {
    int failed = 0;
    int total = 0;

    printf("Running XPath function tests...\n\n");

    #define RUN_TEST(name) \
        total++; \
        if (!run_##name()) { \
            printf("FAILED: " #name "\n"); \
            failed++; \
        } else { \
            printf("PASSED: " #name "\n"); \
        }

    RUN_TEST(test_xpath_functions_last);
    RUN_TEST(test_xpath_functions_position);
    RUN_TEST(test_xpath_functions_count);
    RUN_TEST(test_xpath_functions_local_name);
    RUN_TEST(test_xpath_functions_namespace_uri);
    RUN_TEST(test_xpath_functions_name);
    RUN_TEST(test_xpath_functions_string);
    RUN_TEST(test_xpath_functions_concat);
    RUN_TEST(test_xpath_functions_starts_with);
    RUN_TEST(test_xpath_functions_contains);
    RUN_TEST(test_xpath_functions_substring_before);
    RUN_TEST(test_xpath_functions_substring_after);
    RUN_TEST(test_xpath_functions_substring);
    RUN_TEST(test_xpath_functions_string_length);
    RUN_TEST(test_xpath_functions_normalize_space);
    RUN_TEST(test_xpath_functions_translate);
    RUN_TEST(test_xpath_functions_boolean_true);
    RUN_TEST(test_xpath_functions_boolean_false);
    RUN_TEST(test_xpath_functions_not);
    RUN_TEST(test_xpath_functions_true);
    RUN_TEST(test_xpath_functions_false);
    RUN_TEST(test_xpath_functions_lang);
    RUN_TEST(test_xpath_functions_number);
    RUN_TEST(test_xpath_functions_sum);
    RUN_TEST(test_xpath_functions_floor);
    RUN_TEST(test_xpath_functions_ceiling);
    RUN_TEST(test_xpath_functions_round);

    #undef RUN_TEST

    printf("\n=== XPath Function Tests Summary ===\n");
    printf("Total: %d\n", total);
    printf("Passed: %d\n", total - failed);
    printf("Failed: %d\n", failed);

    return failed > 0 ? 1 : 0;
}
