// benchmarks/matrix/bench_matrix.cpp — Full feature matrix: taurus vs pugixml vs libxml2.
//
// Produces one YAML file per library (build/bench-matrix/<library>.yaml) with
// latency (min/median), throughput, CPU (user/sys) and peak-RSS metrics for
// every benchmark shape. Combine with scripts/bench2html.py to generate an
// HTML report with comparison tables and bar charts.
//
// Not every library supports every feature (pugixml has no SAX; libxml2's
// DOM API differs). Unsupported cells are omitted from that library's YAML —
// the HTML generator renders them as N/A.
//
// Usage: bench_matrix [output_dir] [--iterations N]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/time.h>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <taurus.h>
#include <taurus/sax/sax.h>

#ifdef HAVE_PUGIXML
#include <pugixml.hpp>
#endif

#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/xpath.h>
#endif

// ----------------------------------------------------------------------------
// Helpers

static double now_us() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static size_t current_rss_kb() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (size_t)(info.resident_size / 1024);
    }
    return 0;
#else
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    size_t kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%zu", &kb);
            break;
        }
    }
    fclose(f);
    return kb;
#endif
}

struct CpuTime {
    double user_ms;
    double sys_ms;
};
static CpuTime cpu_time_delta(const struct rusage& before,
                              const struct rusage& after) {
    CpuTime d;
    d.user_ms = (after.ru_utime.tv_sec - before.ru_utime.tv_sec) * 1000.0 +
                (after.ru_utime.tv_usec - before.ru_utime.tv_usec) / 1000.0;
    d.sys_ms = (after.ru_stime.tv_sec - before.ru_stime.tv_sec) * 1000.0 +
               (after.ru_stime.tv_usec - before.ru_stime.tv_usec) / 1000.0;
    return d;
}

static char* slurp(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

// ----------------------------------------------------------------------------
// Fixture generation

struct Fixture {
    std::string label;
    std::string xml;
};

static Fixture gen_text_heavy() {
    size_t cap = 2u << 20;
    std::string b;
    b.reserve(cap);
    b += "<r>";
    const char* word = "lorem_ipsum_dolor_sit_amet_";
    while (b.size() < cap - 64) { b += word; b += ' '; }
    b += "</r>";
    return {"text-heavy-2MB", b};
}

static Fixture gen_attr_heavy() {
    size_t cap = 4u << 20;
    std::string b;
    b.reserve(cap);
    b += "<r>";
    char buf[64];
    for (int e = 0; e < 5000 && b.size() < cap - 4096; e++) {
        b += "<e";
        for (int a = 0; a < 10; a++) {
            snprintf(buf, sizeof(buf), " k%d='v%d'", a, e);
            b += buf;
        }
        b += "/>";
    }
    b += "</r>";
    return {"attr-heavy-5k", b};
}

static Fixture gen_pretty() {
    size_t cap = 1u << 20;
    std::string b;
    b.reserve(cap);
    b += "<r>\n";
    while (b.size() < cap - 32) b += "  <a/>\n";
    b += "</r>\n";
    return {"pretty-ws-1MB", b};
}

static Fixture gen_catalog(int n_items) {
    std::string b;
    b.reserve(n_items * 64 + 16);
    b += "<catalog>";
    char buf[128];
    for (int i = 0; i < n_items; i++) {
        snprintf(buf, sizeof(buf),
                 "<item id='%d' cat='c%d'><name>Item %d</name>"
                 "<price>%d.99</price></item>",
                 i, i % 10, i, i % 100);
        b += buf;
    }
    b += "</catalog>";
    char lbl[64];
    snprintf(lbl, sizeof(lbl), "catalog-%d", n_items);
    return {lbl, b};
}

// ----------------------------------------------------------------------------
// Result recording

struct BenchResult {
    std::string id;
    std::string feature;
    std::string input;
    size_t size_bytes;
    int iterations;
    double latency_us_min;
    double latency_us_median;
    double throughput_mbs;       // 0 if not applicable
    double cpu_user_ms;
    double cpu_sys_ms;
    size_t memory_peak_kb;
    int applicable;
};

struct Library {
    std::string name;
    std::string version;
    std::vector<BenchResult> results;
    void add(const BenchResult& r) { results.push_back(r); }
};

template<typename F>
BenchResult run_bench(const char* id, const char* feature, const char* input,
                      size_t size_bytes, int iterations, F&& fn) {
    BenchResult r = {};
    r.id = id;
    r.feature = feature;
    r.input = input;
    r.size_bytes = size_bytes;
    r.iterations = iterations;
    r.applicable = 1;

    // Warm-up
    fn();

    std::vector<double> times;
    times.reserve(iterations);
    struct rusage ru_before;
    getrusage(RUSAGE_SELF, &ru_before);
    size_t rss_before = current_rss_kb();

    for (int i = 0; i < iterations; i++) {
        double t0 = now_us();
        fn();
        times.push_back(now_us() - t0);
    }

    struct rusage ru_after;
    getrusage(RUSAGE_SELF, &ru_after);
    size_t rss_after = current_rss_kb();

    std::sort(times.begin(), times.end());
    r.latency_us_min = times.front();
    r.latency_us_median = times[times.size() / 2];
    CpuTime cpu = cpu_time_delta(ru_before, ru_after);
    r.cpu_user_ms = cpu.user_ms;
    r.cpu_sys_ms = cpu.sys_ms;
    r.memory_peak_kb = rss_after > rss_before ? rss_after - rss_before : 0;
    r.throughput_mbs = (size_bytes > 0 && r.latency_us_min > 0.001)
        ? size_bytes / r.latency_us_min / 1000.0
        : 0.0;
    return r;
}

// ----------------------------------------------------------------------------
// YAML output

static void yaml_escape(const std::string& s, char* out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; i < s.size() && j < out_sz - 1; i++) {
        char c = s[i];
        if (c == '\'' ) { out[j++] = '\''; out[j++] = '\''; }
        else out[j++] = c;
    }
    out[j] = 0;
}

static void write_yaml(const Library& lib, const char* out_dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.yaml", out_dir, lib.name.c_str());
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    char esc[256];

    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", localtime(&now));

    fprintf(f, "# Generated by bench_matrix — do not edit\n");
    fprintf(f, "meta:\n");
    yaml_escape(lib.name, esc, sizeof(esc));
    fprintf(f, "  library: '%s'\n", esc);
    yaml_escape(lib.version, esc, sizeof(esc));
    fprintf(f, "  version: '%s'\n", esc);
    fprintf(f, "  timestamp: '%s'\n", ts);
    fprintf(f, "  platform: '%s'\n",
#if defined(__APPLE__)
            "macOS-arm64"
#elif defined(__linux__)
            "Linux-x86_64"
#else
            "unknown"
#endif
    );
    fprintf(f, "\nbenchmarks:\n");

    for (const auto& r : lib.results) {
        if (!r.applicable) continue;
        yaml_escape(r.id, esc, sizeof(esc));
        fprintf(f, "  - id: '%s'\n", esc);
        yaml_escape(r.feature, esc, sizeof(esc));
        fprintf(f, "    feature: '%s'\n", esc);
        yaml_escape(r.input, esc, sizeof(esc));
        fprintf(f, "    input: '%s'\n", esc);
        fprintf(f, "    size_bytes: %zu\n", r.size_bytes);
        fprintf(f, "    iterations: %d\n", r.iterations);
        fprintf(f, "    metrics:\n");
        fprintf(f, "      latency_us:\n");
        fprintf(f, "        min: %.1f\n", r.latency_us_min);
        fprintf(f, "        median: %.1f\n", r.latency_us_median);
        if (r.throughput_mbs > 0.01)
            fprintf(f, "      throughput_mbs: %.2f\n", r.throughput_mbs);
        fprintf(f, "      cpu_user_ms: %.2f\n", r.cpu_user_ms);
        fprintf(f, "      cpu_sys_ms: %.2f\n", r.cpu_sys_ms);
        fprintf(f, "      memory_peak_kb: %zu\n", r.memory_peak_kb);
    }
    fclose(f);
    printf("  wrote %s\n", path);
}

// ----------------------------------------------------------------------------
// Benchmark implementations

// -- DOM Parse --

static void bench_dom_parse(Library& lib, const std::vector<Fixture>& fixtures,
                            int iters) {
    for (const auto& fx : fixtures) {
        char id[128];
        snprintf(id, sizeof(id), "dom_parse_%s", fx.label.c_str());
        size_t len = fx.xml.size();
        const char* data = fx.xml.data();

        if (lib.name == "taurus") {
            lib.add(run_bench(id, "DOM parse", fx.label.c_str(), len, iters,
                [&]() {
                    TaurusStatus st = (TaurusStatus)0;
                    TaurusDocument d = taurus_parse_string(data, len, &st);
                    if (d) taurus_document_free(d);
                }));
        }
#ifdef HAVE_PUGIXML
        else if (lib.name == "pugixml") {
            lib.add(run_bench(id, "DOM parse", fx.label.c_str(), len, iters,
                [&]() {
                    pugi::xml_document doc;
                    doc.load_buffer(data, len);
                }));
        }
#endif
#ifdef HAVE_LIBXML2
        else if (lib.name == "libxml2") {
            lib.add(run_bench(id, "DOM parse", fx.label.c_str(), len, iters,
                [&]() {
                    xmlDocPtr doc = xmlReadMemory(data, (int)len, NULL, NULL, 0);
                    if (doc) xmlFreeDoc(doc);
                }));
        }
#endif
    }
}

// -- SAX Parse (taurus + libxml2) --

static volatile size_t g_sax_events;
static void ts_start(void* u, const char* n, const char** a) {
    (void)u; (void)n; (void)a; g_sax_events++;
}
static void ts_end(void* u, const char* n) { (void)u; (void)n; g_sax_events++; }
static void ts_chars(void* u, const char* t, size_t l) {
    (void)u; (void)t; (void)l; g_sax_events++;
}

static void xs_start(void* u, const xmlChar* local, const xmlChar* pfx,
                     const xmlChar* uri, int n_ns, const xmlChar** ns,
                     int n_attr, int n_def, const xmlChar** attr) {
    (void)u; (void)local; (void)pfx; (void)uri; (void)n_ns; (void)ns;
    (void)n_attr; (void)n_def; (void)attr;
    g_sax_events++;
}
static void xs_end(void* u, const xmlChar* local, const xmlChar* pfx,
                   const xmlChar* uri) {
    (void)u; (void)local; (void)pfx; (void)uri;
    g_sax_events++;
}
static void xs_chars(void* u, const xmlChar* t, int l) {
    (void)u; (void)t; (void)l; g_sax_events++;
}

static void bench_sax_parse(Library& lib, const std::vector<Fixture>& fixtures,
                            int iters) {
    for (const auto& fx : fixtures) {
        char id[128];
        snprintf(id, sizeof(id), "sax_parse_%s", fx.label.c_str());
        size_t len = fx.xml.size();
        const char* data = fx.xml.data();

        if (lib.name == "taurus") {
            lib.add(run_bench(id, "SAX parse", fx.label.c_str(), len, iters,
                [&]() {
                    TaurusSAXHandler h;
                    memset(&h, 0, sizeof(h));
                    h.start_element = ts_start;
                    h.end_element = ts_end;
                    h.characters = ts_chars;
                    taurus_sax_parse(data, len, &h, NULL);
                }));
        }
#ifdef HAVE_LIBXML2
        else if (lib.name == "libxml2") {
            lib.add(run_bench(id, "SAX parse", fx.label.c_str(), len, iters,
                [&]() {
                    xmlSAXHandler sax;
                    memset(&sax, 0, sizeof(sax));
                    sax.initialized = XML_SAX2_MAGIC;
                    sax.startElementNs = xs_start;
                    sax.endElementNs = xs_end;
                    sax.characters = xs_chars;
                    xmlSAXUserParseMemory(&sax, NULL, data, (int)len);
                }));
        }
#endif
        // pugixml: no SAX interface — omitted
    }
}

// -- Serialize (taurus + pugixml) --

static void bench_serialize(Library& lib, const Fixture& attr_heavy,
                            const Fixture& text_heavy, int iters) {
    struct Shape { const Fixture* fx; const char* label; };
    Shape shapes[] = {{&attr_heavy, "attr"}, {&text_heavy, "text"}};

    for (auto& sh : shapes) {
        char id[128];
        snprintf(id, sizeof(id), "serialize_%s", sh.label);
        size_t len = sh.fx->xml.size();
        const char* data = sh.fx->xml.data();

        if (lib.name == "taurus") {
            TaurusStatus st = (TaurusStatus)0;
            TaurusDocument doc = taurus_parse_string(data, len, &st);
            lib.add(run_bench(id, "serialize", sh.label, len, iters,
                [&]() {
                    char* s = taurus_document_serialize(doc, NULL);
                    if (s) free(s);
                }));
            taurus_document_free(doc);
        }
#ifdef HAVE_PUGIXML
        else if (lib.name == "pugixml") {
            pugi::xml_document doc;
            doc.load_buffer(data, len);
            struct Sink : pugi::xml_writer {
                std::string out;
                void write(const void* d, size_t sz) override {
                    out.append((const char*)d, sz);
                }
            };
            lib.add(run_bench(id, "serialize", sh.label, len, iters,
                [&]() {
                    Sink w;
                    doc.save(w, "", pugi::format_raw | pugi::format_no_declaration);
                }));
        }
#endif
#ifdef HAVE_LIBXML2
        else if (lib.name == "libxml2") {
            xmlDocPtr doc = xmlReadMemory(data, (int)len, NULL, NULL, 0);
            if (!doc) continue;
            lib.add(run_bench(id, "serialize", sh.label, len, iters,
                [&]() {
                    xmlBufferPtr buf = xmlBufferCreate();
                    xmlNodeDump(buf, doc, xmlDocGetRootElement(doc),
                                0, 0);
                    xmlBufferFree(buf);
                }));
            xmlFreeDoc(doc);
        }
#endif
    }
}

// -- Mutation (taurus + pugixml) --

static void bench_mutation(Library& lib, int iters) {
    if (lib.name == "taurus") {
        lib.add(run_bench("mutation_append_10k", "mutation", "append-10k", 0, iters,
            [&]() {
                TaurusStatus st = (TaurusStatus)0;
                TaurusDocument d = taurus_parse_string("<r/>", 4, &st);
                TaurusElement root = taurus_document_root(d);
                for (int i = 0; i < 10000; i++) {
                    TaurusElement c = taurus_element_create(d, "c");
                    taurus_element_append_child(root, c);
                }
                taurus_document_free(d);
            }));
        lib.add(run_bench("mutation_set_attr_2k", "mutation", "set-attr-2k", 0, iters,
            [&]() {
                TaurusStatus st = (TaurusStatus)0;
                TaurusDocument d = taurus_parse_string("<r/>", 4, &st);
                TaurusElement root = taurus_document_root(d);
                char n[16], v[16];
                for (int i = 0; i < 2000; i++) {
                    snprintf(n, 16, "a%d", i);
                    snprintf(v, 16, "v%d", i);
                    taurus_element_set_attribute(root, n, v);
                }
                taurus_document_free(d);
            }));
    }
#ifdef HAVE_PUGIXML
    else if (lib.name == "pugixml") {
        lib.add(run_bench("mutation_append_10k", "mutation", "append-10k", 0, iters,
            [&]() {
                pugi::xml_document d;
                pugi::xml_node root = d.append_child("r");
                for (int i = 0; i < 10000; i++) root.append_child("c");
            }));
        lib.add(run_bench("mutation_set_attr_2k", "mutation", "set-attr-2k", 0, iters,
            [&]() {
                pugi::xml_document d;
                pugi::xml_node root = d.append_child("r");
                char n[16], v[16];
                for (int i = 0; i < 2000; i++) {
                    snprintf(n, 16, "a%d", i);
                    snprintf(v, 16, "v%d", i);
                    root.append_attribute(n).set_value(v);
                }
            }));
    }
#endif
#ifdef HAVE_LIBXML2
    else if (lib.name == "libxml2") {
        lib.add(run_bench("mutation_append_10k", "mutation", "append-10k", 0, iters,
            [&]() {
                xmlDocPtr d = xmlNewDoc(NULL);
                xmlNodePtr root = xmlNewNode(NULL, (const xmlChar*)"r");
                xmlDocSetRootElement(d, root);
                for (int i = 0; i < 10000; i++) {
                    xmlNewTextChild(root, NULL, (const xmlChar*)"c", NULL);
                }
                xmlFreeDoc(d);
            }));
        lib.add(run_bench("mutation_set_attr_2k", "mutation", "set-attr-2k", 0, iters,
            [&]() {
                xmlDocPtr d = xmlNewDoc(NULL);
                xmlNodePtr root = xmlNewNode(NULL, (const xmlChar*)"r");
                xmlDocSetRootElement(d, root);
                char n[16], v[16];
                for (int i = 0; i < 2000; i++) {
                    snprintf(n, 16, "a%d", i);
                    snprintf(v, 16, "v%d", i);
                    xmlSetProp(root, (const xmlChar*)n, (const xmlChar*)v);
                }
                xmlFreeDoc(d);
            }));
    }
#endif
}

// -- XPath (taurus + pugixml + libxml2) --

static void bench_xpath(Library& lib, const Fixture& catalog, int iters) {
    const char* queries[] = {
        "//item[@cat='3']",
        "count(//item)",
        "//item[price > 50.00]",
    };
    size_t len = catalog.xml.size();
    const char* data = catalog.xml.data();

    for (int q = 0; q < 3; q++) {
        char id[128];
        snprintf(id, sizeof(id), "xpath_q%d", q);

        if (lib.name == "taurus") {
            TaurusStatus st = (TaurusStatus)0;
            TaurusDocument doc = taurus_parse_string(data, len, &st);
            lib.add(run_bench(id, "XPath", queries[q], len, iters,
                [&]() {
                    TaurusXPathResult r = taurus_xpath_eval(doc, NULL, queries[q]);
                    if (r) taurus_xpath_result_free(r);
                }));
            taurus_document_free(doc);
        }
#ifdef HAVE_PUGIXML
        else if (lib.name == "pugixml") {
            pugi::xml_document doc;
            doc.load_buffer(data, len);
            lib.add(run_bench(id, "XPath", queries[q], len, iters,
                [&]() {
                    pugi::xpath_query qy(queries[q]);
                    if (strncmp(queries[q], "count(", 6) == 0) {
                        (void)qy.evaluate_number(doc);
                    } else {
                        pugi::xpath_node_set ns = qy.evaluate_node_set(doc);
                        (void)ns;
                    }
                }));
        }
#endif
#ifdef HAVE_LIBXML2
        else if (lib.name == "libxml2") {
            xmlDocPtr doc = xmlReadMemory(data, (int)len, NULL, NULL, 0);
            xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
            lib.add(run_bench(id, "XPath", queries[q], len, iters,
                [&]() {
                    xmlXPathObjectPtr res =
                        xmlXPathEvalExpression((const xmlChar*)queries[q], ctx);
                    if (res) xmlXPathFreeObject(res);
                }));
            xmlXPathFreeContext(ctx);
            xmlFreeDoc(doc);
        }
#endif
    }
}

// ----------------------------------------------------------------------------
// Main

int main(int argc, char** argv) {
    const char* out_dir = "bench-matrix-results";
    int iters = 20;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iters = atoi(argv[++i]);
        } else {
            out_dir = argv[i];
        }
    }

    printf("bench_matrix — full feature matrix (iterations=%d)\n", iters);
    printf("output: %s/\n\n", out_dir);

#ifdef HAVE_LIBXML2
    xmlInitParser();
#endif

    // Generate fixtures
    std::vector<Fixture> parse_fixtures;
    parse_fixtures.push_back(gen_text_heavy());
    parse_fixtures.push_back(gen_attr_heavy());
    parse_fixtures.push_back(gen_pretty());
    parse_fixtures.push_back(gen_catalog(5000));

    Fixture attr_heavy = gen_attr_heavy();
    Fixture text_heavy = gen_text_heavy();
    Fixture catalog = gen_catalog(5000);

    std::vector<Library> libraries;

    // taurus
    {
        Library lib;
        lib.name = "taurus";
        lib.version = "0.25.11";
        printf("running taurus...\n");
        bench_dom_parse(lib, parse_fixtures, iters);
        bench_sax_parse(lib, parse_fixtures, iters);
        bench_serialize(lib, attr_heavy, text_heavy, iters);
        bench_mutation(lib, iters);
        bench_xpath(lib, catalog, iters);
        libraries.push_back(lib);
    }

#ifdef HAVE_PUGIXML
    {
        Library lib;
        lib.name = "pugixml";
        lib.version = "1.16";
        printf("running pugixml...\n");
        bench_dom_parse(lib, parse_fixtures, iters);
        bench_serialize(lib, attr_heavy, text_heavy, iters);
        bench_mutation(lib, iters);
        bench_xpath(lib, catalog, iters);
        libraries.push_back(lib);
    }
#endif

#ifdef HAVE_LIBXML2
    {
        Library lib;
        lib.name = "libxml2";
        lib.version = "2.9";
        printf("running libxml2...\n");
        bench_dom_parse(lib, parse_fixtures, iters);
        bench_sax_parse(lib, parse_fixtures, iters);
        bench_serialize(lib, attr_heavy, text_heavy, iters);
        bench_mutation(lib, iters);
        bench_xpath(lib, catalog, iters);
        libraries.push_back(lib);
    }
#endif

    // Write YAML files
    for (const auto& lib : libraries) {
        write_yaml(lib, out_dir);
    }

    // Print summary matrix
    printf("\n=== Summary (latency min µs / throughput MB/s) ===\n");
    printf("%-30s", "benchmark");
    for (const auto& lib : libraries) printf("%16s", lib.name.c_str());
    printf("\n");

    // Collect all unique benchmark ids in order
    std::vector<std::string> ids;
    for (const auto& lib : libraries) {
        for (const auto& r : lib.results) {
            if (std::find(ids.begin(), ids.end(), r.id) == ids.end())
                ids.push_back(r.id);
        }
    }
    for (const auto& id : ids) {
        printf("%-30s", id.c_str());
        for (const auto& lib : libraries) {
            const BenchResult* found = nullptr;
            for (const auto& r : lib.results) {
                if (r.id == id) { found = &r; break; }
            }
            if (found)
                printf("%9.0f/%5.1f", found->latency_us_min, found->throughput_mbs);
            else
                printf("%16s", "—");
        }
        printf("\n");
    }

    printf("\nDone. Run: python3 benchmarks/matrix/bench2html.py %s/*.yaml "
           "--output bench_report.html\n", out_dir);
    return 0;
}
