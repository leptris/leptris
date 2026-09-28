// test/dom/test_element_children.cpp — indexed child access specs,
// including the lane-18 round-13 iteration-cache invalidation guards.
#include <gtest/gtest.h>
#include <string>
#include "leptris.h"


TEST(ElementChildCache, MutationInvalidatesIterationSlot) {
    /* Lane-18 round 13: the ns_cache child-iteration slot must never
     * serve a node across a child-list mutation. */
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a/><b/><c/></r>", 19, &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisElement b = leptris_element_child(root, 1);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(std::string(leptris_element_name(b)), "b");

    /* Mutate before the cached position: indices shift. */
    LeptrisDocument fresh = leptris_document_create();
    LeptrisElement z = leptris_element_create(fresh, "z");
    ASSERT_NE(z, nullptr);
    ASSERT_EQ(leptris_element_prepend_child(root, z), LEPTRIS_OK);

    const char* names[] = {"z", "a", "b", "c"};
    for (int i = 0; i < 4; i++) {
        LeptrisElement c = leptris_element_child(root, (size_t)i);
        ASSERT_NE(c, nullptr) << "i=" << i;
        EXPECT_EQ(std::string(leptris_element_name(c)), names[i]) << "i=" << i;
    }
    leptris_document_free(fresh);
    leptris_document_free(doc);
}

TEST(ElementChildCache, RemoveChildInvalidatesIterationSlot) {
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a/><b/><c/></r>", 19, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisElement c = leptris_element_child(root, 2);
    ASSERT_NE(c, nullptr);

    LeptrisElement b = leptris_element_child(root, 1);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(leptris_element_remove_child(root, b), LEPTRIS_OK);

    LeptrisElement c2 = leptris_element_child(root, 1);
    ASSERT_NE(c2, nullptr);
    EXPECT_EQ(std::string(leptris_element_name(c2)), "c");
    EXPECT_EQ(leptris_element_child(root, 2), nullptr);
    leptris_document_free(doc);
}
