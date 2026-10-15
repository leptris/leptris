#include <gtest/gtest.h>
#include <leptris.h>
#include <cstring>

namespace {

TEST(XIncludeFallback, ElementFallbackSplicesOnNoNetworkRefusal) {
    /* leptris-ruby#478 / #1734: a no_network refusal is a resource
     * error — the xi:fallback's ELEMENT children must replace the
     * include (XInclude §4.5), not vanish together with it. */
    const char xml[] =
        "<r xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='https://example.org/nope.xml'>"
        "<xi:fallback><offline reason='refused'/></xi:fallback>"
        "</xi:include>"
        "</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_xinclude_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.no_network = 1;
    ASSERT_EQ(leptris_xinclude_process_opts(doc, nullptr, &opts), LEPTRIS_OK);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisElement child = leptris_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "offline");
    EXPECT_STREQ(leptris_element_attribute(child, "reason"), "refused");
    char* ser = leptris_serialize_document(doc);
    ASSERT_NE(ser, nullptr);
    EXPECT_STREQ(ser,
        "<r xmlns:xi=\"http://www.w3.org/2001/XInclude\">"
        "<offline reason=\"refused\"/></r>");
    leptris_free_string(ser);
    leptris_document_free(doc);
}

TEST(XIncludeFallback, MultiNodeElementFallbackSplicesInOrder) {
    const char xml[] =
        "<r xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='https://example.org/nope.xml'>"
        "<xi:fallback><a/><b/></xi:fallback>"
        "</xi:include></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    leptris_xinclude_opts opts;
    memset(&opts, 0, sizeof(opts));
    ASSERT_EQ(leptris_xinclude_process_opts(doc, nullptr, &opts), LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement c = leptris_element_first_child_any(root);
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(leptris_element_name(c), "a");
    c = leptris_element_next_sibling_any(c);
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(leptris_element_name(c), "b");
    leptris_document_free(doc);
}

TEST(XIncludeFallback, TextFallbackStillWorks) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='/tmp/this_file_does_not_exist_xyz_123.txt' "
        "parse='text'><xi:fallback>Fallback content here</xi:fallback>"
        "</xi:include></root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    ASSERT_EQ(leptris_xinclude_process(doc, nullptr), LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    char* ser = leptris_serialize_document(doc);
    ASSERT_NE(ser, nullptr);
    EXPECT_TRUE(strstr(ser, "Fallback content here") != nullptr);
    leptris_free_string(ser);
    leptris_document_free(doc);
}

}  // namespace
