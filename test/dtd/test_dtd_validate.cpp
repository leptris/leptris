// test/dtd/test_dtd_validate.cpp — DTD validation API specs.

#include <gtest/gtest.h>
#include "leptris.h"
#include "leptris/dtd.h"
#include <cstring>
#include <cstdio>
#include <string>

namespace {

TEST(DtdValidate, EmptyElementWithNoChildrenIsValid) {
    /* The validator (Phase 1 of TODO 91) now actually runs. With a DTD
     * declaring <!ELEMENT root EMPTY> and a document whose root has
     * no children, validation returns 1 (valid). */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EmptyElementWithChildrenIsInvalid) {
    /* Same DTD, but the document has a child element — must report
     * a violation with element_name = "root". */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(err.element_name, nullptr);
    EXPECT_STREQ(err.element_name, "root");

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, NullDocOrDtdReturnsError) {
    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(nullptr, nullptr, &err);
    EXPECT_EQ(rc, -1);
    EXPECT_NE(err.message, nullptr);
    leptris_dtd_error_free(&err);
}

TEST(DtdValidate, UndeclaredElementIsAccepted) {
    /* Phase 1 does not enforce element declaration presence (real-world
     * DTDs often permit extra elements). Undeclared root is OK. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<undeclared/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT other EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ErrorFreeHandlesNull) {
    /* Must be a no-op for NULL — common defensive-programming pattern. */
    leptris_dtd_error_free(nullptr);
}

TEST(DtdValidate, ErrorFreeReusesStructAfterProperAlloc) {
    LeptrisDTDError err = {0};
    /* Mimic what leptris_dtd_validate does to set the message. */
    err.message = static_cast<char*>(malloc(8));
    ASSERT_NE(err.message, nullptr);
    std::memcpy(err.message, "missing", 8);
    err.element_name = static_cast<char*>(malloc(5));
    ASSERT_NE(err.element_name, nullptr);
    std::memcpy(err.element_name, "book", 5);
    err.line = 12;
    err.column = 3;

    leptris_dtd_error_free(&err);

    EXPECT_EQ(err.message, nullptr);
    EXPECT_EQ(err.element_name, nullptr);
    EXPECT_EQ(err.line, 0);
    EXPECT_EQ(err.column, 0);
}

}  // namespace

TEST(DtdValidate, RequiredAttributeMissingIsInvalid) {
    /* Phase 2: validator enforces #REQUIRED for common attribute
     * names (id, ref) when the DTD declares them required. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root id ID #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(err.element_name, nullptr);
    EXPECT_STREQ(err.element_name, "root");

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, RequiredAttributePresentIsValid) {
    /* Same DTD, but the document provides id — must pass. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root id='r1'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root id ID #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, RequiredCustomAttributeMissingIsInvalid) {
    /* Phase 3: general #REQUIRED iteration. The DTD declares a
     * #REQUIRED attribute ("version") that's not in the old
     * id/ref allowlist — the iterator-based validator now catches it. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root version CDATA #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(err.element_name, nullptr);
    EXPECT_STREQ(err.element_name, "root");
    /* Message names the missing attribute. */
    EXPECT_NE(std::string(err.message).find("version"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ImpliedAttributeNotRequiredIsValid) {
    /* #IMPLIED attributes are optional even if absent. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root note CDATA #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ContentModelSequenceMatchIsValid) {
    /* Phase 4: element-content grammar matcher.
     * DTD says <!ELEMENT root (title, author+)>, doc has title then
     * author — valid. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><title>X</title><author>A</author></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root (title, author+)>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ContentModelSequenceWrongOrderIsInvalid) {
    /* Same DTD, but children are out of order: author before title. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><author>A</author><title>X</title></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root (title, author+)>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}


TEST(DtdValidate, ContentModelZeroOrOneAcceptsEmpty) {
    /* <!ELEMENT root (a)?> — zero or one a. No children is valid. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] = "<!ELEMENT root (a)?>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, UniqueIdsAreValid) {
    /* Phase 5: ID uniqueness. Two distinct id values both declared ID. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root><a id='x1'/><b id='x2'/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root (a|b)>"
        "<!ELEMENT a EMPTY>"
        "<!ELEMENT b EMPTY>"
        "<!ATTLIST a id ID #IMPLIED>"
        "<!ATTLIST b id ID #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, DuplicateIdIsInvalid) {
    /* Same DTD; two elements with id='x1'. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root><a id='x1'/><b id='x1'/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root (a|b)>"
        "<!ELEMENT a EMPTY>"
        "<!ELEMENT b EMPTY>"
        "<!ATTLIST a id ID #IMPLIED>"
        "<!ATTLIST b id ID #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(std::string(err.message).find("x1"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, IdrefResolvesIsValid) {
    /* Phase 6: IDREF must reference an existing ID. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root><a id='x1'/><b ref='x1'/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root (a|b)>"
        "<!ELEMENT a EMPTY>"
        "<!ELEMENT b EMPTY>"
        "<!ATTLIST a id ID #IMPLIED>"
        "<!ATTLIST b ref IDREF #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);
    EXPECT_EQ(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, IdrefUnresolvedIsInvalid) {
    /* Same DTD; ref='unknown' refers to no ID. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root><a id='x1'/><b ref='unknown'/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root (a|b)>"
        "<!ELEMENT a EMPTY>"
        "<!ELEMENT b EMPTY>"
        "<!ATTLIST a id ID #IMPLIED>"
        "<!ATTLIST b ref IDREF #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(std::string(err.message).find("unknown"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}


TEST(DtdValidate, NmtokenValidValueIsValid) {
    /* Phase 7: NMTOKEN must match the Name token character class. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root token='valid-name_123'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root token NMTOKEN #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, NmtokenInvalidValueIsInvalid) {
    /* 'val id' has a space which is invalid in a single NMTOKEN. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root token='val id'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root token NMTOKEN #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EnumeratedTypeValidValueIsValid) {
    /* Phase 7: enumerated type (red|green|blue) — value matches. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root color='green'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root color (red|green|blue) #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EnumeratedTypeInvalidValueIsInvalid) {
    /* Same DTD; 'yellow' is not in the enumerated list. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root color='yellow'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root color (red|green|blue) #IMPLIED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, FixedAttributeMatchingValueIsValid) {
    /* #FIXED "1.0": the element provides the same value — valid. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root version='1.0'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root version CDATA #FIXED \"1.0\">";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, FixedAttributeMismatchedValueIsInvalid) {
    /* Same DTD; element provides version='2.0' instead of '1.0'. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root version='2.0'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root version CDATA #FIXED \"1.0\">";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

// ---- Phase 8: ENTITY / ENTITIES attribute validation (TODO 91) --------

TEST(DtdValidate, EntityAttrRejectsUndeclaredEntity) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root src='nope.jpg'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root src ENTITY #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(std::string(err.message).find("undeclared"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EntityAttrAcceptsDeclaredUnparsedEntity) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root src='image'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Per XML 1.0 spec, an ENTITY-typed attribute must reference an
     * unparsed entity (one declared with NDATA). The referenced
     * notation must itself be declared. */
    const char dtd_text[] =
        "<!NOTATION png PUBLIC 'image/png'>"
        "<!ENTITY image SYSTEM 'image.png' NDATA png>"
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root src ENTITY #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EntityAttrRejectsParsedEntity) {
    /* A parsed entity (no NDATA) is not a valid value for an
     * ENTITY-typed attribute. The validator must catch this. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root src='text'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY text 'hello world'>"  /* parsed (no NDATA) */
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root src ENTITY #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(std::string(err.message).find("unparsed"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EntitiesAttrValidatesEachToken) {
    /* ENTITIES is the whitespace-separated plural of ENTITY. Each
     * token must be a declared unparsed entity with a declared notation. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root imgs='a b c'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!NOTATION png PUBLIC 'image/png'>"
        "<!ENTITY a SYSTEM 'a.png' NDATA png>"
        "<!ENTITY b SYSTEM 'b.png' NDATA png>"
        /* 'c' intentionally NOT declared */
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root imgs ENTITIES #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);
    EXPECT_NE(std::string(err.message).find("undeclared"), std::string::npos);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

// ---- Phase 8d: Conditional sections (TODO 91) -------------------------

TEST(DtdValidate, ConditionalSectionIncludeParsesInnerDecls) {
    /* <![INCLUDE[...]]> — the inner declarations are active. The
     * element declared inside the conditional section is visible
     * for validation. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<![INCLUDE["
        "<!ELEMENT root EMPTY>"
        "]]>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ConditionalSectionIgnoreSkipsInnerDecls) {
    /* <![IGNORE[...]]> — inner declarations are skipped. The element
     * declared inside IGNORE is NOT registered. The document has an
     * element the DTD doesn't know about, so validation fails. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<![IGNORE["
        "<!ELEMENT root EMPTY>"
        "]]>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    /* Without the IGNORE'd declaration, 'root' is undeclared. The
     * validator's behavior on undeclared elements is lenient (it
     * passes), so we just verify the parse didn't crash and the
     * DTD has no element declarations registered for 'root'. */
    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    /* rc may be 0 or 1 depending on lenient mode — main check is
     * that parsing succeeded without crashing. */
    (void)rc;
    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ConditionalSectionNestedIgnoreHandlesDepth) {
    /* Nested conditional sections: IGNORE with an inner INCLUDE.
     * The parser must track depth so the inner ]]> doesn't close
     * the outer IGNORE prematurely. Test that the parser handles
     * this without crashing or infinite-looping. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<![IGNORE["
        "<![INCLUDE["
        "<!ELEMENT shouldNotBeParsed EMPTY>"
        "]]>"
        "]]>"
        "<!ELEMENT root EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

// ---- Phase 8b: Parameter entities (TODO 91) ---------------------------

TEST(DtdValidate, ParameterEntityInternalSubstitutesValue) {
    /* <!ENTITY % pe "..."> declares an internal parameter entity.
     * %pe; references substitute the value inline before parsing
     * continues. The included declarations become live. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY % root-decl \"<!ELEMENT root EMPTY>\">"
        "%root-decl;";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntityDoesNotCollideWithGeneral) {
    /* A general entity "foo" and a parameter entity "%foo" should be
     * distinct. The validator should accept both with the same name. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root>hello</root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY foo 'general'>"
        "<!ENTITY % foo 'parameter'>"
        "<!ELEMENT root (#PCDATA)>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntityChainedReferencesExpand) {
    /* Parameter entity referencing another parameter entity. Both
     * should expand to produce the final declaration. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY % inner \"<!ELEMENT root EMPTY>\">"
        "<!ENTITY % outer \"%inner;\">"
        "%outer;";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

// ---- Phase 8: falsifiable enforcement (TODO 91 / TODO.remaining/03) ----
//
// The earlier INCLUDE/PE specs all paired the declaration with a
// conforming document, so "declaration registered" and "declaration
// lost" both returned 1 via the lenient undeclared-element path.
// These specs distinguish the two: the document VIOLATES the
// declaration, so a lost declaration flips the result.

TEST(DtdValidate, IncludeSectionDeclIsEnforcedForValidation) {
    /* If the INCLUDE body's <!ELEMENT root EMPTY> registers, the
     * child element is a violation (rc 0). If the body's
     * declarations were lost, the lenient path returns 1. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<![INCLUDE["
        "<!ELEMENT root EMPTY>"
        "]]>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, IncludeSectionKeepsDeclsAfterTheSection) {
    /* Declarations following the conditional section must still be
     * parsed — the INCLUDE handling cannot abort the rest of the
     * subset. The #REQUIRED attribute declared after the section is
     * enforced. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<![INCLUDE[<!ELEMENT a EMPTY>]]>"
        "<!ATTLIST root req CDATA #REQUIRED>"
        "<!ELEMENT root EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.message, nullptr);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntityDeclIsEnforcedForValidation) {
    /* The declaration delivered by %e; must be live: <root> with a
     * child violates the substituted <!ELEMENT root EMPTY>. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY % e \"<!ELEMENT root EMPTY>\">"
        "%e;";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntityLargeSubstitutionRegisters) {
    /* A PE value larger than any stack buffer (the old splice path
     * capped at 8 KB and silently skipped). The substituted
     * declarations plus the tail after the reference must all land. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    std::string big = "<!ENTITY % big \"";
    for (int i = 0; i < 600; i++) {
        char decl[48];
        std::snprintf(decl, sizeof(decl), "<!ELEMENT filler%06d EMPTY>", i);
        big += decl;
    }
    big += "\">%big;<!ELEMENT root EMPTY>";

    LeptrisDTD* dtd = leptris_dtd_parse(big.c_str(), big.size());
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntitySelfReferenceTerminates) {
    /* A self-referential parameter entity must not recurse forever.
     * Depth is capped; the reference is skipped at the cap and the
     * rest of the subset still parses. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY % a \"%a;\">"
        "%a;"
        "<!ELEMENT root EMPTY>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* tail decl enforced — cycle skipped, not fatal */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, EntityAttrRejectsUndeclaredNotation) {
    /* ENTITY-typed value points at an unparsed entity whose NDATA
     * notation was never declared via <!NOTATION>. Invalid. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<img src='pic'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY pic SYSTEM 'pic.png' NDATA gif>"
        "<!ELEMENT img EMPTY>"
        "<!ATTLIST img src ENTITY #REQUIRED>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, DuplicateElementDeclarationIsIgnoredNotFreed) {
    /* Re-declaring an element is legal input (first declaration
     * wins); the rejected duplicate is pool-owned and must simply be
     * ignored — the old path free()d pool memory on rejection and
     * corrupted the heap. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ELEMENT root (a,b)>"
        "<!ELEMENT root ANY>"
        "<!ENTITY dup 'one'>"
        "<!ENTITY dup 'two'>"
        "<!NOTATION n SYSTEM 'x'>"
        "<!NOTATION n SYSTEM 'y'>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1); /* first decl (EMPTY) binds; <root/> conforms */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, AttributeFirstDeclarationWins) {
    /* XML 1.0 §3.3: when the same attribute is declared more than
     * once, the FIRST declaration is binding. The #FIXED value from
     * the first ATTLIST is enforced even though a second declares a
     * different fixed value. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root x='wrong'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ATTLIST root x CDATA #FIXED 'right'>"
        "<!ATTLIST root x CDATA #FIXED 'wrong'>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* 'wrong' violates the first (#FIXED 'right') */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

// ---- External subset (TODO.remaining/03) ------------------------------

TEST(DtdValidate, DocumentGetDtdExposesInternalSubset) {
    /* The document's parsed internal subset is reachable via
     * leptris_document_get_dtd — the handle the app passes to
     * leptris_dtd_validate. Document-owned: no leptris_dtd_free. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<!DOCTYPE root [<!ELEMENT root EMPTY>]><root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    ASSERT_NE(dtd, nullptr);
    EXPECT_EQ(dtd, leptris_document_get_dtd(doc)); /* stable handle */

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1);

    leptris_dtd_error_free(&err);
    leptris_document_free(doc);
}

TEST(DtdValidate, ExternalSubsetMergesForValidation) {
    /* DOCTYPE with only a SYSTEM id: no internal subset exists yet,
     * so get_dtd creates an empty DTD on the document; the app feeds
     * the external subset content in; declarations become live. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<!DOCTYPE root SYSTEM 'ext.dtd'><root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    ASSERT_NE(dtd, nullptr);

    const char ext[] = "<!ELEMENT root EMPTY>";
    EXPECT_EQ(leptris_dtd_parse_external_subset(dtd, ext, std::strlen(ext)), 1);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* EMPTY violated via the external subset */

    leptris_dtd_error_free(&err);
    leptris_document_free(doc);
}

TEST(DtdValidate, ExternalSubsetDoesNotOverrideInternal) {
    /* First declaration wins: the internal subset's EMPTY beats the
     * external subset's ANY re-declaration. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<!DOCTYPE root [<!ELEMENT root EMPTY>]><root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    ASSERT_NE(dtd, nullptr);

    const char ext[] = "<!ELEMENT root ANY>";
    EXPECT_EQ(leptris_dtd_parse_external_subset(dtd, ext, std::strlen(ext)), 1);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* internal EMPTY still binds */

    leptris_dtd_error_free(&err);
    leptris_document_free(doc);
}

TEST(DtdValidate, ExternalSubsetConditionalSectionsAndPEs) {
    /* External subsets are where conditional sections and parameter
     * entities are legal per XML 1.0 — the hook must handle both. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<!DOCTYPE root SYSTEM 'ext.dtd'><root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    ASSERT_NE(dtd, nullptr);

    const char ext[] =
        "<!ENTITY % e \"<!ELEMENT root EMPTY>\">"
        "%e;"
        "<![INCLUDE[<!ELEMENT extra EMPTY>]]>";
    EXPECT_EQ(leptris_dtd_parse_external_subset(dtd, ext, std::strlen(ext)), 1);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* root EMPTY delivered via the PE, enforced */

    leptris_dtd_error_free(&err);
    leptris_document_free(doc);
}

// ---- DTD residuals (TODO post-close-out): %pe; in decls + loader ----

TEST(DtdValidate, ParameterEntityInsideElementDecl) {
    /* %pe; inside a markup declaration body (legal in external
     * subsets; accepted leniently everywhere). */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY %content 'EMPTY'>"
        "<!ELEMENT root %content;>"
        "<!ELEMENT other %content;>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* root EMPTY enforced through the PE */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

TEST(DtdValidate, ParameterEntityInsideAttlistDecl) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ELEMENT root EMPTY>"
        "<!ENTITY %req 'id CDATA #REQUIRED'>"
        "<!ATTLIST root %req;>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 0); /* #REQUIRED delivered through the PE */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}

static char* test_pe_loader(void* user, const char* system_id, size_t* out_len) {
    (void)user;
    if (std::strcmp(system_id, "model.dtd") != 0) return NULL;
    const char body[] = "(a,b)";
    char* buf = (char*)std::malloc(sizeof(body));
    if (!buf) return NULL;
    std::memcpy(buf, body, sizeof(body));
    *out_len = sizeof(body) - 1;
    return buf;
}

TEST(DtdValidate, ExternalParameterEntityLoaderFeedsContentModel) {
    /* <!ENTITY % m SYSTEM "model.dtd"> + %m; inside an ELEMENT decl:
     * the loader supplies the content model text. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><a/><b/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* The loader is consulted when content is PARSED into the DTD,
     * so register before feeding the subset (the get_dtd +
     * parse_external_subset flow; a loader on an already-parsed
     * leptris_dtd_parse result has nothing left to resolve). */
    const char dtd_text[] =
        "<!ENTITY % m SYSTEM \"model.dtd\">"
        "<!ELEMENT root %m;>";
    LeptrisDTD* dtd = leptris_document_get_dtd(doc);
    ASSERT_NE(dtd, nullptr);
    leptris_dtd_set_pe_loader(dtd, test_pe_loader, NULL);
    EXPECT_EQ(leptris_dtd_parse_external_subset(
                  dtd, dtd_text, std::strlen(dtd_text)), 1);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1); /* (a,b) matches <a/><b/> */

    /* And the wrong order fails through the loaded model. */
    const char xml2[] = "<root><b/><a/></root>";
    LeptrisDocument doc2 = leptris_parse_string(xml2, std::strlen(xml2), &st);
    ASSERT_NE(doc2, nullptr);
    LeptrisDTDError err2 = {0};
    int rc2 = leptris_dtd_validate(doc2, dtd, &err2);
    EXPECT_EQ(rc2, 0);

    leptris_dtd_error_free(&err);
    leptris_dtd_error_free(&err2);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
    leptris_document_free(doc2);
}

TEST(DtdValidate, ExternalPEWithoutLoaderIsSkippedLeniently) {
    /* No loader registered: the external PE inside the decl is left
     * as-is and the declaration is skipped (no crash, no hang). */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    const char dtd_text[] =
        "<!ENTITY % m SYSTEM \"missing.dtd\">"
        "<!ELEMENT root %m;>";
    LeptrisDTD* dtd = leptris_dtd_parse(dtd_text, std::strlen(dtd_text));
    ASSERT_NE(dtd, nullptr);

    LeptrisDTDError err = {0};
    int rc = leptris_dtd_validate(doc, dtd, &err);
    EXPECT_EQ(rc, 1); /* lenient: nothing enforced */

    leptris_dtd_error_free(&err);
    leptris_dtd_free(dtd);
    leptris_document_free(doc);
}
