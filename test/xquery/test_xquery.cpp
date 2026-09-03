/* test_xquery.cpp — XQuery 1.0 core (TODO.xslt-full/11, #684-A).
 *
 * Ground truth: Saxon-HE 12.7 net.sf.saxon.Query probes
 * (/tmp/probe9/xq/{t1,t2,t3}.xq). The engine reuses the XPath
 * evaluator (SSOT): FLWOR results are the synthetic-text sequence
 * the XPath 2.0+ forms established. */
#include "leptris.h"
#include "leptris/xquery/xquery.h"
#include <gtest/gtest.h>
#include <string.h>

namespace {

const char* kBooks =
    "<books><book price='12'><title>AA</title></book>"
    "<book price='5'><title>BB</title></book>"
    "<book price='20'><title>CC</title></book></books>";

std::string seq_string(LeptrisDocument doc, const char* query) {
    LeptrisXQuery xq = leptris_xquery_parse(query, strlen(query));
    if (!xq) return "(parse-failed)";
    LeptrisXPathResult r = leptris_xquery_eval(xq, doc, NULL);
    if (!r) { leptris_xquery_free(xq); return "(eval-failed)"; }
    std::string out;
    if (leptris_xpath_result_type(r) == LEPTRIS_XPATH_NODESET) {
        /* FLWOR sequences join space-separated (value-of rule). */
        size_t n = leptris_xpath_result_count(r);
        for (size_t i = 0; i < n; i++) {
            const char* v = leptris_xpath_result_node_value(r, i);
            if (i) out += ' ';
            out += v ? v : "";
        }
    } else {
        char* s = leptris_xpath_result_string(r);
        out = s ? s : "";
        leptris_free_string(s);
    }
    leptris_xpath_result_free(r);
    leptris_xquery_free(xq);
    return out;
}

}  // namespace

TEST(XQueryCore, PlainExpressionIsXPath) {
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc, "1 + 2"), "3");
    EXPECT_EQ(seq_string(doc, "count(//book)"), "3");
    leptris_document_free(doc);
}

TEST(XQueryCore, PrologVariableAndFlwor) {
    /* Saxon: "Hello 4 Hello 9" */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "declare variable $greeting := \"Hello\";\n"
        "for $i in (1, 2, 3)\n"
        "let $square := $i * $i\n"
        "where $i > 1\n"
        "return concat($greeting, \" \", $square)"),
        "Hello 4 Hello 9");
    leptris_document_free(doc);
}

TEST(XQueryCore, OrderByDescending) {
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $n in 1 to 3 "
        "order by $n descending "
        "return $n * 2"),
        "6 4 2");
    leptris_document_free(doc);
}

TEST(XQueryCore, NodeTuplesWithWhere) {
    /* Saxon: "AA CC" — tuple variables carry nodes; paths in the
     * return clause navigate from them. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $b in //book "
        "where $b/@price > 10 "
        "return string($b/title)"),
        "AA CC");
    leptris_document_free(doc);
}

TEST(XQueryCore, DeclareFunctionLocal) {
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "declare function local:twice($x) { $x * 2 };\n"
        "for $i in 1 to 2 return local:twice($i)"),
        "2 4");
    leptris_document_free(doc);
}

TEST(XQueryCore, DeclareNamespaceAndStableOrderBy) {
    /* Multiple keys, mixed direction; stability on ties. */
    LeptrisDocument doc = leptris_parse_string(
        "<r><i g='1' v='b'>1</i><i g='2' v='a'>2</i>"
        "<i g='1' v='a'>3</i></r>", strlen("<r><i g='1' v='b'>1</i>"
        "<i g='2' v='a'>2</i><i g='1' v='a'>3</i></r>"), nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "declare namespace p = \"http://example.com/p\";\n"
        "declare variable $p:weight := 0;\n"
        "for $i in //i "
        "order by $i/@g, $i/@v descending "
        "return $i + $p:weight"),
        "1 3 2");
    leptris_document_free(doc);
}

TEST(XQueryCore, RejectsUnsupportedProlog) {
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "import module namespace m = 'http://m'; 1"),
        "(parse-failed)");
    leptris_document_free(doc);
}

TEST(XQueryCore, NullArgumentsRejected) {
    EXPECT_EQ(leptris_xquery_parse(NULL, 0), nullptr);
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXQuery xq = leptris_xquery_parse("1", 1);
    ASSERT_NE(xq, nullptr);
    EXPECT_EQ(leptris_xquery_eval(NULL, doc, NULL), nullptr);
    EXPECT_EQ(leptris_xquery_eval(xq, NULL, NULL), nullptr);
    leptris_xquery_free(xq);
    leptris_document_free(doc);
}
