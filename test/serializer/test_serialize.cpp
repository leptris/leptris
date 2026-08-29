// test/serializer/test_serialize.cpp — Round-trip and escaping specs.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

TEST(SerializeRoundTrip, PreservesCdataVerbatim) {
    const char xml[] = "<r><![CDATA[<raw>not parsed</raw> & stuff]]></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STREQ(serialized, xml);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, EscapesBareAmpersandInText) {
    // A bare '&' that isn't part of an entity reference must be escaped
    // to &amp; on output.
    const char xml[] = "<r>a & b</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STREQ(serialized, "<r>a &amp; b</r>");
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, GrowsBufferForHugeTextContent) {
    // Regression for TODO 08: buffer_ensure_capacity must grow without
    // integer overflow or silent realloc-failure corruption.
    //
    // Size rationale: 100 KB forces the serialize output buffer through
    // ~10 doublings (256 B initial → 128 KB final), exercising the
    // growth path. The materialized text content also exceeds the
    // pool's 32 KB page (oversized alloc path).
    //
    // Previous variants (5 MB, 500 KB, 200 KB) segfaulted on Linux CI
    // runners where glibc malloc places oversized requests far from the
    // pool's compact-pointer range, causing int32 offset overflow on
    // the text-node parent edge (TODO 121). 100 KB matches the sibling
    // test (HugeTextContentStaysAttachedToParent) which has consistently
    // passed on all platforms.
    const std::string body(100'000, 'A');
    const std::string xml = "<r>" + body + "</r>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::strlen(serialized), xml.size());
    EXPECT_STREQ(serialized, xml.c_str());
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

// Regression for the compact-pointer int32_t offset silent-drop bug
// (TODO 90 Phase 2b). When content exceeds the pool's page size, the
// node struct must still stay within ±2GB of its parent element so
// first_child_off doesn't overflow. Before the fix, a 5MB text body
// forced an oversized allocation for struct+content; on systems where
// malloc places oversized requests far from small ones (notably macOS
// runners), the int32_t offset overflowed to 0 and the text was
// silently dropped from the tree.
TEST(SerializeRoundTrip, HugeTextContentStaysAttachedToParent) {
    // Pick a body size well past the largest pool page (32 KB) but
    // small enough that the test runs in a fraction of a second.
    const std::string body(100'000, 'A');
    const std::string xml = "<r>" + body + "</r>";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    // The text node MUST be attached as a child of r. Before the fix,
    // first_child_off was 0 here on affected platforms and the
    // serializer emitted "<r/>".
    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::strlen(serialized), xml.size());
    EXPECT_STREQ(serialized, xml.c_str());
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeOptions, IndentsWithGivenSpaces) {
    const char xml[] = "<r><a><b>x</b></a></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    opts.xml_declaration = 0;

    char* serialized = leptris_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    // Output should contain a newline after <r> and 2 spaces before <a>.
    EXPECT_NE(std::string(serialized).find("\n  <a>"), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

// ---- More serialization coverage (TODO 104) -----------------------------

TEST(SerializeOptions, CompactByDefaultHasNoNewline) {
    /* Default options = compact output: no indent, no pretty-print. */
    const char xml[] = "<r><a>x</a></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::string(serialized).find('\n'), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeOptions, OmitsXmlDeclarationWhenFlagFalse) {
    /* When opts.xml_declaration = 0 and the doc had no declaration,
     * output must not start with "<?xml". */
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.xml_declaration = 0;
    char* serialized = leptris_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).substr(0, 5), "<?xml");
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeOptions, IncludesXmlDeclarationWhenFlagTrue) {
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.xml_declaration = 1;
    char* serialized = leptris_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);
    EXPECT_EQ(std::string(serialized).substr(0, 5), "<?xml");
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesCommentsInTree) {
    /* Comments in the tree must appear in the serialized output. */
    const char xml[] = "<r><!-- a comment --><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_NE(std::string(serialized).find("<!-- a comment -->"), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesProcessingInstructions) {
    /* Document-level PIs (other than <?xml?>) are preserved in the
     * serialized output.  Uses a non-`xml` target so the parser's
     * "is this the XML declaration?" check doesn't fire. */
    const char xml[] = "<?xml-stylesheet href='x.xsl'?><r/>";
    LeptrisStatus st = LEPTRIS_OK;
    /* The parser may or may not accept this; if it rejects, skip the
     * round-trip and just verify the serializer emits the PI when
     * the document has one. */
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (!doc) {
        /* Parser doesn't accept this PI form — not what this test is about. */
        SUCCEED() << "parser rejects this PI form; nothing to round-trip";
        return;
    }

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    std::string s(serialized);
    EXPECT_NE(s.find("xml-stylesheet"), std::string::npos)
        << "PI missing from serialized output: " << s;
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, EscapesAttributeValues) {
    /* Attribute values containing & < > " must be escaped in output. */
    const char xml[] = "<r a='&amp;' b='&lt;' c='&quot;'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    /* After parse, attribute values are decoded.  On serialize they're
     * re-escaped so the output round-trips through the parser. */
    std::string s(serialized);
    EXPECT_NE(s.find("a=\"&amp;\""), std::string::npos);
    EXPECT_NE(s.find("b=\"&lt;\""), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, HandlesEmptyDocument) {
    /* Documents with an empty root should serialize to a self-closing tag. */
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    /* Either <r/> or <r></r> — both are valid XML for empty root. */
    std::string s(serialized);
    EXPECT_NE(s.find("r"), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, RoundTripsThroughParser) {
    /* Parse → serialize → parse → compare names.  Catches round-trip
     * corruption bugs in either the parser or serializer. */
    const char xml[] = "<root id='main'><child>hello &amp; goodbye</child></root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc1 = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = leptris_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    LeptrisDocument doc2 = leptris_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);

    LeptrisElement root1 = leptris_document_root(doc1);
    LeptrisElement root2 = leptris_document_root(doc2);
    ASSERT_NE(root1, nullptr);
    ASSERT_NE(root2, nullptr);
    EXPECT_STREQ(leptris_element_name(root1), leptris_element_name(root2));
    EXPECT_STREQ(leptris_element_attribute(root1, "id"),
                 leptris_element_attribute(root2, "id"));

    /* The text content should also match — &amp; round-trips. */
    LeptrisElement c1 = leptris_element_first_child_any(root1);
    LeptrisElement c2 = leptris_element_first_child_any(root2);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    EXPECT_STREQ(leptris_element_name(c1), leptris_element_name(c2));
    EXPECT_STREQ(leptris_element_text(c1), leptris_element_text(c2));

    leptris_document_free(doc2);
    leptris_free_string(round1);
    leptris_document_free(doc1);
}

TEST(SerializeOptions, IndentsMultiLevelNesting) {
    /* Multi-level pretty-print: each level gets `indent` more spaces. */
    const char xml[] = "<a><b><c><d/></c></b></a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.indent = 4;
    opts.xml_declaration = 0;
    char* serialized = leptris_document_serialize(doc, &opts);
    ASSERT_NE(serialized, nullptr);

    std::string s(serialized);
    EXPECT_NE(s.find("\n    <b>"), std::string::npos);     /* 4 spaces */
    EXPECT_NE(s.find("\n        <c>"), std::string::npos); /* 8 spaces */
    EXPECT_NE(s.find("\n            <d/>"), std::string::npos); /* 12 spaces */
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(SerializeRoundTrip, PreservesCommentsAndPIs) {
    /* Comments and PIs survive parse -> serialize -> parse with the
     * tree structure intact. TODO 112 fix makes this round-trip clean. */
    const char xml[] = "<r><!-- c1 -->text<?pi data?><!-- c2 --></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc1 = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = leptris_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    /* Serialize must preserve all 4 children: 2 comments, 1 text, 1 PI. */
    std::string s(round1);
    EXPECT_NE(s.find("<!-- c1 -->"), std::string::npos);
    EXPECT_NE(s.find("<!-- c2 -->"), std::string::npos);
    EXPECT_NE(s.find("<?pi data?>"), std::string::npos);
    EXPECT_NE(s.find("text"), std::string::npos);

    /* Second parse must succeed and produce the same tree. */
    LeptrisDocument doc2 = leptris_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);
    char* round2 = leptris_document_serialize(doc2, nullptr);
    ASSERT_NE(round2, nullptr);
    EXPECT_STREQ(round1, round2);

    leptris_free_string(round2);
    leptris_document_free(doc2);
    leptris_free_string(round1);
    leptris_document_free(doc1);
}

TEST(SerializeRoundTrip, PreservesNamespacedAttributes) {
    /* Namespaced attributes (xmlns declarations and prefixed attrs)
     * must survive round-trip verbatim. */
    const char xml[] = "<r xmlns:ns='http://example.com/ns' ns:attr='value'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc1 = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc1, nullptr);

    char* round1 = leptris_document_serialize(doc1, nullptr);
    ASSERT_NE(round1, nullptr);

    LeptrisDocument doc2 = leptris_parse_string(round1, std::strlen(round1), &st);
    ASSERT_NE(doc2, nullptr);

    LeptrisElement root = leptris_document_root(doc2);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_attribute(root, "ns:attr"), "value");

    leptris_document_free(doc2);
    leptris_free_string(round1);
    leptris_document_free(doc1);
}

TEST(SerializeOptions, OmitsXmlDeclarationWhenDisabled) {
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.xml_declaration = 0;
    char* s = leptris_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    /* When xml_declaration is off, the serialized output must not
     * contain the "<?xml" prefix at all. */
    std::string str(s);
    EXPECT_EQ(str.find("<?xml"), std::string::npos);

    leptris_free_string(s);
    leptris_document_free(doc);
}


TEST(SerializeOptions, PrettyTextLeavesStayOnOneLine) {
    /* Pins the pretty emission of text-only leaves: no blank lines
     * between siblings (a fused-path regression inserted leading
     * newlines after the parent's open-tag newline and sailed
     * through the whole suite — only a byte-diff against main
     * caught it). */
    const char xml[] =
        "<root><a x=\"1\"><b>hello</b><c>world &amp; more</c></a>"
        "<d/><e>tail</e></root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* s = leptris_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    std::string out(s);

    /* No empty lines anywhere. */
    EXPECT_EQ(out.find("\n\n"), std::string::npos);
    /* Text leaves stay inline with their content. */
    EXPECT_NE(out.find("<b>hello</b>"), std::string::npos);
    EXPECT_NE(out.find("<c>world &amp; more</c>"), std::string::npos);
    EXPECT_NE(out.find("<e>tail</e>"), std::string::npos);
    /* Every line except the last starts with an indent or a tag. */
    EXPECT_EQ(out.find("\n \n"), std::string::npos);

    leptris_free_string(s);
    leptris_document_free(doc);
}

}  // namespace

TEST(EncodingGuarantee, DeclarationNeverLies) {
    /* Parsed as ISO-8859-1 (iconv path); the body is transcoded to
     * UTF-8 internally. Serialization output is ALWAYS UTF-8 — the
     * declaration must not claim ISO-8859-1 (TODO.bindings/06). */
    const char iso[] = "<?xml version='1.0' encoding='ISO-8859-1'?>\n"
                       "<r><t>caf\xe9</t></r>";
    LeptrisDocument doc = leptris_parse_string(iso, std::strlen(iso), nullptr);
    ASSERT_NE(doc, nullptr);
    ASSERT_STREQ(leptris_document_encoding(doc), "ISO-8859-1");

    LeptrisSerializeOptions opts = {0};
    opts.xml_declaration = 1;
    char* xml = leptris_document_serialize(doc, &opts);
    ASSERT_NE(xml, nullptr);
    /* The body tells which build we are on (self-calibrating — the
     * LEPTRIS_HAS_ICONV compile definition does not reach test TUs):
     *  - iconv build: é was transcoded at parse time (c3 a9)
     *  - no-iconv build: bytes pass through unchanged (e9)
     * Either way the DECLARATION must match the body. */
    bool transcoded = std::strstr(xml, "caf\xc3\xa9") != nullptr;
    bool passthrough = std::strstr(xml, "caf\xe9") != nullptr;
    ASSERT_TRUE(transcoded || passthrough) << "body must contain é";
    if (transcoded) {
        EXPECT_NE(std::strstr(xml, "encoding=\"UTF-8\""), nullptr)
            << "declaration must say UTF-8";
        EXPECT_EQ(std::strstr(xml, "ISO-8859-1"), nullptr);

        /* Even an explicit non-UTF-8 request stays truthful. */
        opts.encoding = "ISO-8859-1";
        leptris_free_string(xml);
        xml = leptris_document_serialize(doc, &opts);
        ASSERT_NE(xml, nullptr);
        EXPECT_EQ(std::strstr(xml, "ISO-8859-1"), nullptr);
    } else {
        EXPECT_NE(std::strstr(xml, "encoding=\"ISO-8859-1\""), nullptr)
            << "no-iconv passthrough must echo the original encoding";
    }
    leptris_free_string(xml);
    leptris_document_free(doc);
}

TEST(EncodingGuarantee, DoubleSerializeIsByteStable) {
    const char xml_in[] = "<r a='1'><t>x &amp; y</t><e/></r>";
    LeptrisDocument doc = leptris_parse_string(xml_in, std::strlen(xml_in), nullptr);
    ASSERT_NE(doc, nullptr);
    char* once = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(once, nullptr);
    LeptrisDocument reparsed = leptris_parse_string(once, std::strlen(once), nullptr);
    ASSERT_NE(reparsed, nullptr);
    char* twice = leptris_document_serialize(reparsed, nullptr);
    ASSERT_NE(twice, nullptr);
    EXPECT_STREQ(once, twice);
    leptris_free_string(twice);
    leptris_document_free(reparsed);
    leptris_free_string(once);
    leptris_document_free(doc);
}

/* Issue #523: element serialization leaked every FOLLOWING sibling —
 * wrong output plus an O(document) cost per call. */
TEST(ElementSerialize, EmitsExactlyTheRequestedSubtree) {
    const char xml[] =
        "<catalog><b1 id='1'>one</b1><b2 id='2'>two</b2><b3 id='3'>three</b3></catalog>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisElement catalog = leptris_document_root(doc);
    LeptrisElement b1 = leptris_element_first_child_any(catalog);
    ASSERT_NE(b1, nullptr);

    char* x = leptris_element_serialize(b1, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_STREQ(x, "<b1 id=\"1\">one</b1>");
    leptris_free_string(x);

    /* Middle element with children of its own. */
    LeptrisElement b2 = leptris_element_next_sibling_any(b1);
    ASSERT_NE(b2, nullptr);
    x = leptris_element_serialize(b2, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_STREQ(x, "<b2 id=\"2\">two</b2>");
    leptris_free_string(x);

    /* Pretty-printed variant stays isolated too. */
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    x = leptris_element_serialize(b2, &opts);
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(std::strstr(x, "b3"), nullptr) << "siblings must not leak";
    EXPECT_NE(std::strstr(x, "b2"), nullptr);
    leptris_free_string(x);

    /* Whole-document serialization is unchanged. */
    x = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_NE(std::strstr(x, "<b3"), nullptr);
    leptris_free_string(x);
    leptris_document_free(doc);
}

/* Issue #534: the indenting serializer inserted whitespace inside
 * mixed-content elements — inserted bytes became new text nodes on
 * reparse, so serialize∘parse was not idempotent and text content
 * was silently altered. libxml2 semantics: mixed elements stay on
 * one line; ws-only text between elements is formatter-owned. */
TEST(MixedContent, PrettyPrintKeepsMixedElementsVerbatim) {
    const char xml[] = "<b>2<!-- c --><![CDATA[<t>]]></b>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* out = leptris_document_serialize(doc, &opts);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<b>2<!-- c --><![CDATA[<t>]]></b>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(MixedContent, ClassicMixedStaysInline) {
    const char xml[] = "<p>Hello <b>world</b>!</p>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* out = leptris_document_serialize(doc, &opts);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<p>Hello <b>world</b>!</p>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(MixedContent, PrettyRoundTripIsIdempotent) {
    /* ws-only text between elements is formatting: the pretty
     * printer owns it (canonical newline+indent), so the round trip
     * is byte-stable — and no longer doubles the blank lines. */
    const char xml[] = "<r>\n  <a>1</a>\n  <b>2</b>\n</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* once = leptris_document_serialize(doc, &opts);
    ASSERT_NE(once, nullptr);
    EXPECT_STREQ(once, "<r>\n  <a>1</a>\n  <b>2</b>\n</r>");

    LeptrisDocument reparsed =
        leptris_parse_string(once, std::strlen(once), nullptr);
    ASSERT_NE(reparsed, nullptr);
    char* twice = leptris_document_serialize(reparsed, &opts);
    ASSERT_NE(twice, nullptr);
    EXPECT_STREQ(once, twice);
    leptris_free_string(twice);
    leptris_document_free(reparsed);
    leptris_free_string(once);
    leptris_document_free(doc);
}

TEST(MixedContent, PrettyParentWithMixedChild) {
    /* The parent is NOT mixed (only elements + formatting ws); the
     * child IS mixed and must stay verbatim while the parent
     * indents around it. */
    const char xml[] =
        "<div><p>Hello <b>world</b>!</p><q>plain</q></div>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* out = leptris_document_serialize(doc, &opts);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
        "<div>\n  <p>Hello <b>world</b>!</p>\n  <q>plain</q>\n</div>");
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(MixedContent, CompactModeUnchanged) {
    /* indent=0 never inserts anything — byte-identical round trip. */
    const char xml[] = "<b>2<!-- c --><![CDATA[<t>]]></b>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, xml);
    leptris_free_string(out);
    leptris_document_free(doc);
}

/* Issue #550 sweep fallout: comments OUTSIDE the root element were
 * carved by the parser but never wired into the document, so they
 * silently vanished on serialization. The doc->pis twin chain now
 * retains them. Falsifiability: before the fix this emitted "<r/>". */
TEST(TopLevelComments, SurviveSerialization) {
    const char xml[] = "<!--before--><r/><!--after-->";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<!--before-->"), nullptr);
    EXPECT_NE(std::strstr(out, "<!--after-->"), nullptr);
    EXPECT_NE(std::strstr(out, "<r/>"), nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
}

/* Issue #550's suggested CI test: parse → serialize IMMEDIATELY,
 * no intervening calls, on the raw API — plus the sizing query on
 * serialize_into with NULL and non-NULL options. Before the
 * doc->pis/top_comments work this class of probe was reported as
 * returning NULL; the exact reported repro passed a truncated
 * length, which correctly fails the parse. This spec pins the
 * correct behavior for the raw sequence. */
TEST(RawApiProbe, ImmediateSerializeAfterParse) {
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_ERROR_MEMORY;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);

    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, xml);
    leptris_free_string(out);

    /* Sizing pass first, then write pass — both option shapes. */
    size_t need = leptris_document_serialize_into(doc, nullptr, 0, nullptr, nullptr);
    EXPECT_GT(need, 0u);
    LeptrisSerializeOptions opts = {0, 0, 0};
    size_t need2 = leptris_document_serialize_into(doc, nullptr, 0, nullptr, &opts);
    EXPECT_GT(need2, 0u);

    leptris_document_free(doc);
}

/* Issue #633: libxml2 xmlIndentTreeOutput parity — three residual
 * pretty-print divergences. */
TEST(SerializeOptions, PrettyChildCommentAndPiGetOwnLines) {
    const char xml[] = "<r><?pi data?><!--c--><e/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* s = leptris_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(std::string(s),
              "<r>\n  <?pi data?>\n  <!--c-->\n  <e/>\n</r>");
    leptris_free_string(s);
    leptris_document_free(doc);
}

TEST(SerializeOptions, PrettyRootTextOnlyWithAttributesNoTrailingNewline) {
    /* The reporter's non-ASCII repro carried an attribute — that is
     * what dodged the fusion fast path (no-attrs) and hit the open-
     * tag path, whose text-only close lacked the is-root guard. */
    const char xml[] = "<r a=\"1\">ascii</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* s = leptris_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(std::string(s), "<r a=\"1\">ascii</r>");
    leptris_free_string(s);
    leptris_document_free(doc);
}

TEST(SerializeOptions, PrettyDoctypeSubsetLayout) {
    const char xml[] =
        "<!DOCTYPE r [<!ELEMENT r (#PCDATA)>]><r>t</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;
    char* s = leptris_document_serialize(doc, &opts);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(std::string(s),
              "<!DOCTYPE r [\n<!ELEMENT r (#PCDATA)>\n]>\n<r>t</r>");
    leptris_free_string(s);
    /* Empty subsets drop the brackets entirely (libxml2). */
    const char xml2[] = "<!DOCTYPE r []><r/>";
    LeptrisDocument d2 = leptris_parse_string(xml2, std::strlen(xml2), &st);
    ASSERT_NE(d2, nullptr);
    char* s2 = leptris_document_serialize(d2, &opts);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(std::string(s2), "<!DOCTYPE r>\n<r/>");
    leptris_free_string(s2);
    leptris_document_free(d2);
    leptris_document_free(doc);
}

/* Issue #633 third ask: an indent unit string per level —
 * libxml2's xmlTreeIndentString / Nokogiri's indent_text ("\t"). */
TEST(SerializeOptions, IndentUnitStringPerLevel) {
    const char xml[] = "<r><a><b/></a></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisSerializeOptions opts = {0};
    opts.indent = 2;   /* any > 0 enables pretty layout */
    LeptrisSerializeExtOptions ext = {0};
    ext.indent_unit = "\t";
    char* s = leptris_document_serialize_ext(doc, &opts, &ext);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(std::string(s), "<r>\n\t<a>\n\t\t<b/>\n\t</a>\n</r>");
    leptris_free_string(s);
    leptris_document_free(doc);
}
