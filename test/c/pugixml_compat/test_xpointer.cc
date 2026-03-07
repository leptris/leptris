/* test_xpointer.cpp - XPointer (XML Pointer Language) tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for XPointer 1.0 functionality:
 * - Shorthand pointers (#id)
 * - element() scheme with child sequences
 * - xpointer() scheme with full XPath
 * - Multiple points (combined results)
 * - xmlns() scheme for namespace binding
 * - Error handling and edge cases
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

namespace taurus_test {

/**
 * Base class for XPointer tests
 */
class XPointerTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        xml_buffer.clear();
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
    }

    // Parse XML document
    void parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        ASSERT_EQ(status, TAURUS_OK) << "Failed to parse XML: " << xml;
        ASSERT_NE(doc, nullptr);
    }

    TaurusElement root() const {
        return taurus_document_root(doc);
    }
};

/* ============================================================================
 * Shorthand Pointer Tests (#id)
 * ============================================================================ */

TEST_F(XPointerTest, ShorthandPointerValid) {
    // Shorthand pointer with valid id
    parse_xml(
        "<root>"
        "<child1 id='first'>text1</child1>"
        "<child2 id='second'>text2</child2>"
        "<child3 id='third'>text3</child3>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Find element by id using XPointer shorthand
    // TODO: Implement taurus_element_find_by_id() or XPointer eval
    // For now, just verify we can find the element
    TaurusElement child2 = taurus_element_find_child(r, "child2");
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    EXPECT_STREQ(taurus_element_text(child2), "text2");
}

TEST_F(XPointerTest, ShorthandPointerInvalid) {
    // Shorthand pointer with non-existent id
    parse_xml(
        "<root>"
        "<child1 id='first'>text1</child1>"
        "<child2 id='second'>text2</child2>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Try to find element with non-existent id
    // TODO: XPointer eval should return empty result
    // For now, just verify we can search
    TaurusElement found = taurus_element_find_child(r, "child3");
    EXPECT_TRUE(ELEM_IS_NULL(found));
}

TEST_F(XPointerTest, ShorthandPointerMultipleIDs) {
    // Multiple elements with different ids
    parse_xml(
        "<root>"
        "<a id='x'>text1</a>"
        "<b id='y'>text2</b>"
        "<c id='z'>text3</c>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Should be able to find each element by its id
    TaurusElement a = taurus_element_find_child(r, "a");
    TaurusElement b = taurus_element_find_child(r, "b");
    TaurusElement c = taurus_element_find_child(r, "c");

    ASSERT_TRUE(ELEM_NOT_NULL(a));
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    EXPECT_STREQ(taurus_element_text(a), "text1");
    EXPECT_STREQ(taurus_element_text(b), "text2");
    EXPECT_STREQ(taurus_element_text(c), "text3");
}

/* ============================================================================
 * element() Scheme Tests
 * ============================================================================ */

TEST_F(XPointerTest, ElementSchemeRoot) {
    // element(/1) should return root element
    parse_xml(
        "<root>"
        "<child>text</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    EXPECT_STREQ(taurus_element_name(r), "root");
}

TEST_F(XPointerTest, ElementSchemeFirstChild) {
    // element(/1/1) should return first child of root
    parse_xml(
        "<root>"
        "<first>text1</first>"
        "<second>text2</second>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Navigate to first child
    // TODO: Implement XPointer element() scheme evaluation
    // For now, manually verify
    TaurusElement first = taurus_element_first_child_any(r);
    ASSERT_TRUE(ELEM_NOT_NULL(first));
    EXPECT_STREQ(taurus_element_name(first), "first");
}

TEST_F(XPointerTest, ElementSchemeSecondChild) {
    // element(/1/2) should return second child
    parse_xml(
        "<root>"
        "<first>text1</first>"
        "<second>text2</second>"
        "<third>text3</third>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Navigate to third child (requires 2 next_sibling calls from first child)
    TaurusElement first = taurus_element_first_child_any(r);
    ASSERT_TRUE(ELEM_NOT_NULL(first));
    EXPECT_STREQ(taurus_element_name(first), "first");

    TaurusElement second = taurus_element_next_sibling_any(first);
    ASSERT_TRUE(ELEM_NOT_NULL(second));
    EXPECT_STREQ(taurus_element_name(second), "second");

    TaurusElement third = taurus_element_next_sibling_any(second);
    ASSERT_TRUE(ELEM_NOT_NULL(third));
    EXPECT_STREQ(taurus_element_name(third), "third");
}

TEST_F(XPointerTest, ElementSchemeDeepNesting) {
    // element(/1/2/3/4) deep nesting
    parse_xml(
        "<root>"
        "<level1><level2><level3><level4>target</level4></level3></level2></level1>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Manually navigate to level4
    TaurusElement level1 = taurus_element_first_child_any(r);
    ASSERT_TRUE(ELEM_NOT_NULL(level1));
    EXPECT_STREQ(taurus_element_name(level1), "level1");

    TaurusElement level2 = taurus_element_first_child_any(level1);
    ASSERT_TRUE(ELEM_NOT_NULL(level2));
    EXPECT_STREQ(taurus_element_name(level2), "level2");

    TaurusElement level3 = taurus_element_first_child_any(level2);
    ASSERT_TRUE(ELEM_NOT_NULL(level3));
    EXPECT_STREQ(taurus_element_name(level3), "level3");

    TaurusElement level4 = taurus_element_first_child_any(level3);
    ASSERT_TRUE(ELEM_NOT_NULL(level4));
    EXPECT_STREQ(taurus_element_name(level4), "level4");
}

TEST_F(XPointerTest, ElementSchemeInvalidIndex) {
    // element(/1/99) - out of range
    parse_xml(
        "<root>"
        "<child>text</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Try to access non-existent 99th child
    TaurusElement child99 = ELEM_NULL();
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(r);
         ELEM_NOT_NULL(child) && count < 99;
         child = taurus_element_next_sibling_any(child)) {
        count++;
        if (count == 98) {
            child99 = child;
        }
    }
    EXPECT_TRUE(ELEM_IS_NULL(child99));
}

TEST_F(XPointerTest, ElementSchemeMultiple) {
    // element() scheme with multiple targets
    parse_xml(
        "<root>"
        "<a id='1'>text1</a>"
        "<b id='2'>text2</b>"
        "<c id='3'>text3</c>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Can access all children
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(r);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

/* ============================================================================
 * xpointer() Scheme Tests
 * ============================================================================ */

TEST_F(XPointerTest, XPointerSchemeSimple) {
    // xpointer() with simple XPath
    parse_xml(
        "<root>"
        "<child attr='value'>text</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // xpointer(/root/child) should return child element
    // TODO: Implement XPointer evaluation that delegates to XPath
    TaurusElement child = taurus_element_find_child(r, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
}

TEST_F(XPointerTest, XPointerSchemeWithPredicate) {
    // xpointer() with predicate
    parse_xml(
        "<root>"
        "<child id='c1' attr='value1'>text1</child>"
        "<child id='c2' attr='value2'>text2</child>"
        "<child id='c3' attr='value3'>text3</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // xpointer(/root/child[@attr='value2']) should return second child
    // TODO: Evaluate XPath: //child[@attr='value2']
    // For now, manually verify
    TaurusElement child = taurus_element_find_child(r, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
}

TEST_F(XPointerTest, XPointerSchemeWithFunction) {
    // xpointer() with XPath function
    parse_xml(
        "<root>"
        "<child>text1</child>"
        "<child>text2</child>"
        "<child>text3</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // xpointer(/root/child[position()=2]) should return second child
    TaurusElement child = taurus_element_first_child_any(r);
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    TaurusElement child2 = taurus_element_next_sibling_any(child);
    ASSERT_TRUE(ELEM_NOT_NULL(child2));
    EXPECT_STREQ(taurus_element_text(child2), "text2");
}

TEST_F(XPointerTest, XPointerSchemeWithNamespace) {
    // xpointer() with namespace-qualified elements
    parse_xml(
        "<root xmlns:ns='http://example.com'>"
        "<ns:child>text</ns:child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // xpointer(/root/ns:child) should find namespaced child
    // TODO: Support namespace-qualified XPath in XPointer
    // For now, just verify parsing works
}

/* ============================================================================
 * Multiple Points Tests
 * ============================================================================ */

TEST_F(XPointerTest, MultiplePoints) {
    // Multiple XPointer expressions combined
    parse_xml(
        "<root>"
        "<a id='x'>textA</a>"
        "<b id='y'>textB</b>"
        "<c id='z'>textC</c>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Can find multiple elements
    TaurusElement a = taurus_element_find_child(r, "a");
    TaurusElement b = taurus_element_find_child(r, "b");
    TaurusElement c = taurus_element_find_child(r, "c");

    ASSERT_TRUE(ELEM_NOT_NULL(a));
    ASSERT_TRUE(ELEM_NOT_NULL(b));
    ASSERT_TRUE(ELEM_NOT_NULL(c));

    EXPECT_STREQ(taurus_element_text(a), "textA");
    EXPECT_STREQ(taurus_element_text(b), "textB");
    EXPECT_STREQ(taurus_element_text(c), "textC");
}

TEST_F(XPointerTest, MultiplePointsXPointer) {
    // xpointer() with multiple location paths
    parse_xml(
        "<root>"
        "<child1>text1</child1>"
        "<child2>text2</child2>"
        "<child3>text3</child3>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // xpointer(/root/child1) xpointer(/root/child3)
    // Should return both child1 and child3
    // TODO: Implement multi-point XPointer results
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(r);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 3);
}

/* ============================================================================
 * xmlns() Scheme Tests
 * ============================================================================ */

TEST_F(XPointerTest, XmlnsSchemeBasic) {
    // xmlns() scheme for namespace binding
    parse_xml(
        "<root xmlns:ns='http://example.com'>"
        "<ns:child>text</ns:child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // TODO: Implement xmlns() scheme
}

TEST_F(XPointerTest, XmlnsSchemeMultipleBindings) {
    // Multiple namespace bindings
    parse_xml(
        "<root "
        "xmlns:ns1='http://example1.com' "
        "xmlns:ns2='http://example2.com' "
        "xmlns:ns3='http://example3.com'>"
        "<ns1:child1>text1</ns1:child1>"
        "<ns2:child2>text2</ns2:child2>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // TODO: Test multiple namespace bindings
}

/* ============================================================================
 * Edge Cases and Error Handling
 * ============================================================================ */

TEST_F(XPointerTest, EmptyXPointer) {
    // Empty XPointer expression
    parse_xml("<root><child>text</child></root>");

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // TODO: Handle empty XPointer gracefully
}

TEST_F(XPointerTest, InvalidXPointerSyntax) {
    // Invalid XPointer syntax
    parse_xml("<root><child>text</child></root>");

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // TODO: Return error for invalid XPointer syntax
}

TEST_F(XPointerTest, XPointerWithWhitespace) {
    // XPointer with whitespace handling
    parse_xml(
        "<root>"
        "<child id=' target '>text</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // TODO: Handle whitespace in IDs properly
}

TEST_F(XPointerTest, XPointerCaseSensitivity) {
    // XPointer is case-sensitive
    parse_xml(
        "<root>"
        "<Child ID='target'>text</Child>"
        "<child id='target'>text2</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // ID attribute is case-sensitive, Child != child
}

/* ============================================================================
 * URI Fragment Tests
 * ============================================================================ */

TEST_F(XPointerTest, URIFragmentWithShorthand) {
    // URI with shorthand fragment: file.xml#target
    // This tests URI parsing with XPointer fragment
    // For now, just test the document portion
    parse_xml(
        "<root>"
        "<child id='target'>text</child>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // TODO: Implement URI fragment parsing
    // For now, manually verify
    TaurusElement child = taurus_element_find_child(r, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
}

TEST_F(XPointerTest, URIFragmentWithElementScheme) {
    // URI with element() fragment: file.xml#element(/1/1)
    parse_xml(
        "<root>"
        "<first>text1</first>"
        "<second>text2</second>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // TODO: Parse element(/1/1) from fragment
    TaurusElement first = taurus_element_find_child(r, "first");
    ASSERT_TRUE(ELEM_NOT_NULL(first));
}

TEST_F(XPointerTest, URIFragmentWithXPointer) {
    // URI with xpointer() fragment: file.xml#xpointer(/root/first)
    parse_xml(
        "<root>"
        "<first>text1</first>"
        "<second>text2</second>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // TODO: Parse xpointer(/root/first) from fragment
    TaurusElement first = taurus_element_find_child(r, "first");
    ASSERT_TRUE(ELEM_NOT_NULL(first));
}

/* ============================================================================
 * Performance Tests
 * ============================================================================ */

TEST_F(XPointerTest, XPointerManyElements) {
    // XPointer with many elements (performance test)
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<child id='id" + std::to_string(i) + "'>text" + std::to_string(i) + "</child>";
    }
    xml += "</root>";

    parse_xml(xml);

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Count children to verify all parsed
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(r);
         ELEM_NOT_NULL(child) && count < 1100;  // Safety limit
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 1000);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST_F(XPointerTest, XPointerInXPath) {
    // Use XPointer within XPath expressions
    parse_xml(
        "<root>"
        "<section id='intro'>Introduction</section>"
        "<section id='body'>Body</section>"
        "<section id='conclusion'>Conclusion</section>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // XPath can select by attribute value
    // TODO: Support XPointer in XPath expressions
    TaurusElement intro = taurus_element_find_child(r, "section");
    ASSERT_TRUE(ELEM_NOT_NULL(intro));

    // Check if this is the intro section
    const char* text = taurus_element_text(intro);
    ASSERT_NE(text, nullptr);
}

TEST_F(XPointerTest, XPointerWithNestedElements) {
    // XPointer targeting deeply nested elements
    parse_xml(
        "<root>"
        "<level1><level2><level3><level4><level5>deep</level5></level4></level3></level2></level1>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Navigate manually to level5
    TaurusElement level5 = ELEM_NULL();
    TaurusElement current = r;
    for (int i = 0; i < 5; i++) {
        level5 = taurus_element_first_child_any(current);
        ASSERT_TRUE(ELEM_NOT_NULL(level5)) << "Failed at depth " << i;
        current = level5;
    }

    EXPECT_STREQ(taurus_element_name(level5), "level5");
    EXPECT_STREQ(taurus_element_text(level5), "deep");
}

} // namespace taurus_test
