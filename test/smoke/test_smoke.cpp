// test/smoke/test_smoke.cpp — Build-and-link sanity check.
//
// If this fails, the test harness itself is broken; fix before chasing
// failures in other test files.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

constexpr char kBasic[] = "<root><child>hello</child></root>";

TEST(Smoke, ParsesMinimalDocument) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(kBasic, std::strlen(kBasic), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(st, TAURUS_OK);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "root");

    taurus_document_free(doc);
}

TEST(Smoke, RejectsNullInput) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(nullptr, 0, &st);
    EXPECT_EQ(doc, nullptr);
}

TEST(Smoke, RejectsZeroLengthInput) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string("", 0, &st);
    EXPECT_EQ(doc, nullptr);
}

}  // namespace
