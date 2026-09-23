/* TODO.concurrency / leptris-ruby#321: a document freed on thread B
 * must leave NO reachable trace in thread A's root-doc map.
 *
 * The map was thread-local: when the freeing thread was not the
 * thread that registered the roots (Ruby GC finalizers run on
 * arbitrary threads), thread A's bucket kept {root -> freed doc}
 * forever — a later register/unregister walking the stale entry
 * dereferenced the freed document (the unregister+0x6c field
 * crash), and lookups handed the freed document to callers. The
 * memo carried the same hole: its generation counter was TLS, so a
 * foreign sweep's invalidation was invisible to the owner thread.
 *
 * These specs run the registering thread and the freeing thread on
 * different std::threads and pin the invariant with pointer-VALUE
 * probes only (safe after the free: map walks never dereference
 * element storage). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <atomic>
#include <thread>

extern "C" struct leptris_document* leptris_root_doc_lookup(
    LeptrisElement root);
extern "C" struct leptris_document* leptris_element_get_document(
    LeptrisElement elem);
extern "C" LeptrisElement leptris_root_doc_memo_root_for_tests(void);

namespace {

/* Runs `doc`'s free on a fresh thread while the registering (owner)
 * thread waits, then rejoins. */
void free_on_foreign_thread(LeptrisDocument doc) {
    std::atomic<int> step{0};
    std::thread freer([&step, doc]() {
        while (step.load(std::memory_order_acquire) < 1)
            std::this_thread::yield();
        leptris_document_free(doc);
        step.store(2, std::memory_order_release);
    });
    step.store(1, std::memory_order_release);
    while (step.load(std::memory_order_acquire) < 2)
        std::this_thread::yield();
    freer.join();
}

TEST(RootDocCrossThread, FreedDocIsUnreachableFromOwnerThread) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_TRUE(doc != nullptr);
    LeptrisElement root = leptris_element_create(doc, "root");
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);
    /* Resolve once so the owner's memo holds the pair, as any real
     * read path would have done before the hand-off. */
    ASSERT_EQ(leptris_element_get_document(root), doc);

    free_on_foreign_thread(doc);

    /* Pointer-VALUE probe. Pre-fix: the owner's stale entry resolves
     * the freed document (asserted live in the field: the probe
     * returned the freed doc from the owner thread). get_document is
     * NOT probed with the freed root — walking a freed element is
     * caller UB (the binding guards with freed? before any call);
     * the enforceable surface is the map's own state. */
    EXPECT_EQ(leptris_root_doc_lookup(root), nullptr);
    /* The memo must not keep trusting the pair: a foreign sweep
     * bumps the map generation, and that bump must be visible to
     * this thread (pre-fix the generation was TLS — invisible). */
    EXPECT_EQ(leptris_root_doc_memo_root_for_tests(), nullptr);
}

/* Same hand-off shape, but the owner thread keeps churning new
 * documents afterwards. Pre-fix, churn roots recycled the dead
 * root's address with a dirty header flag, the duplicate-check walk
 * matched the stale entry, and doc_chain_unlink dereferenced (and
 * rewired!) the freed document — silent corruption of whichever
 * allocation reused its struct. Post-fix the stale entry cannot
 * exist, so the churn must resolve every root to its own document. */
TEST(RootDocCrossThread, ChurnAfterForeignFreeResolvesOwnDocs) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_TRUE(doc != nullptr);
    LeptrisElement root = leptris_element_create(doc, "root");
    ASSERT_EQ(leptris_document_set_root(doc, root), LEPTRIS_OK);

    free_on_foreign_thread(doc);

    for (int i = 0; i < 20000; i++) {
        LeptrisDocument d = leptris_document_create();
        ASSERT_TRUE(d != nullptr);
        LeptrisElement a = leptris_element_create(d, "a");
        ASSERT_TRUE(a != nullptr);
        LeptrisElement r = leptris_element_create(d, "root");
        ASSERT_TRUE(r != nullptr);
        ASSERT_EQ(leptris_document_set_root(d, r), LEPTRIS_OK);
        /* Both created elements are registered; each must resolve
         * to ITS document — never to the freed one. */
        EXPECT_EQ(leptris_element_get_document(a), d);
        EXPECT_EQ(leptris_element_get_document(r), d);
        leptris_document_free(d);
    }
}

}  // namespace
