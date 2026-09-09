/* Lane 17: native XML diff — digest-pruned ordered edit script.
 * Falsifiability: op COUNTS, TYPES, PATHS, and before/after
 * payloads are asserted per fixture; diff(a,a) is empty. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <string>

namespace {

LeptrisDocument P(const char* xml) {
    LeptrisStatus st;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    return d;
}

std::string S(const char* s) { return s ? s : "(null)"; }

}  // namespace

TEST(XmlDiff, IdenticalDocumentsProduceNoOps) {
    LeptrisDocument a = P("<r><i x='1'>t</i><j/></r>");
    LeptrisDocument b = P("<r><i x='1'>t</i><j/></r>");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, &st);
    ASSERT_NE(df, nullptr);
    EXPECT_EQ(leptris_diff_op_count(df), 0u);
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, DeepIdenticalSubtreePruned) {
    /* A large identical tail must produce exactly ONE op for the
     * changed head text — the rest pruned by digest. */
    std::string xml = "<r><head>old</head><tail>";
    for (int i = 0; i < 200; i++)
        xml += "<item id='" + std::to_string(i) + "'>x" +
               std::to_string(i) + "</item>";
    xml += "</tail></r>";
    LeptrisDocument a = P(xml.c_str());
    std::string xml2 = "<r><head>new</head><tail>" +
                       xml.substr(xml.find("<tail>") + 6);
    LeptrisDocument b = P(xml2.c_str());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 1u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_UPDATE_TEXT);
    EXPECT_EQ(S(leptris_diff_op_path(df, 0)), "/r/head");
    EXPECT_EQ(S(leptris_diff_op_before(df, 0)), "old");
    EXPECT_EQ(S(leptris_diff_op_after(df, 0)), "new");
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, AttributeUpdate) {
    LeptrisDocument a = P("<r><i id='1' k='v'>.</i></r>");
    LeptrisDocument b = P("<r><i id='2' k='v'>.</i></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 1u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_UPDATE_ATTR);
    EXPECT_EQ(S(leptris_diff_op_path(df, 0)), "/r/i");
    EXPECT_EQ(S(leptris_diff_op_name(df, 0)), "id");
    EXPECT_EQ(S(leptris_diff_op_before(df, 0)), "1");
    EXPECT_EQ(S(leptris_diff_op_after(df, 0)), "2");
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, AttributeAddAndRemove) {
    LeptrisDocument a = P("<r><i old='1'/></r>");
    LeptrisDocument b = P("<r><i new='2'/></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 2u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_UPDATE_ATTR);
    EXPECT_EQ(S(leptris_diff_op_name(df, 0)), "old");
    EXPECT_EQ(S(leptris_diff_op_before(df, 0)), "1");
    EXPECT_EQ(S(leptris_diff_op_after(df, 0)), "");
    EXPECT_EQ(leptris_diff_op_type(df, 1), LEPTRIS_DIFF_UPDATE_ATTR);
    EXPECT_EQ(S(leptris_diff_op_name(df, 1)), "new");
    EXPECT_EQ(S(leptris_diff_op_before(df, 1)), "");
    EXPECT_EQ(S(leptris_diff_op_after(df, 1)), "2");
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, InsertAndDeleteChildren) {
    LeptrisDocument a = P("<r><a/><b/></r>");
    LeptrisDocument b = P("<r><a/><c/><b/></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 1u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_INSERT);
    EXPECT_EQ(S(leptris_diff_op_path(df, 0)), "/r/c[1]");
    EXPECT_EQ(S(leptris_diff_op_name(df, 0)), "c");

    LeptrisDocument c = P("<r><a/><x/><b/></r>");
    LeptrisDiff d2 = leptris_diff(b, c, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(d2, nullptr);
    ASSERT_EQ(leptris_diff_op_count(d2), 2u);
    leptris_diff_free(d2);
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
    leptris_document_free(c);
}

TEST(XmlDiff, SameNameRecursionNotDeleteInsert) {
    /* A changed same-named child must recurse (one UPDATE_TEXT),
     * not DELETE + INSERT. */
    LeptrisDocument a = P("<r><i>one</i></r>");
    LeptrisDocument b = P("<r><i>two</i></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 1u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_UPDATE_TEXT);
    EXPECT_EQ(S(leptris_diff_op_path(df, 0)), "/r/i");
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, IndexedPathDisambiguatesSameNamed) {
    LeptrisDocument a = P("<r><i>1</i><i>2</i><i>3</i></r>");
    LeptrisDocument b = P("<r><i>1</i><i>X</i><i>3</i></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    ASSERT_EQ(leptris_diff_op_count(df), 1u);
    EXPECT_EQ(S(leptris_diff_op_path(df, 0)), "/r/i[2]");
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, SerializerForm) {
    LeptrisDocument a = P("<r><i id='1'>old</i></r>");
    LeptrisDocument b = P("<r><i id='2'>new</i></r>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    char* s = leptris_diff_serialize(df);
    ASSERT_NE(s, nullptr);
    std::string text(s);
    leptris_free_string(s);
    EXPECT_NE(text.find("- /r/i @id \"1\" -> \"2\""), std::string::npos);
    EXPECT_NE(text.find("~ /r/i \"old\" -> \"new\""), std::string::npos);
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, IgnoreWsTextOption) {
    LeptrisDocument a = P("<r><i>x</i></r>");
    LeptrisDocument b = P("<r>  <i>x</i>\n</r>");
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDiff df =
        leptris_diff(a, b, LEPTRIS_DIFF_IGNORE_WS_TEXT, &st);
    ASSERT_NE(df, nullptr);
    EXPECT_EQ(leptris_diff_op_count(df), 0u);
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}

TEST(XmlDiff, NullArgsAndMismatchedRoots) {
    EXPECT_EQ(leptris_diff(nullptr, nullptr, LEPTRIS_DIFF_DEFAULT,
                           nullptr),
              nullptr);
    LeptrisDocument a = P("<r/>");
    LeptrisDocument b = P("<other/>");
    LeptrisDiff df = leptris_diff(a, b, LEPTRIS_DIFF_DEFAULT, nullptr);
    ASSERT_NE(df, nullptr);
    EXPECT_EQ(leptris_diff_op_count(df), 2u);
    EXPECT_EQ(leptris_diff_op_type(df, 0), LEPTRIS_DIFF_DELETE);
    EXPECT_EQ(leptris_diff_op_type(df, 1), LEPTRIS_DIFF_INSERT);
    leptris_diff_free(df);
    leptris_document_free(a);
    leptris_document_free(b);
}
