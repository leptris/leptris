#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "simd_text.h"

/* TODO 193 Phase 1: the span scanner must be a pure function of the
 * bytes — every implementation dispatches to identical events. */
namespace {

std::vector<LeptrisScanEvent> Scan(const char* s, size_t len) {
    std::vector<LeptrisScanEvent> v(len + 1);
    size_t n = leptris_text_scan_events(s, len, v.data(), v.size());
    v.resize(n);
    return v;
}

std::vector<LeptrisScanEvent> ScanScalar(const char* s, size_t len) {
    /* Reference: same table, plain loop. */
    std::vector<LeptrisScanEvent> v;
    for (size_t i = 0; i < len; i++) {
        unsigned char cls = leptris_dp_class_table[(unsigned char)s[i]];
        if (cls) v.push_back({(uint32_t)i, cls});
    }
    return v;
}

void ExpectParity(const char* s, size_t len) {
    auto a = Scan(s, len);
    auto b = ScanScalar(s, len);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); i++) {
        ASSERT_EQ(a[i].offset, b[i].offset) << "event " << i;
        ASSERT_EQ(a[i].cls, b[i].cls) << "event " << i;
    }
}

}  // namespace

TEST(SimdSpans, All256BytesClassifiedConsistently) {
    char all[256];
    for (int i = 0; i < 256; i++) all[i] = (char)i;
    ExpectParity(all, 256);
}

TEST(SimdSpans, VectorLoopBoundaryLengths) {
    std::string xml = "<elem attr='v'>text & more</elem>\n\t<e2 a=\"1\"/>";
    for (size_t len = 0; len <= xml.size(); len++) {
        ASSERT_NO_FATAL_FAILURE(ExpectParity(xml.data(), len))
            << "prefix len " << len;
    }
}

TEST(SimdSpans, RealisticDocuments) {
    std::string doc = "<library>";
    for (int s = 0; s < 50; s++) {
        doc += "<section id='s" + std::to_string(s) + "'>";
        for (int i = 0; i < 10; i++)
            doc += "<item n='" + std::to_string(i) + "'>text</item>";
        doc += "<!-- c --><?pi x?><![CDATA[x>y]]></section>";
    }
    doc += "</library>";
    ExpectParity(doc.data(), doc.size());
}

TEST(SimdSpans, RandomBuffers) {
    srand(20260817);
    for (int round = 0; round < 50; round++) {
        char buf[4096];
        size_t len = 64 + (size_t)(rand() % 4000);
        for (size_t i = 0; i < len; i++) {
            int r = rand() % 3;
            buf[i] = (r == 0) ? (char)(rand() % 256)
                              : (char)"<>/'\"=& \t\n\rabcX9-"[rand() % 17];
        }
        ExpectParity(buf, len);
    }
}

TEST(SimdSpans, OverflowSignalsTruncation) {
    const char* xml = "<a x='1' y='2'/>";
    size_t len = std::strlen(xml);
    LeptrisScanEvent one[1];
    EXPECT_EQ(leptris_text_scan_events(xml, len, one, 1), 1u);
    EXPECT_EQ(one[0].offset, 0u);
    EXPECT_EQ(one[0].cls, DPSCAN_LT);
}
