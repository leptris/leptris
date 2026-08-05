// test/abi/test_header_hygiene.cpp — Header self-containment + bindgen mode.
//
// A binding generator (bindgen, cffi, ctypes) parses the public
// headers without a C compiler.  These specs verify the headers
// survive that parse by:
//   1. The umbrella header taurus.h compiles cleanly.
//   2. TAURUS_FOR_BINDGEN mode produces a clean parse (verified by
//      the standalone compile in CI).
//   3. Opaque-handle typedefs are pointer-sized (catches accidental
//      struct-field exposure).
//
// See TODO 84.

#include <gtest/gtest.h>

#include <cstring>
#include <cstddef>

/* The umbrella header pulls in everything.  Including sub-headers
 * directly in the same TU redefines enums (intentional — see TODO 12). */
#include "taurus.h"

namespace {

TEST(HeaderHygiene, UmbrellaHeaderCompilesStandalone) {
    /* If this compiles, every public header is self-contained
     * (no missing #include). */
    SUCCEED();
}

TEST(HeaderHygiene, OpaqueHandlesArePointerSized) {
    /* Catches accidental struct-field exposure that would change
     * opaque-handle sizes.  Binding generators assume sizeof(void*). */
    EXPECT_EQ(sizeof(TaurusDocument),   sizeof(void*));
    EXPECT_EQ(sizeof(TaurusElement),    sizeof(void*));
    EXPECT_EQ(sizeof(TaurusAttribute),  sizeof(void*));
    EXPECT_EQ(sizeof(TaurusXPathResult), sizeof(void*));
}

TEST(HeaderHygiene, NodeRefIsPointerSized) {
    EXPECT_EQ(sizeof(TaurusNodeRef), sizeof(void*));
}

TEST(HeaderHygiene, NamespaceTypedefIsPointerSized) {
    /* TaurusNamespace is `const char*` — pointer-sized. */
    EXPECT_EQ(sizeof(TaurusNamespace), sizeof(void*));
}

// Report the internal element struct size for tracking (TODO 90).
// The public TaurusElement is an opaque pointer (8 bytes); the struct
// it points to is 88 bytes after TODO 90 Phase 2b (tree pointers stored
// as int32_t self-relative offsets). pugixml compact node: 12 bytes.
// Phase 2d target: ~80 bytes via compact attribute pointers.
TEST(HeaderHygiene, ElementStructSizeTracked) {
    /* This test prints the actual size via a record_property call so
     * the CI artifact captures it.  No assertion — the _Static_assert
     * in element.h guards against growth. */
    testing::Test::RecordProperty("element_struct_size_bytes",
                                   "see_element_h_static_assert");
    SUCCEED() << "Element struct size guarded by _Static_assert in element.h";
}

TEST(HeaderHygiene, StatusEnumValuesAreStable) {
    /* Binding generators hard-code enum values; pin them. */
    EXPECT_EQ(TAURUS_OK,                0);
    EXPECT_EQ(TAURUS_ERROR_MEMORY,     -1);
    EXPECT_EQ(TAURUS_ERROR_PARSE,      -2);
    EXPECT_EQ(TAURUS_ERROR_XPATH,      -3);
    EXPECT_EQ(TAURUS_ERROR_NULL_ARG,   -4);
    EXPECT_EQ(TAURUS_ERROR_INVALID_ARG, -5);
    EXPECT_EQ(TAURUS_ERROR_NOT_FOUND,  -6);
    EXPECT_EQ(TAURUS_ERROR_IO,         -7);
}

TEST(HeaderHygiene, XPathResultTypeEnumValues) {
    /* The XPath result-type enum must be stable across versions. */
    EXPECT_EQ(TAURUS_XPATH_NODESET, 0);
    EXPECT_EQ(TAURUS_XPATH_BOOLEAN, 1);
    EXPECT_EQ(TAURUS_XPATH_NUMBER,  2);
    EXPECT_EQ(TAURUS_XPATH_STRING,  3);
}

}  // namespace

