/* test/sax/test_pull_batch.cpp — batched pull delivery (issues
 * #589/#562).
 *
 * The pull cursor costs one FFI dispatch per event (~1.7 µs each
 * through Ruby — streaming measured 145x slower than a DOM parse of
 * the same bytes). leptris_pull_next_batch drains up to N events per
 * call into a caller array; strings live in a staged arena valid
 * until the NEXT batch/cursor call. leptris_pull_attrs collapses the
 * 1+2N attribute round-trips to a count + one flat copy. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {

static const char kDoc[] =
    "<?xml version=\"1.0\"?><lib><book id='1' lang='en'>A<title>T</title>"
    "</book><book id='2'>B</book></lib>";

TEST(PullBatch, DrainsWholeDocumentInOneCall) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    ASSERT_NE(p, nullptr);
    std::vector<LeptrisPullEvent> evs(64);
    size_t n = leptris_pull_next_batch(p, evs.data(), evs.size());
    /* start lib, start book, text A, start title, text T, end title,
     * end book, start book2, text B, end book2, end lib, end doc */
    EXPECT_EQ(n, 12u);
    EXPECT_EQ(evs[0].type, LEPTRIS_PULL_START_ELEMENT);
    EXPECT_STREQ(evs[0].name, "lib");
    EXPECT_EQ(evs[2].type, LEPTRIS_PULL_TEXT);
    EXPECT_STREQ(evs[2].text, "A");
    EXPECT_EQ(evs[10].type, LEPTRIS_PULL_END_ELEMENT);
    EXPECT_EQ(evs[11].type, LEPTRIS_PULL_END_DOCUMENT);
    EXPECT_EQ(leptris_pull_next_batch(p, evs.data(), evs.size()), 0u);
    leptris_pull_free(p);
}

TEST(PullBatch, StringsStayValidAcrossTheWholeBatch) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    ASSERT_NE(p, nullptr);
    std::vector<LeptrisPullEvent> evs(64);
    size_t n = leptris_pull_next_batch(p, evs.data(), evs.size());
    ASSERT_EQ(n, 12u);
    /* every string of every staged event is readable AFTER the call */
    for (size_t i = 0; i < n; i++) {
        if (evs[i].name) EXPECT_GT(strlen(evs[i].name), 0u) << i;
    }
    EXPECT_STREQ(evs[3].name, "title");
    leptris_pull_free(p);
}

TEST(PullBatch, ChunkedBatchesMatchTheCursorStream) {
    /* Equivalence fence: batches of 3 deliver the same logical
     * stream as one-event-at-a-time pull. */
    std::vector<int> cursor;
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    const LeptrisPullEvent* e;
    while ((e = leptris_pull_next(p)) != nullptr) cursor.push_back(e->type);
    leptris_pull_free(p);

    std::vector<int> batched;
    p = leptris_pull_new(kDoc, strlen(kDoc));
    LeptrisPullEvent evs[3];
    size_t n;
    while ((n = leptris_pull_next_batch(p, evs, 3)) > 0)
        for (size_t i = 0; i < n; i++) batched.push_back(evs[i].type);
    leptris_pull_free(p);

    EXPECT_EQ(batched, cursor);
}

TEST(PullBatch, AttrAccessorsServeTheLastBatchedEvent) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    ASSERT_NE(p, nullptr);
    LeptrisPullEvent evs[12];
    size_t n = leptris_pull_next_batch(p, evs, 12);
    ASSERT_EQ(n, 12u);
    /* the batch ended past the first book's attrs — most recent
     * START element is the second book (id=2). Attr accessors serve
     * only the most recent event (the documented contract). */
    EXPECT_EQ(leptris_pull_attr_count(p), 1u);
    EXPECT_STREQ(leptris_pull_attr_name(p, 0), "id");
    EXPECT_STREQ(leptris_pull_attr_value(p, 0), "2");
    leptris_pull_free(p);
}

TEST(PullBatch, BatchStopsOnStartElementForAttrs) {
    /* The useful binding shape: stop the batch ON a start element so
     * its attributes are the most-recent event. */
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    ASSERT_NE(p, nullptr);
    LeptrisPullEvent evs[2];
    size_t n = leptris_pull_next_batch(p, evs, 2);
    ASSERT_EQ(n, 2u);   /* start lib, start book */
    EXPECT_EQ(evs[1].type, LEPTRIS_PULL_START_ELEMENT);
    EXPECT_EQ(leptris_pull_attr_count(p), 2u);
    EXPECT_STREQ(leptris_pull_attr_name(p, 0), "id");
    EXPECT_STREQ(leptris_pull_attr_value(p, 0), "1");
    EXPECT_STREQ(leptris_pull_attr_name(p, 1), "lang");
    leptris_pull_free(p);
}

/* Issue #562: flat attribute fetch — count-only + one copy. */
TEST(PullAttrs, CountOnlyQuery) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    const LeptrisPullEvent* e = leptris_pull_next(p);   /* start lib */
    (void)e;
    e = leptris_pull_next(p);                            /* start book */
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(leptris_pull_attrs(p, nullptr, 0), 2u);
    leptris_pull_free(p);
}

TEST(PullAttrs, FlatCopyCarriesAllPairs) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    leptris_pull_next(p);
    const LeptrisPullEvent* e = leptris_pull_next(p);
    ASSERT_NE(e, nullptr);
    const char* flat[8] = {0};
    size_t pairs = leptris_pull_attrs(p, flat, 8);
    EXPECT_EQ(pairs, 2u);
    ASSERT_NE(flat[0], nullptr);
    EXPECT_STREQ(flat[0], "id");
    EXPECT_STREQ(flat[1], "1");
    EXPECT_STREQ(flat[2], "lang");
    EXPECT_STREQ(flat[3], "en");
    leptris_pull_free(p);
}

TEST(PullAttrs, SmallBufferTruncatesSafely) {
    LeptrisPullParser p = leptris_pull_new(kDoc, strlen(kDoc));
    leptris_pull_next(p);
    leptris_pull_next(p);
    const char* flat[4] = {0};
    size_t pairs = leptris_pull_attrs(p, flat, 1);   /* room for 1 pair */
    EXPECT_EQ(pairs, 2u);          /* total reported */
    EXPECT_STREQ(flat[0], "id");   /* first pair written */
    EXPECT_STREQ(flat[1], "1");
    EXPECT_EQ(flat[2], nullptr);   /* second pair untouched */
    leptris_pull_free(p);
}

TEST(PullBatch, FileSourceBatches) {
    std::string path = std::string(testing::TempDir()) +
                       "leptris_pull_batch.xml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("<r><a x='1'>t</a></r>", f);
    fclose(f);
    LeptrisPullParser p = leptris_pull_new_file(path.c_str());
    ASSERT_NE(p, nullptr);
    LeptrisPullEvent evs[16];
    size_t n = leptris_pull_next_batch(p, evs, 16);
    EXPECT_EQ(n, 6u);   /* start r, start a, text, end a, end r, doc end */
    leptris_pull_free(p);
    remove(path.c_str());
}

}  // namespace
