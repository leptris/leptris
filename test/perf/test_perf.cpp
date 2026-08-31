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
// ASAN: the growth/complexity ratios are symmetric (the same code
// runs on both sides) and stay enabled. The memcpy-reference parse
// specs are skipped: sanitizers slow scattered tree writes far
// more than one bulk memcpy, so that ratio is not comparable.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include "leptris.h"

#include <chrono>
#include <string>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define LEPTRIS_TEST_ASAN 1
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define LEPTRIS_TEST_ASAN 1
#endif
#ifndef LEPTRIS_TEST_ASAN
#  define LEPTRIS_TEST_ASAN 0
#endif

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
        LeptrisStatus st;
        LeptrisDocument doc = leptris_parse_string(xml, len, &st);
        if (doc) leptris_document_free(doc);
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
    /* Per-rep ratio, min of 4 reps: parse and its memcpy reference
     * are measured back-to-back within a rep, so uniform background
     * load inflates both sides and the ratio survives; a transient
     * preemption hitting one rep's parse is filtered out by the min.
     * (Taking independent minimums of the two sides mixed load
     * regimes and flaked on shared CI runners — the parse side is
     * CPU-bound while the cache-resident memcpy sails through.) */
    double best = 1e18;
    for (int rep = 0; rep < 4; rep++) {
        double ratio = ParseBenchUs(xml, std::strlen(xml), 5000) /
                       MemcpyRefUs(xml, std::strlen(xml), 5000);
        if (ratio < best) best = ratio;
    }
    /* Parse does far more work than memcpy over the same bytes, but
     * the multiple is a property of the algorithm, not the machine.
     * Healthy parse measures in the low hundreds x memcpy; a 10x
     * algorithmic regression still clears 100x with margin. */
#if defined(NDEBUG) && !LEPTRIS_TEST_ASAN
        EXPECT_LT(best, 1000.0)
            << "Small-doc parse regression: parse/memcpy ratio " << best;
#else
    (void)best;
#endif
}

TEST(PerfRegression, AttributeHeavyDocumentParseIsFast) {
    /* The attrs.xml regression (TODO 22) was 3.4x slower than libxml2.
     * After the fix, leptris is 1.3x faster.  This spec catches
     * regressions by asserting the intern-bypass is still in place. */
    std::string xml = "<root";
    for (int i = 0; i < 25; i++) {
        xml += " attr" + std::to_string(i) + "='val" + std::to_string(i) + "'";
    }
    xml += ">text</root>";

    /* Per-rep ratio, min of 4 — same load-regime discipline as
     * SmallDocumentParseIsFast above. */
    double best = 1e18;
    for (int rep = 0; rep < 4; rep++) {
        double ratio = ParseBenchUs(xml.data(), xml.size(), 1000) /
                       MemcpyRefUs(xml.data(), xml.size(), 1000);
        if (ratio < best) best = ratio;
    }
#if defined(NDEBUG) && !LEPTRIS_TEST_ASAN
        EXPECT_LT(best, 20000.0)
            << "Attribute-heavy parse regression: parse/memcpy ratio "
            << best;
#else
    (void)best;
#endif
}

TEST(PerfRegression, DeepNestingIsRejectedQuickly) {
    /* 50k-deep nesting must be rejected without crashing.  The depth
     * check itself is O(1) per level — rejection costs one linear
     * scan, the same order as memcpy over the same input. */
    std::string xml;
    for (int i = 0; i < 50000; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 50000; i++) xml += "</a>";

    LeptrisStatus st;
    auto start = clock_type::now();
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    double parse_us = ElapsedUs(start);
    double ref_us = MemcpyRefUs(xml.data(), xml.size(), 1);

    EXPECT_EQ(doc, nullptr);
#ifdef NDEBUG
    EXPECT_LT(parse_us, 200.0 * ref_us)
        << "Deep-nesting rejection regression: " << parse_us
        << " us vs memcpy reference " << ref_us << " us";
#else
    (void)parse_us; (void)ref_us;
#endif
}

// ---- Write + DOM-access regression specs --------------------------------
//
// These specs protect the write-path perf gains from PRs #68, #70.
// Each compares the second half of the workload against the first
// half, measured in the same run: amortized-O(1) appends keep the
// ratio near 1x, while an O(n^2) regression (the original bug
// class) drives it toward 3x+ at these sizes.

TEST(PerfRegression, AppendChildDoesNotRegress) {
    /* Documented shape: append walks to the tail (last_child_off was
     * removed in TODO 155 Phase C), so N appends are O(N^2) with
     * memory-system superlinearity on fresh documents — T(4N)/T(N)
     * measures 60-100x in practice (16x is the pure-walk floor; pool
     * growth interleaving the walks adds the rest). Budget 150x
     * accommodates the measured regime and still flags algorithmic
     * catastrophe. A last-child edge is the known structural fix;
     * see TODO 155. */
    auto append_us = [](int n) {
        LeptrisStatus st;
        LeptrisDocument doc = leptris_parse_string("<r/>", 4, &st);
        LeptrisElement root = leptris_document_root(doc);
        auto start = clock_type::now();
        for (int i = 0; i < n; i++) {
            LeptrisElement c = leptris_element_create(doc, "c");
            leptris_element_append_child(root, c);
        }
        double us = ElapsedUs(start);
        leptris_document_free(doc);
        return us;
    };
    double small = 1e18, large = 1e18;
    for (int rep = 0; rep < 2; rep++) {
        double a = append_us(1500);
        double b = append_us(6000);
        if (a < small) small = a;
        if (b < large) large = b;
    }
    ASSERT_GT(small, 1.0);
    EXPECT_LT(large, 150.0 * small)
        << "AppendChild worse than documented shape: T(4N) " << large
        << " us vs T(N) " << small << " us";
}

TEST(PerfRegression, SetAttributeDoesNotRegress) {
    /* Documented shape: attribute insertion walks the singly-linked
     * list to the tail (parser keeps its own last-attr cache, TODO
     * 159 Phase G; the public API does not). T(4N)/T(N) measures
     * ~100x in practice (superlinear allocator effects on fresh
     * documents); budget 150x. */
    auto setattr_us = [](int n) {
        LeptrisStatus st;
        LeptrisDocument doc = leptris_parse_string("<r/>", 4, &st);
        LeptrisElement root = leptris_document_root(doc);
        char name[16], value[32];
        auto start = clock_type::now();
        for (int i = 0; i < n; i++) {
            snprintf(name, sizeof(name), "a%d", i);
            snprintf(value, sizeof(value), "v-%d", i);
            leptris_element_set_attribute(root, name, value);
        }
        double us = ElapsedUs(start);
        leptris_document_free(doc);
        return us;
    };
    double small = 1e18, large = 1e18;
    for (int rep = 0; rep < 2; rep++) {
        double a = setattr_us(150);
        double b = setattr_us(600);
        if (a < small) small = a;
        if (b < large) large = b;
    }
    ASSERT_GT(small, 1.0);
    EXPECT_LT(large, 150.0 * small)
        << "SetAttribute worse than documented shape: T(4N) " << large
        << " us vs T(N) " << small << " us";
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
        LeptrisStatus st;
        LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
        LeptrisElement root = leptris_document_root(doc);
        auto start = clock_type::now();
        volatile size_t sink = 0;
        for (int iter = 0; iter < iters; iter++) {
            for (int i = 0; i < n; i++) {
                LeptrisElement child = leptris_element_child(root, (size_t)i);
                sink += (size_t)child;
            }
        }
        double us = ElapsedUs(start);
        (void)sink;
        leptris_document_free(doc);
        return us;
    };

    /* Min-of-3 per side: single short measurements on shared CI
     * runners get skewed by preemption between the two sweeps; the
     * minimum filters that noise out (same discipline as the
     * Release A/B gates). */
    double small = 1e18, large = 1e18;
    for (int rep = 0; rep < 3; rep++) {
        double s = sweep_us(25);
        double l = sweep_us(75);
        if (s < small) small = s;
        if (l < large) large = l;
    }
    /* 3x size: O(N^2) sweep => large/small ~ 9x; an O(N^3)
     * regression reaches ~27x. Budget 18x separates both with
     * margin on loaded runners and under ASAN. */
    EXPECT_LT(large, 18.0 * small)
        << "Indexed child access complexity regression: 75-child sweep "
        << large << " us vs 25-child sweep " << small << " us";
}

/* Template dispatch must scale with the CANDIDATE'S subtree, not
 * the parent's child count: the pre-pattern-compiler matcher
 * evaluated every ancestor rung as a full downward XPath and
 * membership-scanned the result — match="book[title]" cost
 * O(siblings) per candidate (5.5 s for a 2000-book dispatch sheet
 * that libxslt runs in ~10 ms). Fixed-shape sheet, N and 4N books:
 * linear work ⇒ ratio ≈ 4; the pathology measures ≈ 16. */
static double DispatchSheetMs(int books) {
    std::string xml = "<catalog>";
    for (int i = 0; i < books; i++) {
        xml += "<book id='" + std::to_string(i) + "'><title>t</title>"
               "<author>a</author></book>";
    }
    xml += "</catalog>";
    static const char XSL[] =
        "<xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        " version='1.0'>"
        "<xsl:template match='/'><out>"
        "<xsl:apply-templates select='//book'/></out></xsl:template>"
        "<xsl:template match='book[title]'><b id='{@id}'>"
        "<xsl:apply-templates select='*'/></b></xsl:template>"
        "<xsl:template match='title'><t><xsl:value-of select='.'/></t>"
        "</xsl:template>"
        "<xsl:template match='author'><a><xsl:value-of select='.'/></a>"
        "</xsl:template>"
        "</xsl:stylesheet>";
    LeptrisDocument d = leptris_parse_string(xml.data(), xml.size(), NULL);
    LeptrisXslt x = leptris_xslt_parse(XSL, sizeof(XSL) - 1);
    if (!d || !x) { if (d) leptris_document_free(d);
                    if (x) leptris_xslt_free(x); return -1.0; }
    auto start = clock_type::now();
    char* out = leptris_xslt_apply_string(x, d);
    double ms = ElapsedUs(start) / 1000.0;
    leptris_free_string(out);
    leptris_xslt_free(x);
    leptris_document_free(d);
    return ms;
}

TEST(PerfRegression, TemplateDispatchScalesLinearly) {
    double small = 1e18, large = 1e18;
    for (int rep = 0; rep < 3; rep++) {
        double s = DispatchSheetMs(300);
        double l = DispatchSheetMs(1200);
        if (s < small) small = s;
        if (l < large) large = l;
    }
    ASSERT_GT(small, 0.0);
    /* 4x the books, pattern matching linear in the candidate's own
     * context: budget 10x leaves headroom for output-building
     * variance; the sibling-scan pathology measures ~16x. */
    EXPECT_LT(large, 10.0 * small)
        << "Template dispatch complexity regression: 1200-book sheet "
        << large << " ms vs 300-book " << small << " ms";
#if defined(NDEBUG) && !LEPTRIS_TEST_ASAN
    /* Generous absolute cap (healthy: a few ms) — the pathology
     * measured ~2.2 s at 1200 books. */
    EXPECT_LT(large, 500.0)
        << "Template dispatch absolute regression: " << large << " ms";
#endif
}

}  // namespace
