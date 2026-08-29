/* test/sax/test_recorder.cpp — the chunked SAX recorder (issue #585).
 *
 * Callback SAX through FFI costs a dispatch per event (~4 Ruby
 * frames + the ffi gem's generic callback machinery), which made
 * leptris SAX SLOWER than Nokogiri SAX. The recorder buffers
 * fixed-size event records plus a packed string arena C-side; the
 * host drains BOTH with one pair of calls per fed chunk, and slices
 * strings in host code — callback count becomes O(chunks), not
 * O(events). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <string>
#include <vector>

namespace {

/* Slice a record's string field out of the arena. */
static std::string sl(const LeptrisSaxEventRecord* r,
                      const char* arena, uint32_t off, uint32_t len) {
    return std::string(arena + off, len);
}

static std::string name_of(const LeptrisSaxEventRecord* r, const char* a) {
    return sl(r, a, r->name_off, r->name_len);
}
static std::string text_of(const LeptrisSaxEventRecord* r, const char* a) {
    return sl(r, a, r->text_off, r->text_len);
}

TEST(Recorder, RecordsEveryEventKindInOrder) {
    const char xml[] =
        "<r a=\"1\" b=\"two\">t"
        "<!-- c --><x><![CDATA[raw]]></x><?pi data?>"
        "</r>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    ASSERT_NE(rec, nullptr);
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, strlen(xml), 1), 0);

    size_t n = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    ASSERT_NE(rs, nullptr);
    ASSERT_NE(arena, nullptr);

    /* start doc, start r, characters t, comment, start x, cdata,
     * end x, pi, end r, end doc. */
    ASSERT_EQ(n, 10u);
    EXPECT_EQ(rs[0].kind, LEPTRIS_SAX_EVENT_START_DOCUMENT);
    EXPECT_EQ(rs[1].kind, LEPTRIS_SAX_EVENT_START_ELEMENT);
    EXPECT_EQ(name_of(&rs[1], arena), "r");
    EXPECT_EQ(rs[2].kind, LEPTRIS_SAX_EVENT_CHARACTERS);
    EXPECT_EQ(text_of(&rs[2], arena), "t");
    EXPECT_EQ(rs[3].kind, LEPTRIS_SAX_EVENT_COMMENT);
    EXPECT_EQ(text_of(&rs[3], arena), " c ");
    EXPECT_EQ(rs[4].kind, LEPTRIS_SAX_EVENT_START_ELEMENT);
    EXPECT_EQ(name_of(&rs[4], arena), "x");
    EXPECT_EQ(rs[5].kind, LEPTRIS_SAX_EVENT_CDATA);
    EXPECT_EQ(text_of(&rs[5], arena), "raw");
    EXPECT_EQ(rs[6].kind, LEPTRIS_SAX_EVENT_END_ELEMENT);
    EXPECT_EQ(name_of(&rs[6], arena), "x");
    EXPECT_EQ(rs[7].kind, LEPTRIS_SAX_EVENT_PI);
    EXPECT_EQ(name_of(&rs[7], arena), "pi");
    EXPECT_EQ(text_of(&rs[7], arena), "data");
    EXPECT_EQ(rs[8].kind, LEPTRIS_SAX_EVENT_END_ELEMENT);
    EXPECT_EQ(name_of(&rs[8], arena), "r");
    EXPECT_EQ(rs[9].kind, LEPTRIS_SAX_EVENT_END_DOCUMENT);
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, AttributesPackedInArena) {
    const char xml[] = "<e id=\"7\" lang=\"en\"/>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, strlen(xml), 1), 0);
    size_t n = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    ASSERT_GE(n, 1u);
    ASSERT_EQ(rs[1].kind, LEPTRIS_SAX_EVENT_START_ELEMENT);
    ASSERT_EQ(rs[1].attr_count, 2u);
    /* attrs: name\0value\0name\0value\0 packed at attrs_off. */
    const char* ap = arena + rs[1].attrs_off;
    EXPECT_STREQ(ap, "id");
    EXPECT_STREQ(ap + 3, "7");
    EXPECT_STREQ(ap + 5, "lang");
    EXPECT_STREQ(ap + 10, "en");
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, NamespaceMappingsRecorded) {
    const char xml[] =
        "<root xmlns:p=\"urn:p\" xmlns=\"urn:d\"><p:c/></root>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, strlen(xml), 1), 0);
    size_t n = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    /* START_PREFIX(p,urn:p), START_PREFIX("",urn:d), START root,
     * START p:c, END p:c, END root, END_PREFIX x2, END doc. */
    ASSERT_GE(n, 10u);
    EXPECT_EQ(rs[1].kind, LEPTRIS_SAX_EVENT_START_PREFIX);
    EXPECT_EQ(name_of(&rs[1], arena), "p");
    EXPECT_EQ(text_of(&rs[1], arena), "urn:p");
    EXPECT_EQ(rs[2].kind, LEPTRIS_SAX_EVENT_START_PREFIX);
    EXPECT_EQ(name_of(&rs[2], arena), "");
    EXPECT_EQ(text_of(&rs[2], arena), "urn:d");
    EXPECT_EQ(rs[3].kind, LEPTRIS_SAX_EVENT_START_ELEMENT);
    EXPECT_EQ(name_of(&rs[3], arena), "root");
    EXPECT_EQ(rs[4].kind, LEPTRIS_SAX_EVENT_START_ELEMENT);
    EXPECT_EQ(name_of(&rs[4], arena), "p:c");
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, EachFeedStartsAFreshChunk) {
    const char xml[] = "<a>1</a><b>2</b><c>3</c>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    /* three slices at element boundaries */
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, 8, 0), 0);
    size_t n1 = 0;
    leptris_sax_recorder_records(rec, &n1);
    ASSERT_EQ(n1, 4u);          /* start doc, start a, chars, end a */

    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml + 8, 8, 0), 0);
    size_t n2 = 0;
    leptris_sax_recorder_records(rec, &n2);
    EXPECT_EQ(n2, 3u);          /* fresh chunk — prior events dropped */

    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml + 16, 8, 1), 0);
    size_t n3 = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n3);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    EXPECT_EQ(n3, 4u);          /* start c, chars, end c, end doc */
    EXPECT_EQ(name_of(&rs[0], arena), "c");
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, ChunkBoundarySplitsMidToken) {
    const char xml[] = "<longname>text</longname>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    /* split inside the name and inside the text */
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, 5, 0), 0);
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml + 5, 8, 0), 0);
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml + 13, strlen(xml) - 13, 1), 0);
    size_t n = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    /* final chunk only: characters + end + end doc */
    ASSERT_GE(n, 3u);
    EXPECT_EQ(rs[0].kind, LEPTRIS_SAX_EVENT_CHARACTERS);
    EXPECT_EQ(text_of(&rs[0], arena), "t");
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, ErrorEventCarriesPosition) {
    const char xml[] = "<a><b></a></b>";
    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    EXPECT_EQ(leptris_sax_recorder_feed(rec, xml, strlen(xml), 1), -1);
    size_t n = 0, alen = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    const char* arena = leptris_sax_recorder_arena(rec, &alen);
    ASSERT_GE(n, 1u);
    const LeptrisSaxEventRecord* last = &rs[n - 1];
    EXPECT_EQ(last->kind, LEPTRIS_SAX_EVENT_ERROR);
    EXPECT_GT(strlen(text_of(last, arena).c_str()), 0u);
    leptris_sax_recorder_free(rec);
}

TEST(Recorder, MatchesCallbackSaxEventStream) {
    /* Equivalence fence: the recorder and the callback API must see
     * the same logical stream. */
    const char xml[] =
        "<?xml version=\"1.0\"?><r x=\"1\">hi<c/><!--m--></r>";
    struct Ctx { std::vector<int> kinds; } ctx;
    LeptrisSAXHandler h = {};
    h.start_document = [](void* ud) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_START_DOCUMENT); };
    h.start_element = [](void* ud, const char*, const char**) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_START_ELEMENT); };
    h.end_element = [](void* ud, const char*) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_END_ELEMENT); };
    h.characters = [](void* ud, const char*, size_t) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_CHARACTERS); };
    h.comment = [](void* ud, const char*) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_COMMENT); };
    h.end_document = [](void* ud) {
        ((Ctx*)ud)->kinds.push_back(LEPTRIS_SAX_EVENT_END_DOCUMENT); };
    ASSERT_EQ(leptris_sax_parse(xml, strlen(xml), &h, &ctx), 0);

    LeptrisSaxRecorder rec = leptris_sax_recorder_new();
    ASSERT_EQ(leptris_sax_recorder_feed(rec, xml, strlen(xml), 1), 0);
    size_t n = 0;
    const LeptrisSaxEventRecord* rs = leptris_sax_recorder_records(rec, &n);
    ASSERT_EQ(n, ctx.kinds.size());
    for (size_t i = 0; i < n; i++)
        EXPECT_EQ((int)rs[i].kind, ctx.kinds[i]) << "record " << i;
    leptris_sax_recorder_free(rec);
}

}  // namespace

/* Issue #594: one recorder across documents — a finalized recorder
 * cannot feed again, but reset gives a fresh parser state with the
 * record/arena capacity RETAINED (the amortization story for
 * one-document-per-parse hosts). */
TEST(Recorder, ResetRestartsAcrossDocuments) {
    LeptrisSaxRecorder r = leptris_sax_recorder_new();
    ASSERT_NE(r, nullptr);

    leptris_sax_recorder_feed(r, "<a x='1'>t</a>", 15, 1);
    size_t n1 = 0;
    const LeptrisSaxEventRecord* recs1 = leptris_sax_recorder_records(r, &n1);
    ASSERT_GT(n1, 0u);
    EXPECT_EQ(recs1[0].kind, LEPTRIS_SAX_EVENT_START_DOCUMENT);

    EXPECT_EQ(leptris_sax_recorder_reset(r), 0);
    leptris_sax_recorder_feed(r, "<b/>", 4, 1);
    size_t n2 = 0;
    leptris_sax_recorder_records(r, &n2);
    /* Fresh document: full event set again, starting over. */
    EXPECT_GT(n2, 0u);
    size_t starts = 0, ends = 0;
    const LeptrisSaxEventRecord* recs2 = leptris_sax_recorder_records(r, &n2);
    const char* arena = leptris_sax_recorder_arena(r, nullptr);
    for (size_t i = 0; i < n2; i++) {
        if (recs2[i].kind == LEPTRIS_SAX_EVENT_START_ELEMENT) {
            starts++;
            EXPECT_EQ(name_of(&recs2[i], arena), "b");
        }
        if (recs2[i].kind == LEPTRIS_SAX_EVENT_END_DOCUMENT) ends++;
    }
    EXPECT_EQ(starts, 1u);
    EXPECT_EQ(ends, 1u);

    leptris_sax_recorder_free(r);
}
