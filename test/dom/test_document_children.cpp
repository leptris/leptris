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

/* Issue #612: parse-created doc-level PIs carry document linkage
 * (setters work); leptris_document_remove_pi unlinks by target or
 * index; set_root splices the new root into the chain. */
TEST(DocumentChildren, ParseCreatedPiHasDocumentLinkage) {
    const char xml[] = "<?pi x?><root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef n = leptris_node_first_child(leptris_document_node(doc));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), LEPTRIS_NODE_TYPE_PI);
    EXPECT_EQ(leptris_pi_node_set_target(n, "t"), LEPTRIS_OK);
    EXPECT_EQ(leptris_pi_node_set_data(n, "d"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_pi_node_get_target(n), "t");
    leptris_document_free(doc);
}

TEST(DocumentChildren, RemovePiByTargetAndIndex) {
    const char xml[] = "<?a x?><?b y?><r/><?c z?><?a w?>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_pi_count(doc), 4u);

    LeptrisNodeRef gone = leptris_document_remove_pi(doc, "b", 0);
    ASSERT_NE(gone, nullptr);
    EXPECT_EQ(leptris_document_pi_count(doc), 3u);
    /* Chain order preserved: a (head), root, c, a. */
    LeptrisNodeRef c = leptris_node_first_child(leptris_document_node(doc));
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_PI);

    /* index path targets the SECOND PI now ("c" after b removed? no —
     * remaining PIs: a, c, a → index 1 = c). */
    gone = leptris_document_remove_pi(doc, nullptr, 1);
    ASSERT_NE(gone, nullptr);
    EXPECT_STREQ(leptris_pi_node_get_target(gone), "c");
    EXPECT_EQ(leptris_document_pi_count(doc), 2u);
    leptris_document_free(doc);
}

TEST(DocumentChildren, SetRootSplicesChain) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement built = leptris_element_create(doc, "built");
    ASSERT_NE(built, nullptr);
    ASSERT_EQ(leptris_document_set_root(doc, built), LEPTRIS_OK);
    LeptrisNodeRef n = leptris_node_first_child(leptris_document_node(doc));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), LEPTRIS_NODE_TYPE_ELEMENT);
    EXPECT_EQ((LeptrisElement)n, built);

    /* Replace: new root takes the old slot between prolog/epilog. */
    leptris_document_add_pi(doc, "pre", "v");
    LeptrisElement second = leptris_element_create(doc, "second");
    ASSERT_EQ(leptris_document_set_root(doc, second), LEPTRIS_OK);
    n = leptris_node_first_child(leptris_document_node(doc));
    EXPECT_EQ(leptris_node_get_type(n), LEPTRIS_NODE_TYPE_PI);  /* prolog */
    n = leptris_node_next_sibling(n);
    ASSERT_EQ((LeptrisElement)n, second);   /* new root at old slot */
    leptris_document_free(doc);
}

/* #1032: document-level comments parse and serialize (#578) but had
 * no writer — the add_pi twin appends at the epilog. */
TEST(DocumentChildren, AddCommentAppendsEpilogAndRoundTrips) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    ASSERT_EQ(leptris_document_comment_count(doc), 0u);

    LeptrisNodeRef n = leptris_document_add_comment(doc, " tail ");
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), LEPTRIS_NODE_TYPE_COMMENT);
    EXPECT_EQ(leptris_document_comment_count(doc), 1u);
    EXPECT_STREQ(leptris_document_comment_content(doc, 0), " tail ");

    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<root/><!-- tail -->") << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(DocumentChildren, AddCommentOnRootlessDocument) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisNodeRef n = leptris_document_add_comment(doc, "note");
    ASSERT_NE(n, nullptr);
    LeptrisNodeRef first = leptris_node_first_child(
        leptris_document_node(doc));
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, n);
    EXPECT_EQ(leptris_document_comment_count(doc), 1u);

    LeptrisElement root = leptris_element_create(doc, "root");
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    /* The comment was first in the chain — it stays in the PROLOG
     * slot ahead of the root after the splice. */
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<!--note--><root/>") << out;
    leptris_free_string(out);
    leptris_document_free(doc);
}


TEST(DocumentChildren, PublicFirstChildWalksTheChain) {
    /* #580 follow-up: the public read path for the document
     * children chain (leptris_document_first_child), needed by
     * hosts to see prolog/epilog nodes without internals. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<!--note--><root/><!--after-->";
    LeptrisDocument doc =
        leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisNodeRef c = leptris_document_first_child(doc);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_COMMENT);
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_ELEMENT);
    EXPECT_STREQ(leptris_element_name((LeptrisElement)c), "root");
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_node_get_type(c), LEPTRIS_NODE_TYPE_COMMENT);
    EXPECT_EQ(leptris_node_next_sibling(c), nullptr);
    EXPECT_EQ(leptris_document_first_child(nullptr), nullptr);
    leptris_document_free(doc);
}

/* leptris-ruby #371: set_root on a foreign element adopts it. Pool
 * ownership makes a MOVE impossible (the subtree's memory belongs to
 * the source document's arena and would dangle after its free), so
 * adoption is a deep copy into this document's pool — with the
 * Nokogiri-steal observable: the source document loses its root. */
TEST(DocumentChildren, SetRootAdoptsForeignRootByCopy) {
    LeptrisStatus st = LEPTRIS_OK;
    const char old_xml[] = "<old><a/></old>";
    LeptrisDocument doc =
        leptris_parse_string(old_xml, std::strlen(old_xml), &st);
    ASSERT_NE(doc, nullptr);
    const char new_xml[] =
        "<new xmlns:n='urn:n'><b n:k='v'><c/>t</b></new>";
    LeptrisDocument other =
        leptris_parse_string(new_xml, std::strlen(new_xml), &st);
    ASSERT_NE(other, nullptr);
    LeptrisElement foreign = leptris_document_root(other);
    ASSERT_NE(foreign, nullptr);

    /* The engine hands back the INSTALLED element — a fresh handle
     * in doc's pool, not the borrowed source pointer. */
    LeptrisElement installed = nullptr;
    ASSERT_EQ(leptris_document_set_root_ex(doc, foreign, &installed),
              LEPTRIS_OK);
    ASSERT_NE(installed, nullptr);
    EXPECT_NE(installed, foreign);
    EXPECT_STREQ(leptris_element_name(installed), "new");

    /* Content survives the adoption: attribute, child, text. */
    LeptrisElement b = leptris_element_child(installed, 0);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_name(b), "b");
    EXPECT_STREQ(leptris_element_attribute(b, "n:k"), "v");
    char* ser = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(ser, nullptr);
    EXPECT_STRNE(ser, "");
    EXPECT_NE(strstr(ser, "<b n:k=\"v\">"), nullptr) << ser;
    leptris_free_string(ser);

    /* The source document lost its root (steal observability); a
     * rootless parsed document serializes to NULL by contract. */
    EXPECT_EQ(leptris_document_root(other), nullptr);
    EXPECT_EQ(leptris_document_serialize(other, nullptr), nullptr);

    /* The adopted tree outlives the source document — the copy is
     * pool-owned by doc, not other. */
    leptris_document_free(other);
    EXPECT_STREQ(leptris_element_name(installed), "new");
    ser = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(ser, nullptr);
    EXPECT_NE(strstr(ser, "<b n:k=\"v\">"), nullptr) << ser;
    leptris_free_string(ser);
    leptris_document_free(doc);
}

TEST(DocumentChildren, SetRootForeignRootLegacyEntryAdopts) {
    /* The original entry keeps its signature; foreign roots adopt
     * instead of EINVAL (leptris-ruby #371's exact repro). */
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string("<old/>", 6, &st);
    LeptrisDocument other =
        leptris_parse_string("<new><b/></new>", 15, &st);
    ASSERT_EQ(leptris_document_set_root(doc, leptris_document_root(other)),
              LEPTRIS_OK);
    EXPECT_EQ(leptris_document_root(doc) != nullptr, true);
    EXPECT_EQ(leptris_document_root(other), nullptr);
    leptris_document_free(other);
    EXPECT_STREQ(leptris_element_name(leptris_document_root(doc)), "new");
    leptris_document_free(doc);
}

TEST(DocumentChildren, SetRootAdoptsDetachedForeignElement) {
    /* An element created in another document but never attached:
     * adoption copies it; nothing to steal from the source. */
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisDocument other = leptris_document_create();
    ASSERT_NE(other, nullptr);
    LeptrisElement e = leptris_element_create(other, "detached");
    ASSERT_NE(e, nullptr);

    ASSERT_EQ(leptris_document_set_root(doc, e), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_name(leptris_document_root(doc)),
                 "detached");
    EXPECT_EQ(leptris_document_root(other), nullptr);
    leptris_document_free(other);
    EXPECT_STREQ(leptris_element_name(leptris_document_root(doc)),
                 "detached");
    leptris_document_free(doc);
}
