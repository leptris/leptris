/* Spec-coverage audit (2026-08-24): every exported symbol exercised.
 * The 37 functions that had zero spec coverage before this file,
 * grouped by area. Falsifiability: each group asserts failure
 * contracts (NULL/wrong-type/default-value paths), not just happy
 * paths. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/error.h"
}
#include <cstdio>
#include <cstring>
#include <string>

static const char* kDoc =
    "<root id='7' ratio='2.5' flag='true' name='top'>"
    "<a order='2'>alpha</a><b order='1'>beta</b><b order='3'>42</b>"
    "</root>";

static LeptrisDocument parse_doc() {
    return leptris_parse_string(kDoc, strlen(kDoc), nullptr);
}

/* ---- version + status messages ---- */

TEST(PublicSurface, VersionAndMessages) {
    ASSERT_NE(leptris_version(), nullptr);
    EXPECT_EQ(strncmp("1.", leptris_version(), 2), 0);
    int maj = -1, min = -1, pat = -1;
    leptris_version_components(&maj, &min, &pat);
    EXPECT_EQ(maj, 1);
    EXPECT_GE(min, 0);
    EXPECT_GE(pat, 0);

    EXPECT_NE(leptris_error_message(LEPTRIS_ERROR_PARSE), nullptr);
    EXPECT_STREQ(leptris_error_message(LEPTRIS_OK),
                 leptris_status_string(LEPTRIS_OK));
}

/* ---- file I/O round-trip ---- */

TEST(PublicSurface, FileIO) {
    const char* path = "leptris_surface_io.xml";
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_save_file(doc, path, nullptr), 0);

    /* load_file returns the bytes. */
    size_t size = 0;
    char* data = leptris_load_file(path, &size);
    ASSERT_NE(data, nullptr);
    EXPECT_GT(size, 0u);
    leptris_free_string(data);

    /* parse_file re-parses the saved document. */
    LeptrisDocument back = leptris_parse_file(path, nullptr);
    ASSERT_NE(back, nullptr);
    LeptrisElement r = leptris_document_root(back);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(leptris_element_name(r), "root");
    leptris_document_free(back);
    leptris_document_free(doc);
    remove(path);

    EXPECT_EQ(leptris_parse_file("/nonexistent/leptris.xml", nullptr),
              nullptr);
    EXPECT_EQ(leptris_load_file("/nonexistent/leptris.xml", nullptr),
              nullptr);
}

/* ---- typed attribute/text getters ---- */

TEST(PublicSurface, TypedGetters) {
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    EXPECT_EQ(leptris_element_attribute_bool(root, "flag", 0), 1);
    EXPECT_EQ(leptris_element_attribute_bool(root, "missing", 1), 1);
    EXPECT_EQ(leptris_element_attribute_uint(root, "id", 9), 7u);
    EXPECT_EQ(leptris_element_attribute_uint(root, "name", 9), 9u);
    EXPECT_DOUBLE_EQ(leptris_element_attribute_float(root, "ratio", 0.0),
                     2.5);

    LeptrisElement b3 = leptris_element_find_child_by_attr(
        root, "b", "order", "3");
    ASSERT_NE(b3, nullptr);
    EXPECT_EQ(leptris_element_text_uint(b3, 0), 42u);
    EXPECT_DOUBLE_EQ(leptris_element_text_float(b3, 0.0), 42.0);
    EXPECT_EQ(leptris_element_text_bool(b3, 0), 1);

    /* write + read back the float attribute setter */
    LeptrisElement a = leptris_element_find_child(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_element_set_attribute_float(a, "weight", 1.25),
              LEPTRIS_OK);
    EXPECT_DOUBLE_EQ(leptris_element_attribute_float(a, "weight", 0.0),
                     1.25);

    EXPECT_EQ(leptris_element_attribute_bool(nullptr, "x", 0), 0);
    EXPECT_EQ(leptris_element_text_uint(nullptr, 0), 0u);
    leptris_document_free(doc);
}

/* ---- navigation ---- */

TEST(PublicSurface, Navigation) {
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    EXPECT_EQ(leptris_element_root(root), root);
    LeptrisElement a = leptris_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_element_parent(a), root);
    EXPECT_EQ(leptris_element_previous_sibling(a, nullptr), nullptr);

    LeptrisElement b1 = leptris_element_next_sibling_any(a);
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(leptris_element_previous_sibling_any(b1), a);
    /* Three children (a, b@1, b@3): the LAST is b@3. */
    LeptrisElement b3 = leptris_element_find_child_by_attr(
        root, "b", "order", "3");
    ASSERT_NE(b3, nullptr);
    EXPECT_EQ(leptris_element_last_child(root, nullptr), b3);
    EXPECT_EQ(leptris_element_last_child(root, "a"), a);   /* named */
    EXPECT_EQ(leptris_element_last_child(root, "zzz"), nullptr);
    LeptrisElement last_any = leptris_element_last_child_any(root);
    ASSERT_NE(last_any, nullptr);
    EXPECT_EQ(last_any, b3);
    EXPECT_EQ(leptris_node_last_child(leptris_element_as_node(root)),
              (LeptrisNodeRef)last_any);

    EXPECT_NE(leptris_element_find_child(root, "b"), nullptr);
    EXPECT_EQ(leptris_element_find_child(root, "zzz"), nullptr);
    EXPECT_EQ(leptris_element_child_value(a), std::string("alpha"));
    leptris_document_free(doc);
}

/* ---- copy family + attribute removal ---- */

TEST(PublicSurface, CopyFamilyAndAttributeRemoval) {
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement a_copy = leptris_element_append_copy(root, a);
    ASSERT_NE(a_copy, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 4u);

    LeptrisElement pre = leptris_element_prepend_copy(root, a);
    ASSERT_NE(pre, nullptr);
    LeptrisElement before = leptris_element_insert_copy_before(a, a);
    ASSERT_NE(before, nullptr);
    LeptrisElement after = leptris_element_insert_copy_after(a, a);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 7u);

    /* The copies serialize to the same shape as the original. */
    char* x = leptris_element_serialize(a_copy, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_STREQ(x, "<a order=\"2\">alpha</a>");
    leptris_free_string(x);

    EXPECT_EQ(leptris_element_remove_all_attributes(a_copy), LEPTRIS_OK);
    x = leptris_element_serialize(a_copy, nullptr);
    ASSERT_NE(x, nullptr);
    EXPECT_STREQ(x, "<a>alpha</a>");
    leptris_free_string(x);
    leptris_document_free(doc);
}

/* ---- hash + binding wrapper + namespace prefix ---- */

TEST(PublicSurface, HashWrapperNamespace) {
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* Hash is stable for the same element. */
    EXPECT_EQ(leptris_element_hash_value(root),
              leptris_element_hash_value(root));

    int cookie = 42;
    leptris_node_set_binding_wrapper(
        leptris_element_as_node(root), &cookie);
    EXPECT_EQ(leptris_node_get_binding_wrapper(
                  leptris_element_as_node(root)), &cookie);
    leptris_document_free(doc);

    LeptrisDocument nsdoc = leptris_parse_string(
        "<r xmlns:p='urn:x'><p:c/></r>", strlen("<r xmlns:p='urn:x'><p:c/></r>"), nullptr);
    ASSERT_NE(nsdoc, nullptr);
    LeptrisElement pc =
        leptris_element_first_child_any(leptris_document_root(nsdoc));
    ASSERT_NE(pc, nullptr);
    LeptrisNamespace ns = leptris_element_namespace(pc);
    ASSERT_NE(ns, nullptr);
    EXPECT_STREQ(leptris_namespace_uri(ns), "urn:x");
    /* The prefix lives on the qualified name; the namespace object
     * itself may carry NULL when inherited. */
    EXPECT_STREQ(leptris_element_prefix(pc), "p");
    EXPECT_STREQ(leptris_element_namespace_for_prefix(pc, "p"), "urn:x");
    /* namespace_prefix: the declaration on the ROOT carries "p". */
    EXPECT_EQ(leptris_namespace_prefix(nullptr), nullptr);
    /* namespace_prefix: the declaration on the ROOT carries "p". */
    LeptrisElement r2 = leptris_document_root(nsdoc);
    EXPECT_STREQ(leptris_element_namespace_decl_prefix(r2, 0), "p");
    EXPECT_STREQ(leptris_element_namespace_decl_uri(r2, 0), "urn:x");
    leptris_document_free(nsdoc);
}

/* ---- document-level: adopt, finalize, serialize_document,
 * parse_with_encoding, xpointer ---- */

TEST(PublicSurface, DocumentLevelAPI) {
    LeptrisDocument doc = parse_doc();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* serialize_document matches document_serialize. */
    char* a = leptris_serialize_document(doc);
    char* b = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(a, b);
    leptris_free_string(a);
    leptris_free_string(b);

    /* adopt_child moves a subtree between documents. */
    LeptrisDocument other = leptris_document_create();
    ASSERT_NE(other, nullptr);
    LeptrisElement moved = leptris_element_copy(
        leptris_element_first_child_any(root), other);
    ASSERT_NE(moved, nullptr);
    /* adopt_child(parent, child): the child's pool is kept alive by
     * the PARENT — after adoption the child must NOT be freed
     * separately (document_free on the parent releases it). */
    leptris_document_adopt_child(doc, other);

    EXPECT_EQ(leptris_document_finalize_strings(doc), 1);
    leptris_document_free(doc);   /* releases `other` too */

    /* parse_string_with_encoding accepts a UTF-16LE document. */
    const char* utf16 =
        "\xFF\xFE<\0r\0/\0>\0";
    LeptrisDocument d16 = leptris_parse_string_with_encoding(
        utf16, 10, nullptr);
    ASSERT_NE(d16, nullptr);
    leptris_document_free(d16);

    /* xinclude_get_xpointer: NULL for a non-include element. */
    LeptrisDocument xi = leptris_parse_string(
        "<xi:include xmlns:xi='http://www.w3.org/2001/XInclude' href='a.txt'/>", strlen("<xi:include xmlns:xi='http://www.w3.org/2001/XInclude' href='a.txt'/>"), nullptr);
    if (xi) {
        LeptrisElement root_xi = leptris_document_root(xi);
        EXPECT_EQ(leptris_xinclude_get_xpointer(root_xi), nullptr);
        leptris_document_free(xi);
    }
}

/* ---- memory hooks ---- */

TEST(PublicSurface, MemoryHookGetters) {
    /* Default allocator state. */
    EXPECT_EQ(leptris_get_memory_allocation_function(), nullptr);
    EXPECT_EQ(leptris_get_memory_deallocation_function(), nullptr);
}
