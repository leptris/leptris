// test/dom/test_compact.cpp — compact allocator / overflow table specs (TODO 39).
//
// The compact allocator is the 4-byte-pointer optimization used by the
// DOM.  When a pointer can't be encoded into 4 bytes (e.g., the
// address is far from the pool's base), an overflow entry is added to
// a thread-local table.  These specs exercise the table's lifecycle
// and verify cleanup happens per-document.

#include <gtest/gtest.h>

#include "taurus.h"

extern "C" {
#include "compact.h"
}

#include <cstring>

namespace {

TEST(CompactAllocator, ParsesMultipleDocumentsWithoutLeak) {
    /* The overflow table is reused across documents within a thread.
     * Each document free must clean up its own entries. */
    const char xml[] = "<r><a/><b/><c/></r>";

    for (int i = 0; i < 5; i++) {
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
        ASSERT_NE(doc, nullptr) << "iter " << i;
        taurus_document_free(doc);
    }
    /* Under leaks --atExit --, zero bytes leaked is the contract. */
}

TEST(CompactAllocator, ExplicitCleanupDoesNotCrash) {
    /* taurus_explicit_cleanup tears down the thread-local overflow
     * table.  Calling it when no documents are active must be safe. */
    taurus_explicit_cleanup();
    taurus_explicit_cleanup();  /* idempotent */
    SUCCEED();
}

TEST(CompactAllocator, LargeDocumentDoesNotLeakOverflowEntries) {
    /* A document large enough to trigger many overflow entries must
     * release them all on free. */
    std::string xml = "<r>";
    for (int i = 0; i < 1000; i++) {
        xml += "<a>text</a>";
    }
    xml += "</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    taurus_document_free(doc);
    /* Zero leaks under valgrind/leaks. */
}

}  // namespace
