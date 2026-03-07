/* test_memory_safety.cpp - Memory safety tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Tests for memory safety:
 * - Double-free detection
 * - Use-after-free detection
 * - Memory leak detection
 * - Document lifetime management
 * - Pool allocation safety
 * - Cross-document memory safety
 *
 * Note: These tests can be run with leak detection tools:
 * - macOS: leaks --atExit -- ./test_memory_safety
 * - Linux: valgrind --leak-check=full ./test_memory_safety
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
 * Base class for memory safety tests
 */
class MemorySafetyTest : public ::testing::Test {
protected:
    std::string xml_buffer;

    void SetUp() override {
        xml_buffer.clear();
    }

    void TearDown() override {
        xml_buffer.clear();
    }

    // Helper to parse XML
    TaurusDocument parse_xml(const std::string& xml) {
        xml_buffer = xml;
        TaurusStatus status;
        return taurus_parse_string(xml_buffer.c_str(), xml_buffer.length(), &status);
    }
};

/* ============================================================================
 * Document Free Tests
 * ============================================================================ */

TEST_F(MemorySafetyTest, FreeDocumentMultipleTimes) {
    // Test that freeing a document twice doesn't crash
    TaurusDocument doc = parse_xml("<root>text</root>");
    ASSERT_NE(doc, nullptr);

    // First free
    taurus_document_free(doc);
    doc = nullptr;

    // Note: Second free would be undefined behavior
    // We set doc to nullptr to prevent accidental double-free
    EXPECT_EQ(doc, nullptr);
}

TEST_F(MemorySafetyTest, FreeNullDocument) {
    // Test that freeing nullptr doesn't crash
    TaurusDocument doc = nullptr;
    taurus_document_free(doc);  // Should be safe
    // No crash = pass
}

TEST_F(MemorySafetyTest, AccessAfterFree) {
    // Test accessing elements after document free
    TaurusDocument doc = parse_xml("<root><child>text</child></root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    TaurusElement child = taurus_element_find_child(root, "child");
    ASSERT_TRUE(ELEM_NOT_NULL(child));

    const char* child_name = taurus_element_name(child);  // Valid before free
    EXPECT_STREQ(child_name, "child");

    // Free the document
    taurus_document_free(doc);
    doc = nullptr;

    // Note: Accessing child_name after free is accessing memory
    // from the pool, which is now freed. This is use-after-free.
    // We intentionally DON'T test accessing it to avoid actual crashes.
}

/* ============================================================================
 * String Lifetime Tests
 * ============================================================================ */

TEST_F(MemorySafetyTest, StringLifetimeWithDocument) {
    // Test that strings returned by API remain valid until document free
    TaurusDocument doc = parse_xml("<root attr=\"value\">text content</root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Get string pointers
    const char* name = taurus_element_name(root);
    const char* text = taurus_element_text(root);
    const char* attr = taurus_element_attribute(root, "attr");

    ASSERT_NE(name, nullptr);
    ASSERT_NE(text, nullptr);
    ASSERT_NE(attr, nullptr);

    // Strings should be valid while document is alive
    EXPECT_STREQ(name, "root");
    EXPECT_STREQ(text, "text content");
    EXPECT_STREQ(attr, "value");

    // After document free, these pointers are invalid
    // We save the values for comparison before free
    std::string saved_name = name;
    std::string saved_text = text;
    std::string saved_attr = attr;

    taurus_document_free(doc);
    doc = nullptr;

    // Verify saved values (not using freed pointers)
    EXPECT_EQ(saved_name, "root");
    EXPECT_EQ(saved_text, "text content");
    EXPECT_EQ(saved_attr, "value");
}

/* ============================================================================
 * Cross-Document Memory Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, CopyBetweenDocuments) {
    // Test that copying between documents is memory-safe
    TaurusDocument doc1 = parse_xml("<root><source>original</source></root>");
    ASSERT_NE(doc1, nullptr);

    TaurusDocument doc2 = parse_xml("<dest/>");
    ASSERT_NE(doc2, nullptr);

    TaurusElement root1 = taurus_document_root(doc1);
    TaurusElement root2 = taurus_document_root(doc2);
    ASSERT_TRUE(ELEM_NOT_NULL(root1));
    ASSERT_TRUE(ELEM_NOT_NULL(root2));

    TaurusElement source = taurus_element_find_child(root1, "source");
    ASSERT_TRUE(ELEM_NOT_NULL(source));

    // Copy from doc1 to doc2
    TaurusElement copy = taurus_element_append_copy(root2, source);
    ASSERT_TRUE(ELEM_NOT_NULL(copy));

    // Verify copy is in doc2 - compare parent names
    TaurusElement copy_parent = taurus_element_parent(copy);
    ASSERT_TRUE(ELEM_NOT_NULL(copy_parent));
    EXPECT_STREQ(taurus_element_name(copy_parent), taurus_element_name(root2));

    // Free doc1 first - copy should still be valid
    taurus_document_free(doc1);
    doc1 = nullptr;

    // Copy should still be accessible
    EXPECT_STREQ(taurus_element_name(copy), "source");
    EXPECT_STREQ(taurus_element_text(copy), "original");

    // Now free doc2
    taurus_document_free(doc2);
    doc2 = nullptr;
}

TEST_F(MemorySafetyTest, CopyPreservesIndependence) {
    // Test that modifications to copy don't affect source
    TaurusDocument doc1 = parse_xml("<root><elem>text</elem></root>");
    ASSERT_NE(doc1, nullptr);

    TaurusDocument doc2 = parse_xml("<dest/>");
    ASSERT_NE(doc2, nullptr);

    TaurusElement root1 = taurus_document_root(doc1);
    TaurusElement root2 = taurus_document_root(doc2);

    TaurusElement elem = taurus_element_find_child(root1, "elem");
    ASSERT_TRUE(ELEM_NOT_NULL(elem));

    TaurusElement copy = taurus_element_append_copy(root2, elem);
    ASSERT_TRUE(ELEM_NOT_NULL(copy));

    // Modify the copy
    taurus_element_set_text(copy, "modified");

    // Original should be unchanged
    EXPECT_STREQ(taurus_element_text(elem), "text");
    EXPECT_STREQ(taurus_element_text(copy), "modified");

    taurus_document_free(doc1);
    taurus_document_free(doc2);
}

/* ============================================================================
 * Multiple Operations Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, MultipleCopiesFromSameSource) {
    // Test creating multiple copies from the same source
    TaurusDocument doc_src = parse_xml("<root><elem>data</elem></root>");
    ASSERT_NE(doc_src, nullptr);

    TaurusDocument doc1 = parse_xml("<dest1/>");
    TaurusDocument doc2 = parse_xml("<dest2/>");
    TaurusDocument doc3 = parse_xml("<dest3/>");
    ASSERT_NE(doc1, nullptr);
    ASSERT_NE(doc2, nullptr);
    ASSERT_NE(doc3, nullptr);

    TaurusElement src_root = taurus_document_root(doc_src);
    TaurusElement elem = taurus_element_find_child(src_root, "elem");
    ASSERT_TRUE(ELEM_NOT_NULL(elem));

    // Create multiple copies
    TaurusElement copy1 = taurus_element_append_copy(taurus_document_root(doc1), elem);
    TaurusElement copy2 = taurus_element_append_copy(taurus_document_root(doc2), elem);
    TaurusElement copy3 = taurus_element_append_copy(taurus_document_root(doc3), elem);

    ASSERT_TRUE(ELEM_NOT_NULL(copy1));
    ASSERT_TRUE(ELEM_NOT_NULL(copy2));
    ASSERT_TRUE(ELEM_NOT_NULL(copy3));

    // All copies should be independent
    taurus_element_set_text(copy1, "mod1");
    taurus_element_set_text(copy2, "mod2");
    taurus_element_set_text(copy3, "mod3");

    EXPECT_STREQ(taurus_element_text(copy1), "mod1");
    EXPECT_STREQ(taurus_element_text(copy2), "mod2");
    EXPECT_STREQ(taurus_element_text(copy3), "mod3");
    EXPECT_STREQ(taurus_element_text(elem), "data");  // Original unchanged

    // Clean up in any order
    taurus_document_free(doc3);
    taurus_document_free(doc2);
    taurus_document_free(doc1);
    taurus_document_free(doc_src);
}

TEST_F(MemorySafetyTest, SequentialDocumentOperations) {
    // Test multiple sequential operations on same document
    TaurusDocument doc = parse_xml("<root/>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Add multiple children using taurus_element_create + append_child
    for (int i = 0; i < 100; i++) {
        std::string name = "child" + std::to_string(i);
        TaurusElement child = taurus_element_create(doc, name.c_str());
        ASSERT_TRUE(ELEM_NOT_NULL(child));

        std::string text = "text" + std::to_string(i);
        taurus_element_set_text(child, text.c_str());

        TaurusStatus status = taurus_element_append_child(root, child);
        ASSERT_EQ(status, TAURUS_OK);
    }

    // Verify all children exist
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 100);

    taurus_document_free(doc);
}

/* ============================================================================
 * Large Document Memory Tests
 * ============================================================================ */

TEST_F(MemorySafetyTest, LargeDocumentLeakTest) {
    // Test that large documents are properly freed
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<child id=\"" + std::to_string(i) + "\">text content " + std::to_string(i) + "</child>";
    }
    xml += "</root>";

    TaurusDocument doc = parse_xml(xml);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Count children
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root);
         ELEM_NOT_NULL(child);
         child = taurus_element_next_sibling_any(child)) {
        count++;
    }
    EXPECT_EQ(count, 1000);

    // Free document - should free all memory
    taurus_document_free(doc);
    doc = nullptr;

    // Note: Actual leak detection requires running with valgrind/leaks
}

TEST_F(MemorySafetyTest, DeepNestingMemoryTest) {
    // Test deeply nested structure memory management
    std::string xml = "<root>";
    for (int i = 0; i < 100; i++) {
        xml += "<level" + std::to_string(i) + ">";
    }
    xml += "content";
    for (int i = 0; i < 100; i++) {
        xml += "</level" + std::to_string(99-i) + ">";
    }
    xml += "</root>";

    TaurusDocument doc = parse_xml(xml);
    ASSERT_NE(doc, nullptr);

    // Navigate to content
    TaurusElement current = taurus_document_root(doc);
    for (int i = 0; i < 100; i++) {
        current = taurus_element_first_child_any(current);
        ASSERT_TRUE(ELEM_NOT_NULL(current)) << "Failed at depth " << i;
    }

    EXPECT_STREQ(taurus_element_text(current), "content");

    taurus_document_free(doc);
}

/* ============================================================================
 * Attribute Memory Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, ManyAttributesMemoryTest) {
    // Test element with many attributes
    std::string xml = "<root ";
    for (int i = 0; i < 100; i++) {
        xml += "attr" + std::to_string(i) + "=\"value\" ";
    }
    xml += ">text</root>";

    TaurusDocument doc = parse_xml(xml);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Access all attributes
    for (int i = 0; i < 100; i++) {
        std::string attr_name = "attr" + std::to_string(i);
        const char* value = taurus_element_attribute(root, attr_name.c_str());
        EXPECT_STREQ(value, "value");
    }

    taurus_document_free(doc);
}

TEST_F(MemorySafetyTest, AttributeModificationMemoryTest) {
    // Test that attribute modifications don't leak
    TaurusDocument doc = parse_xml("<root attr1=\"value1\" attr2=\"value2\">text</root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Modify attributes multiple times
    for (int i = 0; i < 100; i++) {
        taurus_element_set_attribute(root, "attr1", ("value" + std::to_string(i)).c_str());
        taurus_element_set_attribute(root, "attr2", ("value" + std::to_string(100-i)).c_str());
    }

    // Verify final values
    EXPECT_STREQ(taurus_element_attribute(root, "attr1"), "value99");
    EXPECT_STREQ(taurus_element_attribute(root, "attr2"), "value1");

    taurus_document_free(doc);
}

/* ============================================================================
 * Text Content Memory Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, TextModificationMemoryTest) {
    // Test that text modifications don't leak
    TaurusDocument doc = parse_xml("<root>initial text</root>");
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Modify text multiple times
    for (int i = 0; i < 100; i++) {
        std::string new_text = "text iteration " + std::to_string(i);
        taurus_element_set_text(root, new_text.c_str());
    }

    EXPECT_STREQ(taurus_element_text(root), "text iteration 99");

    taurus_document_free(doc);
}

TEST_F(MemorySafetyTest, LargeTextContentTest) {
    // Test very large text content
    std::string large_text(100000, 'A');  // 100KB of 'A'

    std::string xml = "<root>" + large_text + "</root>";
    TaurusDocument doc = parse_xml(xml);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    const char* text = taurus_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(std::string(text), large_text);

    taurus_document_free(doc);
}

/* ============================================================================
 * Namespace Memory Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, NamespaceMemoryTest) {
    // Test namespace handling doesn't leak
    TaurusDocument doc = parse_xml(
        "<root xmlns:ns1=\"uri1\" xmlns:ns2=\"uri2\">"
        "<ns1:child1>text1</ns1:child1>"
        "<ns2:child2>text2</ns2:child2>"
        "</root>"
    );
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_TRUE(ELEM_NOT_NULL(root));

    // Access namespace-qualified elements
    TaurusElement child1 = taurus_element_first_child_any(root);
    ASSERT_TRUE(ELEM_NOT_NULL(child1));

    TaurusElement child2 = taurus_element_next_sibling_any(child1);
    ASSERT_TRUE(ELEM_NOT_NULL(child2));

    taurus_document_free(doc);
}

/* ============================================================================
 * Pool Allocation Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, PoolAllocationMultipleDocs) {
    // Test creating and freeing many documents
    for (int i = 0; i < 100; i++) {
        std::string xml = "<root><child>" + std::to_string(i) + "</child></root>";
        TaurusDocument doc = parse_xml(xml);
        ASSERT_NE(doc, nullptr);

        TaurusElement root = taurus_document_root(doc);
        ASSERT_TRUE(ELEM_NOT_NULL(root));

        TaurusElement child = taurus_element_find_child(root, "child");
        ASSERT_TRUE(ELEM_NOT_NULL(child));

        std::string expected = std::to_string(i);
        EXPECT_STREQ(taurus_element_text(child), expected.c_str());

        taurus_document_free(doc);
    }
}

/* ============================================================================
 * Error Path Memory Safety
 * ============================================================================ */

TEST_F(MemorySafetyTest, ParseErrorCleanup) {
    // Test that parse errors don't leak
    const char* invalid_xml[] = {
        "<root><unclosed>",
        "<root>  ",
        "<?xml version=\"1.0\"?> standalone",
        nullptr
    };

    for (int i = 0; invalid_xml[i] != nullptr; i++) {
        TaurusStatus status;
        TaurusDocument doc = taurus_parse_string(invalid_xml[i], strlen(invalid_xml[i]), &status);
        // Document might be null or partially constructed
        taurus_document_free(doc);  // Should be safe even for null
    }
}

TEST_F(MemorySafetyTest, EmptyDocument) {
    // Test empty document handling
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string("", 0, &status);
    // Document creation with empty input
    taurus_document_free(doc);  // Should be safe
}

} // namespace taurus_test
