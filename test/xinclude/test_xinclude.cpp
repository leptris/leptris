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

TEST(XIncludeProcess, ParseTextUsesFallbackWhenFileMissing) {
    /* When the href can't be loaded, use xi:fallback content instead. */
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/this_file_does_not_exist_xyz_123.txt' "
        "               parse='text'>"
        "    <xi:fallback>Fallback content here</xi:fallback>"
        "  </xi:include>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusStatus rc = taurus_xinclude_process(doc, nullptr);
    EXPECT_EQ(rc, TAURUS_OK);

    /* Verify the fallback content is in the tree as a text node. */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    bool found_fallback = false;
    bool found_include = false;
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    while (child) {
        const char* content = taurus_text_node_get_content(child);
        if (content && std::string(content).find("Fallback content") != std::string::npos) {
            found_fallback = true;
        }
        TaurusElement elem_child = taurus_node_as_element(child);
        if (elem_child && taurus_xinclude_is_include_element(elem_child)) {
            found_include = true;
        }
        child = taurus_node_next_sibling(child);
    }
    EXPECT_TRUE(found_fallback);
    EXPECT_FALSE(found_include);

    taurus_document_free(doc);
}

// ---- parse="xml" ----------------------------------------------------------
//
// XInclude 1.0 default mode: the href is loaded as XML and the root
// of the included document replaces the xi:include element.  Children,
// attributes, and mixed content must all survive the cross-document
// copy.

TEST(XIncludeProcess, ParseXmlReplacesIncludeWithRootElement) {
    const char included_xml[] =
        "<chapter><title>Included</title><para>Body text</para></chapter>";
    const char* inc_path = "/tmp/taurus_xinclude_xml_test.xml";
    FILE* f = fopen(inc_path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(included_xml, 1, std::strlen(included_xml), f);
    fclose(f);

    const char xml[] =
        "<book xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_xml_test.xml'/>"
        "</book>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* The xi:include must be gone, replaced by <chapter>. */
    bool found_chapter = false;
    bool found_include = false;
    for (TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
         child;
         child = taurus_node_next_sibling(child)) {
        TaurusElement e = taurus_node_as_element(child);
        if (!e) continue;
        if (taurus_xinclude_is_include_element(e)) {
            found_include = true;
        }
        if (std::string(taurus_element_name(e)) == "chapter") {
            found_chapter = true;
        }
    }
    EXPECT_TRUE(found_chapter);
    EXPECT_FALSE(found_include);

    taurus_document_free(doc);
    remove(inc_path);
}

TEST(XIncludeProcess, ParseXmlCopiesAttributesAndChildren) {
    /* The deep-copy must carry over attributes and the entire child
     * subtree — not just the bare root element. */
    const char included_xml[] =
        "<data id='x' lang='en'>"
        "  <item>one</item>"
        "  <item>two</item>"
        "  text-around"
        "</data>";
    const char* inc_path = "/tmp/taurus_xinclude_attrs_test.xml";
    FILE* f = fopen(inc_path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(included_xml, 1, std::strlen(included_xml), f);
    fclose(f);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_attrs_test.xml' parse='xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    TaurusElement data_elem = nullptr;
    for (TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
         child;
         child = taurus_node_next_sibling(child)) {
        TaurusElement e = taurus_node_as_element(child);
        if (e && std::string(taurus_element_name(e)) == "data") {
            data_elem = e;
            break;
        }
    }
    ASSERT_NE(data_elem, nullptr);

    /* Attributes survived the copy. */
    EXPECT_STREQ(taurus_element_attribute(data_elem, "id"), "x");
    EXPECT_STREQ(taurus_element_attribute(data_elem, "lang"), "en");

    /* The concatenated text content includes both item text and the
     * surrounding text node — proving the mixed-content subtree copy
     * is complete. */
    const char* text = taurus_element_text(data_elem);
    EXPECT_NE(text, nullptr);
    if (text) {
        std::string s(text);
        EXPECT_NE(s.find("one"), std::string::npos);
        EXPECT_NE(s.find("two"), std::string::npos);
        EXPECT_NE(s.find("text-around"), std::string::npos);
    }

    /* Child count must match (two <item> elements). */
    int item_count = 0;
    for (TaurusElement c = taurus_element_first_child_any(data_elem);
         c;
         c = taurus_element_next_sibling_any(c)) {
        if (std::string(taurus_element_name(c)) == "item") item_count++;
    }
    EXPECT_EQ(item_count, 2);

    taurus_document_free(doc);
    remove(inc_path);
}

TEST(XIncludeProcess, ParseXmlUsesFallbackWhenFileMissing) {
    /* Same fallback contract as parse="text": when the resource can't
     * be loaded, xi:fallback content is spliced in as a text node. */
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/does-not-exist-taurus.xml' parse='xml'>"
        "    <xi:fallback>xml fallback text</xi:fallback>"
        "  </xi:include>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    bool found_fallback = false;
    for (TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
         child;
         child = taurus_node_next_sibling(child)) {
        const char* c = taurus_text_node_get_content(child);
        if (c && std::string(c).find("xml fallback text") != std::string::npos) {
            found_fallback = true;
        }
    }
    EXPECT_TRUE(found_fallback);

    taurus_document_free(doc);
}

TEST(XIncludeProcess, ParseXmlRecursiveIncludesNestedXi) {
    /* An included document can itself contain xi:include; the walker
     * processes bottom-up so the nested include resolves before the
     * outer splice happens. */
    const char inner_xml[] = "<inner>deep</inner>";
    const char* inner_path = "/tmp/taurus_xinclude_inner.xml";
    FILE* f1 = fopen(inner_path, "wb");
    ASSERT_NE(f1, nullptr);
    fwrite(inner_xml, 1, std::strlen(inner_xml), f1);
    fclose(f1);

    const char outer_xml[] =
        "<outer xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_inner.xml'/>"
        "</outer>";
    const char* outer_path = "/tmp/taurus_xinclude_outer.xml";
    FILE* f2 = fopen(outer_path, "wb");
    ASSERT_NE(f2, nullptr);
    fwrite(outer_xml, 1, std::strlen(outer_xml), f2);
    fclose(f2);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_outer.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    /* Expected tree: root > outer > inner > "deep". */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    TaurusElement outer = taurus_element_first_child_any(root);
    ASSERT_NE(outer, nullptr);
    EXPECT_STREQ(taurus_element_name(outer), "outer");
    TaurusElement inner = taurus_element_first_child_any(outer);
    ASSERT_NE(inner, nullptr);
    EXPECT_STREQ(taurus_element_name(inner), "inner");
    EXPECT_STREQ(taurus_element_text(inner), "deep");

    taurus_document_free(doc);
    remove(inner_path);
    remove(outer_path);
}

// ---- Phase 4: xpointer fragment selection (TODO 92) ------------------

TEST(XIncludeXpointer, SelectsFragmentByXPath) {
    /* Included document has multiple sections; xpointer selects one
     * by id attribute. Only that subtree is spliced in. */
    const char included_xml[] =
        "<doc>"
        "  <section id='intro'>introduction</section>"
        "  <section id='body'>main content</section>"
        "  <section id='appendix'>extra</section>"
        "</doc>";
    const char* path = "/tmp/taurus_xinclude_xpointer_src.xml";
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(included_xml, 1, std::strlen(included_xml), f);
    fclose(f);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_xpointer_src.xml'"
        "              xpointer=\"//section[@id='body']\"/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    /* Expected: root has one child <section id='body'>main content</section>. */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "section");
    EXPECT_STREQ(taurus_element_attribute(child, "id"), "body");
    EXPECT_STREQ(taurus_element_text(child), "main content");

    taurus_document_free(doc);
    remove(path);
}

TEST(XIncludeXpointer, EmptyResultFallsBackToRoot) {
    /* If the xpointer matches nothing, the spec is unclear; we fall
     * back to the included doc's root element. */
    const char included_xml[] = "<doc><a/></doc>";
    const char* path = "/tmp/taurus_xinclude_xpointer_empty.xml";
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    fwrite(included_xml, 1, std::strlen(included_xml), f);
    fclose(f);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "  <xi:include href='/tmp/taurus_xinclude_xpointer_empty.xml'"
        "              xpointer=\"//nonexistent\"/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(taurus_xinclude_process(doc, nullptr), TAURUS_OK);

    /* Expected: root has the included doc's root element <doc>. */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "doc");

    taurus_document_free(doc);
    remove(path);
}

TEST(XIncludeRecursion, MutuallyRecursiveIncludesHitDepthLimit) {
    /* Two files include each other — would loop forever without the
     * recursion guard. The processor must stop at XINCLUDE_MAX_DEPTH
     * and return an error (or silently stop; we accept either as long
     * as the process terminates within reasonable time). */
    const char* path_a = "/tmp/taurus_xinclude_recursion_a.xml";
    const char* path_b = "/tmp/taurus_xinclude_recursion_b.xml";
    const char a_xml[] =
        "<a xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='/tmp/taurus_xinclude_recursion_b.xml'/>"
        "</a>";
    const char b_xml[] =
        "<b xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='/tmp/taurus_xinclude_recursion_a.xml'/>"
        "</b>";
    FILE* fa = fopen(path_a, "wb");
    ASSERT_NE(fa, nullptr);
    fwrite(a_xml, 1, std::strlen(a_xml), fa);
    fclose(fa);
    FILE* fb = fopen(path_b, "wb");
    ASSERT_NE(fb, nullptr);
    fwrite(b_xml, 1, std::strlen(b_xml), fb);
    fclose(fb);

    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='/tmp/taurus_xinclude_recursion_a.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Should terminate (not hang). Return value may be OK or an error
     * depending on whether the impl treats max-depth as fatal. The
     * important thing is that this call returns. */
    TaurusStatus rc = taurus_xinclude_process(doc, nullptr);
    (void)rc;  /* either TAURUS_OK or TAURUS_ERROR_INVALID_ARG */

    taurus_document_free(doc);
    remove(path_a);
    remove(path_b);
}
