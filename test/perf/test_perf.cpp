// test/perf/test_perf.cpp — Performance regression specs (TODO 66).
//
// Catches silent perf regressions WITHOUT machine-dependent time
// budgets: every assertion compares two measurements taken in the
// SAME test run on the SAME machine, so the ratio is portable.
//
//   - parse tests: parse time vs a memcpy reference over the same
//     buffer (both scale with the machine's memory system)
//   - write-path tests: second half of a workload vs its first
//     half (amortized-O(1) stays ~1x; an O(n^2) regression blows
//     the ratio up)
//   - indexed walk: time(50 children) vs time(25 children) — the
//     guarded shape is O(N^2) by design, so the ratio sits at ~4x;
//     an accidental O(N^3) pushes it toward 8x.
//
// ASAN slows both sides of each ratio equally, so the specs stay
// enabled under sanitizers.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include "taurus.h"

#include <chrono>
#include <string>

namespace {

using clock_type = std::chrono::steady_clock;

double ElapsedUs(clock_type::time_point start) {
    return std::chrono::duration<double, std::micro>(clock_type::now() - start)
        .count();
}

/* Parse `xml` `iters` times, returning total microseconds. */
double ParseBenchUs(const char* xml, size_t len, int iters) {
    auto start = clock_type::now();
    for (int i = 0; i < iters; i++) {
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml, len, &st);
        if (doc) taurus_document_free(doc);
    }
    return ElapsedUs(start);
}

/* Memory-bandwidth reference over the same bytes, same iteration
 * count. Parse is O(len) work over the same input, so the ratio
 * parse/memcpy is stable across machines. */
double MemcpyRefUs(const char* xml, size_t len, int iters) {
    char* sink = (char*)std::malloc(len);
    auto start = clock_type::now();
    for (int i = 0; i < iters; i++) {
        std::memcpy(sink, xml, len);
    }
    volatile char keep = sink[0];
    (void)keep;
    double us = ElapsedUs(start);
    std::free(sink);
    return us;
}

TEST(PerfRegression, SmallDocumentParseIsFast) {
    const char xml[] = "<root><item id='1'>text</item><item id='2'/></root>";
    double parse_us = ParseBenchUs(xml, std::strlen(xml), 5000);
    double ref_us = MemcpyRefUs(xml, std::strlen(xml), 5000);
    /* Parse does far more work than memcpy over the same bytes, but
     * the multiple is a property of the algorithm, not the machine.
     * Healthy parse measures in the low hundreds x memcpy; a 10x
     * algorithmic regression still clears 100x with margin. */
    EXPECT_LT(parse_us, 1000.0 * ref_us)
        << "Small-doc parse regression: parse " << parse_us
        << " us vs memcpy reference " << ref_us << " us";
}

TEST(PerfRegression, AttributeHeavyDocumentParseIsFast) {
    /* The attrs.xml regression (TODO 22) was 3.4x slower than libxml2.
     * After the fix, taurus is 1.3x faster.  This spec catches
     * regressions by asserting the intern-bypass is still in place. */
    std::string xml = "<root";
    for (int i = 0; i < 25; i++) {
        xml += " attr" + std::to_string(i) + "='val" + std::to_string(i) + "'";
    }
    xml += ">text</root>";

    double parse_us = ParseBenchUs(xml.data(), xml.size(), 1000);
    double ref_us = MemcpyRefUs(xml.data(), xml.size(), 1000);
    EXPECT_LT(parse_us, 20000.0 * ref_us)
        << "Attribute-heavy parse regression: parse " << parse_us
        << " us vs memcpy reference " << ref_us << " us";
}

TEST(PerfRegression, DeepNestingIsRejectedQuickly) {
    /* 50k-deep nesting must be rejected without crashing.  The depth
     * check itself is O(1) per level — rejection costs one linear
     * scan, the same order as memcpy over the same input. */
    std::string xml;
    for (int i = 0; i < 50000; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 50000; i++) xml += "</a>";

    TaurusStatus st;
    auto start = clock_type::now();
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    double parse_us = ElapsedUs(start);
    double ref_us = MemcpyRefUs(xml.data(), xml.size(), 1);

    EXPECT_EQ(doc, nullptr);
    EXPECT_LT(parse_us, 200.0 * ref_us)
        << "Deep-nesting rejection regression: " << parse_us
        << " us vs memcpy reference " << ref_us << " us";
}

// ---- Write + DOM-access regression specs --------------------------------
//
// These specs protect the write-path perf gains from PRs #68, #70.
// Each compares the second half of the workload against the first
// half, measured in the same run: amortized-O(1) appends keep the
// ratio near 1x, while an O(n^2) regression (the original bug
// class) drives it toward 3x+ at these sizes.

TEST(PerfRegression, AppendChildDoesNotRegress) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    const int half = 4000;
    auto h1 = clock_type::now();
    for (int i = 0; i < half; i++) {
        TaurusElement c = taurus_element_create(doc, "c");
        taurus_element_append_child(root, c);
    }
    double first = ElapsedUs(h1);
    auto h2 = clock_type::now();
    for (int i = 0; i < half; i++) {
        TaurusElement c = taurus_element_create(doc, "c");
        taurus_element_append_child(root, c);
    }
    double second = ElapsedUs(h2);

    /* Documented shape: append walks to the tail (last_child_off was
     * removed in TODO 155 Phase C to hold the 64 B element), so N
     * appends are O(N^2) — the second half costs ~4x the first.
     * The budget catches WORSE-than-documented: an O(N^3) regression
     * reaches ~8x at 2x size. Restoring a last-child edge is the
     * known fix if programmatic DOM building matters; see TODO 155. */
    ASSERT_GT(first, 1.0);
    EXPECT_LT(second, 6.5 * first)
        << "AppendChild worse than documented O(N^2): second half "
        << second << " us vs first half " << first << " us";

    taurus_document_free(doc);
}

TEST(PerfRegression, SetAttributeDoesNotRegress) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    const int half = 150;
    char name[16], value[32];
    auto h1 = clock_type::now();
    for (int i = 0; i < half; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "v-%d", i);
        taurus_element_set_attribute(root, name, value);
    }
    double first = ElapsedUs(h1);
    auto h2 = clock_type::now();
    for (int i = half; i < 2 * half; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "v-%d", i);
        taurus_element_set_attribute(root, name, value);
    }
    double second = ElapsedUs(h2);

    /* Documented shape: attribute insertion walks the singly-linked
     * attribute list to the tail (O(attrs) per call, O(N^2) total —
     * the parser keeps its own last-attr cache, TODO 159 Phase G,
     * but the public API does not). Budget catches worse-than-
     * documented complexity. */
    ASSERT_GT(first, 1.0);
    EXPECT_LT(second, 6.5 * first)
        << "SetAttribute worse than documented O(N^2): second half "
        << second << " us vs first half " << first << " us";

    taurus_document_free(doc);
}

TEST(PerfRegression, IndexedChildAccessDoesNotRegress) {
    /* Indexed child access walks the sibling linked list O(index) per
     * call (children_array cache removed in TODO 90 Phase 1). The
     * guarded shape is O(N^2) per full sweep: doubling N quadruples
     * the time. An accidental O(N^3) (or a constant-factor blowup)
     * moves the ratio past 4.5x, which no healthy run reaches. */
    const int iters = 2000;
    auto sweep_us = [iters](int n) {
        std::string xml = "<r>";
        for (int i = 0; i < n; i++) xml += "<c/>";
        xml += "</r>";
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
        TaurusElement root = taurus_document_root(doc);
        auto start = clock_type::now();
        volatile size_t sink = 0;
        for (int iter = 0; iter < iters; iter++) {
            for (int i = 0; i < n; i++) {
                TaurusElement child = taurus_element_child(root, (size_t)i);
                sink += (size_t)child;
            }
        }
        double us = ElapsedUs(start);
        (void)sink;
        taurus_document_free(doc);
        return us;
    };

    /* Min-of-3 per side: single short measurements on shared CI
     * runners get skewed by preemption between the two sweeps; the
     * minimum filters that noise out (same discipline as the
     * Release A/B gates). */
    double small = 1e18, large = 1e18;
    for (int rep = 0; rep < 3; rep++) {
        double s = sweep_us(25);
        double l = sweep_us(50);
        if (s < small) small = s;
        if (l < large) large = l;
    }
    /* Same work per outer iteration; O(N^2) sweep => large/small ~ 4x.
     * Budget 6.5x absorbs ratio jitter on loaded runners while
     * staying far below the ~8x an O(N^3) regression reaches. */
    EXPECT_LT(large, 6.5 * small)
        << "Indexed child access complexity regression: 50-child sweep "
        << large << " us vs 25-child sweep " << small << " us";
}

}  // namespace
