/* test/perf/test_stress_high_doc_count.cpp — Permanent stress test.
 *
 * Regression guard for issues #256, #261, #266: segfaults under
 * benchmark-ips with 15,000+ simultaneously-alive documents on 38 KB
 * inputs. The crash threshold was between 200 and 15,000 docs.
 *
 * This test parses 500 docs per batch × 10 rounds = 5,000 total,
 * verifying child_count on each doc (tree integrity check) before
 * freeing the batch. This catches:
 * - Compact-pointer offset corruption (wrong child_count)
 * - Overflow-table cross-contamination (SEGV on tree access)
 * - Pool-allocator reuse issues (use-after-free on free+reparse)
 *
 * Runs in ~5 seconds on CI hardware (vs 30+ seconds for 15,000). */
#include <gtest/gtest.h>
#include <taurus.h>
#include <string>
#include <vector>

namespace {

std::string make_38kb_xml() {
    std::string xml = "<catalog>";
    for (int i = 1; i <= 1000; i++) {
        xml += "<item id='" + std::to_string(i) + "' cat='" +
               std::to_string(i % 10) + "'>text " +
               std::to_string(i) + "</item>";
    }
    xml += "</catalog>";
    return xml;
}

TEST(HighDocCountStress, ParseVerifyFree5000Docs) {
    std::string xml = make_38kb_xml();
    ASSERT_GT(xml.size(), 30000u);

    const int BATCH = 500;
    const int ROUNDS = 10;
    std::vector<TaurusDocument> docs(BATCH);

    for (int round = 0; round < ROUNDS; round++) {
        /* Parse a batch */
        for (int i = 0; i < BATCH; i++) {
            TaurusStatus st = TAURUS_OK;
            docs[i] = taurus_parse_string(xml.data(), xml.size(), &st);
            ASSERT_NE(docs[i], nullptr)
                << "parse failed at round " << round << " doc " << i
                << " status=" << st;
        }
        /* Verify tree integrity on each doc */
        for (int i = 0; i < BATCH; i++) {
            TaurusElement root = taurus_document_root(docs[i]);
            ASSERT_NE(root, nullptr);
            size_t cc = taurus_element_child_count(root);
            EXPECT_EQ(cc, 1000u)
                << "child_count corruption at round " << round
                << " doc " << i;
        }
        /* Free the batch */
        for (int i = 0; i < BATCH; i++) {
            taurus_document_free(docs[i]);
        }
    }
    SUCCEED() << ROUNDS * BATCH << " docs parsed, verified, freed";
}

}  // namespace
