#include <gtest/gtest.h>
#include <leptris.h>
#include <cstring>
#include <string>

extern "C" struct leptris_document* leptris_root_doc_lookup(
    LeptrisElement root);

/* #1038: root-doc map entries must die with their document. The
 * pool-fallback create path (names > 254 bytes, carve failure)
 * registers the DETACHED element, and adopted-child frees null
 * new_dom_root before recursing — pre-fix those entries outlived
 * the doc, and a malloc-recycled element address later resolved
 * the FREED doc through the stale entry: roaming heap corruption
 * in every downstream binding suite (~5% of runs, v1.9.151-155). */
extern "C" size_t leptris_root_doc_unregister_doc(
    struct leptris_document* doc);
extern "C" void leptris_root_doc_register(LeptrisElement,
                                          struct leptris_document*);

TEST(RootDocMapLifecycle, FallbackEntryDiesWithDocument) {
    const std::string long_name(300, 'n');  /* > 254: pool fallback */
    LeptrisDocument d = leptris_document_create();
    ASSERT_TRUE(d != NULL);
    LeptrisElement e = leptris_element_create(d, long_name.c_str());
    ASSERT_TRUE(e != NULL);
    /* Registered (fallback) and resolvable while the doc lives. */
    EXPECT_TRUE(leptris_root_doc_lookup(e) != NULL);
    /* The document-death sweep (what document_free runs) removes
     * the fallback registration while the element storage is
     * still alive — no freed-memory reads, deterministic. */
    EXPECT_EQ(leptris_root_doc_unregister_doc(d), 1u);
    EXPECT_EQ(leptris_root_doc_lookup(e), nullptr);
    leptris_document_free(d);
}

TEST(RootDocMapLifecycle, RecycleLoopSurvives) {
    const std::string long_name(300, 'm');
    for (int rep = 0; rep < 4000; rep++) {
        LeptrisDocument d = leptris_document_create();
        if (!d) break;
        LeptrisElement root = leptris_element_create(d, "r");
        leptris_document_set_root(d, root);
        for (int k = 0; k < 4; k++) {
            /* Detached fallback-registrations: leaked pre-fix. */
            (void)leptris_element_create(d, long_name.c_str());
        }
        char* s = leptris_document_serialize(d, NULL);
        if (s) leptris_free_string(s);
        leptris_document_free(d);
    }
    SUCCEED();
}

/* The doc-entry chain successor to the bucket sweep: each document's
 * map entries are singly linked off doc->map_entries. The hazard is
 * CHAIN ALIASING — one doc's sweep recycling another doc's live
 * entries (free-list reuse + stale doc_next). Falsifies: free A, and
 * B's registrations must all still resolve to B. */
TEST(RootDocMapLifecycle, ChainSweepNeverTouchesOtherDocsEntries) {
    const std::string long_name(300, 'x');
    LeptrisDocument a = leptris_document_create();
    LeptrisDocument b = leptris_document_create();
    ASSERT_TRUE(a != NULL && b != NULL);

    LeptrisElement a_root = leptris_element_create(a, "ra");
    leptris_document_set_root(a, a_root);
    LeptrisElement b_root = leptris_element_create(b, "rb");
    leptris_document_set_root(b, b_root);
    /* Interleave fallback (detached) registrations from BOTH docs so
     * their free-list recycles collide across the frees. */
    LeptrisElement a_det = leptris_element_create(a, long_name.c_str());
    LeptrisElement b_det = leptris_element_create(b, long_name.c_str());
    ASSERT_TRUE(a_det != NULL && b_det != NULL);
    EXPECT_EQ(leptris_root_doc_lookup(a_det), a);
    EXPECT_EQ(leptris_root_doc_lookup(b_det), b);

    leptris_document_free(a);  /* a's chain dies; entries recycle */

    /* b's registrations survive a's free exactly. */
    EXPECT_EQ(leptris_root_doc_lookup(b_det), b);
    EXPECT_EQ(leptris_root_doc_lookup(b_root), b);

    leptris_document_free(b);
    EXPECT_EQ(leptris_root_doc_lookup(b_det), nullptr);
    EXPECT_EQ(leptris_root_doc_lookup(b_root), nullptr);
}

/* Re-registering the same element under a DIFFERENT document must
 * move the chain entry — otherwise the old doc's sweep recycles an
 * entry that is live for the new doc. */
TEST(RootDocMapLifecycle, ReregisterUnderNewDocMovesChainEntry) {
    const std::string long_name(300, 'y');
    LeptrisDocument a = leptris_document_create();
    LeptrisDocument b = leptris_document_create();
    ASSERT_TRUE(a != NULL && b != NULL);

    LeptrisElement e = leptris_element_create(a, long_name.c_str());
    ASSERT_TRUE(e != NULL);
    EXPECT_EQ(leptris_root_doc_lookup(e), a);

    /* Same element address, new doc — the register-update path. */
    leptris_root_doc_register(e, b);
    EXPECT_EQ(leptris_root_doc_lookup(e), b);

    leptris_document_free(a);  /* a's sweep must NOT recycle e's entry */
    EXPECT_EQ(leptris_root_doc_lookup(e), b);

    leptris_document_free(b);
    EXPECT_EQ(leptris_root_doc_lookup(e), nullptr);
}
