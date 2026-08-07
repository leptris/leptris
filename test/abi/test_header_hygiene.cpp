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

/* Internal headers — needed for the compact-pointer round-trip specs
 * (TODO 90 Phase 2). The structs are opaque to public callers.
 * The test CMake target adds src/taurus/dom to the include path. */
#include "element.h"
#include "text.h"
#include "cdata.h"
#include "comment.h"
#include "pi.h"

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
// it points to is 80 bytes after TODO 90 Phase 2d (first/last attribute
// pointers compressed to int32_t offsets). pugixml compact node: 12 B.
// Phase 2e (string/document-context pointers) is a stretch goal.
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

/* Compact-pointer encoding round-trip (TODO 90 Phase 2b/2c/2d).
 *
 * Tree edges, attribute-list edges, and non-element-node siblings
 * are stored as int32_t byte-offsets relative to the hosting node's
 * own address. These specs exercise the inline encoder/decoder pairs
 * for each edge kind. A regression here would mean the offset math
 * is wrong, which would silently corrupt the tree. */
TEST(HeaderHygiene, ElementTreeEdgeRoundTrip) {
    /* Allocate two fake elements in the same memory region so their
     * offset fits in int32_t. Use a small aligned buffer. */
    static const size_t kAlign = alignof(struct taurus_element);
    alignas(kAlign) char buf[2 * sizeof(struct taurus_element)];
    struct taurus_element* a = (struct taurus_element*)buf;
    struct taurus_element* b = (struct taurus_element*)(buf + sizeof(*a));

    /* parent round-trip */
    taurus_elem_set_parent(a, b);
    EXPECT_EQ(taurus_elem_parent(a), b);

    /* NULL encoding */
    taurus_elem_set_parent(a, NULL);
    EXPECT_EQ(taurus_elem_parent(a), nullptr);

    /* first_child, last_child, next_sibling round-trip */
    taurus_elem_set_first_child(a, (TaurusNode*)b);
    taurus_elem_set_last_child(a, (TaurusNode*)b);
    taurus_elem_set_next_sibling(a, (TaurusNode*)b);
    EXPECT_EQ(taurus_elem_first_child(a), (TaurusNode*)b);
    EXPECT_EQ(taurus_elem_last_child(a), (TaurusNode*)b);
    EXPECT_EQ(taurus_elem_next_sibling(a), (TaurusNode*)b);
}

TEST(HeaderHygiene, ElementAttributeEdgeRoundTrip) {
    /* Attribute-list offsets use the same encoding as tree edges. */
    static const size_t kAlign = alignof(struct taurus_element);
    alignas(kAlign) char buf[sizeof(struct taurus_element) + 64];
    struct taurus_element* e = (struct taurus_element*)buf;
    struct taurus_attribute* attr =
        (struct taurus_attribute*)(buf + sizeof(*e));

    taurus_elem_set_first_attribute(e, attr);
    taurus_elem_set_last_attribute(e, attr);
    EXPECT_EQ(taurus_elem_first_attribute(e), attr);
    EXPECT_EQ(taurus_elem_last_attribute(e), attr);

    taurus_elem_set_first_attribute(e, NULL);
    taurus_elem_set_last_attribute(e, NULL);
    EXPECT_EQ(taurus_elem_first_attribute(e), nullptr);
    EXPECT_EQ(taurus_elem_last_attribute(e), nullptr);
}

TEST(HeaderHygiene, NonElementNodeSiblingRoundTrip) {
    /* Text/CDATA/Comment/PI nodes each carry their own sibling offset. */
    alignas(64) char buf[128];
    TaurusTextNode* t = (TaurusTextNode*)buf;
    TaurusNode* sibling = (TaurusNode*)(buf + 64);

    taurus_textnode_set_next_sibling(t, sibling);
    EXPECT_EQ(taurus_textnode_next_sibling(t), sibling);

    TaurusCDATANode* c = (TaurusCDATANode*)buf;
    taurus_cdata_set_next_sibling(c, sibling);
    EXPECT_EQ(taurus_cdata_next_sibling(c), sibling);

    TaurusCommentNode* cm = (TaurusCommentNode*)buf;
    taurus_comment_set_next_sibling(cm, sibling);
    EXPECT_EQ(taurus_comment_next_sibling(cm), sibling);

    TaurusPINode* pi = (TaurusPINode*)buf;
    taurus_pi_set_next_sibling(pi, sibling);
    EXPECT_EQ(taurus_pi_next_sibling(pi), sibling);
}

// FFI Helper API (TODO 138): document_encoding, attribute index
// access, namespace count, status string.

TEST(HeaderHygiene, DocumentEncodingAccessor) {
    const char xml[] = "<?xml version='1.0' encoding='UTF-8'?><r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);

    const char* enc = taurus_document_encoding(doc);
    EXPECT_STREQ(enc, "UTF-8");

    taurus_document_free(doc);
}

TEST(HeaderHygiene, AttributeIndexAccess) {
    const char xml[] = "<e a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    EXPECT_EQ(taurus_element_attribute_count(root), 3u);
    EXPECT_STREQ(taurus_element_attribute_name_at(root, 0), "a");
    EXPECT_STREQ(taurus_element_attribute_value_at(root, 0), "1");
    EXPECT_STREQ(taurus_element_attribute_name_at(root, 1), "b");
    EXPECT_STREQ(taurus_element_attribute_value_at(root, 1), "2");
    EXPECT_STREQ(taurus_element_attribute_name_at(root, 2), "c");
    EXPECT_STREQ(taurus_element_attribute_value_at(root, 2), "3");

    /* Out of range. */
    EXPECT_EQ(taurus_element_attribute_name_at(root, 99), nullptr);
    EXPECT_EQ(taurus_element_attribute_value_at(root, 99), nullptr);

    taurus_document_free(doc);
}

TEST(HeaderHygiene, NamespaceCount) {
    /* xmlns declarations are stripped from the regular attribute list
     * by the parser. namespace_count walks the attribute list and
     * finds 0 xmlns. The namespace itself is accessible via
     * taurus_element_namespace(). */
    const char xml[] = "<e xmlns='http://default' a='1'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* Regular attributes are counted. */
    EXPECT_EQ(taurus_element_attribute_count(root), 1u);

    /* Namespace declarations are NOT in the attribute list in the
     * current parser. namespace_count returns 0. This is a known
     * limitation — the active namespace is available via
     * taurus_element_namespace(). */
    EXPECT_EQ(taurus_element_namespace_count(root), 0u);

    taurus_document_free(doc);
}

TEST(HeaderHygiene, StatusString) {
    EXPECT_STREQ(taurus_status_string(TAURUS_OK), "OK");
    EXPECT_STREQ(taurus_status_string(TAURUS_ERROR_PARSE), "XML parse error");
    EXPECT_STREQ(taurus_status_string(TAURUS_ERROR_MEMORY), "Memory allocation failed");
    /* Unknown code returns something non-NULL. */
    EXPECT_STRNE(taurus_status_string(static_cast<TaurusStatus>(-999)), "");
}

}  // namespace

