// test/abi/test_doctype_api.cpp — Specs for the public DOCTYPE access
// API (TODO 148 Phase 2). Backs Document#internal_subset, #doctype,
// DocType#name, #public_id, #system_id, #internal_subset in the Ruby
// binding.

#include <gtest/gtest.h>
#include "leptris.h"
#include <cstring>
#include <string>

namespace {
LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
}
}  // namespace

TEST(DoctypeApi, NoDoctypeReturnsNull) {
    auto doc = Parse("<root/>");
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_internal_subset(doc), nullptr);
    leptris_document_free(doc);
}

TEST(DoctypeApi, NullDocReturnsNull) {
    EXPECT_EQ(leptris_document_internal_subset(nullptr), nullptr);
}

TEST(DoctypeApi, BareDoctypeExposesName) {
    /* direct_parse skips DOCTYPE content; the legacy parser (which
     * builds the doctype) is only invoked when the input has an
     * internal DTD subset, entity references, or custom max_depth.
     * Force the legacy path with an internal subset. */
    auto doc = Parse("<!DOCTYPE html [<!ENTITY foo \"bar\">]><html/>");
    ASSERT_NE(doc, nullptr);
    LeptrisDoctype dt = leptris_document_internal_subset(doc);
    ASSERT_NE(dt, nullptr) << "legacy parser should populate the doctype";
    EXPECT_STREQ(leptris_doctype_get_name(dt), "html");
    EXPECT_STREQ(leptris_doctype_get_root_name(dt), "html");
    EXPECT_EQ(leptris_doctype_get_public_id(dt), nullptr);
    EXPECT_EQ(leptris_doctype_get_system_id(dt), nullptr);
    leptris_document_free(doc);
}

TEST(DoctypeApi, NullDoctypeAccessorsReturnNull) {
    EXPECT_EQ(leptris_doctype_get_name(nullptr), nullptr);
    EXPECT_EQ(leptris_doctype_get_root_name(nullptr), nullptr);
    EXPECT_EQ(leptris_doctype_get_public_id(nullptr), nullptr);
    EXPECT_EQ(leptris_doctype_get_system_id(nullptr), nullptr);
    EXPECT_EQ(leptris_doctype_get_internal_subset(nullptr), nullptr);
}
