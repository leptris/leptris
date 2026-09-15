// test/dom/test_node_surface_parity.cpp — #1094 node-surface parity:
// entity reference nodes, XML declaration read/write, programmatic
// DOCTYPE, document-PI identity/anchoring/removal.
//
// These surfaces delete moxml's CustomizedLeptris adapter machinery
// (marker text nodes, TextSegment, flat PI pairs) — the specs pin the
// engine contract the binding will adopt.

#include <gtest/gtest.h>
#include "leptris.h"
#include <cstring>
#include <string>

namespace {

// ---- Entity reference nodes ---------------------------------------

TEST(EntityRef, ParseKeepEntityRefsSplitsRuns) {
    const char* xml =
        "<!DOCTYPE r [<!ENTITY foo 'BAR'>]>\n"
        "<r>a &amp; b &foo; c</r>";
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    // Children: text("a "), ref(amp), text(" b "), ref(foo), text(" c")
    LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)root);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_TEXT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_ENTITY_REF);
    EXPECT_STREQ(leptris_entity_ref_node_name(c), "amp");
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_TEXT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_ENTITY_REF);
    EXPECT_STREQ(leptris_entity_ref_node_name(c), "foo");
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_EQ(leptris_node_next_sibling(c), nullptr);
    leptris_document_free(doc);
}

TEST(EntityRef, DefaultParseStaysExpanded) {
    const char* xml = "<r>a &amp; b</r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)root);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_EQ(leptris_node_next_sibling(c), nullptr);
    const char* text = leptris_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "a & b");
    leptris_document_free(doc);
}

TEST(EntityRef, SerializeRoundTripsReferences) {
    const char* xml =
        "<!DOCTYPE r [<!ENTITY foo 'BAR'>]><r>a &amp; b &foo;</r>";
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, NULL);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(strstr(out, "&amp;"), nullptr);
    EXPECT_NE(strstr(out, "&foo;"), nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(EntityRef, TextContentResolvesPredefinedAndDTD) {
    const char* xml =
        "<!DOCTYPE r [<!ENTITY foo 'BAR'>]><r>a &amp;&foo;</r>";
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    const char* text = leptris_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "a &BAR");
    leptris_document_free(doc);
}

TEST(EntityRef, CreateAttachAndSerialize) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_element_create(doc, "r");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    LeptrisNodeRef ref = leptris_entity_ref_node_create(doc, "nbsp");
    ASSERT_NE(ref, nullptr);
    EXPECT_STREQ(leptris_entity_ref_node_name(ref), "nbsp");
    ASSERT_EQ(leptris_element_append_child(root, (LeptrisElement)ref),
              LEPTRIS_OK);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r>&nbsp;</r>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(EntityRef, NodeNameReaderRejectsOtherKinds) {
    const char* xml = "<r>t</r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef text = leptris_node_first_child((LeptrisNodeRef)root);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_entity_ref_node_name(text), nullptr);
    EXPECT_EQ(leptris_entity_ref_node_name((LeptrisNodeRef)root), nullptr);
    leptris_document_free(doc);
}

// ---- XML declaration surface --------------------------------------

TEST(Declaration, SettersEmitAndRoundTrip) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_element_create(doc, "r");
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_set_version(doc, "1.1"), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_set_encoding(doc, "ISO-8859-1"), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_set_standalone(doc, 1), LEPTRIS_OK);
    EXPECT_STREQ(leptris_document_version(doc), "1.1");
    EXPECT_STREQ(leptris_document_encoding(doc), "ISO-8859-1");
    EXPECT_EQ(leptris_document_standalone(doc), 1);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
        "<?xml version=\"1.1\" encoding=\"ISO-8859-1\" standalone=\"yes\"?>"
        "<r/>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(Declaration, ParsedDocumentReadsBack) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?><r/>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    EXPECT_STREQ(leptris_document_version(doc), "1.0");
    EXPECT_STREQ(leptris_document_encoding(doc), "UTF-8");
    EXPECT_EQ(leptris_document_standalone(doc), 0);
    leptris_document_free(doc);
}

TEST(Declaration, SettersRejectEmptyAndNull) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_set_version(doc, ""), LEPTRIS_ERROR_INVALID_ARG);
    EXPECT_EQ(leptris_document_set_encoding(doc, NULL),
              LEPTRIS_ERROR_INVALID_ARG);
    EXPECT_EQ(leptris_document_set_version(NULL, "1.0"), LEPTRIS_ERROR_NULL_ARG);
    leptris_document_free(doc);
}

// ---- Programmatic DOCTYPE ------------------------------------------

TEST(Doctype, SetDoctypeSerializesInPosition) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_element_create(doc, "svg");
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    LeptrisDoctype dt = leptris_document_set_doctype(
        doc, "svg", "-//W3C//DTD SVG 1.1//EN",
        "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd");
    ASSERT_NE(dt, nullptr);
    EXPECT_STREQ(leptris_doctype_get_name(dt), "svg");
    EXPECT_STREQ(leptris_doctype_get_public_id(dt),
                 "-//W3C//DTD SVG 1.1//EN");
    EXPECT_STREQ(leptris_doctype_get_system_id(dt),
                 "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd");
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
        "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\"><svg/>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(Doctype, SetDoctypeInternalOnly) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_element_create(doc, "r");
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    LeptrisDoctype dt = leptris_document_set_doctype(doc, "r", NULL, NULL);
    ASSERT_NE(dt, nullptr);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<!DOCTYPE r><r/>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

// ---- Document-PI parity --------------------------------------------

TEST(DocumentPI, AppendPiAnchorsEpilogueAfterRoot) {
    const char* xml = "<?pi prolog?><r/><!--epilog comment-->";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef n = leptris_document_append_pi(doc, "after", "x");
    ASSERT_NE(n, nullptr);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
        "<?pi prolog?><r/><!--epilog comment--><?after x?>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(DocumentPI, RemoveChildUnlinksByNodeIdentity) {
    const char* xml = "<?p1?><r/><?p2?><?p3?>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    // Walk the chain: head=p1, then root, then p2, p3.
    LeptrisNodeRef c = leptris_document_first_child(doc);
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_PI);
    c = leptris_node_next_sibling(c);   // root
    c = leptris_node_next_sibling(c);   // p2
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_PI);
    ASSERT_EQ(leptris_document_remove_child(doc, c), LEPTRIS_OK);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<?p1?><r/><?p3?>");
    leptris_free_string(out);
    // Removing an already-removed node is NOT_FOUND.
    EXPECT_EQ(leptris_document_remove_child(doc, c), LEPTRIS_ERROR_NOT_FOUND);
    leptris_document_free(doc);
}

TEST(DocumentPI, RemoveChildHeadUpdatesChain) {
    const char* xml = "<?p1?><r/>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef head = leptris_document_first_child(doc);
    ASSERT_EQ(leptris_node_get_type(head), LEPTRIS_NODE_TYPE_PI);
    ASSERT_EQ(leptris_document_remove_child(doc, head), LEPTRIS_OK);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r/>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(DocumentPI, RemoveChildRejectsRootElement) {
    const char* xml = "<?p1?><r/>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), NULL);
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef c = leptris_document_first_child(doc);
    c = leptris_node_next_sibling(c);   // root element
    ASSERT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_ELEMENT);
    EXPECT_EQ(leptris_document_remove_child(doc, c), LEPTRIS_ERROR_INVALID_ARG);
    leptris_document_free(doc);
}

}  // namespace
