// test/flat/test_flat_promote.cpp — Promote specs (TODO 139 Phase C).
//
// Verifies that flat_parse + flat_promote produces a TaurusDocument
// indistinguishable from what taurus_parse_string produces for the
// same XML input. Specs assert on:
//   - Element names, nesting, and sibling order
//   - Attributes (names + values)
//   - Text content (raw bytes preserved)
//   - Comments / CDATA / PI promotion
//   - XML declaration fields (version / encoding / standalone)
//   - Document-level API (root, encoding accessor, free)

#include <gtest/gtest.h>

extern "C" {
#include "taurus.h"
#include "flat_doc.h"
#include "flat_parser.h"
#include "flat_promote.h"
}

#include <cstring>
#include <string>

namespace {

// Node type integer values, matching the TaurusNodeTypeEnum in
// dom/node.h. Public API exposes taurus_node_get_type() returning
// these stably-numbered codes.
enum {
    kNodeTypeElement = 0,
    kNodeTypeText    = 1,
    kNodeTypeComment = 2,
    kNodeTypeCdata   = 3,
    kNodeTypePi      = 4
};

// First element child via the node-level API. The public
// taurus_element_first_child takes a name filter; for unfiltered
// access we walk the underlying sibling chain.
TaurusElement FirstElementChild(TaurusElement e) {
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(e));
    while (n) {
        if (taurus_node_get_type(n) == kNodeTypeElement) {
            return taurus_node_as_element(n);
        }
        n = taurus_node_next_sibling(n);
    }
    return nullptr;
}

TaurusDocument FlatRoundTrip(const char* xml) {
    size_t len = std::strlen(xml);
    FlatDoc* flat = flat_parse(xml, len);
    if (!flat) return nullptr;
    return flat_promote(flat);
}

TaurusDocument LegacyParse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

// Find an attribute value by name via linear scan of the indexed
// attribute list. The public API doesn't expose a single-call
// name-based getter, so we use the indexed accessors.
const char* AttrValueByName(TaurusElement e, const char* name) {
    size_t n = taurus_element_attribute_count(e);
    for (size_t i = 0; i < n; i++) {
        const char* an = taurus_element_attribute_name_at(e, i);
        if (an && std::strcmp(an, name) == 0) {
            return taurus_element_attribute_value_at(e, i);
        }
    }
    return nullptr;
}

TEST(FlatPromote, SelfClosingRoot) {
    const char xml[] = "<root/>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "root");
    taurus_document_free(doc);
}

TEST(FlatPromote, ElementWithAttributes) {
    const char xml[] = "<e a='1' b=\"2\"/>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_attribute_count(root), 2u);
    EXPECT_STREQ(AttrValueByName(root, "a"), "1");
    EXPECT_STREQ(AttrValueByName(root, "b"), "2");
    taurus_document_free(doc);
}

TEST(FlatPromote, NestedElementsPreserveHierarchy) {
    const char xml[] = "<a><b><c/></b></a>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement a = taurus_document_root(doc);
    ASSERT_NE(a, nullptr);

    TaurusElement b = FirstElementChild(a);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(taurus_element_name(b), "b");

    TaurusElement c = FirstElementChild(b);
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(taurus_element_name(c), "c");

    EXPECT_EQ(taurus_node_next_sibling(taurus_element_as_node(a)), nullptr);
    EXPECT_EQ(taurus_node_next_sibling(taurus_element_as_node(b)), nullptr);
    EXPECT_EQ(taurus_node_next_sibling(taurus_element_as_node(c)), nullptr);
    taurus_document_free(doc);
}

TEST(FlatPromote, SiblingsPreserveOrder) {
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    TaurusElement a = FirstElementChild(r);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(taurus_element_name(a), "a");

    TaurusNodeRef b_ref = taurus_node_next_sibling(taurus_element_as_node(a));
    ASSERT_NE(b_ref, nullptr);
    TaurusElement b = taurus_node_as_element(b_ref);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(taurus_element_name(b), "b");

    TaurusNodeRef c_ref = taurus_node_next_sibling(b_ref);
    ASSERT_NE(c_ref, nullptr);
    TaurusElement c = taurus_node_as_element(c_ref);
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(taurus_element_name(c), "c");

    EXPECT_EQ(taurus_node_next_sibling(c_ref), nullptr);
    taurus_document_free(doc);
}

TEST(FlatPromote, TextContentPreserved) {
    const char xml[] = "<r>hello world</r>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    const char* text = taurus_element_text(r);
    EXPECT_STREQ(text, "hello world");
    taurus_document_free(doc);
}

TEST(FlatPromote, CommentPreservedAsChildNode) {
    // The public first_child API skips non-elements. Use
    // taurus_node_first_child to walk the underlying chain.
    const char xml[] = "<r><!-- a comment --></r>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(r));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeComment);
    EXPECT_STREQ(taurus_comment_node_get_content(n), " a comment ");
    taurus_document_free(doc);
}

TEST(FlatPromote, CdataPreservedAsChildNode) {
    const char xml[] = "<r><![CDATA[<raw> & unescaped]]></r>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(r));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeCdata);
    taurus_document_free(doc);
}

TEST(FlatPromote, XmlDeclarationFields) {
    const char xml[] = "<?xml version='1.0' encoding='UTF-8' standalone='yes'?><r/>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_STREQ(taurus_document_encoding(doc), "UTF-8");
    taurus_document_free(doc);
}

TEST(FlatPromote, DoctypeStrippedSilently) {
    // Flat parser skips DOCTYPE (no validation, no entity
    // declarations). After promote the document has no DOCTYPE
    // node — known limitation, documented in flat_parser.h.
    const char xml[] = "<!DOCTYPE html><html/>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "html");
    taurus_document_free(doc);
}

TEST(FlatPromote, ProducesSameShapeAsLegacyParser) {
    const char xml[] =
        "<catalog>"
        "  <book id='b1'>"
        "    <title>ABC</title>"
        "    <author>X</author>"
        "  </book>"
        "  <book id='b2'>"
        "    <title>DEF</title>"
        "  </book>"
        "</catalog>";

    TaurusDocument flat_doc = FlatRoundTrip(xml);
    TaurusDocument leg_doc  = LegacyParse(xml);
    ASSERT_NE(flat_doc, nullptr);
    ASSERT_NE(leg_doc, nullptr);

    TaurusElement flat_root = taurus_document_root(flat_doc);
    TaurusElement leg_root  = taurus_document_root(leg_doc);
    ASSERT_NE(flat_root, nullptr);
    ASSERT_NE(leg_root, nullptr);
    EXPECT_STREQ(taurus_element_name(flat_root), "catalog");
    EXPECT_STREQ(taurus_element_name(leg_root), "catalog");

    // Both should have 2 book children.
    TaurusXPathResult flat_books = taurus_xpath_eval(flat_doc, flat_root, "book");
    TaurusXPathResult leg_books  = taurus_xpath_eval(leg_doc, leg_root, "book");
    ASSERT_NE(flat_books, nullptr);
    ASSERT_NE(leg_books, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(flat_books),
              taurus_xpath_result_count(leg_books));
    EXPECT_EQ(taurus_xpath_result_count(flat_books), 2u);

    // Each book should have its id attribute preserved.
    for (size_t i = 0; i < taurus_xpath_result_count(flat_books); i++) {
        TaurusElement elem = taurus_xpath_result_get(flat_books, i);
        ASSERT_NE(elem, nullptr);
        const char* id = AttrValueByName(elem, "id");
        EXPECT_NE(id, nullptr);
    }

    taurus_xpath_result_free(flat_books);
    taurus_xpath_result_free(leg_books);
    taurus_document_free(flat_doc);
    taurus_document_free(leg_doc);
}

TEST(FlatPromote, RoundTripFreeIsSafe) {
    const char xml[] = "<r><a/><b/><c><d/></c></r>";
    for (int i = 0; i < 100; i++) {
        TaurusDocument doc = FlatRoundTrip(xml);
        ASSERT_NE(doc, nullptr);
        taurus_document_free(doc);
    }
    SUCCEED();
}

TEST(FlatPromote, NullInputFailsCleanly) {
    EXPECT_EQ(flat_promote(nullptr), nullptr);

    FlatDoc* empty = flat_doc_new(nullptr, 0);
    EXPECT_EQ(flat_promote(empty), nullptr);
}

TEST(FlatPromote, ProcessingInstructionPreserved) {
    const char xml[] = "<r><?app data='x'?></r>";
    TaurusDocument doc = FlatRoundTrip(xml);
    ASSERT_NE(doc, nullptr);
    TaurusElement r = taurus_document_root(doc);
    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(r));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypePi);
    taurus_document_free(doc);
}

TEST(FlatPromote, LargeDocRoundTrip) {
    std::string xml = "<root>";
    for (int i = 0; i < 1000; i++) {
        xml += "<item id='";
        xml += std::to_string(i);
        xml += "'/>";
    }
    xml += "</root>";

    TaurusDocument doc = FlatRoundTrip(xml.c_str());
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult items = taurus_xpath_eval(doc, root, "item");
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(items), 1000u);
    taurus_xpath_result_free(items);

    taurus_document_free(doc);
}

}  // namespace
