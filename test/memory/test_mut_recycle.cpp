#include <gtest/gtest.h>

#include "leptris.h"

#include "../leptris/leptris_internal.h"
#include "../leptris/dom/mut_recycle.h"

#include <cstddef>

namespace {

/* Builds one doc-cycle: create doc, carve mutation elements + names,
 * free the doc. This is the shape that parks blocks on the TLS
 * free-lists when leptris_document_free walks mut_*_blocks. */
void run_doc_cycle(int elements) {
    LeptrisDocument doc = leptris_document_create();
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_element_create(doc, "root");
    ASSERT_NE(root, nullptr);
    for (int i = 0; i < elements; i++) {
        LeptrisElement child = leptris_element_create(doc, "child");
        ASSERT_NE(child, nullptr);
        ASSERT_EQ(leptris_element_append_child(root, child), LEPTRIS_OK);
    }
    leptris_document_free(doc);
}

}  // namespace

/* Parking: after a doc-cycle, freed mutation blocks must be cached on
 * this thread (the whole point of the recycle — no free() back to
 * malloc). Falsified if leptris_document_free frees blocks instead of
 * parking them. */
TEST(MutRecycle, ParksBlocksAfterDocCycle) {
    leptris_explicit_cleanup();
    run_doc_cycle(4);
    EXPECT_GT(leptris_mut_recycle_cached_bytes(), 0u);
    leptris_explicit_cleanup();
}

/* Drain: leptris_explicit_cleanup (public, opt-in) must free every
 * parked block — the LSan-clean escape hatch for embedders and tests.
 * Falsified if the lists keep blocks (counter > 0) after the call. */
TEST(MutRecycle, ExplicitCleanupDrainsAllKinds) {
    leptris_explicit_cleanup();
    run_doc_cycle(4);
    ASSERT_GT(leptris_mut_recycle_cached_bytes(), 0u);
    leptris_explicit_cleanup();
    EXPECT_EQ(leptris_mut_recycle_cached_bytes(), 0u);
}

/* Reuse: a second doc-cycle after parking must come back from the
 * free-lists, not allocate anew — cached bytes stay bounded across
 * many cycles. 250 cycles × 2+ elem blocks each would park > 20MB if
 * nothing were reused; the caps hold the steady state at ~16MB. */
TEST(MutRecycle, SteadyStateRespectsByteCaps) {
    leptris_explicit_cleanup();
    for (int cycle = 0; cycle < 250; cycle++) run_doc_cycle(1024);
    size_t cached = leptris_mut_recycle_cached_bytes();
    EXPECT_GT(cached, 0u);
    EXPECT_LE(cached, 16u * 1024 * 1024);
    leptris_explicit_cleanup();
    EXPECT_EQ(leptris_mut_recycle_cached_bytes(), 0u);
}
