/* test_xpath_axes.cc - XPath Axes W3C Tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * W3C XPath 1.0 conformance tests for all 13 axes
 */

#include "xpath_test_utils.h"

using namespace taurus_test;

class XPathAxesTest : public XPathTestBase {
protected:
    void SetUp() override {
        XPathTestBase::SetUp();
        load_fixture("w3c/xpath/axes/axis_tests.xml");
    }
};

/* ============================================================================
 * child:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Child_DirectChildren) {
    auto result = eval_xpath("child::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 4);  // level1, sibling1, sibling2, sibling3
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Child_WithNodeTest) {
    auto result = eval_xpath("/root/child::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Child_NoMatch) {
    auto result = eval_xpath("/root/child::nonexistent");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Child_WithPredicate) {
    auto result = eval_xpath("/root/child::*[1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "level1");
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Child_SelectAll) {
    auto result = eval_xpath("/root/level1/child::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // level2a, level2b, level2c
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * descendant:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Descendant_AllDescendants) {
    auto result = eval_xpath("/root/descendant::*");
    ASSERT_NE(result, nullptr);
    // Should include all elements under root (not root itself)
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Descendant_WithNodeTest) {
    auto result = eval_xpath("/root/descendant::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // l3a, l3b, l3c
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Descendant_FromSpecificNode) {
    auto result = eval_xpath("/root/level1/descendant::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Descendant_Nested) {
    auto result = eval_xpath("/root/sibling3/descendant::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);  // nested element
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Descendant_WithPredicate) {
    auto result = eval_xpath("/root/level1/descendant::*[@id]");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * descendant-or-self:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, DescendantOrSelf_IncludesSelf) {
    auto result = eval_xpath("/root/descendant-or-self::*");
    ASSERT_NE(result, nullptr);
    // Should include root and all descendants
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, DescendantOrSelf_WithNodeTest) {
    auto result = eval_xpath("/root/level1/descendant-or-self::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, DescendantOrSelf_AbbreviatedForm) {
    auto result = eval_xpath("//level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, DescendantOrSelf_FromRoot) {
    auto result = eval_xpath("/descendant-or-self::root");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, DescendantOrSelf_Multiple) {
    auto result = eval_xpath("//level2a/descendant-or-self::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);  // level2a and level3
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * parent:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Parent_DirectParent) {
    auto result = eval_xpath("/root/level1/parent::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "root");
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Parent_AbbreviatedForm) {
    auto result = eval_xpath("/root/level1/..");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Parent_FromLeaf) {
    auto result = eval_xpath("//level3[@id='l3a']/parent::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "level2a");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * ancestor:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Ancestor_AllAncestors) {
    auto result = eval_xpath("//level3[@id='l3a']/ancestor::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // level2a, level1, root
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Ancestor_WithNodeTest) {
    auto result = eval_xpath("//level3[@id='l3a']/ancestor::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Ancestor_FromRoot) {
    auto result = eval_xpath("/root/ancestor::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);  // root has no element ancestors
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Ancestor_DeeplyNested) {
    auto result = eval_xpath("//nested[@id='n1']/ancestor::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);  // sibling3, root
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Ancestor_WithPredicate) {
    auto result = eval_xpath("//level3/ancestor::*[@id]");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * ancestor-or-self:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, AncestorOrSelf_IncludesSelf) {
    auto result = eval_xpath("//level3[@id='l3a']/ancestor-or-self::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 4);  // l3a, level2a, level1, root
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, AncestorOrSelf_WithNodeTest) {
    auto result = eval_xpath("//level3[@id='l3a']/ancestor-or-self::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, AncestorOrSelf_Root) {
    auto result = eval_xpath("/root/ancestor-or-self::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);  // just root
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, AncestorOrSelf_AllElements) {
    auto result = eval_xpath("//level3[@id='l3b']/ancestor-or-self::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 4);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, AncestorOrSelf_WithPredicate) {
    auto result = eval_xpath("//level3/ancestor-or-self::*[@id='l1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * following-sibling:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, FollowingSibling_AllFollowing) {
    auto result = eval_xpath("/root/sibling1/following-sibling::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);  // sibling2, sibling3
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, FollowingSibling_WithNodeTest) {
    auto result = eval_xpath("/root/sibling1/following-sibling::sibling2");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, FollowingSibling_None) {
    auto result = eval_xpath("/root/sibling3/following-sibling::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, FollowingSibling_WithPredicate) {
    auto result = eval_xpath("/root/level1/level2a/following-sibling::*[1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "level2b");
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * preceding-sibling:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, PrecedingSibling_AllPreceding) {
    auto result = eval_xpath("/root/sibling3/preceding-sibling::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // level1, sibling1, sibling2
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, PrecedingSibling_WithNodeTest) {
    auto result = eval_xpath("/root/sibling2/preceding-sibling::sibling1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, PrecedingSibling_None) {
    auto result = eval_xpath("/root/level1/preceding-sibling::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, PrecedingSibling_WithPredicate) {
    auto result = eval_xpath("/root/level1/level2b/preceding-sibling::*[@id]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * following:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Following_AllFollowing) {
    auto result = eval_xpath("/root/level1/following::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);  // Should include sibling1, sibling2, sibling3 and nested
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Following_WithNodeTest) {
    auto result = eval_xpath("/root/level1/following::sibling1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Following_FromDeep) {
    auto result = eval_xpath("//level3[@id='l3a']/following::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Following_ExcludesDescendants) {
    auto result = eval_xpath("/root/sibling3/following::nested");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);  // nested is descendant, not following
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * preceding:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Preceding_AllPreceding) {
    auto result = eval_xpath("/root/sibling3/preceding::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Preceding_WithNodeTest) {
    auto result = eval_xpath("/root/sibling2/preceding::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Preceding_FromDeep) {
    auto result = eval_xpath("//nested[@id='n1']/preceding::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Preceding_ExcludesAncestors) {
    auto result = eval_xpath("//level3[@id='l3a']/preceding::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);  // level1 is ancestor, not preceding
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * attribute:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Attribute_AllAttributes) {
    auto result = eval_xpath("/root/@*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);  // id attribute
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Attribute_SpecificAttribute) {
    auto result = eval_xpath("/root/level1/@id");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Attribute_MultipleAttributes) {
    auto result = eval_xpath("/root/level1/level2b/@*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // id, attr, class
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Attribute_NoAttributes) {
    auto result = eval_xpath("//level3[@id='l3a']/@*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);  // only id
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Attribute_WithPredicate) {
    auto result = eval_xpath("//level2b/attribute::*[name()='attr']");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * namespace:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Namespace_HasNamespace) {
    auto result = eval_xpath("/root/namespace::*");
    ASSERT_NE(result, nullptr);
    // Should have at least xml namespace
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Namespace_WithPrefix) {
    auto result = eval_xpath("/root/namespace::ns1");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GE(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Namespace_Inherited) {
    auto result = eval_xpath("/root/level1/namespace::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);  // Inherits from root
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Namespace_DefaultNamespace) {
    auto result = eval_xpath("/root/namespace::xml");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GE(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Namespace_Count) {
    auto result = eval_xpath("count(/root/namespace::*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_NUMBER);
    double count = taurus_xpath_result_number(result);
    EXPECT_GT(count, 0.0);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * self:: Axis Tests - XPath 1.0 Section 2.2
 * ============================================================================ */

TEST_F(XPathAxesTest, Self_CurrentNode) {
    auto result = eval_xpath("/root/self::root");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Self_AbbreviatedForm) {
    auto result = eval_xpath("/root/.");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Self_NoMatch) {
    auto result = eval_xpath("/root/self::notroot");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_EMPTY_NODESET(result);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Self_WithPredicate) {
    auto result = eval_xpath("/root/level1/self::*[@id='l1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Self_AnyNode) {
    auto result = eval_xpath("//level3[@id='l3a']/self::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

/* ============================================================================
 * Integration Tests - Complex axis combinations
 * ============================================================================ */

TEST_F(XPathAxesTest, Integration_ChildDescendant) {
    auto result = eval_xpath("/root/child::level1/descendant::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_ParentChild) {
    auto result = eval_xpath("//level3[@id='l3a']/parent::*/child::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AncestorDescendant) {
    auto result = eval_xpath("//level3[@id='l3a']/ancestor::level1/descendant::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_SiblingCombination) {
    auto result = eval_xpath("/root/sibling2/preceding-sibling::*[1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "sibling1");
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_FollowingPreceding) {
    auto result = eval_xpath("/root/level1/following::*/preceding::level1");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AttributeParent) {
    auto result = eval_xpath("/root/level1/level2b/@attr/parent::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "level2b");
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_SelfWithFunction) {
    auto result = eval_xpath("count(/root/self::*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 1.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_MultipleAxesChained) {
    auto result = eval_xpath("//level3[@id='l3b']/parent::*/preceding-sibling::*/child::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_DescendantWithAttribute) {
    auto result = eval_xpath("/root/descendant::*[@class]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 3);  // level2a, level2b, level2c
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AncestorWithAttribute) {
    auto result = eval_xpath("//level3[@id='l3b']/ancestor::*[@id='l1']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_NamespaceAware) {
    auto result = eval_xpath("//ns1:level2c/child::*");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_DescendantOrSelfWithPredicate) {
    auto result = eval_xpath("/root/descendant-or-self::*[@id='l3a']");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_FollowingSiblingWithPosition) {
    auto result = eval_xpath("/root/level1/following-sibling::*[position()=1]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    auto name = get_nodeset_element_name(result, 0);
    EXPECT_EQ(name, "sibling1");
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_PrecedingSiblingWithLast) {
    auto result = eval_xpath("/root/sibling3/preceding-sibling::*[last()]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AttributeInPredicate) {
    auto result = eval_xpath("/root/descendant::*[@id='l2b']/attribute::attr");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_ChildWithCount) {
    auto result = eval_xpath("count(/root/child::*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_DescendantWithName) {
    auto result = eval_xpath("name(/root/descendant::*[1])");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_TYPE(result, TAURUS_XPATH_STRING);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_ParentFromAttribute) {
    auto result = eval_xpath("//level2b/@class/parent::*/child::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_SelfInPath) {
    auto result = eval_xpath("/root/level1/self::*/level2a");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_FollowingWithDescendant) {
    auto result = eval_xpath("/root/level1/level2a/following::*/descendant-or-self::*");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GT(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_ComplexPredicate) {
    auto result = eval_xpath("/root/child::*[count(child::*)>0]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);  // level1 and sibling3 have children
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AncestorOrSelfWithCount) {
    auto result = eval_xpath("count(//level3[@id='l3a']/ancestor-or-self::*)");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NUMBER(result, 4.0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_AllAxesCombined) {
    auto result = eval_xpath("//level3[@id='l3c']/parent::*/preceding-sibling::*/descendant::level3");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 1);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_NamespaceInherited) {
    auto result = eval_xpath("//ns1:level3/namespace::ns1");
    ASSERT_NE(result, nullptr);
    size_t count = taurus_xpath_result_count(result);
    EXPECT_GE(count, 0);
    taurus_xpath_result_free(result);
}

TEST_F(XPathAxesTest, Integration_PredicateWithAxis) {
    auto result = eval_xpath("/root/level1/*[following-sibling::*]");
    ASSERT_NE(result, nullptr);
    EXPECT_XPATH_NODESET_SIZE(result, 2);  // level2a and level2b have following siblings
    taurus_xpath_result_free(result);
}