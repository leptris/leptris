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

}  // namespace
