// test/dtd/test_dtd_validate.cpp — DTD validation API specs.

#include <gtest/gtest.h>
#include "taurus.h"
#include "taurus/dtd.h"
#include <cstring>

namespace {

TEST(DtdValidate, ReturnsNotImplementedWithNullDtd) {
    /* The validator is currently a stub (TODO 91). It must:
     * - Not crash on NULL arguments.
     * - Return -1 (distinguishable from "valid" = 1 and "invalid" = 0).
     * - Populate `error` with a message explaining the limitation.
     *
     * We pass NULL for the DTD to avoid exercising the DTD parser
     * (which has a separate Linux ASAN issue being tracked). */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusDTDError err = {0};
    int rc = taurus_dtd_validate(doc, nullptr, &err);
    EXPECT_EQ(rc, -1);
    EXPECT_NE(err.message, nullptr);

    taurus_dtd_error_free(&err);
    taurus_document_free(doc);
}

TEST(DtdValidate, ErrorFreeHandlesNull) {
    /* Must be a no-op for NULL — common defensive-programming pattern. */
    taurus_dtd_error_free(nullptr);
}

TEST(DtdValidate, ErrorFreeReusesStructAfterProperAlloc) {
    TaurusDTDError err = {0};
    /* Mimic what taurus_dtd_validate does to set the message. */
    err.message = static_cast<char*>(malloc(8));
    ASSERT_NE(err.message, nullptr);
    std::memcpy(err.message, "missing", 8);
    err.element_name = static_cast<char*>(malloc(5));
    ASSERT_NE(err.element_name, nullptr);
    std::memcpy(err.element_name, "book", 5);
    err.line = 12;
    err.column = 3;

    taurus_dtd_error_free(&err);

    EXPECT_EQ(err.message, nullptr);
    EXPECT_EQ(err.element_name, nullptr);
    EXPECT_EQ(err.line, 0);
    EXPECT_EQ(err.column, 0);
}

}  // namespace
