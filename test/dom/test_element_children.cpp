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

TEST(ElementChildCache, AttrFreeParentLazyCacheStaysMutationSafe) {
    /* Lane-18 round 15: attribute-free elements carry no ns_cache
     * from the parser (the cache exists only when the parse stamped
     * xmlns/prefix state), so the round-13 resume slot never engaged
     * for them and every indexed read re-walked from the first
     * child. child() now materializes the cache lazily. The mutation
     * contract must hold for that lazily-created cache exactly as it
     * does for parse-created ones. */
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a/><b/><c/></r>", 19, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_attribute(root, "id"), nullptr);

    /* Warm the iteration slot through the lazy path. */
    for (int i = 0; i < 3; i++) {
        LeptrisElement c = leptris_element_child(root, (size_t)i);
        ASSERT_NE(c, nullptr) << "i=" << i;
    }

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

    LeptrisElement b = leptris_element_child(root, 2);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(leptris_element_remove_child(root, b), LEPTRIS_OK);
    LeptrisElement c = leptris_element_child(root, 2);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(std::string(leptris_element_name(c)), "c");
    EXPECT_EQ(leptris_element_child(root, 3), nullptr);
    leptris_document_free(fresh);
    leptris_document_free(doc);
}

TEST(ElementRemove, RemoveChildLeavesCleanOrphan) {
    /* leptris-ruby #370: remove_child unlinked the child from the
     * parent's list but left the child's OWN next_sibling pointing
     * at its former following sibling. A later append then spliced
     * that stale run into the new parent — the re-appended element
     * dragged siblings it no longer owned, and when the run led
     * back into the destination subtree the tree contained itself
     * (non-terminating serialize / SystemStackError upstream).
     * leptris_node_unlink and leptris_document_remove_child both
     * clear the removed node's own sibling link; remove_child must
     * leave a clean orphan the same way. */
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a/><b/><c/></r>", 19, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisElement b = leptris_element_child(root, 1);
    ASSERT_NE(b, nullptr);

    ASSERT_EQ(leptris_element_remove_child(root, b), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_next_sibling_any(b), nullptr);

    /* Re-append elsewhere: the destination adopts exactly b — the
     * stale chain must not drag c along (children() walks the real
     * chain, not the counter). */
    LeptrisElement dest = leptris_element_create(doc, "dest");
    ASSERT_NE(dest, nullptr);
    ASSERT_EQ(leptris_element_append_child(dest, b), LEPTRIS_OK);

    LeptrisElement got[4] = {nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(leptris_element_children(dest, got, 4), 1u);
    EXPECT_EQ(std::string(leptris_element_name(got[0])), "b");

    /* The old parent kept the rest, in order. */
    LeptrisElement kept[4] = {nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(leptris_element_children(root, kept, 4), 2u);
    EXPECT_EQ(std::string(leptris_element_name(kept[0])), "a");
    EXPECT_EQ(std::string(leptris_element_name(kept[1])), "c");
    leptris_document_free(doc);
}

TEST(ElementRemove, RemoveAllChildrenLeavesCleanOrphans) {
    /* Same invariant for the bulk path: remove_all_children cleared
     * parent backpointers (#1220) but left the sibling chain intact,
     * so every removed child still pointed at its former run and a
     * later append of any one of them spliced the rest in. */
    LeptrisStatus st;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a/><b/><c/></r>", 19, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    LeptrisElement a = leptris_element_child(root, 0);
    LeptrisElement b = leptris_element_child(root, 1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    ASSERT_EQ(leptris_element_remove_children(root), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_next_sibling_any(a), nullptr);
    EXPECT_EQ(leptris_element_next_sibling_any(b), nullptr);

    LeptrisElement dest = leptris_element_create(doc, "dest");
    ASSERT_NE(dest, nullptr);
    ASSERT_EQ(leptris_element_append_child(dest, b), LEPTRIS_OK);

    LeptrisElement got[4] = {nullptr, nullptr, nullptr, nullptr};
    EXPECT_EQ(leptris_element_children(dest, got, 4), 1u);
    EXPECT_EQ(std::string(leptris_element_name(got[0])), "b");
    leptris_document_free(doc);
}
