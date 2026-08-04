// test/perf/test_perf.cpp — Performance regression specs (TODO 66).
//
// Catches silent perf regressions by asserting parse throughput
// stays above a budget.  Budgets are absolute and machine-dependent;
// tuned conservatively so they pass on CI runners (typically 2-core
// GitHub Actions instances) but catch major regressions like the
// attrs.xml 3.4x slowdown the validation pass found.

#include <gtest/gtest.h>
#include <cstring>
#include "taurus.h"

#include <chrono>
#include <string>

namespace {

using clock_type = std::chrono::steady_clock;

/* Parse `xml` `iters` times, returning total milliseconds. */
double ParseBenchMs(const char* xml, size_t len, int iters) {
    auto start = clock_type::now();
    for (int i = 0; i < iters; i++) {
        TaurusStatus st;
        TaurusDocument doc = taurus_parse_string(xml, len, &st);
        if (doc) taurus_document_free(doc);
    }
    auto end = clock_type::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

TEST(PerfRegression, SmallDocumentParseIsFast) {
    const char xml[] = "<root><item id='1'>text</item><item id='2'/></root>";
    /* 5000 parses should take < 500 ms even on a slow CI runner.
     * The validation pass measured ~0.02 ms/parse on Apple Silicon. */
    double ms = ParseBenchMs(xml, std::strlen(xml), 5000);
    EXPECT_LT(ms, 500.0)
        << "Small-doc parse regression: " << ms << " ms for 5000 parses";
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

    /* 1000 parses should take < 200 ms.  Pre-fix was ~14 ms/parse;
     * post-fix is ~2 ms/parse. */
    double ms = ParseBenchMs(xml.data(), xml.size(), 1000);
    EXPECT_LT(ms, 200.0)
        << "Attribute-heavy parse regression: " << ms << " ms for 1000 parses";
}

TEST(PerfRegression, DeepNestingIsRejectedQuickly) {
    /* 50k-deep nesting must be rejected without crashing.
     * The depth check itself should be O(1) per level — total time
     * is the time to scan past 256 opening tags. */
    std::string xml;
    for (int i = 0; i < 50000; i++) xml += "<a>";
    xml += 'x';
    for (int i = 0; i < 50000; i++) xml += "</a>";

    /* Should reject in < 100 ms even on slow hardware. */
    auto start = clock_type::now();
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_type::now() - start).count();

    EXPECT_EQ(doc, nullptr);
    EXPECT_LT(ms, 100);
}

// ---- Write + DOM-access regression specs --------------------------------
//
// These specs protect the write-path perf gains from PRs #68, #70.
// Budgets are generous (10x the Release+LTO time on M-series Mac) so
// they pass on any CI runner but catch 10x+ regressions.

TEST(PerfRegression, AppendChildDoesNotRegress) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    auto start = clock_type::now();
    for (int i = 0; i < 1000; i++) {
        TaurusElement c = taurus_element_create(doc, "c");
        taurus_element_append_child(root, c);
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_type::now() - start).count();

    /* Release+LTO: ~0.015 ms on M-series.
     * Budget: 10 ms (667x margin for slow CI). */
    EXPECT_LT(ms, 10);

    taurus_document_free(doc);
}

TEST(PerfRegression, SetAttributeDoesNotRegress) {
    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string("<r/>", 4, &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    auto start = clock_type::now();
    char name[16], value[32];
    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "a%d", i);
        snprintf(value, sizeof(value), "v-%d", i);
        taurus_element_set_attribute(root, name, value);
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_type::now() - start).count();

    /* Release+LTO: ~0.043 ms on M-series.
     * Budget: 20 ms (465x margin). */
    EXPECT_LT(ms, 20);

    taurus_document_free(doc);
}

TEST(PerfRegression, IndexedChildAccessDoesNotRegress) {
    /* PR #68 added children_array cache for O(1) indexed access.
     * This spec catches if someone removes the cache. */
    std::string xml = "<r>";
    for (int i = 0; i < 50; i++) xml += "<c/>";
    xml += "</r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    /* Access by index 1000 times — should be fast due to cache. */
    auto start = clock_type::now();
    volatile size_t sink = 0;
    for (int iter = 0; iter < 1000; iter++) {
        for (size_t i = 0; i < 50; i++) {
            TaurusElement child = taurus_element_child(root, i);
            sink += (size_t)child;
        }
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock_type::now() - start).count();

    /* Release+LTO: ~0.3 ms on M-series with cache.
     * Without cache it would be ~6 ms (O(N²)).
     * Budget: 5 ms — catches cache removal without false-alarming on slow CI. */
    EXPECT_LT(ms, 5);
    (void)sink;

    taurus_document_free(doc);
}

}  // namespace
