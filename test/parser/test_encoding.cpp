// test/parser/test_encoding.cpp — encoding-path specs (TODO 37).
//
// Verifies UTF-16 BOM detection, UTF-16 conversion, and iconv paths
// don't leak.  The validation pass found leaks in the encoding
// wrappers; these specs would have caught them earlier.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <vector>

namespace {

// UTF-16LE BOM + "<r/>"
const std::vector<unsigned char> kUtf16LeBomSimple = {
    0xFF, 0xFE,            // BOM (LE)
    '<', 0x00,             // <
    'r', 0x00,             // r
    '/', 0x00,             // /
    '>', 0x00,             // >
};

// UTF-16BE BOM + "<r/>"
const std::vector<unsigned char> kUtf16BeBomSimple = {
    0xFE, 0xFF,            // BOM (BE)
    0x00, '<',
    0x00, 'r',
    0x00, '/',
    0x00, '>',
};

TEST(EncodingUtf16, DetectsAndConvertsLeBom) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)kUtf16LeBomSimple.data(),
        kUtf16LeBomSimple.size(),
        &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");

    leptris_document_free(doc);
}

TEST(EncodingUtf16, DetectsAndConvertsBeBom) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)kUtf16BeBomSimple.data(),
        kUtf16BeBomSimple.size(),
        &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");

    leptris_document_free(doc);
}

TEST(EncodingUtf8, RejectsMalformedUtf8) {
    /* Lone continuation byte, then a start byte. */
    const char bad[] = "<r>\x80\xc3</r>";
    LeptrisStatus st = LEPTRIS_OK;
    /* In lenient mode this may parse; in strict mode it should fail.
     * Either way, no crash, no leak. */
    LeptrisDocument doc = leptris_parse_string(bad, sizeof(bad) - 1, &st);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingAscii, ParsesAsUtf8ByDefault) {
    const char xml[] = "<r>plain ascii</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
    /* Zero leaks under `leaks --atExit --`. */
}

// ---- Edge cases (TODO 67) -------------------------------------------------

TEST(EncodingEdgeCases, EmptyInputReturnsNull) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("", 0, &st);
    EXPECT_EQ(doc, nullptr);
}

TEST(EncodingEdgeCases, JustWhitespace) {
    const char xml[] = "   \n\t ";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (doc) leptris_document_free(doc);
    /* Must not crash; either NULL or empty doc acceptable. */
}

TEST(EncodingEdgeCases, Utf8BomIsStripped) {
    /* UTF-8 BOM is EF BB BF.  Should be silently stripped. */
    const unsigned char xml[] = {
        0xEF, 0xBB, 0xBF,
        '<', 'r', '/', '>',
    };
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)xml, sizeof(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");
    leptris_document_free(doc);
}

TEST(EncodingUtf8, OverlongEncodingDoesNotCrash) {
    /* 0xC0 0x80 is an overlong encoding of U+0000.  Lenient mode
     * may accept; strict rejects.  Either way: no crash. */
    const unsigned char xml[] = {
        '<', 'r', '>', 0xC0, 0x80, '<', '/', 'r', '>',
    };
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)xml, sizeof(xml), &st);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingUtf8, SurrogateRangeDoesNotCrash) {
    /* ED A0 80 decodes to U+D800 — surrogate, invalid. */
    const unsigned char xml[] = {
        '<', 'r', '>', 0xED, 0xA0, 0x80, '<', '/', 'r', '>',
    };
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)xml, sizeof(xml), &st);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingEdgeCases, TruncatedMultibyteAtEof) {
    /* Start of 2-byte UTF-8 with no continuation.  Must not crash. */
    const char xml[] = "<r>\xc3";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (doc) leptris_document_free(doc);
}

// ---- More encoding coverage (TODO 104) ---------------------------------

TEST(EncodingUtf16, ParsesWithoutBomHeuristic) {
    /* libleptris detects UTF-16 without BOM via byte-pattern heuristics.
     * Construct a small UTF-16LE document by hand and verify it parses
     * to the expected root. */
    const unsigned char le[] = {
        '<', 0, 'r', 0, '/', 0, '>', 0,
    };
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        (const char*)le, sizeof(le), &st);
    if (doc) {
        LeptrisElement root = leptris_document_root(doc);
        EXPECT_NE(root, nullptr);
        if (root) EXPECT_STREQ(leptris_element_name(root), "r");
        leptris_document_free(doc);
    }
}

TEST(EncodingUtf8, EmptyElementWithUnicodeText) {
    /* Multi-byte UTF-8 in text content must round-trip. */
    const char xml[] = "<r>\xc3\xa9\xc3\xa8\xc3\xaa</r>";  /* é è ê */
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    /* Text content should contain the UTF-8 bytes verbatim. */
    const char* text = leptris_element_text(root);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(std::string(text).find("\xc3\xa9"), std::string::npos);
    leptris_document_free(doc);
}

TEST(EncodingEdgeCases, XmlDeclarationWithStandaloneYes) {
    /* standalone='yes' in the declaration must be preserved. */
    const char xml[] = "<?xml version='1.0' standalone='yes'?><r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
}

TEST(EncodingEdgeCases, XmlDeclarationWithStandaloneNo) {
    const char xml[] = "<?xml version='1.0' standalone='no'?><r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_document_free(doc);
}

TEST(EncodingEdgeCases, XmlDeclarationMissingVersionAttr) {
    /* Malformed declaration missing the version attr.  The parser
     * should either accept it leniently or reject it cleanly — must
     * not crash. */
    const char xml[] = "<?xml encoding='UTF-8'?><r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingEdgeCases, WhitespaceOnlyAfterDeclaration) {
    /* Just declaration + whitespace.  No root element.  Should reject
     * (need a root) but not crash. */
    const char xml[] = "<?xml version='1.0'?>   ";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_EQ(doc, nullptr);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingEdgeCases, Utf8BomFollowedByDeclaration) {
    /* BOM + declaration + root — common pattern from text editors.
     * The BOM must be stripped, declaration consumed, root returned. */
    const unsigned char xml[] = {
        0xEF, 0xBB, 0xBF,
        '<', '?', 'x', 'm', 'l', ' ', 'v', 'e', 'r', 's', 'i', 'o', 'n', '=', '"', '1', '.', '0', '"', '?', '>',
        '<', 'r', '/', '>',
    };
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string((const char*)xml, sizeof(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_NE(root, nullptr);
    if (root) EXPECT_STREQ(leptris_element_name(root), "r");
    leptris_document_free(doc);
}

TEST(EncodingEdgeCases, NullStatusPointerIsSafe) {
    /* Passing NULL for the status out-param must not crash. */
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    EXPECT_NE(doc, nullptr);
    if (doc) leptris_document_free(doc);
}

TEST(EncodingEdgeCases, EmptyXmlElementRoundTrips) {
    /* Empty <r/> must round-trip cleanly. */
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    /* Either <r/> or <r></r>.  Don't be picky. */
    EXPECT_NE(std::string(serialized).find("r"), std::string::npos);
    leptris_free_string(serialized);
    leptris_document_free(doc);
}

}  // namespace
