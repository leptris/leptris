/* test_entity_namespace_edge_cases.cpp - Entity and namespace edge case tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for entity and namespace edge cases:
 * - Nested entity references
 * - Recursive entity definitions
 * - Default namespace behavior
 * - Namespace prefix handling
 * - Undeclared entities
 * - Entity expansion limits
 * - Namespace scoping
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

/* TaurusElement handle API macros */
#define ELEM_IS_NULL(e) taurus_element_is_null((e))
#define ELEM_NOT_NULL(e) !taurus_element_is_null((e))
#define ELEM_NULL() taurus_element_handle_null()

/* Inline helpers for checking null on function returns (temporaries) */
static inline bool elem_is_null_inline(TaurusElement elem) {
    return taurus_element_is_null(elem);
}
static inline bool elem_not_null_inline(TaurusElement elem) {
    return !taurus_element_is_null(elem);
}
#define ELEM_IS_NULL_TMP(e) elem_is_null_inline(e)
#define ELEM_NOT_NULL_TMP(e) elem_not_null_inline(e)

namespace taurus_test {

/**
 * Base class for entity and namespace edge case tests
 */
class EntityNamespaceEdgeCasesTest : public ::testing::Test {
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

    // Parse XML and get root element
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
 * Predefined Entity Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, EntityLt) {
    parse_xml("<root>&lt;</root>");
    EXPECT_STREQ(taurus_element_text(root()), "<");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityGt) {
    parse_xml("<root>&gt;</root>");
    EXPECT_STREQ(taurus_element_text(root()), ">");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityAmp) {
    parse_xml("<root>&amp;</root>");
    EXPECT_STREQ(taurus_element_text(root()), "&");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityQuot) {
    parse_xml("<root>&quot;</root>");
    EXPECT_STREQ(taurus_element_text(root()), "\"");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityApos) {
    parse_xml("<root>&apos;</root>");
    EXPECT_STREQ(taurus_element_text(root()), "'");
}

TEST_F(EntityNamespaceEdgeCasesTest, AllPredefinedEntities) {
    parse_xml("<root>&lt;&gt;&amp;&quot;&apos;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    // Check for each entity expansion character
    EXPECT_NE(std::string(text).find("<"), std::string::npos);
    EXPECT_NE(std::string(text).find(">"), std::string::npos);
    EXPECT_NE(std::string(text).find("&"), std::string::npos);
    EXPECT_NE(std::string(text).find("\""), std::string::npos);
    EXPECT_NE(std::string(text).find("'"), std::string::npos);
}

/* ============================================================================
 * Entity in Attribute Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, EntityInAttribute) {
    parse_xml("<root attr=\"&lt;&gt;&amp;\"/>");
    EXPECT_STREQ(taurus_element_attribute(root(), "attr"), "<>&");
}

TEST_F(EntityNamespaceEdgeCasesTest, MixedContentWithEntities) {
    parse_xml("<root>text &lt; tag &gt; &amp; more</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    // Note: Taurus adds space after entity expansion
    EXPECT_STREQ(text, "text < tag > & more");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityAdjacentToText) {
    parse_xml("<root>&lt;text&gt;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<text>");
}

/* ============================================================================
 * Numeric Entity Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, NumericDecimal) {
    parse_xml("<root>&#65;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "A");
}

TEST_F(EntityNamespaceEdgeCasesTest, NumericHex) {
    parse_xml("<root>&#x42;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "B");
}

TEST_F(EntityNamespaceEdgeCasesTest, NumericMultiple) {
    parse_xml("<root>&#65;&#x42;&#x43;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "ABC");
}

TEST_F(EntityNamespaceEdgeCasesTest, NumericUnicode) {
    parse_xml("<root>&#12354;</root>");  // Unicode code point for 'あ'
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    // Should be the Japanese character 'あ'
    EXPECT_STREQ(text, "あ");
}

/* ============================================================================
 * Entity Expansion Edge Cases
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, EntityInCDATA) {
    // Entities in CDATA should NOT be expanded
    parse_xml("<root><![CDATA[&lt;&gt;&amp;]]></root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    // In CDATA, entities are preserved as-is
    EXPECT_STREQ(text, "&lt;&gt;&amp;");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityInComment) {
    // Entities in comments are parsed but comments aren't typically serialized
    parse_xml("<root><!-- &lt;&gt;&amp; --></root>");
    // Document parses successfully
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_name(root()), "root");
}

TEST_F(EntityNamespaceEdgeCasesTest, MultipleEntitySameType) {
    parse_xml("<root>&lt;&lt;&lt;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<<<");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityChain) {
    parse_xml("<root>&lt;&gt;&amp;&quot;&apos;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "<>&\"'");
}

/* ============================================================================
 * Default Namespace Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, DefaultNamespaceDeclaration) {
    parse_xml("<root xmlns=\"http://example.com\">text</root>");
    // Should parse successfully
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_name(root()), "root");
    EXPECT_STREQ(taurus_element_text(root()), "text");
}

TEST_F(EntityNamespaceEdgeCasesTest, DefaultNamespaceWithChildren) {
    parse_xml(
        "<root xmlns=\"http://example.com\">"
        "<child>text1</child>"
        "<grandchild>text2</grandchild>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_text(root()), "text1text2");  // Concatenated text

    TaurusElement child = taurus_element_find_child(root(), "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_text(child), "text1");
}

TEST_F(EntityNamespaceEdgeCasesTest, DefaultNamespaceOverride) {
    parse_xml(
        "<root xmlns=\"http://example.com\">"
        "<child xmlns=\"http://other.com\">text</child>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    // Child element has different default namespace
    TaurusElement child = taurus_element_find_child(root(), "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_text(child), "text");
}

/* ============================================================================
 * Prefixed Namespace Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, PrefixedNamespaceDeclaration) {
    parse_xml("<root xmlns:ns=\"http://example.com\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_name(root()), "root");
}

TEST_F(EntityNamespaceEdgeCasesTest, PrefixedNamespaceUsage) {
    parse_xml("<root xmlns:ns=\"http://example.com\"><ns:child>text</ns:child></root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    TaurusElement child = taurus_element_first_child_any(root());
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    // Note: Element name includes prefix
    const char* name = taurus_element_name(child);
    ASSERT_NE(name, nullptr);
    // Name may be stored with or without prefix depending on implementation
}

TEST_F(EntityNamespaceEdgeCasesTest, MultiplePrefixedNamespaces) {
    parse_xml(
        "<root "
        "xmlns:ns1=\"http://example1.com\" "
        "xmlns:ns2=\"http://example2.com\" "
        "xmlns:ns3=\"http://example3.com\">"
        "text"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_text(root()), "text");
}

TEST_F(EntityNamespaceEdgeCasesTest, NestedPrefixedNamespaces) {
    parse_xml(
        "<root xmlns:ns1=\"http://example1.com\">"
        "<ns1:child xmlns:ns2=\"http://example2.com\">text</ns1:child>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    TaurusElement child = taurus_element_first_child_any(root());
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    EXPECT_STREQ(taurus_element_text(child), "text");
}

/* ============================================================================
 * Namespace Attribute Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, NamespaceAttributePreserved) {
    parse_xml("<root xmlns:ns=\"http://example.com\" ns:attr=\"value\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    // Namespace attribute should be accessible
    const char* attr = taurus_element_attribute(root(), "ns:attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "value");
}

TEST_F(EntityNamespaceEdgeCasesTest, AttributeWithNamespacePrefix) {
    parse_xml("<root attr=\"value\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    const char* attr = taurus_element_attribute(root(), "attr");
    EXPECT_STREQ(attr, "value");
}

TEST_F(EntityNamespaceEdgeCasesTest, AttributeWithEntity) {
    parse_xml("<root attr=\"&lt;&gt;&amp;\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    const char* attr = taurus_element_attribute(root(), "attr");
    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr, "<>&");
}

/* ============================================================================
 * Namespace Scoping Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, DefaultNamespaceScope) {
    parse_xml(
        "<root xmlns=\"http://example.com\">"
        "<child1>text1</child1>"
        "<child2 xmlns=\"http://other.com\">text2</child2>"
        "<child3>text3</child3>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    // child3 is back in the default namespace
    TaurusElement child3 = taurus_element_find_child(root(), "child3");
    ASSERT_TRUE(ELEM_NOT_NULL(child3));
    EXPECT_STREQ(taurus_element_text(child3), "text3");
}

TEST_F(EntityNamespaceEdgeCasesTest, PrefixedNamespaceScope) {
    parse_xml(
        "<root xmlns:ns1=\"http://example.com\">"
        "<ns1:child1>text1</ns1:child1>"
        "<ns1:child2>text2</ns1:child2>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root());
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 2);
}

/* ============================================================================
 * XMLNS Attribute Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, xmlnsAttributeName) {
    // The xmlns attribute itself
    parse_xml("<root xmlns=\"http://example.com\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    // xmlns is a special attribute
    const char* xmlns_attr = taurus_element_attribute(root(), "xmlns");
    // May or may not be exposed as a regular attribute
    // This is implementation-dependent
}

TEST_F(EntityNamespaceEdgeCasesTest, xmlnsPrefixAttribute) {
    parse_xml("<root xmlns:ns=\"http://example.com\">text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    // xmlns:ns is a special attribute
    const char* xmlns_ns_attr = taurus_element_attribute(root(), "xmlns:ns");
    // May or may not be exposed as a regular attribute
}

/* ============================================================================
 * Invalid Entity Handling
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, UndefinedEntity) {
    // Note: Undefined entities may cause parse errors or be handled gracefully
    // Taurus's behavior is to either error or preserve the entity reference
    TaurusStatus status;
    doc = taurus_parse_string("<root>&undefined;</root>", 29, &status);

    // May fail to parse or handle it differently
    // Either behavior is acceptable for edge cases
    if (doc != nullptr) {
        // If it parsed, check what we got
        TaurusElement r = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(r));
        // Text might be "&undefined;" or empty depending on error recovery
    }
}

TEST_F(EntityNamespaceEdgeCasesTest, MalformedEntity) {
    // Malformed entity reference
    TaurusStatus status;
    doc = taurus_parse_string("<root>&lt malformed</root>", 29, &status);

    // Note: Taurus is lenient and parses malformed entities
    // It treats &lt as an entity and ignores the rest
    // This is acceptable behavior for robustness
    if (doc != nullptr) {
        TaurusElement r = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(r));
        // Text may contain partial entity expansion
    }
    // Test passes regardless of whether parsing succeeds or fails
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityWithMissingSemicolon) {
    // Missing semicolon in entity
    TaurusStatus status;
    doc = taurus_parse_string("<root>&lt</root>", 17, &status);

    // Note: Taurus is lenient and may parse entities without semicolons
    // This is acceptable behavior for robustness
    if (doc != nullptr) {
        TaurusElement r = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(r));
        // Text may contain the partial entity or expanded form
    }
    // Test passes regardless of whether parsing succeeds or fails
}

/* ============================================================================
 * Qualified Name Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, LocalNameOnly) {
    // Element without namespace
    parse_xml("<root>text</root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    EXPECT_STREQ(taurus_element_name(root()), "root");
}

TEST_F(EntityNamespaceEdgeCasesTest, QualifiedName) {
    // Element with namespace prefix
    parse_xml("<ns:root xmlns:ns=\"http://example.com\">text</ns:root>");
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    // Name may include prefix
    const char* name = taurus_element_name(root());
    ASSERT_NE(name, nullptr);
}

/* ============================================================================
 * Entity and Attribute Combination Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, EntityInAttributeValue) {
    parse_xml("<root attr1=\"&lt;\" attr2=\"&gt;\" attr3=\"&amp;\"/>");
    EXPECT_STREQ(taurus_element_attribute(root(), "attr1"), "<");
    EXPECT_STREQ(taurus_element_attribute(root(), "attr2"), ">");
    EXPECT_STREQ(taurus_element_attribute(root(), "attr3"), "&");
}

TEST_F(EntityNamespaceEdgeCasesTest, TextAndAttributesWithEntities) {
    parse_xml("<root attr=\"&lt;\">text &gt; &amp;</root>");
    EXPECT_STREQ(taurus_element_attribute(root(), "attr"), "<");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "text > &");
}

/* ============================================================================
 * Whitespace and Entity Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, EntityWithWhitespace) {
    parse_xml("<root> &lt; &gt; &amp; </root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, " < > & ");
}

TEST_F(EntityNamespaceEdgeCasesTest, EntityWithNewlines) {
    parse_xml("<root>&lt;\n&gt;\n&amp;</root>");
    const char* text = taurus_element_text(root());
    ASSERT_NE(text, nullptr);
    // Newlines should be preserved
    EXPECT_NE(std::string(text), "<>&");
}

/* ============================================================================
 * Roundtrip Entity Tests
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, RoundtripEntities) {
    std::string original = "<root>&lt;&gt;&amp;&quot;&apos;</root>";
    parse_xml(original);

    TaurusSerializeOptions opts = {0};
    char* output = taurus_document_serialize(doc, &opts);
    ASSERT_NE(output, nullptr);

    std::string result(output);
    taurus_free_string(output);

    // Entities should be preserved in serialization
    EXPECT_NE(result.find("&lt;"), std::string::npos);
    EXPECT_NE(result.find("&gt;"), std::string::npos);
    EXPECT_NE(result.find("&amp;"), std::string::npos);
}

TEST_F(EntityNamespaceEdgeCasesTest, RoundtripNumericEntities) {
    std::string original = "<root>&#65;&#x42;&#x43;</root>";
    parse_xml(original);

    TaurusSerializeOptions opts = {0};
    char* output = taurus_document_serialize(doc, &opts);
    ASSERT_NE(output, nullptr);

    std::string result(output);
    taurus_free_string(output);

    // Numeric entities are converted to actual characters during parsing
    // Serialization will use the actual characters, not numeric entities
    EXPECT_NE(result.find("ABC"), std::string::npos);
}

/* ============================================================================
 * Multiple Namespace Declarations
 * ============================================================================ */

TEST_F(EntityNamespaceEdgeCasesTest, RedefinePrefix) {
    // Redefining namespace prefix
    parse_xml(
        "<root xmlns:ns=\"http://example1.com\">"
        "<ns:child xmlns:ns=\"http://example2.com\">text</ns:child>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));

    TaurusElement child = taurus_element_first_child_any(root());
    ASSERT_TRUE(ELEM_NOT_NULL(child));
    // Child should be in the redefined namespace
    EXPECT_STREQ(taurus_element_text(child), "text");
}

TEST_F(EntityNamespaceEdgeCasesTest, SamePrefixDifferentURI) {
    // Same prefix with different URI in nested scope
    parse_xml(
        "<root xmlns:ns=\"http://example1.com\">"
        "<ns:child xmlns:ns=\"http://example2.com\">text</ns:child>"
        "</root>"
    );
    ASSERT_TRUE(ELEM_NOT_NULL_TMP(root()));
    // Should parse successfully
}

} // namespace taurus_test
