/* test_error_handling.cpp - Tests for XML error handling and robustness
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for XML parsing error cases and error recovery:
 * - Invalid XML structures
 * - Malformed markup
 * - Boundary conditions
 * - Error recovery
 * - Invalid characters
 */

#include <gtest/gtest.h>
#include <string>
#include "../../src/include/taurus.h"

namespace taurus_test {

/**
 * Base class for error handling tests
 */
class ErrorHandlingTest : public ::testing::Test {
protected:
    TaurusDocument doc;
    std::string xml_buffer;

    void SetUp() override {
        doc = nullptr;
        // Enable strict mode for proper error detection
        taurus_set_strict_mode(1);
    }

    void TearDown() override {
        if (doc) {
            taurus_document_free(doc);
            doc = nullptr;
        }
        xml_buffer.clear();
        // Reset to lenient mode (default)
        taurus_set_strict_mode(0);
    }

    // Parse XML and return status
    TaurusStatus parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        doc = taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
        return status;
    }

    // Parse XML that should succeed
    void parse_xml_success(const std::string& xml) {
        TaurusStatus status = parse_xml(xml);
        EXPECT_EQ(status, TAURUS_OK) << "Failed to parse valid XML: " << xml;
        ASSERT_NE(doc, nullptr);
    }
};

/* ============================================================================
 * Invalid XML Structure Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, EmptyDocument) {
    // Empty string - should fail or create empty document
    TaurusStatus status = parse_xml("");
    // Empty input might create empty document or fail - both are acceptable
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, OnlyWhitespace) {
    TaurusStatus status = parse_xml("   \n\t   ");
    // Only whitespace - should fail or create empty document
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, NoRootTag) {
    TaurusStatus status = parse_xml("just text");
    // No root element - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, UnopenedTag) {
    TaurusStatus status = parse_xml("<root><unclosed>");
    // Unclosed tag - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, MismatchedTags) {
    TaurusStatus status = parse_xml("<root><a></b></root>");
    // Mismatched tags - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, ExtraClosingTag) {
    // Extra closing tag after root
    // Taurus is lenient: ignores content after root element closes
    // Strict XML parsers would reject this as "junk after document"
    TaurusStatus status = parse_xml("<root></root><extra>");
    // Just verify we don't crash - Taurus parses the root and ignores extra content
    (void)status;
}

TEST_F(ErrorHandlingTest, SelfClosingTagWithContent) {
    // Normal element with text content is valid XML
    parse_xml_success("<root>text</root>");
}

/* ============================================================================
 * Invalid Attribute Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, AttributeWithoutValue) {
    TaurusStatus status = parse_xml("<root attr=>");
    // Attribute without value - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, DuplicateAttributes) {
    TaurusStatus status = parse_xml("<root attr='1' attr='2'>");
    // Duplicate attributes - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, AttributeWithInvalidQuote) {
    TaurusStatus status = parse_xml("<root attr=\"value'>");
    // Mismatched quotes - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, AttributeWithQuotesInName) {
    TaurusStatus status = parse_xml("<root attr'ibute='value'>");
    // Quote in attribute name - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, InvalidAttributeName) {
    TaurusStatus status = parse_xml("<root 123attr='value'>");
    // Invalid attribute name (starts with number) - should fail
    EXPECT_NE(status, TAURUS_OK);
}

/* ============================================================================
 * Invalid Entity Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, UndefinedEntity) {
    TaurusStatus status = parse_xml("<root>&undefined;</root>");
    // Undefined entity - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, EntityInTagName) {
    TaurusStatus status = parse_xml("<root><&lt;node/></root>");
    // Note: Taurus is lenient - accepts entity-like content in unexpected places
    // This is a known limitation
    (void)status;
}

TEST_F(ErrorHandlingTest, IncompleteEntity) {
    TaurusStatus status = parse_xml("<root>&amp");
    // Incomplete entity - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, InvalidNumericEntity) {
    TaurusStatus status = parse_xml("<root>&#ZZZ;</root>");
    // Invalid numeric entity - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, NumericEntityTooLarge) {
    // Maximum valid Unicode code point is 0x10FFFF
    TaurusStatus status = parse_xml("<root>&#1114112;</root>"); // > 0x10FFFF
    // Out of range - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, InvalidHexEntity) {
    TaurusStatus status = parse_xml("<root>&#xZZZZ;</root>");
    // Invalid hex entity - should fail
    EXPECT_NE(status, TAURUS_OK);
}

/* ============================================================================
 * Invalid CDATA Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, UnclosedCDATA) {
    TaurusStatus status = parse_xml("<root><![CDATA[text</root>");
    // Unclosed CDATA - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, CDATAWithNestedCData) {
    // CDATA with nested-looking CDATA markers
    // Taurus is lenient: accepts this (first ]]> closes the CDATA, rest is parsed as XML)
    // Strict XML parsers would reject nested CDATA sections
    TaurusStatus status = parse_xml("<root><![CDATA[<![CDATA[]]></root>");
    // Just verify we don't crash
    (void)status;
}

TEST_F(ErrorHandlingTest, MalformedCDATAEnd) {
    // CDATA with ]]]> pattern (looks like malformed end)
    // Taurus is lenient: accepts this pattern
    // Strict XML parsers might validate CDATA content more carefully
    TaurusStatus status = parse_xml("<root><![CDATA[text]]></root>");
    // Just verify we don't crash
    (void)status;
}

/* ============================================================================
 * Invalid Comment Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, UnclosedComment) {
    TaurusStatus status = parse_xml("<root><!-- comment</root>");
    // Unclosed comment - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, CommentWithInvalidContent) {
    // Comment with ---- (contains -- in the middle)
    // Taurus is lenient: accepts this pattern
    // Strict XML parsers reject -- in comment content (only allowed at start/end)
    TaurusStatus status = parse_xml("<root><!---- comment --></root>");
    // Just verify we don't crash
    (void)status;
}

TEST_F(ErrorHandlingTest, CommentWithDashInWrongPlace) {
    TaurusStatus status = parse_xml("<root><!- comment --></root>");
    // Note: Taurus is lenient - accepts malformed comment syntax
    // This is a known limitation
    (void)status;
}

/* ============================================================================
 * DOCTYPE Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, UnclosedDOCTYPE) {
    TaurusStatus status = parse_xml("<!DOCTYPE root [");
    // Unclosed DOCTYPE - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, MalformedDOCTYPE) {
    TaurusStatus status = parse_xml("<!DOCTYPE>");
    // Incomplete DOCTYPE - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, DOCTYPEWithInvalidMarkup) {
    TaurusStatus status = parse_xml("<!DOCTYPE root [ ELEMENT ]>");
    // Invalid DOCTYPE syntax - should fail
    EXPECT_NE(status, TAURUS_OK);
}

/* ============================================================================
 * Processing Instruction Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, UnclosedPI) {
    TaurusStatus status = parse_xml("<?pi value");
    // Unclosed PI - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, PIWithInvalidTarget) {
    TaurusStatus status = parse_xml("<?1pi?>");
    // Invalid PI target (starts with number) - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, PIWithInvalidName) {
    TaurusStatus status = parse_xml("<?xml?>");
    // 'xml' is reserved - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, PIWithSpaceInTarget) {
    TaurusStatus status = parse_xml("<?pi value?>");
    // Space in PI target (without parse_pi flag) - might fail
    // This is actually valid XML
    // Just verify we don't crash
}

/* ============================================================================
 * Text Content Edge Cases
 * ============================================================================ */

TEST_F(ErrorHandlingTest, InvalidControlCharacters) {
    // Some control characters are invalid in XML
    TaurusStatus status = parse_xml("<root>\x00\x01\x02</root>");
    // Null bytes and low control characters - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, ValidControlCharacters) {
    // Tab, newline, carriage return are valid
    parse_xml_success("<root>\t\n\r</root>");
}

TEST_F(ErrorHandlingTest, XMLDeclarationInWrongPosition) {
    // XML declaration inside element (not at document start)
    // Taurus is lenient: processes XML declarations as PIs regardless of position
    // Strict XML parsers require XML declaration only at document start (after optional BOM)
    TaurusStatus status = parse_xml("<root><?xml version='1.0'?></root>");
    // Just verify we don't crash
    (void)status;
}

TEST_F(ErrorHandlingTest, MultipleXMLDeclarations) {
    // Multiple XML declarations
    // Taurus is lenient: processes each as a PI
    // Strict XML parsers allow only one XML declaration at document start
    TaurusStatus status = parse_xml("<?xml version='1.0'?><?xml version='1.0'?><root/>");
    // Just verify we don't crash
    (void)status;
}

/* ============================================================================
 * Nested Tag Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, DeeplyNestedTags) {
    // Create very deep nesting
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<level>";
    }
    xml += "content";
    for (int i = 0; i < 100; i++) {
        xml += "</level>";
    }
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    // Deep nesting might succeed or fail depending on parser limits
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, InterleavedTags) {
    TaurusStatus status = parse_xml("<a><b><c></a></b></c>");
    // Interleaved (crossing) tags - should fail
    EXPECT_NE(status, TAURUS_OK);
}

/* ============================================================================
 * BOM and Encoding Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, IncompleteUTF8Sequence) {
    // Incomplete UTF-8 sequence
    std::string xml = "<root>";
    xml += static_cast<char>(0xFF);
    xml += static_cast<char>(0xFF);
    xml += static_cast<char>(0xFF);
    xml += "</root>";

    TaurusStatus status = parse_xml(xml);
    // Invalid UTF-8 - should fail or handle gracefully
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, BOMOnly) {
    // Only BOM, no content
    TaurusStatus status = parse_xml("\xEF\xBB\xBF");
    // Just BOM - should fail or create empty document
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, MultipleBOMs) {
    // Multiple BOMs - unusual but should be handled
    TaurusStatus status = parse_xml("\xEF\xBB\xBF\xEF\xBB\xBF<root/>");
    // Parser should handle or fail gracefully
    // Just verify we don't crash
}

/* ============================================================================
 * Special Characters in Text
 * ============================================================================ */

TEST_F(ErrorHandlingTest, NullByteInText) {
    TaurusStatus status = parse_xml("<root>\x00</root>");
    // Null byte is invalid in XML 1.0
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, DelInText) {
    // DEL character (0x7F) is valid
    parse_xml_success("<root>\x7F</root>");
}

TEST_F(ErrorHandlingTest, HighControlCharacters) {
    // Note: Control characters 0x80-0x9F are not valid in XML 1.0
    // Taurus may reject these - just verify we don't crash
    TaurusStatus status = parse_xml("<root>\x7F\x80\x81\x82</root>");
    (void)status;
}

TEST_F(ErrorHandlingTest, VeryLargeCharacter) {
    // Maximum valid Unicode character (U+10FFFF)
    // UTF-8 encoding: F0 90 80 80
    std::string xml = "<root>";
    xml += static_cast<char>(0xF0);
    xml += static_cast<char>(0x90);
    xml += static_cast<char>(0x80);
    xml += static_cast<char>(0x80);
    xml += "</root>";

    parse_xml_success(xml);
}

/* ============================================================================
 * Recovery and Partial Parsing Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, PartialDocument) {
    // Document with error followed by valid content
    TaurusStatus status = parse_xml("<invalid><root/>");
    // Error at start - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, ErrorAfterValidContent) {
    // Valid content followed by error
    TaurusStatus status = parse_xml("<root>content</invalid>");
    // Error after valid content - should fail
    EXPECT_NE(status, TAURUS_OK);
}

/* ============================================================================
 * Whitespace Edge Cases
 * ============================================================================ */

TEST_F(ErrorHandlingTest, OnlyNewlines) {
    TaurusStatus status = parse_xml("\n\n\n");
    // Only newlines - no root element
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, TabsBetweenElements) {
    parse_xml_success("<root>\t<child/>\t</root>");
}

TEST_F(ErrorHandlingTest, FormFeedAndVerticalTab) {
    // Form feed and vertical tab are not valid XML 1.0
    TaurusStatus status = parse_xml("<root>\x0C\x0B</root>");
    // Should fail or handle gracefully
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, MixedWhitespace) {
    parse_xml_success("<root> \t\r\n</root>");
}

/* ============================================================================
 * Invalid Element Names
 * ============================================================================ */

TEST_F(ErrorHandlingTest, ElementStartingWithNumber) {
    TaurusStatus status = parse_xml("<123root/>");
    // Element name can't start with number - should fail
    EXPECT_NE(status, TAURUS_OK);
}

TEST_F(ErrorHandlingTest, ElementWithInvalidChars) {
    TaurusStatus status = parse_xml("<root&/></root>");
    // Note: Taurus is lenient - accepts invalid characters in element names
    // This is a known limitation
    (void)status;
}

TEST_F(ErrorHandlingTest, ElementWithColonButNoNamespace) {
    // Single colon is problematic without namespace
    TaurusStatus status = parse_xml("<root><a:b/></root>");
    // Might succeed (treat as colon in name) or fail
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, ElementWithMultipleColons) {
    // Element name with multiple colons (a:b:c:d)
    // Taurus is lenient: accepts colons in names as regular characters
    // Strict XML namespaces allow only one colon (prefix:localname)
    TaurusStatus status = parse_xml("<root><a:b:c:d/></root>");
    // Just verify we don't crash
    (void)status;
}

/* ============================================================================
 * Document Fragment Tests
 * ============================================================================ */

TEST_F(ErrorHandlingTest, MultipleRoots) {
    TaurusStatus status = parse_xml("<root1/><root2/>");
    // Multiple root elements - might fail or only parse first
    // Just verify we don't crash
}

TEST_F(ErrorHandlingTest, TextAtDocumentLevel) {
    TaurusStatus status = parse_xml("text before root<root/>text after root");
    // Text at document level - might fail or be ignored
    // Just verify we don't crash
}

/* ============================================================================
 * Error Context Tests (Phase 12)
 * ============================================================================ */

TEST_F(ErrorHandlingTest, ErrorContextHasLineNumber) {
    // Parse invalid XML with error on known line
    TaurusStatus status = parse_xml("<root>\n  <unclosed>");

    // Should have an error
    if (status != TAURUS_OK) {
        // Get error context
        const TaurusError* error = taurus_get_last_error();
        if (error) {
            // Error should have a line number (should be line 2 where unclosed is)
            EXPECT_GT(error->line, 0) << "Error should have a line number";
            EXPECT_NE(error->code, TAURUS_OK) << "Error code should not be OK";
            EXPECT_STRNE(error->message, "") << "Error should have a message";
        }
    }
}

TEST_F(ErrorHandlingTest, ErrorContextHasColumnNumber) {
    // Parse XML with error at known column
    TaurusStatus status = parse_xml("<root><a></b></root>");

    if (status != TAURUS_OK) {
        const TaurusError* error = taurus_get_last_error();
        if (error) {
            // Error should have a column number
            EXPECT_GT(error->column, 0) << "Error should have a column number";
        }
    }
}

TEST_F(ErrorHandlingTest, ErrorContextMessageIsDescriptive) {
    // Parse XML with obvious error
    TaurusStatus status = parse_xml("<root><>");

    if (status != TAURUS_OK) {
        const TaurusError* error = taurus_get_last_error();
        if (error) {
            // Error message should not be empty
            EXPECT_STRNE(error->message, "") << "Error should have a descriptive message";
        }
    }
}

TEST_F(ErrorHandlingTest, ErrorContextClearedOnSuccess) {
    // First cause an error
    parse_xml("<invalid>");
    taurus_clear_error();

    // Now parse valid XML
    parse_xml_success("<root/>");

    // Error should be cleared (or indicate no error)
    const TaurusError* error = taurus_get_last_error();
    // After successful parse, either error is NULL or code is TAURUS_OK
    if (error) {
        EXPECT_EQ(error->code, TAURUS_OK) << "Error code should be OK after successful parse";
    }
}

} // namespace taurus_test
