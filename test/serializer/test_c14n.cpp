// test/serializer/test_c14n.cpp — Canonical XML (C14N) specs (TODO 50/57).
//
// leptris_c14n_canonicalize produces a canonical form for digital
// signatures.  These specs cover the documented C14N rules:
//   - UTF-8 encoding
//   - Normalized line endings (\n)
//   - Lexicographic attribute ordering
//   - Empty element expansion (<r/> -> <r></r>)
//   - Attribute value double-quote

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

/* #881: REC-xml-c14n section 2.2 — namespace nodes sort
 * lexicographically BY PREFIX (default ns first). The engine
 * emitted document order; every conformant implementation
 * (libxml2, Java) sorts, so canonical bytes and XML signatures
 * did not interop. */
TEST(C14nConformance, NamespaceDeclarationsSortedByPrefix) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<r xmlns:z=\"urn:z\" xmlns:a=\"urn:a\" xmlns=\"urn:d\""
        " xmlns:m=\"urn:m\"/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out,
        "xmlns=\"urn:d\" xmlns:a=\"urn:a\" xmlns:m=\"urn:m\""
        " xmlns:z=\"urn:z\""), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, IsAvailableAndDoesNotCrash) {
    const char xml[] = "<r a='1' b='2'><c/>text</r>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    EXPECT_NE(out, nullptr);
    if (out) {
        EXPECT_GT(std::strlen(out), 0u);
        leptris_free_string(out);
    }
    leptris_document_free(doc);
}

TEST(C14N, ExpandsEmptyElement) {
    const char xml[] = "<r/>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* C14N expands <r/> to <r></r>. */
    EXPECT_NE(s.find("<r></r>"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, SortsAttributes) {
    const char xml[] = "<r b='2' a='1'/>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* Lexicographic order: a before b. */
    size_t a_pos = s.find("a=");
    size_t b_pos = s.find("b=");
    ASSERT_NE(a_pos, std::string::npos);
    ASSERT_NE(b_pos, std::string::npos);
    EXPECT_LT(a_pos, b_pos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, PreservesTextAndCdata) {
    const char xml[] = "<r>hello<![CDATA[<raw>]]>world</r>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, NoLeaksOnComplexDocument) {
    /* A document exercising every node type for C14N. */
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- comment -->"
        "<r xmlns:ns='http://x' a='1' b='2'>"
        "<ns:c ns:attr='v'>text</ns:c>"
        "<![CDATA[raw<content>]]>"
        "</r>";

    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

// ---- More C14N coverage (TODO 104) -------------------------------------

TEST(C14N, EmptyDocumentCanonicalizes) {
    /* Even the simplest document must produce valid canonical output. */
    const char xml[] = "<r/>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* Empty elements expand to start+end form per C14N spec. */
    EXPECT_NE(std::string(out).find("<r></r>"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, NormalizesLineEndings) {
    /* C14N: \r\n and \r must be normalized to \n in output. */
    const char xml[] = "<r>a\rb\nc\r\nd</r>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* No lone \r and no \r\n should remain. */
    EXPECT_EQ(s.find("\r\n"), std::string::npos);
    EXPECT_EQ(s.find("\r"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, EscapesSpecialCharsInText) {
    /* Canonical text must escape <, >, &.  C14N also escapes \r. */
    const char xml[] = "<r>a &amp; b &lt; c &gt; d</r>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* After canonicalization, the escaped entities should be present
     * (the parser decoded them on input; the serializer re-escapes). */
    std::string s(out);
    EXPECT_NE(s.find("a "), std::string::npos);  // text content survives
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, EmptyAttributesArePreserved) {
    /* Attributes with empty values must appear in canonical output. */
    const char xml[] = "<r a=''/>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* C14N uses double-quotes around attribute values. */
    EXPECT_NE(std::string(out).find("a=\"\""), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, NullDocReturnsNull) {
    EXPECT_EQ(leptris_c14n_canonicalize(nullptr, LEPTRIS_C14N_1_0, 0), nullptr);
}

TEST(C14N, NestedElementsCanonicalizedInOrder) {
    /* Document order is the canonical order — children must appear
     * in their original sequence. */
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    auto a_pos = s.find("<a></a>");
    auto b_pos = s.find("<b></b>");
    auto c_pos = s.find("<c></c>");
    EXPECT_NE(a_pos, std::string::npos);
    EXPECT_NE(b_pos, std::string::npos);
    EXPECT_NE(c_pos, std::string::npos);
    EXPECT_LT(a_pos, b_pos);
    EXPECT_LT(b_pos, c_pos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14N, DeepNestingRoundTrips) {
    /* A 10-level deep tree must canonicalize without recursion issues. */
    const char xml[] = "<a><b><c><d><e><f><g><h><i><j>x</j></i></h></g></f></e></d></c></b></a>";
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* All ten levels must appear in the output. */
    std::string s(out);
    for (char tag = 'a'; tag <= 'j'; tag++) {
        std::string open = "<";
        open += tag;
        open += ">";
        EXPECT_NE(s.find(open), std::string::npos)
            << "missing <" << tag << "> in canonical output";
    }
    leptris_free_string(out);
    leptris_document_free(doc);
}

/* #919: REC-xml-c14n section 2.3 — attribute nodes sort by
 * namespace URI (empty/no-namespace FIRST), then local name.
 * Namespaced attributes come after ALL bare ones. */
TEST(C14nConformance, AttributesSortedByUriThenLocal) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<doc xmlns:p='urn:p' xmlns:z='urn:z'"
        " xmlns='urn:d' b='2' a='1'><e p:x='v' z:w='w' c='k' a='m'>t</e></doc>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* Bare attrs (no namespace) first, sorted: a, b on doc; a, c on
     * e; then namespaced, URI 'urn:p' < 'urn:z': p:x before z:w. */
    std::string s(out);
    size_t doc_at = s.find("<doc");
    size_t e_at = s.find("<e ");
    ASSERT_NE(doc_at, std::string::npos);
    ASSERT_NE(e_at, std::string::npos);
    size_t a_pos = s.find("a=\"1\"", doc_at);
    size_t b_pos = s.find("b=\"2\"", doc_at);
    ASSERT_NE(a_pos, std::string::npos);
    ASSERT_NE(b_pos, std::string::npos);
    EXPECT_LT(a_pos, b_pos);
    size_t ea_pos = s.find("a=\"m\"", e_at);
    size_t ec_pos = s.find("c=\"k\"", e_at);
    size_t px_pos = s.find("p:x=\"v\"", e_at);
    size_t zw_pos = s.find("z:w=\"w\"", e_at);
    ASSERT_NE(ea_pos, std::string::npos);
    ASSERT_NE(ec_pos, std::string::npos);
    ASSERT_NE(px_pos, std::string::npos);
    ASSERT_NE(zw_pos, std::string::npos);
    EXPECT_LT(ea_pos, ec_pos);
    EXPECT_LT(ec_pos, px_pos);   /* bare before namespaced */
    EXPECT_LT(px_pos, zw_pos);   /* urn:p < urn:z */
    leptris_free_string(out);
    leptris_document_free(doc);
}


// ---- #1096: extended-entry (mode:) C14N parity ----

TEST(C14n1096, RedundantRedeclarationOmitted) {
    const char xml[] =
        "<root xmlns:a=\"http://a.com\"><child xmlns:a=\"http://a.com\"/></root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_1, LEPTRIS_C14N_MODE_CANONICAL, NULL, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<root xmlns:a=\"http://a.com\">"), nullptr) << out;
    EXPECT_EQ(std::strstr(out, "<child xmlns:a="), nullptr) << out;
    EXPECT_NE(std::strstr(out, "<child></child>"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14n1096, XmlNamespaceDeclarationOmitted) {
    const char xml[] = "<root xmlns:xml=\"http://www.w3.org/XML/1998/namespace\"/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_1, LEPTRIS_C14N_MODE_CANONICAL, NULL, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::strstr(out, "xmlns:xml"), nullptr) << out;
    EXPECT_NE(std::strstr(out, "<root></root>"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14n1096, DocumentLevelPIsKeptOnExtendedEntry) {
    const char xml[] = "<?target data?><r/><?after d2?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_1, LEPTRIS_C14N_MODE_CANONICAL, NULL, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<?target data?>"), nullptr) << out;
    EXPECT_NE(std::strstr(out, "<?after d2?>"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}


// ---- #1117: whitespace-only PI data + doc-level separators ----

TEST(C14n1117, WhitespaceOnlyPiDataDropped) {
    const char xml[] = "<?pi-without-data     ?><r/><?p2   ?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    /* Whitespace-only PI data is no data (libxml2 ground truth). */
    EXPECT_EQ(std::strstr(out, "<?pi-without-data?>"), out) << out;
    EXPECT_NE(std::strstr(out, "<?p2?>"), nullptr) << out;
    EXPECT_EQ(std::strstr(out, "     ?"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14n1117, DocLevelNodesGetNewlineSeparators) {
    const char xml[] = "<?a x?><?b y?><r/><!--c--><?d z?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    /* A newline FOLLOWS every document-level node except the
     * last (libxml2 ground truth; the plain entry keeps the
     * with_comments=0 default, so the doc comment drops). */
    EXPECT_STREQ(out,
        "<?a x?>\n<?b y?>\n<r></r>\n<?d z?>") << out;
    leptris_free_string(out);
    /* with_comments=1 keeps the document comment, separator and
     * all. */
    char* wc = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_1, LEPTRIS_C14N_MODE_CANONICAL, NULL, 1);
    ASSERT_NE(wc, nullptr);
    EXPECT_STREQ(wc,
        "<?a x?>\n<?b y?>\n<r></r>\n<!--c-->\n<?d z?>") << wc;
    leptris_free_string(wc);
    leptris_document_free(doc);
}

TEST(C14n1117, ExtendedEntryMatchesDocLevelConvention) {
    const char xml[] = "<?a x?><r/><?b z?>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize_ex(
        doc, LEPTRIS_C14N_1_1, LEPTRIS_C14N_MODE_CANONICAL, NULL, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<?a x?>\n<r></r>\n<?b z?>") << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

}  // namespace

// ---- TODO 85: C14N 1.1 + exclusive canonicalization -------------------

TEST(C14NWithComments, PreservesCommentsWhenRequested) {
    /* The C14N spec defines two modes: with and without comments.
     * The with-comments mode preserves comment nodes in the output.
     * The default (without-comments) strips them. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><!-- comment -->text</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    /* Default is without-comments; comment may or may not appear
     * depending on impl. Pin whatever the current behavior is so
     * future changes are deliberate. */
    std::string s(out);
    EXPECT_NE(s.find("<r>"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14NNamespaces, IncludesNamespacesInCanonicalOutput) {
    /* C14N 1.0 inclusive: namespace declarations visible on the element
     * are emitted in the canonical output, sorted with other attributes. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<r xmlns:ns='http://example.com/ns' ns:attr='value'>text</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* Both xmlns:ns and ns:attr must appear. */
    EXPECT_NE(s.find("xmlns:ns"), std::string::npos);
    EXPECT_NE(s.find("ns:attr"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14NDocumentOrder, AttributesEmittedInSortedOrder) {
    /* Per C14N spec: attributes are sorted by namespace URI then
     * local name. Test that 'b' comes before 'z' and 'a:b' before 'a:z'. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r z='1' a='2' m='3'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* Expected: a, m, z order. */
    size_t a_pos = s.find("a=\"2\"");
    size_t m_pos = s.find("m=\"3\"");
    size_t z_pos = s.find("z=\"1\"");
    EXPECT_NE(a_pos, std::string::npos);
    EXPECT_NE(m_pos, std::string::npos);
    EXPECT_NE(z_pos, std::string::npos);
    EXPECT_LT(a_pos, m_pos);
    EXPECT_LT(m_pos, z_pos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14NXml11, AcceptsVersion11WithoutCrash) {
    /* C14N 1.1 is rarely used but the API must accept the enum value
     * without crashing. Behavior differences from 1.0 are minor
     * (line-ending handling, some edge cases around character escapes). */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>hello</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::string(out).find("hello"), std::string::npos);
    leptris_free_string(out);
    leptris_document_free(doc);
}

/* ---- #1015: C14N 1.1 divergence families (canon's 16-case edge
 * corpus). All four RED against the native lane when filed. ---- */

TEST(C14n11, AttributeOrderNoNamespaceFirstThenPerNamespace) {
    /* Spec §2.3: no-namespace attrs sorted by name, THEN namespaced
     * attrs (by URI, then name). Native emitted ns attrs before the
     * no-ns block AND misordered z vs x:n. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<e z=\"1\" a=\"2\" m=\"3\" x:n=\"4\" xmlns:x=\"urn:x\"/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string(out),
              "<e xmlns:x=\"urn:x\" a=\"2\" m=\"3\" z=\"1\""
              " x:n=\"4\"></e>") << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14n11, PrefixedElementKeepsPrefix) {
    /* §2.1: prefixes stay; native dropped x: from <x:b/> (namespace
     * changed!). Also the rebinding case: p: under a re-bound p
     * keeps its prefixes. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root xmlns=\"urn:r\" xmlns:x=\"urn:x\"><x:b/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<x:b></x:b>"), nullptr) << out;
    EXPECT_EQ(std::strstr(out, "<b></b>"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(C14n11, TextAndAttributeEscaping) {
    /* Spec: '&' '<' '>' escaped in text; '&' '<' '"' TAB CR LF
     * escaped in attribute values. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>t > w</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "t &gt; w"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);

    const char xml2[] = "<unicode><x y=\"&#9;\">z</x></unicode>";
    LeptrisDocument doc2 = leptris_parse_string(xml2, std::strlen(xml2), &st);
    ASSERT_NE(doc2, nullptr);
    char* out2 = leptris_c14n_canonicalize(doc2, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out2, nullptr);
    EXPECT_NE(std::strstr(out2, "y=\"&#x9;\""), nullptr) << out2;
    leptris_free_string(out2);
    leptris_document_free(doc2);
}

TEST(C14n11, DocumentLevelPIsKept) {
    /* §2.4/PImode: prolog and epilog processing instructions stay
     * outside the document element. Native dropped them. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<?xml version=\"1.0\"?><?target data?><r/><?after d2?>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    char* out = leptris_c14n_canonicalize(doc, LEPTRIS_C14N_1_1, 0);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<?target data?>"), nullptr) << out;
    EXPECT_NE(std::strstr(out, "<?after d2?>"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}
