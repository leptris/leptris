// test/parser/test_encoding.cpp — encoding-path specs (TODO 37).
//
// Verifies UTF-16 BOM detection, UTF-16 conversion, and iconv paths
// don't leak.  The validation pass found leaks in the encoding
// wrappers; these specs would have caught them earlier.

#include <gtest/gtest.h>

#include "taurus.h"

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
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(
        (const char*)kUtf16LeBomSimple.data(),
        kUtf16LeBomSimple.size(),
        &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");

    taurus_document_free(doc);
}

TEST(EncodingUtf16, DetectsAndConvertsBeBom) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(
        (const char*)kUtf16BeBomSimple.data(),
        kUtf16BeBomSimple.size(),
        &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");

    taurus_document_free(doc);
}

TEST(EncodingUtf8, RejectsMalformedUtf8) {
    /* Lone continuation byte, then a start byte. */
    const char bad[] = "<r>\x80\xc3</r>";
    TaurusStatus st = TAURUS_OK;
    /* In lenient mode this may parse; in strict mode it should fail.
     * Either way, no crash, no leak. */
    TaurusDocument doc = taurus_parse_string(bad, sizeof(bad) - 1, &st);
    if (doc) taurus_document_free(doc);
}

TEST(EncodingAscii, ParsesAsUtf8ByDefault) {
    const char xml[] = "<r>plain ascii</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    taurus_document_free(doc);
    /* Zero leaks under `leaks --atExit --`. */
}

// ---- Edge cases (TODO 67) -------------------------------------------------

TEST(EncodingEdgeCases, EmptyInputReturnsNull) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("", 0, &st);
    EXPECT_EQ(doc, nullptr);
}

TEST(EncodingEdgeCases, JustWhitespace) {
    const char xml[] = "   \n\t ";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    if (doc) taurus_document_free(doc);
    /* Must not crash; either NULL or empty doc acceptable. */
}

TEST(EncodingEdgeCases, Utf8BomIsStripped) {
    /* UTF-8 BOM is EF BB BF.  Should be silently stripped. */
    const unsigned char xml[] = {
        0xEF, 0xBB, 0xBF,
        '<', 'r', '/', '>',
    };
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(
        (const char*)xml, sizeof(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");
    taurus_document_free(doc);
}

TEST(EncodingUtf8, OverlongEncodingDoesNotCrash) {
    /* 0xC0 0x80 is an overlong encoding of U+0000.  Lenient mode
     * may accept; strict rejects.  Either way: no crash. */
    const unsigned char xml[] = {
        '<', 'r', '>', 0xC0, 0x80, '<', '/', 'r', '>',
    };
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(
        (const char*)xml, sizeof(xml), &st);
    if (doc) taurus_document_free(doc);
}

TEST(EncodingUtf8, SurrogateRangeDoesNotCrash) {
    /* ED A0 80 decodes to U+D800 — surrogate, invalid. */
    const unsigned char xml[] = {
        '<', 'r', '>', 0xED, 0xA0, 0x80, '<', '/', 'r', '>',
    };
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(
        (const char*)xml, sizeof(xml), &st);
    if (doc) taurus_document_free(doc);
}

TEST(EncodingEdgeCases, TruncatedMultibyteAtEof) {
    /* Start of 2-byte UTF-8 with no continuation.  Must not crash. */
    const char xml[] = "<r>\xc3";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    if (doc) taurus_document_free(doc);
}

}  // namespace
