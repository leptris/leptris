// test/abi/test_xpath_custom_fns.cpp — Specs for the custom XPath
// function registration API (TODO 148 Phase 5).
//
// Exercises leptris_xpath_register_function. Each spec registers a
// simple string-valued handler, evaluates an XPath that invokes it,
// and asserts the result.

#include <gtest/gtest.h>
#include "leptris.h"
#include <cstring>
#include <cstdlib>
#include <string>

namespace {
LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
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
    LeptrisDocument doc = Parse("<r/>");
    EXPECT_EQ(leptris_xpath_register_function(nullptr, "f", echo_fn, nullptr),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_xpath_register_function(doc, nullptr, echo_fn, nullptr),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_xpath_register_function(doc, "f", nullptr, nullptr),
              LEPTRIS_ERROR_NULL_ARG);
    leptris_document_free(doc);
}

TEST(CustomXPath, RegisteredFunctionInvocable) {
    LeptrisDocument doc = Parse("<root><a>hello</a><b>world</b></root>");
    EXPECT_EQ(leptris_xpath_register_function(doc, "concat-text", echo_fn, nullptr),
              LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisXPathResult r = leptris_xpath_eval(doc, root, "concat-text(//a, //b)");
    ASSERT_NE(r, nullptr);
    { char* s = leptris_xpath_result_string(r); EXPECT_STREQ(s, "helloworld"); free(s); }
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(CustomXPath, UserDataPassedThrough) {
    LeptrisDocument doc = Parse("<r/>");
    EXPECT_EQ(leptris_xpath_register_function(doc, "literal", constant_fn,
                                              (void*)"the-answer"),
              LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisXPathResult r = leptris_xpath_eval(doc, root, "literal()");
    ASSERT_NE(r, nullptr);
    { char* s = leptris_xpath_result_string(r); EXPECT_STREQ(s, "the-answer"); free(s); }
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(CustomXPath, StandardFunctionsStillWork) {
    LeptrisDocument doc = Parse("<root><a/><b/><c/></root>");
    EXPECT_EQ(leptris_xpath_register_function(doc, "f", echo_fn, nullptr),
              LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisXPathResult r = leptris_xpath_eval(doc, root, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 4.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}

TEST(CustomXPath, StandardWinsNameCollision) {
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    /* Register a handler named "count" — should be shadowed by
     * the standard library's count, which is added first by
     * the registry builder. */
    EXPECT_EQ(leptris_xpath_register_function(doc, "count", echo_fn, nullptr),
              LEPTRIS_OK);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisXPathResult r = leptris_xpath_eval(doc, root, "count(//*)");
    ASSERT_NE(r, nullptr);
    /* Standard count returns 3 (root + a + b), not a string. */
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 3.0);
    leptris_xpath_result_free(r);
    leptris_document_free(doc);
}
