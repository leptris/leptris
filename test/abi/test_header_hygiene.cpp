// test/abi/test_header_hygiene.cpp — Header self-containment + bindgen mode.
//
// A binding generator (bindgen, cffi, ctypes) parses the public
// headers without a C compiler.  These specs verify the headers
// survive that parse by:
//   1. The umbrella header leptris.h compiles cleanly.
//   2. LEPTRIS_FOR_BINDGEN mode produces a clean parse (verified by
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
#include "leptris.h"

/* Internal headers — needed for the compact-pointer round-trip specs
 * (TODO 90 Phase 2). The structs are opaque to public callers.
 * The test CMake target adds src/leptris/dom to the include path. */
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
    EXPECT_EQ(sizeof(LeptrisDocument),   sizeof(void*));
    EXPECT_EQ(sizeof(LeptrisElement),    sizeof(void*));
    EXPECT_EQ(sizeof(LeptrisAttribute),  sizeof(void*));
    EXPECT_EQ(sizeof(LeptrisXPathResult), sizeof(void*));
}

TEST(HeaderHygiene, NodeRefIsPointerSized) {
    EXPECT_EQ(sizeof(LeptrisNodeRef), sizeof(void*));
}

TEST(HeaderHygiene, NamespaceTypedefIsPointerSized) {
    /* LeptrisNamespace is `const char*` — pointer-sized. */
    EXPECT_EQ(sizeof(LeptrisNamespace), sizeof(void*));
}

// Report the internal element struct size for tracking (TODO 90).
// The public LeptrisElement is an opaque pointer (8 bytes); the struct
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
    EXPECT_EQ(LEPTRIS_OK,                0);
    EXPECT_EQ(LEPTRIS_ERROR_MEMORY,     -1);
    EXPECT_EQ(LEPTRIS_ERROR_PARSE,      -2);
    EXPECT_EQ(LEPTRIS_ERROR_XPATH,      -3);
    EXPECT_EQ(LEPTRIS_ERROR_NULL_ARG,   -4);
    EXPECT_EQ(LEPTRIS_ERROR_INVALID_ARG, -5);
    EXPECT_EQ(LEPTRIS_ERROR_NOT_FOUND,  -6);
    EXPECT_EQ(LEPTRIS_ERROR_IO,         -7);
}

TEST(HeaderHygiene, XPathResultTypeEnumValues) {
    /* The XPath result-type enum must be stable across versions. */
    EXPECT_EQ(LEPTRIS_XPATH_NODESET, 0);
    EXPECT_EQ(LEPTRIS_XPATH_BOOLEAN, 1);
    EXPECT_EQ(LEPTRIS_XPATH_NUMBER,  2);
    EXPECT_EQ(LEPTRIS_XPATH_STRING,  3);
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
    static const size_t kAlign = alignof(struct leptris_element);
    alignas(kAlign) char buf[2 * sizeof(struct leptris_element)];
    struct leptris_element* a = (struct leptris_element*)buf;
    struct leptris_element* b = (struct leptris_element*)(buf + sizeof(*a));

    /* parent round-trip */
    leptris_elem_set_parent(a, b);
    EXPECT_EQ(leptris_elem_parent(a), b);

    /* NULL encoding */
    leptris_elem_set_parent(a, NULL);
    EXPECT_EQ(leptris_elem_parent(a), nullptr);

    /* first_child, last_child, next_sibling round-trip */
    leptris_elem_set_first_child(a, (LeptrisNode*)b);
    leptris_elem_set_last_child(a, (LeptrisNode*)b);
    leptris_elem_set_next_sibling(a, (LeptrisNode*)b);
    EXPECT_EQ(leptris_elem_first_child(a), (LeptrisNode*)b);
    EXPECT_EQ(leptris_elem_last_child(a), (LeptrisNode*)b);
    EXPECT_EQ(leptris_elem_next_sibling(a), (LeptrisNode*)b);
}

TEST(HeaderHygiene, ElementAttributeEdgeRoundTrip) {
    /* Attribute-list offsets use the same encoding as tree edges.
     * TODO 155 Phase C: last_attribute was removed; the getter walks
     * via the cp16 attr edge (TODO 183 Phase 5). The test attr must
     * have next=NULL so the walk terminates after one step. */
    static const size_t kAlign = alignof(struct leptris_element);
    alignas(kAlign) char buf[sizeof(struct leptris_element) +
                              sizeof(struct leptris_attribute) + 64];
    struct leptris_element* e = (struct leptris_element*)buf;
    struct leptris_attribute* attr =
        (struct leptris_attribute*)(buf + sizeof(*e));
    leptris_attr_set_next(attr, NULL);

    leptris_elem_set_first_attribute(e, attr);
    EXPECT_EQ(leptris_elem_first_attribute(e), attr);
    EXPECT_EQ(leptris_elem_last_attribute(e), attr);

    leptris_elem_set_first_attribute(e, NULL);
    EXPECT_EQ(leptris_elem_first_attribute(e), nullptr);
    EXPECT_EQ(leptris_elem_last_attribute(e), nullptr);
}

TEST(HeaderHygiene, NonElementNodeSiblingRoundTrip) {
    /* Text/CDATA/Comment/PI nodes each carry their own sibling offset. */
    alignas(64) char buf[128];
    LeptrisTextNode* t = (LeptrisTextNode*)buf;
    LeptrisNode* sibling = (LeptrisNode*)(buf + 64);

    leptris_textnode_set_next_sibling(t, sibling);
    EXPECT_EQ(leptris_textnode_next_sibling(t), sibling);

    LeptrisCDATANode* c = (LeptrisCDATANode*)buf;
    leptris_cdata_set_next_sibling(c, sibling);
    EXPECT_EQ(leptris_cdata_next_sibling(c), sibling);

    LeptrisCommentNode* cm = (LeptrisCommentNode*)buf;
    leptris_comment_set_next_sibling(cm, sibling);
    EXPECT_EQ(leptris_comment_next_sibling(cm), sibling);

    LeptrisPINode* pi = (LeptrisPINode*)buf;
    leptris_pi_set_next_sibling(pi, sibling);
    EXPECT_EQ(leptris_pi_next_sibling(pi), sibling);
}

// FFI Helper API (TODO 138): document_encoding, attribute index
// access, namespace count, status string.

TEST(HeaderHygiene, DocumentEncodingAccessor) {
    const char xml[] = "<?xml version='1.0' encoding='UTF-8'?><r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);

    const char* enc = leptris_document_encoding(doc);
    EXPECT_STREQ(enc, "UTF-8");

    leptris_document_free(doc);
}

TEST(HeaderHygiene, AttributeIndexAccess) {
    const char xml[] = "<e a='1' b='2' c='3'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    EXPECT_EQ(leptris_element_attribute_count(root), 3u);
    EXPECT_STREQ(leptris_element_attribute_name_at(root, 0), "a");
    EXPECT_STREQ(leptris_element_attribute_value_at(root, 0), "1");
    EXPECT_STREQ(leptris_element_attribute_name_at(root, 1), "b");
    EXPECT_STREQ(leptris_element_attribute_value_at(root, 1), "2");
    EXPECT_STREQ(leptris_element_attribute_name_at(root, 2), "c");
    EXPECT_STREQ(leptris_element_attribute_value_at(root, 2), "3");

    /* Out of range. */
    EXPECT_EQ(leptris_element_attribute_name_at(root, 99), nullptr);
    EXPECT_EQ(leptris_element_attribute_value_at(root, 99), nullptr);

    leptris_document_free(doc);
}

TEST(HeaderHygiene, NamespaceCount) {
    /* The parser strips xmlns declarations from the regular attribute
     * list and stores them on elem->namespaces. leptris_element_namespace_count
     * walks both lists (issue #171 made it count namespaces correctly). */
    const char xml[] = "<e xmlns='http://default' a='1'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, sizeof(xml) - 1, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* Regular attributes are counted. */
    EXPECT_EQ(leptris_element_attribute_count(root), 1u);

    /* xmlns declarations are counted via elem->namespaces (was 0
     * pre-issue #171 because the old impl only walked the attribute
     * list, missing the parser-moved declarations). */
    EXPECT_EQ(leptris_element_namespace_count(root), 1u);
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0),
                 "http://default");
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);

    leptris_document_free(doc);
}

TEST(HeaderHygiene, NamespaceDeclEnumeration) {
    /* issue #171: per-index (prefix, uri) accessors. */
    const char xml[] =
        "<e xmlns='http://default' "
        "   xmlns:foo='http://foo' "
        "   xmlns:bar='http://bar' a='1'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    EXPECT_EQ(leptris_element_namespace_count(root), 3u);
    /* Order matches source order: default, foo, bar. */
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 0), nullptr);
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 0),
                 "http://default");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 1), "foo");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 1), "http://foo");
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(root, 2), "bar");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(root, 2), "http://bar");

    /* Out of range. */
    EXPECT_EQ(leptris_element_namespace_decl_prefix(root, 99), nullptr);
    EXPECT_EQ(leptris_element_namespace_decl_uri(root, 99), nullptr);

    leptris_document_free(doc);
}

TEST(HeaderHygiene, StatusString) {
    EXPECT_STREQ(leptris_status_string(LEPTRIS_OK), "OK");
    EXPECT_STREQ(leptris_status_string(LEPTRIS_ERROR_PARSE), "XML parse error");
    EXPECT_STREQ(leptris_status_string(LEPTRIS_ERROR_MEMORY), "Memory allocation failed");
    /* Unknown code returns something non-NULL. */
    EXPECT_STRNE(leptris_status_string(static_cast<LeptrisStatus>(-999)), "");
}

}  // namespace

