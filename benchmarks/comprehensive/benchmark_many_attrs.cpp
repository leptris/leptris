// benchmark_many_attrs.cpp - Scenario 7: High-Attribute-Count Parse
//
// Regression benchmark for TODO 159 Phase G. Generates XML with
// varying numbers of attributes per element (K = 5, 20, 50, 100)
// and measures parse time for both leptris and pugixml.
//
// Before Phase G, leptris's per-element attr wiring was O(K^2) due
// to walking the existing attr list to find the tail on every
// insertion. This benchmark catches that regression and documents
// where leptris is competitive (or ahead) on high-attr inputs.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>

#include <leptris.h>

#include <pugixml.hpp>

namespace {

struct Stats {
    double mean_us;
    double median_us;
    double stddev_us;
    double min_us;
    double max_us;
};

Stats compute_stats(const std::vector<double>& samples) {
        Stats s = {};
        if (samples.empty()) return s;
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double v : sorted) sum += v;
        s.mean_us = sum / sorted.size();
        s.median_us = sorted[sorted.size() / 2];
        s.min_us = sorted.front();
        s.max_us = sorted.back();
        double sq_sum = 0;
        for (double v : sorted) sq_sum += (v - s.mean_us) * (v - s.mean_us);
        s.stddev_us = sqrt(sq_sum / sorted.size());
        return s;
}

void print_stats(const char* label, const Stats& s) {
    printf("  %-10s  mean %8.2f us  median %8.2f us  "
           "min %8.2f  max %8.2f  std %6.2f\n",
           label, s.mean_us, s.median_us, s.min_us, s.max_us, s.stddev_us);
}

// Generate XML with `n_elements` elements, each carrying `k_attrs` attrs.
// Returns the XML as a std::string. Attrs are zero-copy-friendly: short
// ASCII names + values, no entities.
std::string generate_xml(size_t n_elements, size_t k_attrs) {
    std::string s;
    s.reserve(n_elements * (k_attrs * 24 + 32));
    s.append("<root>");
    char buf[64];
    for (size_t i = 0; i < n_elements; i++) {
        s.append("<e");
        for (size_t j = 0; j < k_attrs; j++) {
            int name_len = snprintf(buf, sizeof(buf), " a%zu='%zu'", j, j);
            s.append(buf, name_len);
        }
        s.append("/>");
    }
    s.append("</root>");
    return s;
}

Stats bench_leptris(const std::string& xml, int iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        LeptrisStatus st;
        LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
        auto t1 = std::chrono::high_resolution_clock::now();
        if (doc) {
            leptris_document_free(doc);
        }
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        samples.push_back(us);
    }
    return compute_stats(samples);
}

Stats bench_pugixml(const std::string& xml, int iterations) {
    std::vector<double> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        pugi::xml_document doc;
        doc.load_buffer(xml.data(), xml.size());
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        samples.push_back(us);
    }
    return compute_stats(samples);
}

} // namespace

int main() {
    const int ITERS = 200;
    const size_t N_ELEMENTS = 1000;

    printf("\n");
    printf("=================================================================\n");
    printf("Benchmark: High-Attribute-Count Parse (TODO 165)\n");
    printf("=================================================================\n");
    printf("Elements: %zu  Iterations: %d\n", N_ELEMENTS, ITERS);
    printf("=================================================================\n");

    size_t attr_counts[] = {5, 20, 50, 100};
    for (size_t k : attr_counts) {
        std::string xml = generate_xml(N_ELEMENTS, k);
        printf("\n--- %zu attrs/element, %zu bytes ---\n", k, xml.size());

        Stats t = bench_leptris(xml, ITERS);
        Stats p = bench_pugixml(xml, ITERS);
        print_stats("leptris:", t);
        print_stats("pugixml:", p);

        double ratio = (p.mean_us > 0) ? t.mean_us / p.mean_us : 0;
        const char* verdict = (ratio < 1.0) ? "leptris AHEAD"
                              : (ratio < 1.5) ? "leptris competitive"
                                              : "pugixml ahead";
        printf("  ratio %.2fx (%s)\n", ratio, verdict);
    }

    printf("\n");
    printf("=================================================================\n");
    printf("Interpretation:\n");
    printf("  - leptris should be competitive with pugixml on K <= 20\n");
    printf("  - For K >= 50, leptris's per-attr work (hash, entity check,\n");
    printf("    string view setup) shows up; pugixml ships fewer features\n");
    printf("    per attr. The gap is structural, not a bug.\n");
    printf("  - Regression check: any per-attr cost increase should be\n");
    printf("    visible here first.\n");
    printf("=================================================================\n");
    return 0;
}
