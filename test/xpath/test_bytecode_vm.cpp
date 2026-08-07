// test/xpath/test_bytecode_vm.cpp — TODO 120 Phase A: bytecode + VM specs.
//
// The bytecode compiler + VM are linked into libtaurus but not yet
// wired into taurus_xpath_eval (AST evaluator is still the default).
// These specs verify the new files don't break existing XPath paths
// and that simple literal queries still work end-to-end via the
// public API.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include "taurus.h"

namespace {

TEST(XPathBytecode, NumberLiteralRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "1.5");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.5);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecode, StringLiteralRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "'hello'");
    ASSERT_NE(r, nullptr);
    char* s = taurus_xpath_result_string(r);
    EXPECT_STREQ(s, "hello");
    taurus_free_string(s);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecode, PathQueryStillWorks) {
    const char xml[] = "<root><a>1</a><a>2</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 2.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

// Specialized axis opcodes (TODO 126). Each must produce results
// identical to the AST-evaluator path. The compiler emits the
// specialized opcodes only when the step shape matches: no
// namespace prefix, no predicate, single name test or wildcard.

TEST(XPathBytecodeSpecializedAxes, ChildNameMatches) {
    const char xml[] =
        "<root><a>1</a><b>2</b><a>3</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `child::a` and bare `a` both lower to BC_AXIS_CHILD_NAME. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(//a)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 2.0);
    taurus_xpath_result_free(r1);

    /* Wildcard path uses BC_AXIS_CHILD_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 4.0);  /* root + 3 children */
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, AttributeNameMatches) {
    const char xml[] =
        "<root a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* `attribute::a` and `@a` both lower to BC_AXIS_ATTRIBUTE_NAME. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr, "count(/root/@a)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* Attribute wildcard — BC_AXIS_ATTRIBUTE_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(/root/@*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 3.0);
    taurus_xpath_result_free(r2);

    /* Attribute value via string(). */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr, "string(/root/@b)");
    ASSERT_NE(r3, nullptr);
    char* s = taurus_xpath_result_string(r3);
    EXPECT_STREQ(s, "2");
    taurus_free_string(s);
    taurus_xpath_result_free(r3);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, SelfAndParentAxes) {
    const char xml[] =
        "<root><child><leaf/></child></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* self::* lowers to BC_AXIS_SELF_WILD. */
    TaurusXPathResult r1 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::*)");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r1), 1.0);
    taurus_xpath_result_free(r1);

    /* parent::* lowers to BC_AXIS_PARENT_WILD. */
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr,
                                                "count(//leaf/parent::*)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    /* Named self filters. */
    TaurusXPathResult r3 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::root)");
    ASSERT_NE(r3, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r3), 1.0);
    taurus_xpath_result_free(r3);

    TaurusXPathResult r4 = taurus_xpath_eval(doc, nullptr,
                                                "count(/root/self::other)");
    ASSERT_NE(r4, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r4), 0.0);
    taurus_xpath_result_free(r4);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, PredicateFallsBackToAxisStep) {
    /* Predicated steps stay on BC_AXIS_STEP — the inline handler
     * doesn't support predicates. Ensures the compiler's fast-path
     * gating doesn't accidentally inline a predicated step. */
    const char xml[] =
        "<root><a id='1'>one</a><a id='2'>two</a></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//a[@id='2'])");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(XPathBytecodeSpecializedAxes, NamespacePrefixFallsBack) {
    /* Steps with namespace prefix (`ns:name`) cannot use the inline
     * handler — they need xpath_context_resolve_prefix. The
     * compiler must fall back to BC_AXIS_STEP. */
    const char xml[] =
        "<root xmlns:ns='http://example.com'><ns:child/></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr,
                                               "count(//ns:child)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 1.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

}  // namespace
