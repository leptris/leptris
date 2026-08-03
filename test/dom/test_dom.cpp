// test/dom/test_dom.cpp — DOM creation/traversal/modification specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

// Public API documents these as integers (see taurus_node_get_type docs).
constexpr int kNodeTypeElement  = 0;
constexpr int kNodeTypeText     = 1;
constexpr int kNodeTypeComment  = 2;
constexpr int kNodeTypeCDATA    = 3;
constexpr int kNodeTypePI       = 4;
constexpr int kNodeTypeDoctype  = 5;
constexpr int kNodeTypeAttribute = 6;

TEST(DomBasics, EmptyDocumentRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");
    taurus_document_free(doc);
}

TEST(DomBasics, AttributeLookupByName) {
    const char xml[] = "<r a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_STREQ(taurus_element_attribute(root, "a"), "1");
    EXPECT_STREQ(taurus_element_attribute(root, "b"), "2");
    EXPECT_STREQ(taurus_element_attribute(root, "c"), "3");
    EXPECT_EQ(taurus_element_attribute(root, "missing"), nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, TraversesChildrenInDocumentOrder) {
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "a");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "b");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "c");

    child = taurus_element_next_sibling_any(child);
    EXPECT_EQ(child, nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, NodeRefTraversalCoversAllNodeTypes) {
    // TaurusNodeRef traversal exposes every child regardless of type;
    // TaurusElement-only traversal skips text/comment/cdata siblings.
    const char xml[] = "<r><!--c-->text<x/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeComment);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeText);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeElement);

    taurus_document_free(doc);
}

}  // namespace

// ---- Freeze API (TODO 88) ------------------------------------------------

TEST(DocumentFreeze, FreshDocumentIsFrozenAfterParse) {
    /* The parser calls taurus_document_freeze_tree internally. */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, ExplicitFreezeSetsFlag) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    /* Already frozen by parser, but explicit freeze should still work. */
    EXPECT_EQ(taurus_document_freeze(doc), TAURUS_OK);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, NullDocReturnsSafe) {
    EXPECT_EQ(taurus_document_freeze(nullptr), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_document_is_frozen(nullptr), 0);
}

// ---- Node content accessors (TODO: specs coverage) -----------------------

TEST(NodeContentAccessors, CdataNodeReturnsContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><![CDATA[raw <content> & stuff]]></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* cdata = taurus_cdata_node_get_content(child);
    EXPECT_NE(cdata, nullptr);
    EXPECT_STREQ(cdata, "raw <content> & stuff");

    taurus_document_free(doc);
}

TEST(NodeContentAccessors, CommentNodeReturnsContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><!-- a comment --></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* comment = taurus_comment_node_get_content(child);
    EXPECT_NE(comment, nullptr);
    EXPECT_STREQ(comment, " a comment ");

    taurus_document_free(doc);
}

// ---- Attribute type accessors -------------------------------------------

TEST(AttributeAccessors, IntAttributeReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r count='42'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_attribute_int(root, "count", 0), 42);
    EXPECT_EQ(taurus_element_attribute_int(root, "missing", 99), 99);
    taurus_document_free(doc);
}

TEST(AttributeAccessors, DoubleAttributeReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r pi='3.14'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_DOUBLE_EQ(taurus_element_attribute_double(root, "pi", 0.0), 3.14);
    taurus_document_free(doc);
}

// ---- Text content type accessors ----------------------------------------

TEST(TextContentAccessors, IntTextReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>123</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_text_int(root, 0), 123);
    taurus_document_free(doc);
}

TEST(TextContentAccessors, DoubleTextReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>45.67</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(root, 0.0), 45.67);
    taurus_document_free(doc);
}

// ---- Serialize document ---------------------------------------------------

TEST(SerializeDocument, RoundTripPreservesStructure) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child>text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* output = taurus_document_serialize(doc, nullptr);
    EXPECT_NE(output, nullptr);
    if (output) {
        /* The serialized output should contain the element names. */
        std::string s(output);
        EXPECT_NE(s.find("root"), std::string::npos);
        EXPECT_NE(s.find("child"), std::string::npos);
        EXPECT_NE(s.find("text"), std::string::npos);
        taurus_free_string(output);
    }
    taurus_document_free(doc);
}

// ---- Element mutation -----------------------------------------------------

TEST(ElementMutation, AppendChildMovesElement) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(taurus_element_name(a), "a");

    TaurusElement b = taurus_element_next_sibling_any(a);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(taurus_element_name(b), "b");

    /* Move b under a. */
    TaurusStatus rc = taurus_element_append_child(a, b);
    EXPECT_EQ(rc, TAURUS_OK);

    /* a should now have b as a child. */
    TaurusElement child = taurus_element_first_child_any(a);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "b");

    taurus_document_free(doc);
}
