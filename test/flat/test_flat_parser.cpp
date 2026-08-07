// test/flat/test_flat_parser.cpp — Flat parser specs (TODO 139 Phase B).
//
// Verifies flat_parse() builds correct FlatDoc trees for the
// supported XML subset:
//   - Elements (open/close/self-closing, nested, siblings)
//   - Attributes (single + double quoted, multi-attr, mixed quotes)
//   - Text (inter-element, mixed with elements)
//   - Comments, CDATA, processing instructions
//   - XML declaration (version / encoding / standalone)
//   - DOCTYPE (skipped, not validated)
//   - Whitespace handling at the document level
//   - BOM handling
//
// And that malformed XML correctly fails:
//   - Unterminated tags
//   - Mismatched close tags
//   - Unbalanced brackets
//   - Truncated input

#include <gtest/gtest.h>

extern "C" {
#include "flat_doc.h"
#include "flat_parser.h"
}

#include <cstring>
#include <string>

namespace {

// Helper: parse and check the doc returned. Caller frees.
FlatDoc* MustParse(const char* xml) {
    size_t len = std::strlen(xml);
    FlatDoc* doc = flat_parse(xml, len);
    EXPECT_NE(doc, nullptr) << "flat_parse returned NULL for: " << xml;
    return doc;
}

// Helper: fetch a pointer to a name within the parsed buffer.
const char* NamePtr(const FlatDoc* doc, const FlatNode* n) {
    return doc->xml_buffer + n->name_offset;
}

std::string NameStr(const FlatDoc* doc, const FlatNode* n) {
    return std::string(NamePtr(doc, n), n->name_len);
}

std::string TextStr(const FlatDoc* doc, const FlatNode* n) {
    return std::string(doc->xml_buffer + flat_node_text_offset(n),
                       flat_node_text_len(n));
}

TEST(FlatParse, EmptyInputFails) {
    EXPECT_EQ(flat_parse("", 0), nullptr);
    EXPECT_EQ(flat_parse(nullptr, 10), nullptr);
}

TEST(FlatParse, SelfClosingRoot) {
    const char xml[] = "<root/>";
    FlatDoc* doc = MustParse(xml);
    EXPECT_EQ(doc->node_count, 1u);
    EXPECT_EQ(doc->attr_count, 0u);
    EXPECT_NE(doc->root_index, FLAT_INDEX_NULL);
    EXPECT_EQ(NameStr(doc, &doc->nodes[doc->root_index]), "root");
    flat_doc_free(doc);
}

TEST(FlatParse, EmptyElementWithAttributes) {
    const char xml[] = "<e a='1' b=\"2\"/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 1u);
    EXPECT_EQ(doc->attr_count, 2u);
    const FlatNode* e = &doc->nodes[doc->root_index];
    EXPECT_EQ(e->attr_count, 2u);
    EXPECT_EQ(e->attr_start, 0u);

    EXPECT_EQ(std::string(doc->xml_buffer + doc->attrs[0].name_offset,
                          doc->attrs[0].name_len), "a");
    EXPECT_EQ(std::string(doc->xml_buffer + doc->attrs[0].value_offset,
                          doc->attrs[0].value_len), "1");
    EXPECT_EQ(std::string(doc->xml_buffer + doc->attrs[1].name_offset,
                          doc->attrs[1].name_len), "b");
    EXPECT_EQ(std::string(doc->xml_buffer + doc->attrs[1].value_offset,
                          doc->attrs[1].value_len), "2");
    flat_doc_free(doc);
}

TEST(FlatParse, NestedElements) {
    const char xml[] = "<a><b><c/></b></a>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 3u);

    // a -> b -> c
    const FlatNode* a = &doc->nodes[doc->root_index];
    EXPECT_EQ(NameStr(doc, a), "a");
    EXPECT_EQ(a->depth, 0u);
    ASSERT_NE(a->first_child, FLAT_INDEX_NULL);

    const FlatNode* b = &doc->nodes[a->first_child];
    EXPECT_EQ(NameStr(doc, b), "b");
    EXPECT_EQ(b->depth, 1u);
    EXPECT_EQ(b->parent, doc->root_index);
    ASSERT_NE(b->first_child, FLAT_INDEX_NULL);

    const FlatNode* c = &doc->nodes[b->first_child];
    EXPECT_EQ(NameStr(doc, c), "c");
    EXPECT_EQ(c->depth, 2u);
    EXPECT_EQ(c->parent, a->first_child);

    // Each level has only one child; next_sibling must be NULL.
    EXPECT_EQ(a->next_sibling, FLAT_INDEX_NULL);
    EXPECT_EQ(b->next_sibling, FLAT_INDEX_NULL);
    EXPECT_EQ(c->next_sibling, FLAT_INDEX_NULL);
    flat_doc_free(doc);
}

TEST(FlatParse, SiblingChain) {
    const char xml[] = "<r><a/><b/><c/></r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 4u);

    const FlatNode* r = &doc->nodes[doc->root_index];
    const FlatNode* a = &doc->nodes[r->first_child];
    EXPECT_EQ(NameStr(doc, a), "a");
    ASSERT_NE(a->next_sibling, FLAT_INDEX_NULL);

    const FlatNode* b = &doc->nodes[a->next_sibling];
    EXPECT_EQ(NameStr(doc, b), "b");
    ASSERT_NE(b->next_sibling, FLAT_INDEX_NULL);

    const FlatNode* c = &doc->nodes[b->next_sibling];
    EXPECT_EQ(NameStr(doc, c), "c");
    EXPECT_EQ(c->next_sibling, FLAT_INDEX_NULL);
    flat_doc_free(doc);
}

TEST(FlatParse, TextNodesPreserveRawBytes) {
    const char xml[] = "<r>hello world</r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];
    ASSERT_NE(r->first_child, FLAT_INDEX_NULL);
    const FlatNode* t = &doc->nodes[r->first_child];
    EXPECT_EQ(t->type, (uint16_t)FLAT_NODE_TEXT);
    EXPECT_EQ(TextStr(doc, t), "hello world");
    flat_doc_free(doc);
}

TEST(FlatParse, MixedTextAndElements) {
    const char xml[] = "<r>before<x/>after</r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];

    const FlatNode* t1 = &doc->nodes[r->first_child];
    EXPECT_EQ(t1->type, (uint16_t)FLAT_NODE_TEXT);
    EXPECT_EQ(TextStr(doc, t1), "before");

    const FlatNode* x = &doc->nodes[t1->next_sibling];
    EXPECT_EQ(x->type, (uint16_t)FLAT_NODE_ELEMENT);
    EXPECT_EQ(NameStr(doc, x), "x");

    const FlatNode* t2 = &doc->nodes[x->next_sibling];
    EXPECT_EQ(t2->type, (uint16_t)FLAT_NODE_TEXT);
    EXPECT_EQ(TextStr(doc, t2), "after");

    EXPECT_EQ(t2->next_sibling, FLAT_INDEX_NULL);
    flat_doc_free(doc);
}

TEST(FlatParse, CommentNode) {
    const char xml[] = "<r><!-- a comment --></r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];
    const FlatNode* c = &doc->nodes[r->first_child];
    EXPECT_EQ(c->type, (uint16_t)FLAT_NODE_COMMENT);
    EXPECT_EQ(TextStr(doc, c), " a comment ");
    flat_doc_free(doc);
}

TEST(FlatParse, CdataNode) {
    const char xml[] = "<r><![CDATA[<not a tag> & raw]]></r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];
    const FlatNode* c = &doc->nodes[r->first_child];
    EXPECT_EQ(c->type, (uint16_t)FLAT_NODE_CDATA);
    EXPECT_EQ(TextStr(doc, c), "<not a tag> & raw");
    flat_doc_free(doc);
}

TEST(FlatParse, ProcessingInstruction) {
    const char xml[] = "<r><?xml-stylesheet href='a.css'?></r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];
    const FlatNode* pi = &doc->nodes[r->first_child];
    EXPECT_EQ(pi->type, (uint16_t)FLAT_NODE_PI);
    EXPECT_EQ(NameStr(doc, pi), "xml-stylesheet");
    std::string data(doc->xml_buffer + flat_node_pi_data_offset(pi),
                     flat_node_pi_data_len(pi));
    /* Phase D: flat parser strips leading whitespace from PI data
     * to match the legacy parser's behavior (the serializer adds
     * its own space between target and data). */
    EXPECT_EQ(data, "href='a.css'");
    flat_doc_free(doc);
}

TEST(FlatParse, XmlDeclaration) {
    const char xml[] = "<?xml version='1.1' encoding='UTF-8' standalone='yes'?><r/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    // xml decl is NOT a node — only fields on doc.
    EXPECT_EQ(doc->node_count, 1u);
    EXPECT_EQ(doc->standalone, 1);
    EXPECT_EQ(std::string(doc->xml_buffer + doc->version_offset,
                          doc->version_len), "1.1");
    EXPECT_EQ(std::string(doc->xml_buffer + doc->encoding_offset,
                          doc->encoding_len), "UTF-8");
    flat_doc_free(doc);
}

TEST(FlatParse, XmlDeclarationDoubleQuoted) {
    const char xml[] = "<?xml version=\"1.0\"?><r/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(std::string(doc->xml_buffer + doc->version_offset,
                          doc->version_len), "1.0");
    EXPECT_EQ(doc->standalone, -1);  // not set
    flat_doc_free(doc);
}

TEST(FlatParse, DoctypeSkippedNotValidated) {
    const char xml[] = "<!DOCTYPE html><html/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 1u);
    EXPECT_EQ(NameStr(doc, &doc->nodes[doc->root_index]), "html");
    flat_doc_free(doc);
}

TEST(FlatParse, DoctypeWithInternalSubset) {
    const char xml[] =
        "<!DOCTYPE root ["
        "  <!ELEMENT root (child*)>"
        "  <!ENTITY foo 'bar'>"
        "]>"
        "<root/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 1u);
    flat_doc_free(doc);
}

TEST(FlatParse, BomStripped) {
    const unsigned char xml[] = {0xEF, 0xBB, 0xBF, '<', 'r', '/', '>'};
    FlatDoc* doc = flat_parse((const char*)xml, sizeof(xml));
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 1u);
    flat_doc_free(doc);
}

TEST(FlatParse, TopLevelWhitespaceSkipped) {
    const char xml[] = "\n  <r/>\n  ";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 1u);
    flat_doc_free(doc);
}

TEST(FlatParse, InterElementWhitespaceIsText) {
    const char xml[] = "<r>\n  <a/>\n</r>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    const FlatNode* r = &doc->nodes[doc->root_index];

    // First child is a text node "\n  "
    const FlatNode* t1 = &doc->nodes[r->first_child];
    EXPECT_EQ(t1->type, (uint16_t)FLAT_NODE_TEXT);
    EXPECT_EQ(TextStr(doc, t1), "\n  ");

    // Then <a/>
    const FlatNode* a = &doc->nodes[t1->next_sibling];
    EXPECT_EQ(a->type, (uint16_t)FLAT_NODE_ELEMENT);
    EXPECT_EQ(NameStr(doc, a), "a");

    // Then "\n"
    const FlatNode* t2 = &doc->nodes[a->next_sibling];
    EXPECT_EQ(t2->type, (uint16_t)FLAT_NODE_TEXT);
    EXPECT_EQ(TextStr(doc, t2), "\n");
    flat_doc_free(doc);
}

// --- Failure cases ---

TEST(FlatParse, UnterminatedElementFails) {
    EXPECT_EQ(flat_parse("<r>", 3), nullptr);
}

TEST(FlatParse, MismatchedCloseFails) {
    EXPECT_EQ(flat_parse("<a></b>", 7), nullptr);
}

TEST(FlatParse, ExtraCloseFails) {
    EXPECT_EQ(flat_parse("<a></a></b>", 11), nullptr);
}

TEST(FlatParse, UnterminatedCommentFails) {
    EXPECT_EQ(flat_parse("<r><!-- never ends", 18), nullptr);
}

TEST(FlatParse, UnterminatedCdataFails) {
    EXPECT_EQ(flat_parse("<r><![CDATA[ never ends", 23), nullptr);
}

TEST(FlatParse, UnterminatedAttributeFails) {
    EXPECT_EQ(flat_parse("<r a='unterminated>", 19), nullptr);
}

TEST(FlatParse, MultipleTopLevelElementsFails) {
    EXPECT_EQ(flat_parse("<a/><b/>", 8), nullptr);
}

TEST(FlatParse, DeepNestingAtLimitSucceeds) {
    // 256 levels of nesting should be OK (matches TAURUS_MAX_ELEMENT_DEPTH).
    std::string xml;
    for (int i = 0; i < 256; i++) xml += "<a>";
    for (int i = 0; i < 256; i++) xml += "</a>";
    FlatDoc* doc = MustParse(xml.c_str());
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->node_count, 256u);
    flat_doc_free(doc);
}

TEST(FlatParse, DeepNestingOverLimitFails) {
    std::string xml;
    for (int i = 0; i < 257; i++) xml += "<a>";
    for (int i = 0; i < 257; i++) xml += "</a>";
    EXPECT_EQ(flat_parse(xml.c_str(), xml.size()), nullptr);
}

TEST(FlatParse, TrailingGarbageFails) {
    // After root closes, the only valid content is whitespace,
    // comments, and PIs. Random chars must fail.
    EXPECT_EQ(flat_parse("<r/>x", 5), nullptr);
}

TEST(FlatParse, NamespacedNamePreserved) {
    // Namespace handling is deferred to promote. The flat parser
    // must record the full prefixed name unchanged.
    const char xml[] = "<ns:elem xmlns:ns='http://x'/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(NameStr(doc, &doc->nodes[doc->root_index]), "ns:elem");
    flat_doc_free(doc);
}

TEST(FlatParse, ZeroCopyInputReference) {
    char xml[] = "<r/>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    // Borrowed: same pointer, no owned copy.
    EXPECT_EQ(doc->xml_buffer, xml);
    EXPECT_EQ(doc->xml_buffer_owned, nullptr);
    flat_doc_free(doc);
}

TEST(FlatParse, RealisticDocumentStructure) {
    // A realistic-shaped doc covering every node type the flat
    // parser supports. Used as a smoke test for the integration
    // with Phase C (promote).
    const char xml[] =
        "<?xml version='1.0' encoding='UTF-8'?>"
        "<!DOCTYPE catalog SYSTEM 'catalog.dtd'>"
        "<catalog>"
        "  <!-- books -->"
        "  <?instruction side-effect none?>"
        "  <book id='b1' available=\"yes\">"
        "    <title>Cracking the Coding Interview</title>"
        "    <author>G. Laakmann McDowell</author>"
        "    <description><![CDATA[<raw> & unescaped]]></description>"
        "  </book>"
        "</catalog>";
    FlatDoc* doc = MustParse(xml);
    ASSERT_NE(doc, nullptr);
    // 9 nodes: catalog, comment, PI, book, title, "Cracking...",
    // author, "G. Laakmann...", description, CDATA. Plus whitespace
    // text nodes between them. The exact count is not load-bearing
    // here — Phase C specs verify the promote pass produces a
    // correct tree; here we just check the doc parses and the
    // declaration fields are extracted.
    EXPECT_GE(doc->node_count, 10u);
    EXPECT_EQ(doc->standalone, -1);
    EXPECT_EQ(std::string(doc->xml_buffer + doc->encoding_offset,
                          doc->encoding_len), "UTF-8");
    flat_doc_free(doc);
}

}  // namespace
