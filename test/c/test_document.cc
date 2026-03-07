/* test_document.cc - Document Operations Tests
 * Copyright (c) 2025, Ribose Inc.
 *
 * Tests for document-level operations in Taurus.
 * Based on pugixml test_document.cpp (C-appropriate subset).
 */

#include <taurus.h>
#include <stdio.h>
#include <string.h>

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
 * Test 1: Create Empty Document
 * ============================================================================ */

TEST(DocumentTest, CreateEmpty) {
    TaurusDocument doc = taurus_parse_string("", 0, NULL);
    /* Empty string is invalid XML, should return NULL */
    EXPECT_EQ(doc, nullptr) << "Empty string is not valid XML, should return NULL";

    /* If document was created, clean it up */
    if (doc != NULL) {
        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Test 2: Create Document with Root Element
 * ============================================================================ */

TEST(DocumentTest, CreateWithRoot) {
    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* name = taurus_element_name(root);
    EXPECT_STREQ(name, "root") << "Root name should be 'root'";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 3: Parse with Children
 * ============================================================================ */

TEST(DocumentTest, ParseWithChildren) {
    const char* xml = "<root><child1/><child2/><child3/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    size_t child_count = taurus_element_child_count(root);
    EXPECT_EQ(child_count, 3) << "Should have 3 children";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 4: Parse with Nested Structure
 * ============================================================================ */

TEST(DocumentTest, ParseNested) {
    const char* xml = "<root><a><b><c/></b></a><d/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    size_t root_children = taurus_element_child_count(root);
    EXPECT_EQ(root_children, 2) << "Root should have 2 children";

    TaurusElement a = taurus_element_child(root, 0);
    ASSERT_ELEM_NOT_NULL(a) << "Element 'a' should exist";

    size_t a_children = taurus_element_child_count(a);
    EXPECT_EQ(a_children, 1) << "Element 'a' should have 1 child";

    TaurusElement b = taurus_element_child(a, 0);
    ASSERT_ELEM_NOT_NULL(b) << "Element 'b' should exist";

    size_t b_children = taurus_element_child_count(b);
    EXPECT_EQ(b_children, 1) << "Element 'b' should have 1 child";

    TaurusElement c = taurus_element_child(b, 0);
    ASSERT_ELEM_NOT_NULL(c) << "Element 'c' should exist";

    const char* c_name = taurus_element_name(c);
    EXPECT_STREQ(c_name, "c") << "Leaf element name should be 'c'";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 5: Parse with Text Content
 * ============================================================================ */

TEST(DocumentTest, ParseWithText) {
    const char* xml = "<root>Hello, World!</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "Hello, World!") << "Text content should match";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 6: Parse with Attributes
 * ============================================================================ */

TEST(DocumentTest, ParseWithAttributes) {
    const char* xml = "<root attr1=\"value1\" attr2=\"value2\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* attr1 = taurus_element_attribute(root, "attr1");
    EXPECT_STREQ(attr1, "value1") << "Attribute attr1 should be 'value1'";

    const char* attr2 = taurus_element_attribute(root, "attr2");
    EXPECT_STREQ(attr2, "value2") << "Attribute attr2 should be 'value2'";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 7: Parse with Mixed Content
 * ============================================================================ */

TEST(DocumentTest, ParseMixedContent) {
    const char* xml = "<root>Text1<child/>Text2</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    /* Get text content */
    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "Text1Text2") << "Text content should concatenate";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 8: Parse Error Handling
 * ============================================================================ */

TEST(DocumentTest, ParseError) {
    /* Malformed XML - missing closing tag */
    const char* xml = "<root><child>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    /* Should return NULL or document with error status */
    if (doc != NULL) {
        /* If document created, root should be NULL */
        TaurusElement root = taurus_document_root(doc);
        EXPECT_ELEM_NULL(root) << "Malformed XML should result in NULL root";
        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Test 9: Parse Empty String
 * ============================================================================ */

TEST(DocumentTest, ParseEmptyString) {
    TaurusDocument doc = taurus_parse_string("", 0, NULL);
    /* Empty string is invalid XML, should return NULL */
    EXPECT_EQ(doc, nullptr) << "Empty string is not valid XML, should return NULL";

    /* If document was created, clean it up */
    if (doc != NULL) {
        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Test 10: Parse Whitespace Only
 * ============================================================================ */

TEST(DocumentTest, ParseWhitespace) {
    const char* xml = "   ";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    /* Whitespace-only is invalid XML, should return NULL */
    EXPECT_EQ(doc, nullptr) << "Whitespace-only is not valid XML, should return NULL";

    /* If document was created, clean it up */
    if (doc != NULL) {
        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Test 11: Parse with XML Declaration
 * ============================================================================ */

TEST(DocumentTest, ParseWithDeclaration) {
    const char* xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with declaration should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 12: Parse with Comments
 * ============================================================================ */

TEST(DocumentTest, ParseWithComments) {
    const char* xml = "<root><!-- This is a comment --><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with comments should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    size_t child_count = taurus_element_child_count(root);
    EXPECT_EQ(child_count, 1) << "Should have 1 child element";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 13: Parse with CDATA
 * ============================================================================ */

TEST(DocumentTest, ParseWithCDATA) {
    const char* xml = "<root><![CDATA[<not>&parsed>]]></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with CDATA should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "<not>&parsed>") << "CDATA content should be preserved";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 14: Parse with Processing Instruction
 * ============================================================================ */

TEST(DocumentTest, ParseWithPI) {
    const char* xml = "<?target content?><root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with PI should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 15: Parse with Multiple Root Elements (error case)
 * ============================================================================ */

TEST(DocumentTest, ParseMultipleRoots) {
    const char* xml = "<root1/><root2/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    /* Should parse but only get first root */
    if (doc != NULL) {
        TaurusElement root = taurus_document_root(doc);
        if (!element_is_null(root)) {
            const char* name = taurus_element_name(root);
            EXPECT_STREQ(name, "root1") << "Should get first root element";
        }
        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Test 16: Parse with Namespaces
 * ============================================================================ */

TEST(DocumentTest, ParseWithNamespaces) {
    const char* xml = "<root xmlns:ns=\"http://example.com\"><ns:child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with namespaces should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    size_t child_count = taurus_element_child_count(root);
    EXPECT_EQ(child_count, 1) << "Should have 1 child";

    TaurusElement child = taurus_element_child(root, 0);
    ASSERT_ELEM_NOT_NULL(child) << "Child should exist";

    const char* child_name = taurus_element_name(child);
    EXPECT_STREQ(child_name, "child") << "Child name should be 'child'";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 17: Parse Large Document
 * ============================================================================ */

TEST(DocumentTest, ParseLargeDocument) {
    /* Create document with 1000 elements */
    char* xml = (char*)malloc(100000);
    ASSERT_NE(xml, nullptr) << "Memory allocation failed";

    strcpy(xml, "<root>");
    for (int i = 0; i < 1000; i++) {
        char elem[50];
        snprintf(elem, sizeof(elem), "<item id=\"%d\"/>", i);
        strcat(xml, elem);
    }
    strcat(xml, "</root>");

    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Large document should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    size_t child_count = taurus_element_child_count(root);
    EXPECT_EQ(child_count, 1000) << "Should have 1000 children";

    free(xml);
    taurus_document_free(doc);
}

/* ============================================================================
 * Test 18: Parse with Special Characters
 * ============================================================================ */

TEST(DocumentTest, ParseSpecialCharacters) {
    const char* xml = "<root>&lt;&gt;&amp;&quot;&apos;</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with entities should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "<>&\"'") << "Entities should be decoded";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 19: Parse with Unicode Characters
 * ============================================================================ */

TEST(DocumentTest, ParseUnicode) {
    /* UTF-8 encoded string with various Unicode characters */
    const char* xml = "<root>Hello 世界 🌍</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with Unicode should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "Hello 世界 🌍") << "Unicode should be preserved";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 20: Parse with Nested Attributes
 * ============================================================================ */

TEST(DocumentTest, ParseNestedAttributes) {
    const char* xml = "<root><child id=\"1\" name=\"test\"><grandchild value=\"data\"/></child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document with nested elements and attributes should parse";

    TaurusElement root = taurus_document_root(doc);
    ASSERT_ELEM_NOT_NULL(root) << "Root should not be NULL";

    TaurusElement child = taurus_element_child(root, 0);
    ASSERT_ELEM_NOT_NULL(child) << "Child should exist";

    const char* id = taurus_element_attribute(child, "id");
    EXPECT_STREQ(id, "1") << "Child id attribute should be '1'";

    const char* name = taurus_element_attribute(child, "name");
    EXPECT_STREQ(name, "test") << "Child name attribute should be 'test'";

    TaurusElement grandchild = taurus_element_child(child, 0);
    ASSERT_ELEM_NOT_NULL(grandchild) << "Grandchild should exist";

    const char* value = taurus_element_attribute(grandchild, "value");
    EXPECT_STREQ(value, "data") << "Grandchild value attribute should be 'data'";

    taurus_document_free(doc);
}

/* ============================================================================
 * Test 21: Serialize Document
 * ============================================================================ */

TEST(DocumentTest, SerializeDocument) {
    const char* xml = "<root><child attr=\"value\">Text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr) << "Document creation failed";

    char* serialized = taurus_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr) << "Serialization should succeed";

    /* Serialized XML should contain key elements */
    EXPECT_NE(strstr(serialized, "<root>"), nullptr) << "Should contain root element";
    EXPECT_NE(strstr(serialized, "<child"), nullptr) << "Should contain child element";
    EXPECT_NE(strstr(serialized, "attr=\"value\""), nullptr) << "Should contain attribute";

    taurus_free_string(serialized);
    taurus_document_free(doc);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
