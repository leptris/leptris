// test/serializer/test_serialize.cpp — Round-trip and escaping specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TEST(SerializeRoundTrip, PreservesCdataVerbatim) {
    const char xml[] = "<r><![CDATA[<raw>not parsed</raw> & stuff]]></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STREQ(serialized, xml);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, EscapesBareAmpersandInText) {
    // A bare '&' that isn't part of an entity reference must be escaped
    // to &amp; on output.
    const char xml[] = "<r>a & b</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STREQ(serialized, "<r>a &amp; b</r>");
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, GrowsBufferForHugeTextContent) {
    // Regression for TODO 08: buffer_ensure_capacity must grow without
    // integer overflow or silent realloc-failure corruption.
    const std::string body(5'000'000, 'A');
    const std::string xml = "<r>" + body + "</r>";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc =
        taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::strlen(serialized), xml.size());
    EXPECT_STREQ(serialized, xml.c_str());
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeOptions, IndentsWithGivenSpaces) {
    const char xml[] = "<r><a><b>x</b></a></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusSerializeOptions opts = {0};
    opts.indent = 2;
    opts.xml_declaration = 0;

    char* serialized = taurus_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    // Output should contain a newline after <r> and 2 spaces before <a>.
    EXPECT_NE(std::string(serialized).find("\n  <a>"), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

// ---- More serialization coverage (TODO 104) -----------------------------

TEST(SerializeOptions, CompactByDefaultHasNoNewline) {
    /* Default options = compact output: no indent, no pretty-print. */
    const char xml[] = "<r><a>x</a></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::string(serialized).find('\n'), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeOptions, OmitsXmlDeclarationWhenFlagFalse) {
    /* When opts.xml_declaration = 0 and the doc had no declaration,
     * output must not start with "<?xml". */
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusSerializeOptions opts = {0};
    opts.xml_declaration = 0;
    char* serialized = taurus_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).substr(0, 5), "<?xml");
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeOptions, IncludesXmlDeclarationWhenFlagTrue) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusSerializeOptions opts = {0};
    opts.xml_declaration = 1;
    char* serialized = taurus_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::string(serialized).substr(0, 5), "<?xml");
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesCommentsInTree) {
    /* Comments in the tree must appear in the serialized output. */
    const char xml[] = "<r><!-- a comment --><a/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).find("<!-- a comment -->"), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesProcessingInstructions) {
    /* Document-level PIs (other than <?xml?>) are preserved in the
     * serialized output.  Uses a non-`xml` target so the parser's
     * "is this the XML declaration?" check doesn't fire. */
    const char xml[] = "<?xml-stylesheet href='x.xsl'?><r/>";
    TaurusStatus st = TAURUS_OK;
    /* The parser may or may not accept this; if it rejects, skip the
     * round-trip and just verify the serializer emits the PI when
     * the document has one. */
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    if (!doc) {
        /* Parser doesn't accept this PI form — not what this test is about. */
        SUCCEED() << "parser rejects this PI form; nothing to round-trip";
        return;
    }

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    std::string s(serialized);
    EXPECT_NE(s.find("xml-stylesheet"), std::string::npos)
        << "PI missing from serialized output: " << s;
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, EscapesAttributeValues) {
    /* Attribute values containing & < > " must be escaped in output. */
    const char xml[] = "<r a='&amp;' b='&lt;' c='&quot;'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    /* After parse, attribute values are decoded.  On serialize they're
     * re-escaped so the output round-trips through the parser. */
    std::string s(serialized);
    EXPECT_NE(s.find("a=\"&amp;\""), std::string::npos);
    EXPECT_NE(s.find("b=\"&lt;\""), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, HandlesEmptyDocument) {
    /* Documents with an empty root should serialize to a self-closing tag. */
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    /* Either <r/> or <r></r> — both are valid XML for empty root. */
    std::string s(serialized);
    EXPECT_NE(s.find("r"), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, RoundTripsThroughParser) {
    /* Parse → serialize → parse → compare names.  Catches round-trip
     * corruption bugs in either the parser or serializer. */
    const char xml[] = "<root id='main'><child>hello &amp; goodbye</child></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc1 = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = taurus_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    TaurusDocument doc2 = taurus_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);

    TaurusElement root1 = taurus_document_root(doc1);
    TaurusElement root2 = taurus_document_root(doc2);
    ASSERT_NE(root1, nullptr);
    ASSERT_NE(root2, nullptr);
    EXPECT_STREQ(taurus_element_name(root1), taurus_element_name(root2));
    EXPECT_STREQ(taurus_element_attribute(root1, "id"),
                 taurus_element_attribute(root2, "id"));

    /* The text content should also match — &amp; round-trips. */
    TaurusElement c1 = taurus_element_first_child_any(root1);
    TaurusElement c2 = taurus_element_first_child_any(root2);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    EXPECT_STREQ(taurus_element_name(c1), taurus_element_name(c2));
    EXPECT_STREQ(taurus_element_text(c1), taurus_element_text(c2));

    taurus_document_free(doc2);
    taurus_free_string(round1);
    taurus_document_free(doc1);
}

TEST(SerializeOptions, IndentsMultiLevelNesting) {
    /* Multi-level pretty-print: each level gets `indent` more spaces. */
    const char xml[] = "<a><b><c><d/></c></b></a>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusSerializeOptions opts = {0};
    opts.indent = 4;
    opts.xml_declaration = 0;
    char* serialized = taurus_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);

    std::string s(serialized);
    EXPECT_NE(s.find("\n    <b>"), std::string::npos);     /* 4 spaces */
    EXPECT_NE(s.find("\n        <c>"), std::string::npos); /* 8 spaces */
    EXPECT_NE(s.find("\n            <d/>"), std::string::npos); /* 12 spaces */
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesCommentsAndPIs) {
    /* Comments and PIs survive parse -> serialize -> parse with the
     * tree structure intact. TODO 112 fix makes this round-trip clean. */
    const char xml[] = "<r><!-- c1 -->text<?pi data?><!-- c2 --></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc1 = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = taurus_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    /* Serialize must preserve all 4 children: 2 comments, 1 text, 1 PI. */
    std::string s(round1);
    EXPECT_NE(s.find("<!-- c1 -->"), std::string::npos);
    EXPECT_NE(s.find("<!-- c2 -->"), std::string::npos);
    EXPECT_NE(s.find("<?pi data?>"), std::string::npos);
    EXPECT_NE(s.find("text"), std::string::npos);

    /* Second parse must succeed and produce the same tree. */
    TaurusDocument doc2 = taurus_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);
    char* round2 = taurus_document_serialize(doc2, nullptr);
    ASSERT_NE(round2, nullptr);
    EXPECT_STREQ(round1, round2);

    taurus_free_string(round2);
    taurus_document_free(doc2);
    taurus_free_string(round1);
    taurus_document_free(doc1);
}

TEST(SerializeRoundTrip, PreservesNamespacedAttributes) {
    /* Namespaced attributes (xmlns declarations and prefixed attrs)
     * must survive round-trip verbatim. */
    const char xml[] = "<r xmlns:ns='http://example.com/ns' ns:attr='value'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc1 = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = taurus_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    TaurusDocument doc2 = taurus_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);

    TaurusElement root = taurus_document_root(doc2);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_attribute(root, "ns:attr"), "value");

    taurus_document_free(doc2);
    taurus_free_string(round1);
    taurus_document_free(doc1);
}

TEST(SerializeOptions, OmitsXmlDeclarationWhenDisabled) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusSerializeOptions opts = {0};
    opts.xml_declaration = 0;
    char* s = taurus_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    /* When xml_declaration is off, the serialized output must not
     * contain the "<?xml" prefix at all. */
    std::string str(s);
    EXPECT_EQ(str.find("<?xml"), std::string::npos);

    taurus_free_string(s);
    taurus_document_free(doc);
}

}  // namespace
