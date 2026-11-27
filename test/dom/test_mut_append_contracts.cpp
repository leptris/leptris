// test/dom/test_mut_append_contracts.cpp — #2132 mutcycle: append and
// element-modify contracts that the document-resolution hoist and the
// assign-instead-of-clear carve must preserve.
//
// The mutcycle row (bench_append_attr_pugixml) spends its time in two
// places this engine is allowed to make cheaper only if nothing
// observable moves. These specs pin the observable surface:
//
//   MutAppend.*       — leptris_element_append_child semantics: fresh
//                       create-then-append splices, interleaved parents
//                       (the tail-cache collision shape), same-doc
//                       moves, #1528 foreign adoption, #1636 cycle
//                       rejection, and get_document across every tree
//                       shape the append path resolves.
//   MutCarve.*        — mutation-block recycling: a carved element must
//                       read FULLY INITIALIZED even when its 44 bytes
//                       recycle a previous document's element storage
//                       (name, counts, edges, flags-driven doc
//                       resolution, serialization). This is the
//                       contract that lets the carve skip its bulk
//                       zeroing: the create path assigns every field.
//   MutCreateChild.*  — the fused create+append face stays
//                       indistinguishable from the two-call pair.
//
// Fresh-create defaults are load-bearing for the hoist: the fused
// append fast path is proven by (mut-block extent) + (childless) +
// (parentless); the parentless leg was added with #2132 after the
// move-of-fresh-child shape showed the old two-leg gate splicing an
// attached child under a second parent while the first parent still
// counted it (double parentage — the non-fresh move path already
// defined the contract: MOVE).

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "leptris.h"

extern "C" {
#include "element.h" /* leptris_element_get_document — the resolve under test */
}

namespace {

// ---------------------------------------------------------------------------
// Append contracts
// ---------------------------------------------------------------------------

// The bench shape: many fresh create-then-append pairs under one root.
// Order, parent links, counts, and serialization must all read back.
TEST(MutAppend, FreshChildChainPreservesOrderAndLinks) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const int kN = 300;
    for (int i = 0; i < kN; i++) {
        LeptrisElement c = leptris_element_create(d, "c");
        ASSERT_NE(c, nullptr);
        ASSERT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);
    }
    ASSERT_EQ(leptris_element_child_count(root), (size_t)kN);
    int seen = 0;
    for (LeptrisElement c = leptris_element_first_child_any(root); c;
         c = leptris_element_next_sibling_any(c), seen++) {
        EXPECT_STREQ(leptris_element_name(c), "c");
        EXPECT_EQ(leptris_element_parent(c), root);
        EXPECT_EQ(leptris_element_get_document(c), d);
    }
    EXPECT_EQ(seen, kN);
    EXPECT_STREQ(leptris_element_name(root), "root");

    char* xml = leptris_document_serialize(d, NULL);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "<c"), nullptr);
    leptris_free_string(xml);
    leptris_document_free(d);
}

// Two parents appended to alternately: the per-parent tail caches live
// in the same document table (keyed on parent address bits), so this
// shape exercises cache collisions. Every child must land under the
// parent it was appended to, in append order (#682 class).
TEST(MutAppend, InterleavedParentsKeepTheirOwnChildrenInOrder) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    LeptrisElement p1 = leptris_element_create(d, "p1");
    LeptrisElement p2 = leptris_element_create(d, "p2");
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, p1), LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(root, p2), LEPTRIS_OK);

    const int kN = 50;
    for (int i = 0; i < kN; i++) {
        std::string a = "a" + std::to_string(i);
        std::string b = "b" + std::to_string(i);
        LeptrisElement ea = leptris_element_create(d, a.c_str());
        LeptrisElement eb = leptris_element_create(d, b.c_str());
        ASSERT_NE(ea, nullptr);
        ASSERT_NE(eb, nullptr);
        ASSERT_EQ(leptris_element_append_child(p1, ea), LEPTRIS_OK);
        ASSERT_EQ(leptris_element_append_child(p2, eb), LEPTRIS_OK);
    }
    EXPECT_EQ(leptris_element_child_count(p1), (size_t)kN);
    EXPECT_EQ(leptris_element_child_count(p2), (size_t)kN);
    for (int which = 1; which <= 2; which++) {
        LeptrisElement p = (which == 1) ? p1 : p2;
        int i = 0;
        for (LeptrisElement c = leptris_element_first_child_any(p); c;
             c = leptris_element_next_sibling_any(c), i++) {
            std::string want =
                (which == 1 ? "a" : "b") + std::to_string(i);
            EXPECT_STREQ(leptris_element_name(c), want.c_str());
            EXPECT_EQ(leptris_element_parent(c), p);
        }
        EXPECT_EQ(i, kN);
    }
    leptris_document_free(d);
}

// A same-doc append of a child that ALREADY has a parent is a MOVE:
// the old parent loses it, the new parent gains it, exactly once.
// The non-fresh path defined this contract; the fresh fast path must
// not deviate (the double-parentage regression this pins showed up as
// old-parent child_count still counting the moved child).
TEST(MutAppend, SameDocAppendOfAttachedChildMoves) {
    // Case A: the child has its own subtree (never fresh by extent+
    // childless, so this shape rode the general path all along).
    {
        LeptrisDocument d = leptris_document_create();
        ASSERT_NE(d, nullptr);
        LeptrisElement root1 = leptris_element_create(d, "r1");
        LeptrisElement root2 = leptris_element_create(d, "r2");
        ASSERT_EQ(leptris_document_set_root(d, root1), LEPTRIS_OK);
        LeptrisElement c = leptris_element_create(d, "c");
        LeptrisElement g = leptris_element_create(d, "g");
        ASSERT_EQ(leptris_element_append_child(c, g), LEPTRIS_OK);
        ASSERT_EQ(leptris_element_append_child(root1, c), LEPTRIS_OK);
        ASSERT_EQ(leptris_element_child_count(root1), 1u);
        ASSERT_EQ(leptris_element_append_child(root2, c), LEPTRIS_OK);
        EXPECT_EQ(leptris_element_child_count(root1), 0u);
        EXPECT_EQ(leptris_element_child_count(root2), 1u);
        EXPECT_EQ(leptris_element_parent(c), root2);
        EXPECT_STREQ(
            leptris_element_name(leptris_element_first_child_any(root2)),
            "c");
        EXPECT_EQ(leptris_element_first_child_any(root1), nullptr);
        leptris_document_free(d);
    }
    // Case B: the child is CHILDLESS and still inside the current mut
    // block (fresh by extent) but attached — the fast-path gate must
    // still prove PARENTLESS before splicing, or the old parent keeps
    // a dangling edge to the moved child.
    {
        LeptrisDocument d = leptris_document_create();
        ASSERT_NE(d, nullptr);
        LeptrisElement root1 = leptris_element_create(d, "r1");
        LeptrisElement root2 = leptris_element_create(d, "r2");
        ASSERT_EQ(leptris_document_set_root(d, root1), LEPTRIS_OK);
        LeptrisElement c = leptris_element_create(d, "c");
        ASSERT_EQ(leptris_element_append_child(root1, c), LEPTRIS_OK);
        ASSERT_EQ(leptris_element_append_child(root2, c), LEPTRIS_OK);
        EXPECT_EQ(leptris_element_child_count(root1), 0u);
        EXPECT_EQ(leptris_element_child_count(root2), 1u);
        EXPECT_EQ(leptris_element_parent(c), root2);
        EXPECT_EQ(leptris_element_first_child_any(root1), nullptr);
        EXPECT_STREQ(
            leptris_element_name(leptris_element_first_child_any(root2)),
            "c");
        leptris_document_free(d);
    }
}

// #1528: a foreign document's node adopts by deep copy. The target
// tree keeps readable content after the source document dies, and the
// source's free is clean.
TEST(MutAppend, ForeignDocChildAdoptsByCopy) {
    LeptrisDocument d1 = leptris_document_create();
    LeptrisDocument d2 = leptris_document_create();
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);
    LeptrisElement r1 = leptris_element_create(d1, "r1");
    ASSERT_EQ(leptris_document_set_root(d1, r1), LEPTRIS_OK);
    LeptrisElement c2 = leptris_element_create(d2, "c2");
    ASSERT_EQ(leptris_element_append_child(r1, c2), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_child_count(r1), 1u);
    EXPECT_STREQ(
        leptris_element_name(leptris_element_first_child_any(r1)), "c2");
    leptris_document_free(d2);
    EXPECT_EQ(leptris_element_child_count(r1), 1u);
    EXPECT_STREQ(
        leptris_element_name(leptris_element_first_child_any(r1)), "c2");
    // the adopted copy resolves to the TARGET document, not the dead one
    EXPECT_EQ(leptris_element_get_document(
                  leptris_element_first_child_any(r1)),
              d1);
    leptris_document_free(d1);
}

// #1636: appending a node into its own descendant subtree (or itself)
// must be rejected, not spliced into a cycle.
TEST(MutAppend, SelfAppendAndAncestorCycleRejected) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_element_create(d, "r");
    ASSERT_EQ(leptris_document_set_root(d, r), LEPTRIS_OK);
    LeptrisElement c = leptris_element_create(d, "c");
    ASSERT_EQ(leptris_element_append_child(r, c), LEPTRIS_OK);
    LeptrisElement g = leptris_element_create(d, "g");
    ASSERT_EQ(leptris_element_append_child(c, g), LEPTRIS_OK);

    EXPECT_EQ(leptris_element_append_child(r, r), LEPTRIS_ERROR_INVALID_ARG);
    EXPECT_EQ(leptris_element_append_child(g, r), LEPTRIS_ERROR_INVALID_ARG);
    // the tree is intact after the rejections
    EXPECT_EQ(leptris_element_child_count(r), 1u);
    EXPECT_EQ(leptris_element_child_count(c), 1u);
    EXPECT_EQ(leptris_element_child_count(g), 0u);
    leptris_document_free(d);
}

// The document-resolution contract the hoist rides: every shape the
// append path resolves must keep answering with the owning document.
TEST(MutAppend, GetDocumentAcrossTreeShapes) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    // registered root
    EXPECT_EQ(leptris_element_get_document(root), d);
    // mid-tree element (attached, parent chain tops at the root)
    LeptrisElement mid = leptris_element_create(d, "mid");
    ASSERT_EQ(leptris_element_append_child(root, mid), LEPTRIS_OK);
    LeptrisElement leaf = leptris_element_create(d, "leaf");
    ASSERT_EQ(leptris_element_append_child(mid, leaf), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_get_document(mid), d);
    EXPECT_EQ(leptris_element_get_document(leaf), d);
    // UNATTACHED namebp element (#1189): created, never appended — its
    // create-time name slot is the authoritative document
    LeptrisElement detached = leptris_element_create(d, "det");
    ASSERT_NE(detached, nullptr);
    EXPECT_EQ(leptris_element_get_document(detached), d);
    // NULL out
    EXPECT_EQ(leptris_element_get_document(NULL),
              (struct leptris_document*)NULL);
    leptris_document_free(d);
}

// ---------------------------------------------------------------------------
// Carve contracts — recycled mutation blocks yield fully initialized
// elements (the assign-instead-of-clear load-bearing pin)
// ---------------------------------------------------------------------------

// Cross-document block recycling: each cycle allocates > 1024 elements
// (one full mut elem block plus spill), frees the document (the block
// parks in the thread recycle), and the next cycle's carves reuse the
// same storage. Every element of every cycle must read back correct:
// name, parentage, counts, and doc resolution — stale fields from the
// recycled storage would surface as wrong names, phantom parents,
// phantom attributes, or a wrong get_document.
TEST(MutCarve, RecycledBlocksYieldFullyInitializedElements) {
    const int kCycles = 12;
    const int kChildren = 1100; /* > MUT_ELEM_BLOCK_COUNT: force spill */
    for (int cycle = 0; cycle < kCycles; cycle++) {
        LeptrisDocument d = leptris_document_create();
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_element_create(d, "root");
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
        EXPECT_EQ(leptris_element_get_document(root), d);
        for (int i = 0; i < kChildren; i++) {
            LeptrisElement e = leptris_element_create(d, "c");
            ASSERT_NE(e, nullptr);
            ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
            // recycled storage must read initialized even mid-cycle
            ASSERT_EQ(leptris_element_get_document(e), d)
                << "cycle " << cycle << " child " << i;
            ASSERT_STREQ(leptris_element_name(e), "c");
            ASSERT_EQ(leptris_element_parent(e), root);
            ASSERT_EQ(leptris_element_child_count(e), 0u);
            ASSERT_EQ(leptris_element_first_child_any(e), nullptr);
        }
        EXPECT_EQ(leptris_element_child_count(root), (size_t)kChildren);
        leptris_document_free(d);
    }
    // The recycle layer is adaptive; after this many parked blocks a
    // drain keeps the process leak-check clean on CI.
    leptris_explicit_cleanup();
}

// Distinct-name recycling: the intern table inserts through the same
// carve; recycled name-block storage must not corrupt distinct names
// (each child gets a unique name, all must read back exactly).
TEST(MutCarve, RecycledCarveDistinctNamesReadBackExactly) {
    const int kCycles = 6;
    const int kChildren = 600;
    for (int cycle = 0; cycle < kCycles; cycle++) {
        LeptrisDocument d = leptris_document_create();
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_element_create(d, "root");
        ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
        for (int i = 0; i < kChildren; i++) {
            std::string nm = "n" + std::to_string(i);
            LeptrisElement e = leptris_element_create(d, nm.c_str());
            ASSERT_NE(e, nullptr);
            ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
            ASSERT_STREQ(leptris_element_name(e), nm.c_str())
                << "cycle " << cycle << " child " << i;
        }
        // full walk: every name present exactly once, in order
        int i = 0;
        for (LeptrisElement c = leptris_element_first_child_any(root); c;
             c = leptris_element_next_sibling_any(c), i++) {
            std::string want = "n" + std::to_string(i);
            ASSERT_STREQ(leptris_element_name(c), want.c_str());
        }
        ASSERT_EQ(i, kChildren);
        leptris_document_free(d);
    }
    leptris_explicit_cleanup();
}

// Address reuse against a REGISTERED root: doc1's registered root dies,
// its mut-block address recycles into doc2's fresh element. The stale
// registration must not resolve the new element to the dead document
// (#1242/#1189 class), and the map's marked-flag on recycled storage
// must not corrupt register/unregister bookkeeping.
TEST(MutCarve, RecycledRegisteredRootAddressStaysSound) {
    for (int cycle = 0; cycle < 8; cycle++) {
        LeptrisDocument d1 = leptris_document_create();
        ASSERT_NE(d1, nullptr);
        LeptrisElement r1 = leptris_element_create(d1, "root");
        ASSERT_EQ(leptris_document_set_root(d1, r1), LEPTRIS_OK);
        LeptrisElement c1 = leptris_element_create(d1, "c");
        ASSERT_EQ(leptris_element_append_child(r1, c1), LEPTRIS_OK);
        LeptrisDocument d2 = leptris_document_create();
        ASSERT_NE(d2, nullptr);
        leptris_document_free(d1);
        // fresh elements in d2 recycle d1's storage (same allocator,
        // the parked blocks are thread-local): none may resolve to d1
        // and set_root/append/readback must work on all of them.
        LeptrisElement r2 = leptris_element_create(d2, "root");
        ASSERT_NE(r2, nullptr);
        ASSERT_EQ(leptris_document_set_root(d2, r2), LEPTRIS_OK);
        EXPECT_NE(leptris_element_get_document(r2),
                  (struct leptris_document*)NULL);
        for (int i = 0; i < 40; i++) {
            LeptrisElement e = leptris_element_create(d2, "c");
            ASSERT_NE(e, nullptr);
            ASSERT_EQ(leptris_element_append_child(r2, e), LEPTRIS_OK);
            ASSERT_EQ(leptris_element_get_document(e), d2)
                << "cycle " << cycle << " child " << i;
        }
        EXPECT_EQ(leptris_element_child_count(r2), 40u);
        leptris_document_free(d2);
    }
    leptris_explicit_cleanup();
}

// Fresh detached element defaults: everything the create path does not
// explicitly assign must read as its zero value. This is the contract
// the carve's assign-instead-of-clear must satisfy for storage that
// previously held a populated element.
TEST(MutCarve, FreshDetachedElementDefaultsAreClean) {
    // Populate a document first so the allocator hands back used storage.
    {
        LeptrisDocument warm = leptris_document_create();
        LeptrisElement wroot = leptris_element_create(warm, "w");
        leptris_document_set_root(warm, wroot);
        for (int i = 0; i < 64; i++) {
            LeptrisElement e = leptris_element_create(warm, "w");
            leptris_element_set_attribute(e, "k", "v");
            leptris_element_append_child(wroot, e);
        }
        leptris_document_free(warm);
    }
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    for (int i = 0; i < 64; i++) {
        LeptrisElement e = leptris_element_create(d, "e");
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(leptris_element_child_count(e), 0u);
        EXPECT_EQ(leptris_element_first_child_any(e), nullptr);
        EXPECT_EQ(leptris_element_parent(e), nullptr);
        EXPECT_STREQ(leptris_element_name(e), "e");
        EXPECT_EQ(leptris_element_attribute(e, "k"), nullptr);
        EXPECT_EQ(leptris_element_get_document(e), d);
        // serialization of a tree of fresh defaults is well-formed
    }
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    for (int i = 0; i < 16; i++) {
        LeptrisElement e = leptris_element_create(d, "e");
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    }
    char* xml = leptris_document_serialize(d, NULL);
    ASSERT_NE(xml, nullptr);
    const char* p = xml;
    int hits = 0;
    while ((p = strstr(p, "<e/>")) != NULL) { hits++; p++; }
    EXPECT_EQ(hits, 16);
    leptris_free_string(xml);
    leptris_document_free(d);
    leptris_explicit_cleanup();
}

// ---------------------------------------------------------------------------
// Fused create+append face
// ---------------------------------------------------------------------------

// create_child must build the identical tree the create+append pair
// builds (same order, links, doc resolution).
TEST(MutCreateChild, FusedFaceMatchesPairFace) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    for (int i = 0; i < 100; i++) {
        LeptrisElement e = leptris_element_create_child(root, "k");
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(leptris_element_parent(e), root);
        EXPECT_EQ(leptris_element_get_document(e), d);
    }
    EXPECT_EQ(leptris_element_child_count(root), 100u);
    int seen = 0;
    for (LeptrisElement c = leptris_element_first_child_any(root); c;
         c = leptris_element_next_sibling_any(c), seen++) {
        EXPECT_STREQ(leptris_element_name(c), "k");
    }
    EXPECT_EQ(seen, 100);
    // NULL/foreign-bail faces
    EXPECT_EQ(leptris_element_create_child(NULL, "x"), nullptr);
    EXPECT_EQ(leptris_element_create_child(root, NULL), nullptr);
    leptris_document_free(d);
}

} // namespace
