// test/xinclude/test_xinclude.cpp — XInclude classification specs.

#include <gtest/gtest.h>
#include "taurus.h"
#include <cstring>

namespace {

constexpr char kXIncludeNs[] = "http://www.w3.org/2001/XInclude";

TEST(XIncludeClassify, IdentifiesIncludeElement) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='chapter.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);

    EXPECT_EQ(taurus_xinclude_is_include_element(child), 1);
    EXPECT_EQ(taurus_xinclude_is_fallback_element(child), 0);

    taurus_document_free(doc);
}

TEST(XIncludeClassify, IdentifiesFallbackElement) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='missing.xml'><xi:fallback>not found</xi:fallback></xi:include>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    TaurusElement include_elem = taurus_element_first_child_any(root);
    ASSERT_NE(include_elem, nullptr);
    ASSERT_EQ(taurus_xinclude_is_include_element(include_elem), 1);

    TaurusElement fallback_elem = taurus_element_first_child_any(include_elem);
    ASSERT_NE(fallback_elem, nullptr);
    EXPECT_EQ(taurus_xinclude_is_fallback_element(fallback_elem), 1);

    taurus_document_free(doc);
}

TEST(XIncludeClassify, RejectsNonXIncludeElement) {
    const char xml[] = "<root><child/></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);

    EXPECT_EQ(taurus_xinclude_is_include_element(child), 0);
    EXPECT_EQ(taurus_xinclude_is_fallback_element(child), 0);

    taurus_document_free(doc);
}

TEST(XIncludeAttrs, ReturnsHrefAttribute) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='chapter1.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement include_elem = taurus_element_first_child_any(root);

    EXPECT_STREQ(taurus_xinclude_get_href(include_elem), "chapter1.xml");

    taurus_document_free(doc);
}

TEST(XIncludeAttrs, ParseDefaultsToXml) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='chapter.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement include_elem = taurus_element_first_child_any(root);

    /* parse attribute omitted -> default is "xml" per XInclude spec. */
    EXPECT_STREQ(taurus_xinclude_get_parse(include_elem), "xml");

    taurus_document_free(doc);
}

TEST(XIncludeProcess, ReturnsNotImplemented) {
    const char xml[] = "<root/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* The processor is a stub (TODO 92). The call must succeed without
     * crashing and return the documented error code. */
    TaurusStatus rc = taurus_xinclude_process(doc, nullptr);
    EXPECT_EQ(rc, TAURUS_ERROR_NOT_IMPLEMENTED);

    taurus_document_free(doc);
}

}  // namespace
