// test/html/test_html_borrowed_names.cpp — #1285 slice 5: zero-copy
// element names in the HTML builder (the in-place lowercasing carve).
//
// Tag names lowercase IN PLACE in the doc-owned input copy with a
// NUL written over the name's delimiter; elements borrow the pointer
// (no pool copy, no create_with_view copy). The delimiter byte's
// consumers — the attribute scanner and h_input_type_hidden — skip
// the NUL with the delimiter's original semantics preserved (a '>'
// delimiter ended the tag; ws/'/' were skippable). Attribute names
// still pool-copy: the #635 raw-attr views keep source case.
//
// Falsifiable: case folding, QName-literal names, attributes after
// every delimiter shape, hidden-input detection through the NUL, and
// no phantom attributes from content after a NUL-terminated '>'.

#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/html.h"
}
#include <cstring>
#include <string>

namespace {

std::string FirstElemName(const char* html) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    if (!d) return "(parse-failed)";
    /* HTML docs get the html/body wrapper — query the FIRST element
     * below the wrapper instead of the document root. */
    LeptrisXPathResult xr =
        leptris_xpath_eval(d, nullptr, "name(/*/*/*[1])");
    std::string r = "(none)";
    if (leptris_xpath_result_type(xr) == LEPTRIS_XPATH_STRING) {
        char* s = leptris_xpath_result_string(xr);
        if (s) {
            r = s;
            leptris_free_string(s);
        }
    }
    leptris_xpath_result_free(xr);
    leptris_document_free(d);
    return r;
}

}  // namespace

TEST(HtmlBorrowedNames, UpperCaseTagFoldsLower) {
    EXPECT_EQ(FirstElemName("<DIV>t</DIV>"), "div");
    EXPECT_EQ(FirstElemName("<MiXeD/>"), "mixed");
}

TEST(HtmlBorrowedNames, QNameStaysLiteralWholeToken) {
    /* tests14 semantics: HTML keeps qualified names literally — the
     * whole lowercased token is the name, no prefix split. */
    EXPECT_EQ(FirstElemName("<xyz:ABC>t</xyz:ABC>"), "xyz:abc");
}

TEST(HtmlBorrowedNames, AttributesAfterWsDelimiter) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* html = "<DIV CLASS=\"X\" id='q'>t</DIV>";
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult xr = leptris_xpath_eval(d, nullptr, "//div");
    ASSERT_EQ(leptris_xpath_result_type(xr), LEPTRIS_XPATH_NODESET);
    ASSERT_EQ(leptris_xpath_result_count(xr), (size_t)1);
    LeptrisElement div = leptris_xpath_result_get(xr, 0);
    ASSERT_NE(div, nullptr);
    EXPECT_STREQ(leptris_element_name(div), "div");
    EXPECT_STREQ(leptris_element_attribute(div, "class"), "X");
    EXPECT_STREQ(leptris_element_attribute(div, "id"), "q");
    leptris_xpath_result_free(xr);
    leptris_document_free(d);
}

TEST(HtmlBorrowedNames, AttributesAfterBareSlash) {
    /* '<a/ href=...>': the bare '/' after the name is ignored. */
    const char* html = "<a/ href='x'>t</a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_NE(d, nullptr);
    LeptrisXPathResult xr = leptris_xpath_eval(d, nullptr, "//a");
    ASSERT_EQ(leptris_xpath_result_type(xr), LEPTRIS_XPATH_NODESET);
    ASSERT_EQ(leptris_xpath_result_count(xr), (size_t)1);
    LeptrisElement a = leptris_xpath_result_get(xr, 0);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(leptris_element_attribute(a, "href"), "x");
    leptris_xpath_result_free(xr);
    leptris_document_free(d);
}

TEST(HtmlBorrowedNames, NameThenGtDoesNotEatContentAsAttrs) {
    /* '<br>' — the NUL lands on the '>'; following content must not
     * become phantom attributes or text loss. */
    const char* html = "<div><br>x<a href='y'>z</a></div>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_NE(d, nullptr);
    /* Query the tree rather than walk the child API surface. */
    LeptrisXPathResult xr = leptris_xpath_eval(d, nullptr, "//a/@href");
    ASSERT_EQ(leptris_xpath_result_type(xr), LEPTRIS_XPATH_NODESET);
    EXPECT_EQ(leptris_xpath_result_count(xr), (size_t)1);
    leptris_xpath_result_free(xr);
    xr = leptris_xpath_eval(d, nullptr, "//br");
    EXPECT_EQ(leptris_xpath_result_count(xr), (size_t)1);
    leptris_xpath_result_free(xr);
    leptris_document_free(d);
}

TEST(HtmlBorrowedNames, HiddenInputDetectedThroughNul) {
    /* webkit01:51 — input type=hidden must not clear frameset-ok;
     * the type scan runs over the NUL'd delimiter. Unquoted value. */
    const char* html = "<input type=hidden><frameset></frameset>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(html, strlen(html), &st);
    ASSERT_NE(d, nullptr);
    /* hidden input does NOT clear frameset-ok: the frameset lands */
    LeptrisXPathResult xr = leptris_xpath_eval(d, nullptr, "//frameset");
    ASSERT_EQ(leptris_xpath_result_type(xr), LEPTRIS_XPATH_NODESET);
    EXPECT_EQ(leptris_xpath_result_count(xr), (size_t)1);
    leptris_xpath_result_free(xr);
    leptris_document_free(d);

    /* ...and a VISIBLE input clears it: no frameset */
    const char* html2 = "<input type=text><frameset></frameset>";
    LeptrisDocument d2 = leptris_parse_html_string(html2, strlen(html2), &st);
    ASSERT_NE(d2, nullptr);
    LeptrisXPathResult xr2 = leptris_xpath_eval(d2, nullptr, "//frameset");
    EXPECT_EQ(leptris_xpath_result_count(xr2), (size_t)0);
    leptris_xpath_result_free(xr2);
    leptris_document_free(d2);
}

TEST(HtmlBorrowedNames, ImageRenamedImg) {
    EXPECT_EQ(FirstElemName("<image src='x'>"), "img");
}

TEST(HtmlBorrowedNames, LongCustomTagName) {
    std::string n(200, 'Q');
    std::string html = "<" + n + ">t</" + n + ">";
    EXPECT_EQ(FirstElemName(html.c_str()), std::string(200, 'q'));
}
