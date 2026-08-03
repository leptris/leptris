// test/xinclude/test_xinclude.cpp — XInclude classification specs.

#include <gtest/gtest.h>
#include "taurus.h"
#include <cstring>
#include <cstdio>
#include <string>

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

TEST(XIncludeProcess, NoIncludesIsOk) {
    /* A document with no xi:include elements processes cleanly. */
    const char xml[] = "<root/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusStatus rc = taurus_xinclude_process(doc, nullptr);
    EXPECT_EQ(rc, TAURUS_OK);

    taurus_document_free(doc);
}

TEST(XIncludeProcess, ParseTextReplacesIncludeWithContent) {
    /* Create a temp file, include it via parse="text", verify the
     * content appears as a text node. */
    const char text_content[] = "Hello from included file!";
    const char* tmp_path = "/tmp/taurus_xinclude_test.txt";
    FILE* f = fopen(tmp_path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(text_content, 1, std::strlen(text_content), f);
    fclose(f);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_test.txt' parse='text'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusStatus rc = taurus_xinclude_process(doc, nullptr);
    EXPECT_EQ(rc, TAURUS_OK);

    /* The xi:include should be replaced by a text node. Walk children. */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* Check that no child is xi:include anymore, and text content
     * from the file is present. */
    bool found_text = false;
    bool found_include = false;
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    while (child) {
        /* Try text accessor — returns NULL for non-text nodes. */
        const char* content = taurus_text_node_get_content(child);
        if (content && std::string(content).find("Hello from") != std::string::npos) {
            found_text = true;
        }
        /* Try element accessor — returns NULL for non-elements. */
        TaurusElement elem_child = taurus_node_as_element(child);
        if (elem_child && taurus_xinclude_is_include_element(elem_child)) {
            found_include = true;
        }
        child = taurus_node_next_sibling(child);
    }
    EXPECT_TRUE(found_text);
    EXPECT_FALSE(found_include);

    taurus_document_free(doc);
    remove(tmp_path);
}

}  // namespace
