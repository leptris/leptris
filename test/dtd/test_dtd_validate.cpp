// test/dtd/test_dtd_validate.cpp — DTD validation API specs.

#include <gtest/gtest.h>
#include "taurus.h"
#include "taurus/dtd.h"
#include <cstring>

namespace {

TEST(DtdValidate, EmptyElementWithNoChildrenIsValid) {
    /* The validator (Phase 1 of TODO 91) now actually runs. With a DTD
     * declaring <!ELEMENT root EMPTY> and a document whose root has
     * no children, validation returns 1 (valid). */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root EMPTY>";
    TaurusDTD* dtd = taurus_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    TaurusDTDError err = {0};
    int rc = taurus_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    taurus_dtd_error_free(&err);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);
}

TEST(DtdValidate, EmptyElementWithChildrenIsInvalid) {
    /* Same DTD, but the document has a child element — must report
     * a violation with element_name = "root". */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root EMPTY>";
    TaurusDTD* dtd = taurus_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    TaurusDTDError err = {0};
    int rc = taurus_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(err.element_name, nullptr);
    EXPECT_STREQ(err.element_name, "root");

    taurus_dtd_error_free(&err);
    taurus_dtd_free(dtd);
    taurus_document_free(doc);
}

TEST(DtdValidate, NullDocOrDtdReturnsError) {
    TaurusDTDError err = {0};
    int rc = taurus_dtd_validate(nullptr, nullptr, &err);
    EXPECT_EQ(rc, -1);
    EXPECT_NE(err.message, nullptr);
    taurus_dtd_error_free(&err);
}

TEST(DtdValidate, UndeclaredElementIsAccepted) {
    /* Phase 1 does not enforce element declaration presence (real-world
     * DTDs often permit extra elements). Undeclared root is OK. */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<undeclared/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT other EMPTY>";
    TaurusDTD* dtd = taurus_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    TaurusDTDError err = {0};
    int rc = taurus_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    taurus_dtd_error_free(&err);
    taurus_dtd_free(dtd);
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
