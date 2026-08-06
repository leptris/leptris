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
    EXPECT_STREQ(taurus_xpath_result_string(r), "hello");
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

}  // namespace
