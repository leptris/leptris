// leptris#1904: LEPTRIS_PARSE_KEEP_ENTITY_REFS yields entity
// references as FIRST-CLASS nodes — segments split around the five
// built-ins and custom named references, numeric character
// references folded into text (they are character references, not
// entity references, per XML 1.0 §4.1). The DOM the binding
// consumes: text / entity-ref / text / ... with string-value,
// serialization, and XPath walking the expanded value.
#include <gtest/gtest.h>
#include <leptris.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct ChildInfo {
    int type;
    std::string text;
    std::string name;
};

std::vector<ChildInfo> children_of(LeptrisElement e) {
    std::vector<ChildInfo> rows;
    for (LeptrisNodeRef c = leptris_node_first_child(
             leptris_element_as_node(e));
         c; c = leptris_node_next_sibling(c)) {
        ChildInfo row;
        row.type = (int)leptris_node_get_type(c);
        if (row.type == LEPTRIS_NODE_TYPE_TEXT)
            row.text = leptris_text_node_get_content(c);
        if (row.type == LEPTRIS_NODE_TYPE_ENTITY_REF)
            row.name = leptris_entity_ref_node_name(c);
        rows.push_back(row);
    }
    return rows;
}

}  // namespace

TEST(EntityRefNodes, BuiltinsBecomeNodes) {
    const char* xml = "<a>x&amp;y&lt;z&gt;w</a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, &st);
    ASSERT_NE(doc, (LeptrisDocument)0);
    LeptrisElement a = leptris_document_root(doc);
    std::vector<ChildInfo> kids = children_of(a);
    ASSERT_EQ(kids.size(), 7u);
    EXPECT_EQ(kids[0].type, (int)LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_EQ(kids[0].text, "x");
    EXPECT_EQ(kids[1].type, (int)LEPTRIS_NODE_TYPE_ENTITY_REF);
    EXPECT_EQ(kids[1].name, "amp");
    EXPECT_EQ(kids[2].text, "y");
    EXPECT_EQ(kids[3].name, "lt");
    EXPECT_EQ(kids[4].text, "z");
    EXPECT_EQ(kids[5].name, "gt");
    EXPECT_EQ(kids[6].text, "w");
    leptris_document_free(doc);
}

TEST(EntityRefNodes, NumericCharacterRefsStayText) {
    /* XML 1.0 §4.1: character references are NOT entity references. */
    const char* xml = "<a>&#65;&#x42;</a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, &st);
    ASSERT_NE(doc, (LeptrisDocument)0);
    LeptrisElement a = leptris_document_root(doc);
    std::vector<ChildInfo> kids = children_of(a);
    ASSERT_EQ(kids.size(), 1u);
    EXPECT_EQ(kids[0].type, (int)LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_EQ(kids[0].text, "AB");
    leptris_document_free(doc);
}

TEST(EntityRefNodes, StringValueSerializationAndXPathAgree) {
    const char* xml = "<a>x&amp;y&lt;z</a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, &st);
    ASSERT_NE(doc, (LeptrisDocument)0);
    LeptrisElement a = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_text(a), "x&y<z");
    char* ser = leptris_document_serialize(doc, NULL);
    ASSERT_NE(ser, nullptr);
    EXPECT_STREQ(ser, "<a>x&amp;y&lt;z</a>");
    leptris_free_string(ser);
    LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, "string(/a)");
    ASSERT_NE(r, nullptr);
    char* sv = leptris_xpath_result_string(r);
    EXPECT_STREQ(sv, "x&y<z");
    leptris_free_string(sv);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(EntityRefNodes, CustomNamedRefsAreNodes) {
    const char* xml =
        "<!DOCTYPE r [<!ENTITY foo \"x\">]><r>a&foo;b</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string_flags(
        xml, strlen(xml), LEPTRIS_PARSE_KEEP_ENTITY_REFS, &st);
    ASSERT_NE(doc, (LeptrisDocument)0);
    LeptrisElement r = leptris_document_root(doc);
    std::vector<ChildInfo> kids = children_of(r);
    ASSERT_EQ(kids.size(), 3u);
    EXPECT_EQ(kids[1].type, (int)LEPTRIS_NODE_TYPE_ENTITY_REF);
    EXPECT_EQ(kids[1].name, "foo");
    EXPECT_STREQ(leptris_element_text(r), "axb");
    leptris_document_free(doc);
}
