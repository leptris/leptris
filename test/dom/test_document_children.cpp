// test/dom/test_document_children.cpp — issue #580: document-level
// PIs/comments are tree children of the document node (libxml2
// model). Navigation from leptris_document_node, XPath §5 visibility
// at the document level, #526 flat accessors over the same store.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

/* <!-- pro --!><?pp d?><r><!-- in --><?ip?></r><!-- epi --> */
constexpr char kDocLevel[] =
    "<!-- pro --><?pp d?><r><!-- in --><?ip?></r><!-- epi --><?ep?>";

TEST(DocumentChildren, ChainIsPrologRootEpilog) {
    LeptrisDocument doc =
        leptris_parse_string(kDocLevel, std::strlen(kDocLevel), nullptr);
    ASSERT_NE(doc, nullptr);

    LeptrisNodeRef n = leptris_document_node(doc);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), LEPTRIS_NODE_TYPE_DOCUMENT);

    LeptrisNodeRef c = leptris_node_first_child(n);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_COMMENT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_PI);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_ELEMENT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_COMMENT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_PI);
    EXPECT_EQ(leptris_node_next_sibling(c), nullptr);

    leptris_document_free(doc);
}

TEST(DocumentChildren, FlatAccessorsReadTheSameStore) {
    LeptrisDocument doc =
        leptris_parse_string(kDocLevel, std::strlen(kDocLevel), nullptr);
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(leptris_document_pi_count(doc), 2u);
    EXPECT_STREQ(leptris_document_pi_target(doc, 0), "pp");
    EXPECT_STREQ(leptris_document_pi_target(doc, 1), "ep");
    EXPECT_EQ(leptris_document_comment_count(doc), 2u);
    EXPECT_STREQ(leptris_document_comment_content(doc, 0), " pro ");
    EXPECT_STREQ(leptris_document_comment_content(doc, 1), " epi ");

    leptris_document_free(doc);
}

TEST(DocumentChildren, XpathSeesDocumentLevelNodes) {
    LeptrisDocument doc =
        leptris_parse_string(kDocLevel, std::strlen(kDocLevel), nullptr);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "count(/processing-instruction())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "count(/comment())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);

    /* Target-filtered document-level PI. */
    r = leptris_xpath_eval(doc, nullptr, "count(/processing-instruction('ep'))");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    /* descendant-or-self from the document covers document-level AND
     * tree-internal nodes. */
    r = leptris_xpath_eval(doc, nullptr, "count(//comment())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 3.0);
    leptris_xpath_result_free(r);

    r = leptris_xpath_eval(doc, nullptr, "count(//processing-instruction())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 3.0);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(DocumentChildren, SerializeKeepsOrderAfterQuery) {
    LeptrisDocument doc =
        leptris_parse_string(kDocLevel, std::strlen(kDocLevel), nullptr);
    ASSERT_NE(doc, nullptr);

    /* Force any lazy structure, then round-trip. */
    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "count(//comment())");
    ASSERT_NE(r, nullptr);
    leptris_xpath_result_free(r);

    char* s = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(s, nullptr);
    std::string out(s);
    EXPECT_NE(out.find("<!-- pro --><?pp d?><r>"), std::string::npos)
        << "prolog order must be preserved: " << out;
    EXPECT_NE(out.find("</r><!-- epi --><?ep?>"), std::string::npos)
        << "epilog order must be preserved: " << out;
    leptris_free_string(s);
    leptris_document_free(doc);
}

TEST(DocumentChildren, AddPiIsVisibleInChainAndXpath) {
    LeptrisDocument doc =
        leptris_parse_string("<r/>", 4, nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef added = leptris_document_add_pi(doc, "extra", "x");
    ASSERT_NE(added, nullptr);

    LeptrisXPathResult r =
        leptris_xpath_eval(doc, nullptr, "count(/processing-instruction())");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    EXPECT_EQ(leptris_document_pi_count(doc), 1u);
    EXPECT_STREQ(leptris_document_pi_target(doc, 0), "extra");
    leptris_document_free(doc);
}

}  // namespace
