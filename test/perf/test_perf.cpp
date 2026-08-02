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

}  // namespace
