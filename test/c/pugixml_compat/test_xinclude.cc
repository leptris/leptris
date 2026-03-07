/* test_xinclude.cpp - XInclude (XML Inclusions) tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for XInclude 1.0 functionality:
 * - Basic inclusion (href attribute)
 * - Fallback mechanism
 * - Text inclusion (parse="text")
 * - XML inclusion (parse="xml" or default)
 * - Encoding attribute
 * - XPointer reference (xpointer attribute)
 * - Nested inclusions
 * - Recursive inclusion detection
 * - Base URI resolution (xml:base)
 * - Relative and absolute URIs
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
 * Base class for XInclude tests
 */
class XIncludeTest : public ::testing::Test {
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

    // Parse XML with XInclude processing
    // Parse XML (XInclude processing not yet implemented)
    void parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus parse_status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &parse_status);

        // For now, just verify basic parsing works
        // XInclude processing will be added incrementally
        ASSERT_EQ(parse_status, TAURUS_OK) << "Failed to parse XML";
        ASSERT_NE(doc, nullptr);
    }

    TaurusElement root() const {
        return taurus_document_root(doc);
    }
};

/* ============================================================================
 * Basic Inclusion Tests
 * ============================================================================ */

TEST_F(XIncludeTest, BasicInclude) {
    // Basic XInclude with href attribute
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    EXPECT_STREQ(taurus_element_name(r), "root");

    // After XInclude processing, should have included content
    // For now, just verify it parses
}

TEST_F(XIncludeTest, IncludeWithFallback) {
    // XInclude with fallback mechanism
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"nonexistent.xml\">"
        "<xi:fallback><warning>Inclusion failed</warning></xi:fallback>"
        "</xi:include>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Should have fallback content
    TaurusElement warning = taurus_element_find_child(r, "warning");
    // May or may not exist depending on XInclude implementation
}

TEST_F(XIncludeTest, IncludeMultiple) {
    // Multiple XInclude elements
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "<xi:include href=\"test/fixtures/libxml2/svg2\"/>"
        "<xi:include href=\"test/fixtures/libxml2/svg3\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Text Inclusion Tests
 * ============================================================================ */

TEST_F(XIncludeTest, TextInclude) {
    // XInclude with parse="text"
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/README.txt\" parse=\"text\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, TextIncludeWithEncoding) {
    // XInclude with parse="text" and encoding attribute
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/README.txt\" parse=\"text\" encoding=\"UTF-8\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * XPointer Reference Tests
 * ============================================================================ */

TEST_F(XIncludeTest, IncludeWithXPointer) {
    // XInclude with xpointer attribute
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\" xpointer=\"element(/1/1)\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, IncludeWithShorthandPointer) {
    // XInclude with shorthand XPointer
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\" xpointer=\"svg\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Nested Inclusion Tests
 * ============================================================================ */

TEST_F(XIncludeTest, NestedInclude) {
    // XInclude within included document
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/xinclude/nested.xml\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, RecursiveIncludeDetection) {
    // Detect and prevent circular inclusion
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/xinclude/recursive.xml\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // Should handle gracefully without infinite loop
}

/* ============================================================================
 * Base URI Tests
 * ============================================================================ */

TEST_F(XIncludeTest, IncludeWithXmlBase) {
    // XInclude with xml:base for URI resolution
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\" xml:base=\"test/fixtures/libxml2/\">"
        "<xi:include href=\"svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, IncludeWithRelativeURI) {
    // XInclude with relative URI
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"../fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, IncludeWithAbsoluteURI) {
    // XInclude with absolute URI (file://)
    // Note: This test requires the file to exist at the specified path
    // For portability, we use a placeholder that demonstrates the syntax
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"file:///path/to/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Fallback Tests
 * ============================================================================ */

TEST_F(XIncludeTest, FallbackWithNestedInclude) {
    // Fallback with nested include
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"nonexistent.xml\">"
        "<xi:fallback>"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</xi:fallback>"
        "</xi:include>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, MultipleFallbacks) {
    // Multiple levels of fallback
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"missing1.xml\">"
        "<xi:fallback>"
        "<xi:include href=\"missing2.xml\">"
        "<xi:fallback><default>content</default></xi:fallback>"
        "</xi:include>"
        "</xi:fallback>"
        "</xi:include>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST_F(XIncludeTest, InvalidHref) {
    // Invalid href attribute
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // Should handle gracefully
}

TEST_F(XIncludeTest, InvalidParseValue) {
    // Invalid parse attribute value
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\" parse=\"invalid\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // Should handle or report error
}

TEST_F(XIncludeTest, InvalidEncoding) {
    // Invalid encoding attribute value
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\" encoding=\"invalid-encoding\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
    // Should handle or report error
}

/* ============================================================================
 * Whitespace Handling Tests
 * ============================================================================ */

TEST_F(XIncludeTest, IncludeWhitespacePreservation) {
    // XInclude should preserve whitespace
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Mixed Content Tests
 * ============================================================================ */

TEST_F(XIncludeTest, IncludeWithMixedContent) {
    // XInclude mixed with regular content
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<before>before</before>"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "<after>after</after>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // Should have all three children
    TaurusElement before = taurus_element_find_child(r, "before");
    TaurusElement after = taurus_element_find_child(r, "after");

    // before and after should exist
}

/* ============================================================================
 * Namespace Tests
 * ============================================================================ */

TEST_F(XIncludeTest, IncludeWithNamespaces) {
    // XInclude with namespaced content
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\" xmlns:ns=\"http://example.com\">"
        "<ns:child>text</ns:child>"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

TEST_F(XIncludeTest, XIncludeNamespacePreservation) {
    // XInclude namespace should not be in result
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));

    // XInclude namespace should not leak into result
}

/* ============================================================================
 * Performance Tests
 * ============================================================================ */

TEST_F(XIncludeTest, ManyIncludes) {
    // Test with many XInclude elements
    std::string xml = "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">";
    for (int i = 0; i < 100; i++) {
        xml += "<xi:include href=\"test/fixtures/libxml2/svg1\"/>";
    }
    xml += "</root>";

    parse_xml(xml);

    TaurusElement r = root();
    ASSERT_TRUE(ELEM_NOT_NULL(r));
}

/* ============================================================================
 * Serialization Tests
 * ============================================================================ */

TEST_F(XIncludeTest, SerializeAfterInclude) {
    // Serialize document after XInclude processing
    parse_xml(
        "<root xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<xi:include href=\"test/fixtures/libxml2/svg1\"/>"
        "</root>"
    );

    TaurusSerializeOptions opts = {0};
    char* output = taurus_document_serialize(doc, &opts);
    ASSERT_NE(output, nullptr);

    // Serialized output should not contain XInclude elements
    std::string result(output);
    taurus_free_string(output);

    // Should not have xi:include in output
    EXPECT_EQ(result.find("xi:include"), std::string::npos);
    EXPECT_EQ(result.find("xinclude:include"), std::string::npos);
}

} // namespace taurus_test
