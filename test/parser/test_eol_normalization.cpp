/* XML 1.0 §2.11 end-of-line handling (leptris-ruby#326): literal
 * CRLF and lone CR in parsed character data normalize to LF before
 * reference expansion; attribute-value normalization collapses CR+LF
 * to ONE space (§3.3.3 order: #xD #xA -> #xA, then #xA -> #x20).
 * Character references expand AFTER normalization, so &#xD; stays CR
 * (spec-mandated; libxml2 parity). CDATA is NOT normalized (§2.7:
 * CDATA sections are character data only in the unescaped sense —
 * their line endings are literal). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <string>

namespace {

char* eval_string(LeptrisDocument doc, const char* expr) {
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, expr);
    EXPECT_TRUE(r != nullptr);
    char* s = leptris_xpath_result_string(r);
    leptris_xpath_result_free(r);
    return s;
}

TEST(XmlEolNormalization, TextCrlfAndCrNormalizeToLf) {
    LeptrisStatus st;
    const char* xml = "<a>x\r\ny\rz\nw</a>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_TRUE(d != nullptr);
    char* s = eval_string(d, "//a/text()");
    ASSERT_TRUE(s != nullptr);
    EXPECT_STREQ(s, "x\ny\nz\nw");
    leptris_free_string(s);
    leptris_document_free(d);
}

TEST(XmlEolNormalization, CrCharRefSurvivesNormalization) {
    LeptrisStatus st;
    /* &#xD; expands AFTER §2.11 normalization — the CR is data. */
    const char* xml = "<a>x&#xD;y</a>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_TRUE(d != nullptr);
    char* s = eval_string(d, "//a/text()");
    ASSERT_TRUE(s != nullptr);
    EXPECT_STREQ(s, "x\ry");
    leptris_free_string(s);
    leptris_document_free(d);
}

TEST(XmlEolNormalization, AttributeAvnCollapsesCrlfToOneSpace) {
    LeptrisStatus st;
    const char* xml = "<a att=\"p\r\nq\rs\tt\">u</a>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_TRUE(d != nullptr);
    char* s = eval_string(d, "//a/@att");
    ASSERT_TRUE(s != nullptr);
    EXPECT_STREQ(s, "p q s t");
    leptris_free_string(s);
    leptris_document_free(d);
}

TEST(XmlEolNormalization, CdataIsNotNormalized) {
    LeptrisStatus st;
    const char* xml = "<a><![CDATA[x\r\ny\rz]]></a>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_TRUE(d != nullptr);
    char* s = eval_string(d, "//a/text()");
    ASSERT_TRUE(s != nullptr);
    EXPECT_STREQ(s, "x\r\ny\rz");
    leptris_free_string(s);
    leptris_document_free(d);
}

TEST(XmlEolNormalization, EntityBearingRunsNormalizeLiteralCrOnly) {
    LeptrisStatus st;
    /* Literal CRLF normalizes; the &amp; still decodes. */
    const char* xml = "<a>a&amp;\r\nb</a>";
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_TRUE(d != nullptr);
    char* s = eval_string(d, "//a/text()");
    ASSERT_TRUE(s != nullptr);
    EXPECT_STREQ(s, "a&\nb");
    leptris_free_string(s);
    leptris_document_free(d);
}

}  // namespace
