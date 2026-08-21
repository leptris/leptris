// test/smoke/test_smoke.cpp — Build-and-link sanity check.
//
// If this fails, the test harness itself is broken; fix before chasing
// failures in other test files.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>

namespace {

constexpr char kBasic[] = "<root><child>hello</child></root>";

TEST(Smoke, ParsesMinimalDocument) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(kBasic, std::strlen(kBasic), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "root");

    leptris_document_free(doc);
}

TEST(Smoke, RejectsNullInput) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(nullptr, 0, &st);
    EXPECT_EQ(doc, nullptr);
}

TEST(Smoke, RejectsZeroLengthInput) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string("", 0, &st);
    EXPECT_EQ(doc, nullptr);
}

}  // namespace
