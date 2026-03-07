/* test_xpath_functions_nodeset.cc - XPath Node-set Functions W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for node-set functions
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathNodesetFunctionsTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/functions/nodeset_tests.xml");
    }
};

/* ============================================================================
 * count() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Count_MultipleElements) {
    auto result = eval_xpath("count(//items/item)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_EmptyNodeset) {
    auto result = eval_xpath("count(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_SingleElement) {
    auto result = eval_xpath("count(//single/child)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_AllDescendants) {
    auto result = eval_xpath("count(//*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER);
    // Should count all elements
    double count = taurus_xpath_result_number(result);
    EXPECT_GT(count, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_WithPredicate) {
    auto result = eval_xpath("count(//items/item[@id])");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_ChildNodes) {
    auto result = eval_xpath("count(//items/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Count_Union) {
    auto result = eval_xpath("count(//items/item | //ordered/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 10.0); // 5 items + 5 ordered
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * last() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Last_InPredicate) {
    auto result = eval_xpath("//items/item[last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    // Should select the last item
    EXPECT_STREQ(get_nodeset_element_name(result, 0).c_str(), "item");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Last_Standalone) {
    // last() returns context size, which is 1 for root
    auto result = eval_xpath("last()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Last_WithPosition) {
    auto result = eval_xpath("//items/item[position() = last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Last_MinusOne) {
    auto result = eval_xpath("//items/item[last() - 1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    // Should select the second-to-last item
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Last_Comparison) {
    auto result = eval_xpath("//items/item[position() < last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 4); // All but last
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * position() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Position_First) {
    auto result = eval_xpath("//items/item[position() = 1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_Specific) {
    auto result = eval_xpath("//items/item[position() = 3]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_GreaterThan) {
    auto result = eval_xpath("//items/item[position() > 2]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3); // Items 3, 4, 5
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_LessThan) {
    auto result = eval_xpath("//items/item[position() < 3]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2); // Items 1, 2
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_ShorthandNumeric) {
    auto result = eval_xpath("//items/item[1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_Standalone) {
    // position() returns 1 when evaluated at root
    auto result = eval_xpath("position()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Position_Mod) {
    auto result = eval_xpath("//items/item[position() mod 2 = 0]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2); // Even positions: 2, 4
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * id() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Id_Single) {
    auto result = eval_xpath("id('book1')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    EXPECT_STREQ(get_nodeset_element_name(result, 0).c_str(), "book");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Id_Multiple) {
    auto result = eval_xpath("id('book1 book2 book3')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Id_NotFound) {
    auto result = eval_xpath("id('nonexistent')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Id_EmptyString) {
    auto result = eval_xpath("id('')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Id_FromNodeset) {
    // id() can take a nodeset argument and use string values
    auto result = eval_xpath("id(//ref/@idref)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NODESET);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * local-name() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, LocalName_NoNamespace) {
    auto result = eval_xpath("local-name(//no-namespace)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "no-namespace");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, LocalName_WithNamespace) {
    auto result = eval_xpath("local-name(//ns1:element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "element");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, LocalName_NoArgs_ContextNode) {
    auto result = eval_xpath("local-name()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    // Should return local name of context node (root)
    char* str = taurus_xpath_result_string(result);
    EXPECT_NE(str, nullptr);
    taurus_free_string(str);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, LocalName_EmptyNodeset) {
    auto result = eval_xpath("local-name(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, LocalName_FirstNode) {
    auto result = eval_xpath("local-name(//items/item)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "item");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, LocalName_QualifiedName) {
    auto result = eval_xpath("local-name(//ns1:qualified)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "qualified");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * namespace-uri() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_NoNamespace) {
    auto result = eval_xpath("namespace-uri(//no-namespace)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_WithNamespace) {
    auto result = eval_xpath("namespace-uri(//ns1:element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "http://example.com/ns1");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_DifferentNamespace) {
    auto result = eval_xpath("namespace-uri(//ns2:element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "http://example.com/ns2");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_NoArgs_ContextNode) {
    auto result = eval_xpath("namespace-uri()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_EmptyNodeset) {
    auto result = eval_xpath("namespace-uri(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, NamespaceUri_FirstNode) {
    auto result = eval_xpath("namespace-uri(//ns1:parent/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * name() Function Tests - XPath 1.0 Section 4.1
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Name_NoNamespace) {
    auto result = eval_xpath("name(//no-namespace)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "no-namespace");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_WithNamespace) {
    auto result = eval_xpath("name(//ns1:element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ns1:element");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_QualifiedName) {
    auto result = eval_xpath("name(//ns1:qualified)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ns1:qualified");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_NoArgs_ContextNode) {
    auto result = eval_xpath("name()");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_EmptyNodeset) {
    auto result = eval_xpath("name(//nonexistent)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_FirstNode) {
    auto result = eval_xpath("name(//names/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    // Should return name of first element
    char* str = taurus_xpath_result_string(result);
    EXPECT_NE(str, nullptr);
    taurus_free_string(str);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Name_DifferentNamespaces) {
    auto result = eval_xpath("name(//ns2:element)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "ns2:element");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Integration Tests - Combining Node-set Functions
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, Integration_CountWithName) {
    auto result = eval_xpath("count(//items/*[name() = 'item'])");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 5.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Integration_PositionLast) {
    auto result = eval_xpath("//items/item[position() = last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Integration_LocalNameNamespaceUri) {
    auto result = eval_xpath("//ns1:element[local-name() = 'element' and namespace-uri() = 'http://example.com/ns1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Integration_CountPosition) {
    auto result = eval_xpath("count(//items/item[position() <= 3])");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 3.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, Integration_NameComparison) {
    auto result = eval_xpath("//names/*[name() = 'simple']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST_F(XPathNodesetFunctionsTest, EdgeCase_LastInEmptyNodeset) {
    auto result = eval_xpath("//nonexistent[last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_PositionOutOfRange) {
    auto result = eval_xpath("//items/item[position() = 100]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_IdWithWhitespace) {
    auto result = eval_xpath("id('  book1  ')");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NODESET);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_CountZero) {
    auto result = eval_xpath("count(//empty-parent/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 0.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_NameOfRoot) {
    auto result = eval_xpath("name(/*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "root");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_LocalNameWithoutPrefix) {
    auto result = eval_xpath("local-name(//unqualified)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "unqualified");
    taurus_xpath_result_free(result);
}

TEST_F(XPathNodesetFunctionsTest, EdgeCase_NamespaceUriNested) {
    auto result = eval_xpath("namespace-uri(//ns1:parent/ns1:child)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_STRING(result, "http://example.com/ns1");
    taurus_xpath_result_free(result);
}