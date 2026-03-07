/* test_xpath_api.cc - XPath API Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for XPath API surface in Taurus.
 * Tests API completeness: eval, result types, accessors, etc.
 */

#include <taurus.h>
#include <gtest/gtest.h>

/**
 * Helper to check if a TaurusElement is null
 */
static inline bool element_is_null(TaurusElement elem) {
    return taurus_element_is_null(elem);
}

/**
 * Helper macros for TaurusElement assertions
 */
#define EXPECT_ELEM_NOT_NULL(elem) EXPECT_TRUE(!taurus_element_is_null((elem)))
#define ASSERT_ELEM_NOT_NULL(elem) ASSERT_TRUE(!taurus_element_is_null((elem)))
#define EXPECT_ELEM_NULL(elem) EXPECT_TRUE(taurus_element_is_null((elem)))

/* ============================================================================
 * Test 1: XPath Result Type Check
 * ============================================================================ */

TEST(XPathApiTest, ResultType) {
    const char* xml = "<root><node/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* Boolean result */
    TaurusXPathResult bool_result = taurus_xpath_eval(doc, root, "count(//node) > 0");
    ASSERT_NE(bool_result, nullptr) << "Boolean XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(bool_result), TAURUS_XPATH_BOOLEAN);
    taurus_xpath_result_free(bool_result);

    /* Number result */
    TaurusXPathResult num_result = taurus_xpath_eval(doc, root, "count(//node)");
    ASSERT_NE(num_result, nullptr) << "Number XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(num_result), TAURUS_XPATH_NUMBER);
    taurus_xpath_result_free(num_result);

    /* String result */
    TaurusXPathResult str_result = taurus_xpath_eval(doc, root, "name(//node)");
    ASSERT_NE(str_result, nullptr) << "String XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(str_result), TAURUS_XPATH_STRING);
    taurus_xpath_result_free(str_result);

    /* Nodeset result */
    TaurusXPathResult nodeset_result = taurus_xpath_eval(doc, root, "//node");
    ASSERT_NE(nodeset_result, nullptr) << "Nodeset XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(nodeset_result), TAURUS_XPATH_NODESET);
    taurus_xpath_result_free(nodeset_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 2: XPath Result Count
 * ============================================================================ */

TEST(XPathApiTest, ResultCount) {
    const char* xml = "<root><a/><b/><c/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* Empty nodeset */
    TaurusXPathResult empty_result = taurus_xpath_eval(doc, root, "//nonexistent");
    ASSERT_NE(empty_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(empty_result), TAURUS_XPATH_NODESET);
    EXPECT_EQ(taurus_xpath_result_count(empty_result), 0) << "Should find 0 nodes";
    taurus_xpath_result_free(empty_result);

    /* Single node */
    TaurusXPathResult single_result = taurus_xpath_eval(doc, root, "//a");
    ASSERT_NE(single_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(single_result), TAURUS_XPATH_NODESET);
    EXPECT_EQ(taurus_xpath_result_count(single_result), 1) << "Should find 1 node";
    taurus_xpath_result_free(single_result);

    /* Multiple nodes */
    TaurusXPathResult multi_result = taurus_xpath_eval(doc, root, "//*");
    ASSERT_NE(multi_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(multi_result), TAURUS_XPATH_NODESET);
    EXPECT_EQ(taurus_xpath_result_count(multi_result), 4) << "Should find 4 nodes (root + a + b + c)";
    taurus_xpath_result_free(multi_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 3: XPath Result Get - Nodeset Access
 * ============================================================================ */

TEST(XPathApiTest, ResultGet) {
    const char* xml = "<root><a id='1'/><b id='2'/><c id='3'/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    TaurusXPathResult result = taurus_xpath_eval(doc, root, "//*");
    ASSERT_NE(result, nullptr) << "XPath evaluation failed";
    ASSERT_EQ(taurus_xpath_result_type(result), TAURUS_XPATH_NODESET);
    EXPECT_EQ(taurus_xpath_result_count(result), 4);

    /* Access first node (root) */
    TaurusElement root_elem = taurus_xpath_result_get(result, 0);
    ASSERT_ELEM_NOT_NULL(root_elem) << "First node should not be NULL";
    const char* root_name = taurus_element_name(root_elem);
    EXPECT_STREQ(root_name, "root");

    /* Access second node (a) */
    TaurusElement a = taurus_xpath_result_get(result, 1);
    ASSERT_ELEM_NOT_NULL(a) << "Second node should not be NULL";
    const char* a_name = taurus_element_name(a);
    EXPECT_STREQ(a_name, "a");

    /* Access third node (b) */
    TaurusElement b = taurus_xpath_result_get(result, 2);
    ASSERT_ELEM_NOT_NULL(b) << "Third node should not be NULL";
    const char* b_name = taurus_element_name(b);
    EXPECT_STREQ(b_name, "b");

    /* Access fourth node (c) */
    TaurusElement c = taurus_xpath_result_get(result, 3);
    ASSERT_ELEM_NOT_NULL(c) << "Fourth node should not be NULL";
    const char* c_name = taurus_element_name(c);
    EXPECT_STREQ(c_name, "c");

    /* Access out of bounds should return NULL */
    TaurusElement out_of_bounds = taurus_xpath_result_get(result, 99);
    EXPECT_ELEM_NULL(out_of_bounds) << "Out of bounds access should return NULL";

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
}

/* ============================================================================
 * Test 4: XPath Result Boolean
 * ============================================================================ */

TEST(XPathApiTest, ResultBoolean) {
    const char* xml = "<root><node/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* True boolean */
    TaurusXPathResult true_result = taurus_xpath_eval(doc, root, "count(//node) > 0");
    ASSERT_NE(true_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(true_result), TAURUS_XPATH_BOOLEAN);
    int true_value = taurus_xpath_result_boolean(true_result);
    EXPECT_TRUE(true_value != 0) << "Boolean should be true";
    taurus_xpath_result_free(true_result);

    /* False boolean */
    TaurusXPathResult false_result = taurus_xpath_eval(doc, root, "count(//node) > 100");
    ASSERT_NE(false_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(false_result), TAURUS_XPATH_BOOLEAN);
    int false_value = taurus_xpath_result_boolean(false_result);
    EXPECT_FALSE(false_value != 0) << "Boolean should be false";
    taurus_xpath_result_free(false_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 5: XPath Result Number
 * ============================================================================ */

TEST(XPathApiTest, ResultNumber) {
    const char* xml = "<root><a/><b/><c/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    TaurusXPathResult result = taurus_xpath_eval(doc, root, "count(//*)");
    ASSERT_NE(result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(result), TAURUS_XPATH_NUMBER);

    double count = taurus_xpath_result_number(result);
    EXPECT_DOUBLE_EQ(count, 4.0) << "Should count 4 nodes";

    taurus_xpath_result_free(result);
    taurus_document_free(doc);
}

/* ============================================================================
 * Test 6: XPath Result String
 * ============================================================================ */

TEST(XPathApiTest, ResultString) {
    const char* xml = "<root><node/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* name() function */
    TaurusXPathResult name_result = taurus_xpath_eval(doc, root, "name(//node)");
    ASSERT_NE(name_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(name_result), TAURUS_XPATH_STRING);

    char* name = taurus_xpath_result_string(name_result);
    ASSERT_NE(name, nullptr) << "String result should not be NULL";
    EXPECT_STREQ(name, "node");
    taurus_free_string(name);  /* Free the returned string */
    taurus_xpath_result_free(name_result);

    /* string() function on element */
    TaurusXPathResult str_result = taurus_xpath_eval(doc, root, "string(//node)");
    ASSERT_NE(str_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(str_result), TAURUS_XPATH_STRING);

    char* str = taurus_xpath_result_string(str_result);
    ASSERT_NE(str, nullptr) << "String result should not be NULL";
    EXPECT_STREQ(str, "");  /* Empty element has empty string content */
    taurus_free_string(str);
    taurus_xpath_result_free(str_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 7: XPath with Variables
 * ============================================================================ */

TEST(XPathApiTest, WithVariables) {
    const char* xml = "<root><item id='1'/><item id='2'/><item id='3'/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    /* Create variable set */
    TaurusXPathVariableSet vars = taurus_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr) << "Variable set creation failed";

    /* Set number variable */
    TaurusStatus status = taurus_xpath_variable_set_number(vars, "target_id", 2.0);
    EXPECT_EQ(status, TAURUS_OK) << "Setting number variable should succeed";

    /* Set string variable */
    status = taurus_xpath_variable_set_string(vars, "target_name", "item");
    EXPECT_EQ(status, TAURUS_OK) << "Setting string variable should succeed";

    /* Set boolean variable */
    status = taurus_xpath_variable_set_boolean(vars, "check_exists", 1);
    EXPECT_EQ(status, TAURUS_OK) << "Setting boolean variable should succeed";

    /* Query using variables - DISABLED: Parser doesn't support $var syntax */
    TaurusXPathResult result = taurus_xpath_eval_with_vars(doc, "//item[@id = $target_id]", vars);
    ASSERT_NE(result, nullptr) << "XPath evaluation with variables failed";
    EXPECT_EQ(taurus_xpath_result_type(result), TAURUS_XPATH_NODESET);
    EXPECT_EQ(taurus_xpath_result_count(result), 1) << "Should find exactly 1 item with id=2";

    TaurusElement item = taurus_xpath_result_get(result, 0);
    ASSERT_ELEM_NOT_NULL(item) << "Item should not be NULL";
    const char* id_attr = taurus_element_attribute(item, "id");
    EXPECT_STREQ(id_attr, "2");

    taurus_xpath_result_free(result);
    taurus_xpath_variable_set_free(vars);
    taurus_document_free(doc);
}

/* ============================================================================
 * Test 8: Complex XPath Expressions
 * ============================================================================ */

TEST(XPathApiTest, ComplexExpressions) {
    const char* xml = "<root><a><b id='1'/><b id='2'/></a><c><b id='3'/></c></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* Descendant axis */
    TaurusXPathResult desc_result = taurus_xpath_eval(doc, root, "//descendant::b");
    ASSERT_NE(desc_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(desc_result), 3) << "Should find 3 b elements";
    taurus_xpath_result_free(desc_result);

    /* Child axis with predicate */
    TaurusXPathResult pred_result = taurus_xpath_eval(doc, root, "//a/b[@id='2']");
    ASSERT_NE(pred_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(pred_result), 1) << "Should find 1 b element with id=2";
    taurus_xpath_result_free(pred_result);

    /* Parent axis */
    TaurusXPathResult parent_result = taurus_xpath_eval(doc, root, "//b/..");
    ASSERT_NE(parent_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(parent_result), 2) << "Should find 2 parents (a and c)";
    taurus_xpath_result_free(parent_result);

    /* Following-sibling */
    TaurusXPathResult sibling_result = taurus_xpath_eval(doc, root, "//a/b[@id='1']/following-sibling::b");
    ASSERT_NE(sibling_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(sibling_result), 1) << "Should find 1 following sibling";
    taurus_xpath_result_free(sibling_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 9: XPath Attribute Selection
 * ============================================================================ */

TEST(XPathApiTest, AttributeSelection) {
    const char* xml = "<root><node id='1' name='first'/><node id='2' name='second'/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* Select attributes */
    TaurusXPathResult attr_result = taurus_xpath_eval(doc, root, "//@id");
    ASSERT_NE(attr_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(attr_result), 2) << "Should find 2 id attributes";

    /* Note: In Taurus, attribute nodes are accessed differently
     * This test verifies the query doesn't crash */
    taurus_xpath_result_free(attr_result);

    /* Select node with specific attribute value */
    TaurusXPathResult node_result = taurus_xpath_eval(doc, root, "//node[@id='2']");
    ASSERT_NE(node_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_count(node_result), 1) << "Should find 1 node with id=2";

    TaurusElement node = taurus_xpath_result_get(node_result, 0);
    ASSERT_ELEM_NOT_NULL(node) << "Node should not be NULL";
    const char* name_attr = taurus_element_attribute(node, "name");
    EXPECT_STREQ(name_attr, "second");

    taurus_xpath_result_free(node_result);
    taurus_document_free(doc);
}

/* ============================================================================
 * Test 10: XPath Functions
 * ============================================================================ */

TEST(XPathApiTest, Functions) {
    const char* xml = "<root><a>10</a><b>20</b><c>30</c></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* sum() function - sum direct children */
    TaurusXPathResult sum_result = taurus_xpath_eval(doc, root, "sum(child::*)");
    ASSERT_NE(sum_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(sum_result), TAURUS_XPATH_NUMBER);
    double sum = taurus_xpath_result_number(sum_result);
    EXPECT_DOUBLE_EQ(sum, 60.0) << "Sum should be 10+20+30=60";
    taurus_xpath_result_free(sum_result);

    /* contains() function */
    TaurusXPathResult contains_result = taurus_xpath_eval(doc, root, "contains(name(//a), 'a')");
    ASSERT_NE(contains_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(contains_result), TAURUS_XPATH_BOOLEAN);
    int contains = taurus_xpath_result_boolean(contains_result);
    EXPECT_TRUE(contains != 0) << "contains() should return true";
    taurus_xpath_result_free(contains_result);

    /* concat() function */
    TaurusXPathResult concat_result = taurus_xpath_eval(doc, root, "concat(name(//a), '-', name(//b))");
    ASSERT_NE(concat_result, nullptr) << "XPath evaluation failed";
    EXPECT_EQ(taurus_xpath_result_type(concat_result), TAURUS_XPATH_STRING);
    char* concat = taurus_xpath_result_string(concat_result);
    ASSERT_NE(concat, nullptr) << "concat() result should not be NULL";
    EXPECT_STREQ(concat, "a-b");
    taurus_free_string(concat);
    taurus_xpath_result_free(concat_result);

    taurus_document_free(doc);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
