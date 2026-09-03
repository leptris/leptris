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

TEST(XQueryCore, DocAndComputedConstructors) {
    /* Saxon t5: doc() + where + order by descending + computed
     * element/attribute/text constructors. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $b in doc(\"" LEPTRIS_XQUERY_FIXTURE_DIR
        "/books.xml\")//book "
        "where $b/@price > 10 "
        "order by $b/@price descending "
        "return element r { attribute price { $b/@price },"
        " text { $b/title } }"),
        "<r price=\"20\">CC</r> <r price=\"12\">AA</r>");
    leptris_document_free(doc);
}

TEST(XQueryCore, NestedComputedConstructors) {
    /* Saxon t6: <items><n>1</n><n>2</n><n>3</n></items> */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "element items { for $n in 1 to 3"
        " return element n { $n } }"),
        "<items><n>1</n><n>2</n><n>3</n></items>");
    leptris_document_free(doc);
}

TEST(XQueryCore, DirectConstructorWithTemplates) {
    /* Saxon t7: <out total="3"><x>AA</x></out> — attribute value
     * template + enclosed content expression. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc, "<a p=\"{1 + 1}\"/>"), "<a p=\"2\"/>");
    EXPECT_EQ(seq_string(doc,
        "<out total=\"{count(//book)}\">"
        "<x>{string(//book[1]/title)}</x></out>"),
        "<out total=\"3\"><x>AA</x></out>");
    leptris_document_free(doc);
}

TEST(XQueryCore, PositionalFor) {
    /* Saxon t8: "1:a 2:b 3:c" */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $x at $i in ('a','b','c')"
        " return concat($i, ':', $x)"),
        "1:a 2:b 3:c");
    leptris_document_free(doc);
}

TEST(XQueryCore, DocumentConstructor) {
    /* Saxon t9: <n>1</n><n>2</n> — document serializes its content. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $v in (1,2) return document { element n { $v } }"),
        "<n>1</n> <n>2</n>");
    leptris_document_free(doc);
}

TEST(XQueryCore, TryCatch) {
    /* #692's silent-wrong case: try/catch must catch dynamic errors
     * and run the matching handler (Saxon t11/t15/t16). */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    /* No error: the try body's value passes through. */
    EXPECT_EQ(seq_string(doc,
        "try { 1 + count((1,2)) } catch * { -1 }"),
        "3");
    /* Dynamic error: catch * runs; $err:description carries the
     * diagnostic. */
    EXPECT_EQ(seq_string(doc,
        "try { doc('no-such-file-xyz.xml') } catch * {"
        " 'caught:' || contains($err:description, 'doc') }"),
        "caught:true");
    /* Named tests do not match (no code model yet): the error
     * propagates as an evaluation failure. */
    EXPECT_EQ(seq_string(doc,
        "try { doc('no-such-file-xyz.xml') }"
        " catch err:XPST0008 { 'undef' }"),
        "(eval-failed)");
    leptris_document_free(doc);
}

TEST(XQueryCore, GroupBy) {
    /* Saxon g1-g3: groups in first-appearance order; the for var
     * is rebound to the group's items as a sequence. */
    LeptrisDocument doc = leptris_parse_string(
        "<r><book cat='a'/><book cat='b'/><book cat='a'/>"
        "<book cat='c'/><book cat='b'/></r>", 82, nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $b in //book group by $cat := $b/@cat"
        " return element g { attribute c { $cat },"
        " attribute n { count($b) } }"),
        "<g c=\"a\" n=\"2\"/> <g c=\"b\" n=\"2\"/> <g c=\"c\" n=\"1\"/>");
    /* group by + order by: the sort sees the grouped state. */
    EXPECT_EQ(seq_string(doc,
        "for $b in //book group by $cat := $b/@cat"
        " order by count($b) descending"
        " return $cat || ':' || count($b)"),
        "a:2 b:2 c:1");
    EXPECT_EQ(seq_string(doc,
        "for $x in ('a','b','a') group by $k := $x"
        " return concat($k, ':', count($x))"),
        "a:2 b:1");
    leptris_document_free(doc);
}

TEST(XQueryCore, Issue790GrammarGaps) {
    /* #790: constructor-as-return, FLWOR-with-where/at inside a
     * function argument, cast lexical errors reachable by catch. */
    LeptrisDocument doc = leptris_parse_string(
        "<r><item><name>a</name></item><item><name>b</name></item>"
        "<item><name>c</name></item></r>", 88, nullptr);
    ASSERT_NE(doc, nullptr);
    /* 1. Constructor as the return clause (Saxon t20). */
    EXPECT_EQ(seq_string(doc,
        "for $i in //item return <v>{$i/name/text()}</v>"),
        "<v>a</v> <v>b</v> <v>c</v>");
    /* 2. where / at inside a function-argument FLWOR (Saxon t21). */
    EXPECT_EQ(seq_string(doc,
        "count(for $i in //item where number(1) > 0 return $i)"),
        "3");
    EXPECT_EQ(seq_string(doc,
        "count(for $i at $p in //item return $p)"),
        "3");
    /* 3. cast lexical error is catchable (Saxon t22). */
    EXPECT_EQ(seq_string(doc,
        "try { 'nope' cast as xs:integer } catch * { 'caught' }"),
        "caught");
    leptris_document_free(doc);
}

TEST(XQueryCore, TumblingWindow) {
    /* Saxon w1: <w s="1" n="3"/><w s="4" n="3"/> */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for tumbling window $w in (1,2,3,4,5,6)"
        " start $s at $sp when true()"
        " end $e at $ep when $ep - $sp ge 2"
        " return <w s='{$s}' n='{count($w)}'/>"),
        "<w s=\"1\" n=\"3\"/> <w s=\"4\" n=\"3\"/>");
    leptris_document_free(doc);
}

TEST(XQueryCore, SlidingWindow) {
    /* Saxon: overlapping windows over (1,2,3,4) */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for sliding window $w in (1,2,3,4)"
        " start $s at $sp when true()"
        " end $e at $ep when $ep - $sp ge 1"
        " return string-join(for $x in $w return string($x), ',')"),
        "1,2 2,3 3,4 4");
    leptris_document_free(doc);
}

TEST(XQueryCore, Typeswitch) {
    /* Saxon ts1/ts2: per-case type dispatch with default. */
    LeptrisDocument doc = leptris_parse_string(
        "<r><item>a</item><item>b</item></r>", 35, nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "for $v in (42, 'str', //item)"
        " return typeswitch ($v)"
        " case xs:integer return 'int'"
        " case xs:string return 'string'"
        " case element() return 'element'"
        " default return 'other'"),
        "int string element element");
    EXPECT_EQ(seq_string(doc,
        "typeswitch (//item[1]) case node() return 'node'"
        " default return 'no'"),
        "node");
    leptris_document_free(doc);
}

TEST(XQueryCore, NamedCatchWithErrorCode) {
    /* Saxon c1: err:FODC0002 matches a doc() failure. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc,
        "try { doc('no-file.xml') } catch err:FODC0002 { 'FODC' }"
        " catch * { 'ANY' }"),
        "FODC");
    /* Wrong code: falls to catch *. */
    EXPECT_EQ(seq_string(doc,
        "try { doc('no-file.xml') } catch err:XPST0008 { 'WRONG' }"
        " catch * { 'ANY' }"),
        "ANY");
    leptris_document_free(doc);
}

TEST(XQueryCore, CollectionRequiresCatalog) {
    /* Saxon: no default collection defined — a dynamic error. */
    LeptrisDocument doc = leptris_parse_string(kBooks, strlen(kBooks),
                                               nullptr);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(seq_string(doc, "try { count(collection()) }"
                              " catch err:FODC0002 { 'nocoll' }"),
        "nocoll");
    leptris_document_free(doc);
}
