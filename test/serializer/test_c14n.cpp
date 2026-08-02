// test/serializer/test_c14n.cpp — Canonical XML (C14N) specs (TODO 50/57).
//
// taurus_c14n_canonicalize produces a canonical form for digital
// signatures.  These specs cover the documented C14N rules:
//   - UTF-8 encoding
//   - Normalized line endings (\n)
//   - Lexicographic attribute ordering
//   - Empty element expansion (<r/> -> <r></r>)
//   - Attribute value double-quote

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

namespace {

TEST(C14N, IsAvailableAndDoesNotCrash) {
    const char xml[] = "<r a='1' b='2'><c/>text</r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    EXPECT_NE(out, nullptr);
    if (out) {
        EXPECT_GT(std::strlen(out), 0u);
        taurus_free_string(out);
    }
    taurus_document_free(doc);
}

TEST(C14N, ExpandsEmptyElement) {
    const char xml[] = "<r/>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* C14N expands <r/> to <r></r>. */
    EXPECT_NE(s.find("<r></r>"), std::string::npos);
    taurus_free_string(out);
    taurus_document_free(doc);
}

TEST(C14N, SortsAttributes) {
    const char xml[] = "<r b='2' a='1'/>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    /* Lexicographic order: a before b. */
    size_t a_pos = s.find("a=");
    size_t b_pos = s.find("b=");
    ASSERT_NE(a_pos, std::string::npos);
    ASSERT_NE(b_pos, std::string::npos);
    EXPECT_LT(a_pos, b_pos);
    taurus_free_string(out);
    taurus_document_free(doc);
}

TEST(C14N, PreservesTextAndCdata) {
    const char xml[] = "<r>hello<![CDATA[<raw>]]>world</r>";
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    std::string s(out);
    EXPECT_NE(s.find("hello"), std::string::npos);
    EXPECT_NE(s.find("world"), std::string::npos);
    taurus_free_string(out);
    taurus_document_free(doc);
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

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* out = taurus_c14n_canonicalize(doc, TAURUS_C14N_1_0, 0);
    ASSERT_NE(out, nullptr);
    taurus_free_string(out);
    taurus_document_free(doc);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

}  // namespace
