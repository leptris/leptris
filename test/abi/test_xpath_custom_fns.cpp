// test/abi/test_xpath_custom_fns.cpp — Specs for the custom XPath
// function registration API (TODO 148 Phase 5).
//
// Exercises taurus_xpath_register_function. Each spec registers a
// simple string-valued handler, evaluates an XPath that invokes it,
// and asserts the result.

#include <gtest/gtest.h>
#include "taurus.h"
#include <cstring>
#include <cstdlib>
#include <string>

namespace {
TaurusDocument Parse(const char* xml) {
    TaurusStatus st = TAURUS_OK;
    return taurus_parse_string(xml, std::strlen(xml), &st);
}

/* Echo handler: concatenates all args. */
char* echo_fn(const char* const* args, int argc, void* user_data) {
    size_t total = 1;
    for (int i = 0; i < argc; i++) {
        total += std::strlen(args[i]);
    }
    char* buf = (char*)malloc(total);
    if (!buf) return nullptr;
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        std::strcat(buf, args[i]);
    }
    (void)user_data;
    return buf;
}

/* Constant handler — ignores args, returns user_data as the string. */
char* constant_fn(const char* const* args, int argc, void* user_data) {
    (void)args; (void)argc;
    const char* s = (const char*)user_data;
    return s ? strdup(s) : strdup("");
}
}  // namespace

TEST(CustomXPath, NullArgsReturnError) {
    TaurusDocument doc = Parse("<r/>");
    EXPECT_EQ(taurus_xpath_register_function(nullptr, "f", echo_fn, nullptr),
              TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_xpath_register_function(doc, nullptr, echo_fn, nullptr),
              TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_xpath_register_function(doc, "f", nullptr, nullptr),
              TAURUS_ERROR_NULL_ARG);
    taurus_document_free(doc);
}

TEST(CustomXPath, RegisteredFunctionInvocable) {
    TaurusDocument doc = Parse("<root><a>hello</a><b>world</b></root>");
    EXPECT_EQ(taurus_xpath_register_function(doc, "concat-text", echo_fn, nullptr),
              TAURUS_OK);
    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "concat-text(//a, //b)");
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(taurus_xpath_result_string(r), "helloworld");
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(CustomXPath, UserDataPassedThrough) {
    TaurusDocument doc = Parse("<r/>");
    EXPECT_EQ(taurus_xpath_register_function(doc, "literal", constant_fn,
                                              (void*)"the-answer"),
              TAURUS_OK);
    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "literal()");
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(taurus_xpath_result_string(r), "the-answer");
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(CustomXPath, StandardFunctionsStillWork) {
    TaurusDocument doc = Parse("<root><a/><b/><c/></root>");
    EXPECT_EQ(taurus_xpath_register_function(doc, "f", echo_fn, nullptr),
              TAURUS_OK);
    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 4.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}

TEST(CustomXPath, StandardWinsNameCollision) {
    TaurusDocument doc = Parse("<root><a/><b/></root>");
    /* Register a handler named "count" — should be shadowed by
     * the standard library's count, which is added first by
     * the registry builder. */
    EXPECT_EQ(taurus_xpath_register_function(doc, "count", echo_fn, nullptr),
              TAURUS_OK);
    TaurusElement root = taurus_document_root(doc);
    TaurusXPathResult r = taurus_xpath_eval(doc, root, "count(//*)");
    ASSERT_NE(r, nullptr);
    /* Standard count returns 3 (root + a + b), not a string. */
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 3.0);
    taurus_xpath_result_free(r);
    taurus_document_free(doc);
}
