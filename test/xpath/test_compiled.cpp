/* TODO.bindings/03 — compiled XPath expression specs. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/error.h"
}
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static const char* kDoc =
    "<library><book n='1'><title>A</title></book>"
    "<book n='4'><title>B</title></book>"
    "<book n='9'><title>C</title></book></library>";

TEST(CompiledXPath, MatchesPlainEval) {
    LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//book[@n > 3])");
    ASSERT_NE(c, nullptr);
    for (int i = 0; i < 3; i++) {
        LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
        leptris_xpath_result_free(r);
    }
    leptris_xpath_compiled_free(c);
    leptris_document_free(doc);
}

TEST(CompiledXPath, ReusedAcrossDocuments) {
    LeptrisXPathCompiled c = leptris_xpath_compile("string(//title)");
    ASSERT_NE(c, nullptr);
    for (int i = 0; i < 2; i++) {
        LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
        ASSERT_NE(doc, nullptr);
        LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
        ASSERT_NE(r, nullptr);
        char* s = leptris_xpath_result_string(r);
        EXPECT_STREQ(s, "A");
        leptris_free_string(s);
        leptris_xpath_result_free(r);
        leptris_document_free(doc);
    }
    leptris_xpath_compiled_free(c);
}

TEST(CompiledXPath, SyntaxErrorReportsMessage) {
    EXPECT_EQ(leptris_xpath_compile(nullptr), nullptr);
    EXPECT_EQ(leptris_xpath_compile(""), nullptr);
    EXPECT_EQ(leptris_xpath_compile("count(///bad[)"), nullptr);
    EXPECT_NE(leptris_last_error(), nullptr);
}

TEST(CompiledXPath, FailureSnapshotsIntoDocSlot) {
    LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathCompiled c = leptris_xpath_compile("1 div 0");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
    if (r) leptris_xpath_result_free(r);   /* may legitimately succeed */
    leptris_xpath_compiled_free(c);
    leptris_document_free(doc);
}

TEST(CompiledXPath, ConcurrentEvalOneHandle) {
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//book[@n > 3])");
    ASSERT_NE(c, nullptr);
    std::vector<int> failures(4, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&failures, c, t]() {
            for (int i = 0; i < 25; i++) {
                LeptrisDocument doc =
                    leptris_parse_string(kDoc, strlen(kDoc), nullptr);
                if (!doc) { failures[t]++; continue; }
                LeptrisXPathResult r =
                    leptris_xpath_compiled_eval(c, doc, nullptr);
                if (!r || leptris_xpath_result_number(r) != 2.0) failures[t]++;
                if (r) leptris_xpath_result_free(r);
                leptris_document_free(doc);
            }
            /* Worker-exit cache drain (TODO.concurrency/08). */
            leptris_thread_cleanup();
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < 4; t++) EXPECT_EQ(failures[t], 0) << "thread " << t;
    leptris_xpath_compiled_free(c);
}
