/* benchmark_base.h - Base class for benchmarks
 * Copyright (c) 2024, Ribose Inc.
 */

#ifndef BENCHMARK_BASE_H
#define BENCHMARK_BASE_H

#include <taurus.h>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>

class BenchmarkBase {
public:
    struct Result {
        std::string name;
        double taurus_time_us;
        double libxml2_time_us;
        double pugixml_time_us;
        size_t taurus_memory;
        size_t libxml2_memory;
        size_t pugixml_memory;
    };

    BenchmarkBase(const std::string& name, int iterations = 1000)
        : name_(name), iterations_(iterations) {}

    virtual ~BenchmarkBase() {}

    void run_all() {
        setup();

        std::cout << "Running " << name_ << " benchmark..." << std::endl;

        // Run Taurus benchmark
        auto taurus_result = benchmark([this]() { run_taurus(); });
        results_.taurus_time_us = taurus_result.first;
        results_.taurus_memory = taurus_result.second;

        // Run libxml2 benchmark (if available)
        #ifdef HAVE_LIBXML2
        auto libxml2_result = benchmark([this]() { run_libxml2(); });
        results_.libxml2_time_us = libxml2_result.first;
        results_.libxml2_memory = libxml2_result.second;
        #else
        results_.libxml2_time_us = -1.0;
        results_.libxml2_memory = 0;
        #endif

        // Run pugixml benchmark (if available)
        #ifdef HAVE_PUGIXML
        auto pugixml_result = benchmark([this]() { run_pugixml(); });
        results_.pugixml_time_us = pugixml_result.first;
        results_.pugixml_memory = pugixml_result.second;
        #else
        results_.pugixml_time_us = -1.0;
        results_.pugixml_memory = 0;
        #endif

        teardown();
    }

    void report() const {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\nResults for " << name_ << ":" << std::endl;
        std::cout << "  Taurus:  " << std::setw(8) << results_.taurus_time_us << " µs";
        if (results_.taurus_memory > 0) {
            std::cout << " (" << results_.taurus_memory << " bytes)";
        }
        std::cout << std::endl;

        if (results_.libxml2_time_us >= 0) {
            std::cout << "  libxml2: " << std::setw(8) << results_.libxml2_time_us << " µs";
            if (results_.libxml2_memory > 0) {
                std::cout << " (" << results_.libxml2_memory << " bytes)";
            }
            std::cout << std::endl;
        }

        if (results_.pugixml_time_us >= 0) {
            std::cout << "  pugixml: " << std::setw(8) << results_.pugixml_time_us << " µs";
            if (results_.pugixml_memory > 0) {
                std::cout << " (" << results_.pugixml_memory << " bytes)";
            }
            std::cout << std::endl;
        }

        // Determine and report winner
        double best_time = results_.taurus_time_us;
        std::string winner = "Taurus";

        if (results_.libxml2_time_us >= 0 && results_.libxml2_time_us < best_time) {
            best_time = results_.libxml2_time_us;
            winner = "libxml2";
        }
        if (results_.pugixml_time_us >= 0 && results_.pugixml_time_us < best_time) {
            best_time = results_.pugixml_time_us;
            winner = "pugixml";
        }

        std::cout << "  Winner:  " << winner << std::endl;
    }

    Result get_results() const { return results_; }

protected:
    // Override these in derived classes
    virtual void setup() {}
    virtual void teardown() {}
    virtual void run_taurus() = 0;
    virtual void run_libxml2() {}
    virtual void run_pugixml() {}

    // Helper to load test data
    std::string load_xml_file(const std::string& path) {
        // Try multiple possible paths
        std::vector<std::string> possible_paths = {
            path,                          // Direct path (if absolute or correct relative)
            "benchmarks/data/" + path,     // From project root
            "../benchmarks/data/" + path,  // From build/ directory
            "../../benchmarks/data/" + path // From build/benchmarks/ directory
        };

        for (const auto& full_path : possible_paths) {
            FILE* f = fopen(full_path.c_str(), "rb");
            if (!f) continue;

            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);

            std::string content(size, '\0');
            fread(&content[0], 1, size, f);
            fclose(f);

            return content;
        }

        // Failed to load from any path
        return "";
    }

private:
    template<typename Func>
    std::pair<double, size_t> benchmark(Func fn) {
        // Warm up
        for (int i = 0; i < 10; i++) {
            fn();
        }

        // Actual benchmark
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations_; i++) {
            fn();
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double avg_us = static_cast<double>(duration.count()) / iterations_;

        // Memory measurement would require platform-specific code
        // For now, return 0
        size_t memory = 0;

        return {avg_us, memory};
    }

    std::string name_;
    int iterations_;
    Result results_;
};

#endif // BENCHMARK_BASE_H