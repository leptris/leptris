/**
 * Real-World Scenario Benchmarks
 *
 * Measures performance in realistic use cases.
 *
 * Tests:
 * 1. Configuration file parsing (small, many attributes)
 * 2. RSS/Atom feed processing (medium, mixed content)
 * 3. SVG processing (deep nesting, namespaces)
 * 4. SOAP/REST response parsing (medium, namespace heavy)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>

// Taurus API (C)
extern "C" {
#include <taurus.h>
}

// pugixml API (C++)
#include <pugixml.hpp>

// libxml2 API (C)
#include <libxml/parser.h>
#include <libxml/xpath.h>

// Benchmark utilities
extern "C" {
#include "utils.h"
}

/* TaurusElement null check helper for C++ code */
static inline bool elem_not_null(const TaurusElement& elem) {
    return !taurus_element_is_null(&elem);
}

// Quick mode for development (set to 0 for full runs)
#define QUICK_MODE 1

#if QUICK_MODE
#define ITERATIONS 100
#define WARMUP_ITERS 10
#else
#define ITERATIONS 1000
#define WARMUP_ITERS 100
#endif

// Read file into string
static std::string read_file(const char* filename) {
    std::ifstream file(filename);
    if (!file) {
        fprintf(stderr, "Error: Cannot open file: %s\n", filename);
        exit(1);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// ============================================================================
// Scenario 1: Configuration File Processing
// ============================================================================

// Generate a typical config file
static std::string generate_config_xml(int num_entries) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<configuration>\n";

    ss << "  <database driver=\"postgresql\" host=\"localhost\" port=\"5432\" "
       << "name=\"mydb\" user=\"admin\" password=\"secret\" "
       << "pool_size=\"10\" timeout=\"30\" ssl=\"true\"/>\n";

    ss << "  <logging level=\"info\" format=\"json\" output=\"file\" "
       << "path=\"/var/log/app.log\" rotation=\"daily\" max_size=\"100MB\" "
       << "retention=\"30\" compress=\"true\"/>\n";

    for (int i = 0; i < num_entries; i++) {
        ss << "  <setting key=\"config_" << i << "\" value=\"" << i * 10 << "\" "
           << "type=\"integer\" category=\"runtime\" enabled=\"true\" "
           << "description=\"Configuration setting number " << i << "\"/>\n";
    }

    ss << "</configuration>";
    return ss.str();
}

static void bench_config_taurus(const std::string& xml) {
    TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);

    // Access all database attributes
    TaurusElement db = taurus_element_first_child_any(root);
    const char* host = taurus_element_attribute(db, "host");
    const char* port = taurus_element_attribute(db, "port");
    const char* name = taurus_element_attribute(db, "name");
    (void)host; (void)port; (void)name;

    // Iterate settings
    int count = 0;
    for (TaurusElement child = taurus_element_first_child_any(root);
         elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
        const char* key = taurus_element_attribute(child, "key");
        const char* value = taurus_element_attribute(child, "value");
        (void)key; (void)value;
        count++;
    }

    taurus_document_free(doc);
}

static void bench_config_pugixml(const std::string& xml) {
    pugi::xml_document doc;
    doc.load_buffer(xml.c_str(), xml.length());

    pugi::xml_node root = doc.root().first_child();

    // Access all database attributes
    pugi::xml_node db = root.first_child();
    const char* host = db.attribute("host").value();
    const char* port = db.attribute("port").value();
    const char* name = db.attribute("name").value();
    (void)host; (void)port; (void)name;

    // Iterate settings
    int count = 0;
    for (pugi::xml_node child : root.children()) {
        const char* key = child.attribute("key").value();
        const char* value = child.attribute("value").value();
        (void)key; (void)value;
        count++;
    }
}

// ============================================================================
// Scenario 2: RSS Feed Processing
// ============================================================================

// Generate RSS-like content
static std::string generate_rss_xml(int num_items) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<rss version=\"2.0\">\n";
    ss << "  <channel>\n";
    ss << "    <title>Example Feed</title>\n";
    ss << "    <link>https://example.com</link>\n";
    ss << "    <description>An example RSS feed</description>\n";
    ss << "    <language>en-us</language>\n";

    for (int i = 0; i < num_items; i++) {
        ss << "    <item>\n";
        ss << "      <title>Item " << i << "</title>\n";
        ss << "      <link>https://example.com/item/" << i << "</link>\n";
        ss << "      <description>This is the description for item " << i
           << ". It contains some text content.</description>\n";
        ss << "      <pubDate>Mon, 01 Jan 2024 00:00:00 +0000</pubDate>\n";
        ss << "      <guid isPermaLink=\"true\">https://example.com/item/" << i << "</guid>\n";
        ss << "    </item>\n";
    }

    ss << "  </channel>\n";
    ss << "</rss>";
    return ss.str();
}

static void bench_rss_taurus(const std::string& xml) {
    TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);

    // XPath to get all items
    TaurusXPathResult result = taurus_xpath_eval(doc, root, "//item");
    if (result) {
        size_t count = taurus_xpath_result_count(result);
        for (size_t i = 0; i < count; i++) {
            TaurusElement item = taurus_xpath_result_get(result, i);
            // Get title and link
            for (TaurusElement child = taurus_element_first_child_any(item);
                 elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
                const char* name = taurus_element_name(child);
                if (strcmp(name, "title") == 0 || strcmp(name, "link") == 0) {
                    const char* text = taurus_element_text(child);
                    (void)text;
                }
            }
        }
        taurus_xpath_result_free(result);
    }

    taurus_document_free(doc);
}

static void bench_rss_pugixml(const std::string& xml) {
    pugi::xml_document doc;
    doc.load_buffer(xml.c_str(), xml.length());

    // XPath to get all items
    pugi::xpath_node_set items = doc.select_nodes("//item");
    for (pugi::xpath_node item : items) {
        // Get title and link
        for (pugi::xml_node child : item.node().children()) {
            const char* name = child.name();
            if (strcmp(name, "title") == 0 || strcmp(name, "link") == 0) {
                const char* text = child.text().get();
                (void)text;
            }
        }
    }
}

// ============================================================================
// Scenario 3: SVG Processing
// ============================================================================

// Generate SVG-like content
static std::string generate_svg_xml(int num_elements) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
       << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
       << "width=\"800\" height=\"600\" viewBox=\"0 0 800 600\">\n";

    ss << "  <defs>\n";
    ss << "    <linearGradient id=\"grad1\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"0%\">\n";
    ss << "      <stop offset=\"0%\" style=\"stop-color:rgb(255,255,0)\"/>\n";
    ss << "      <stop offset=\"100%\" style=\"stop-color:rgb(255,0,0)\"/>\n";
    ss << "    </linearGradient>\n";
    ss << "  </defs>\n";

    ss << "  <g id=\"layer1\">\n";
    for (int i = 0; i < num_elements; i++) {
        ss << "    <rect x=\"" << (i % 10) * 80 << "\" y=\"" << (i / 10) * 60
           << "\" width=\"70\" height=\"50\" fill=\"url(#grad1)\" "
           << "stroke=\"black\" stroke-width=\"1\" id=\"rect" << i << "\"/>\n";
    }
    ss << "  </g>\n";

    ss << "  <g id=\"layer2\">\n";
    for (int i = 0; i < num_elements / 2; i++) {
        ss << "    <circle cx=\"" << 40 + (i % 10) * 80 << "\" cy=\""
           << 30 + (i / 10) * 60 << "\" r=\"25\" fill=\"blue\" "
           << "opacity=\"0.5\" id=\"circle" << i << "\"/>\n";
    }
    ss << "  </g>\n";

    ss << "</svg>";
    return ss.str();
}

static void bench_svg_taurus(const std::string& xml) {
    TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);

    // Walk all elements recursively
    int count = 0;
    std::vector<TaurusElement> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        TaurusElement elem = stack.back();
        stack.pop_back();

        const char* name = taurus_element_name(elem);
        if (strcmp(name, "rect") == 0) {
            const char* x = taurus_element_attribute(elem, "x");
            const char* y = taurus_element_attribute(elem, "y");
            const char* fill = taurus_element_attribute(elem, "fill");
            (void)x; (void)y; (void)fill;
        } else if (strcmp(name, "circle") == 0) {
            const char* cx = taurus_element_attribute(elem, "cx");
            const char* cy = taurus_element_attribute(elem, "cy");
            (void)cx; (void)cy;
        }

        for (TaurusElement child = taurus_element_first_child_any(elem);
             elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
            stack.push_back(child);
        }
        count++;
    }

    taurus_document_free(doc);
}

static void bench_svg_pugixml(const std::string& xml) {
    pugi::xml_document doc;
    doc.load_buffer(xml.c_str(), xml.length());

    pugi::xml_node root = doc.root().first_child();

    // Walk all elements recursively
    int count = 0;
    std::vector<pugi::xml_node> stack;
    stack.push_back(root);

    while (!stack.empty()) {
        pugi::xml_node node = stack.back();
        stack.pop_back();

        const char* name = node.name();
        if (strcmp(name, "rect") == 0) {
            const char* x = node.attribute("x").value();
            const char* y = node.attribute("y").value();
            const char* fill = node.attribute("fill").value();
            (void)x; (void)y; (void)fill;
        } else if (strcmp(name, "circle") == 0) {
            const char* cx = node.attribute("cx").value();
            const char* cy = node.attribute("cy").value();
            (void)cx; (void)cy;
        }

        for (pugi::xml_node child : node.children()) {
            stack.push_back(child);
        }
        count++;
    }
}

// ============================================================================
// Scenario 4: SOAP Response Processing
// ============================================================================

// Generate SOAP-like content
static std::string generate_soap_xml(int num_records) {
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
       << "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
       << "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n";
    ss << "  <soap:Body>\n";
    ss << "    <GetUsersResponse xmlns=\"http://example.com/users\">\n";
    ss << "      <GetUsersResult>\n";

    for (int i = 0; i < num_records; i++) {
        ss << "        <User>\n";
        ss << "          <Id>" << i + 1 << "</Id>\n";
        ss << "          <Name>User " << i << "</Name>\n";
        ss << "          <Email>user" << i << "@example.com</Email>\n";
        ss << "          <Active>true</Active>\n";
        ss << "          <Created>2024-01-01T00:00:00Z</Created>\n";
        ss << "        </User>\n";
    }

    ss << "      </GetUsersResult>\n";
    ss << "    </GetUsersResponse>\n";
    ss << "  </soap:Body>\n";
    ss << "</soap:Envelope>";
    return ss.str();
}

static void bench_soap_taurus(const std::string& xml) {
    TaurusDocument doc = taurus_parse_string(xml.c_str(), xml.length(), NULL);
    if (!doc) return;

    TaurusElement root = taurus_document_root(doc);

    // XPath to get all users
    TaurusXPathResult result = taurus_xpath_eval(doc, root, "//*[local-name()='User']");
    if (result) {
        size_t count = taurus_xpath_result_count(result);
        for (size_t i = 0; i < count; i++) {
            TaurusElement user = taurus_xpath_result_get(result, i);
            // Get child elements
            for (TaurusElement child = taurus_element_first_child_any(user);
                 elem_not_null(child); child = taurus_element_next_sibling_any(child)) {
                const char* text = taurus_element_text(child);
                (void)text;
            }
        }
        taurus_xpath_result_free(result);
    }

    taurus_document_free(doc);
}

static void bench_soap_pugixml(const std::string& xml) {
    pugi::xml_document doc;
    doc.load_buffer(xml.c_str(), xml.length());

    // XPath to get all users
    pugi::xpath_node_set users = doc.select_nodes("//*[local-name()='User']");
    for (pugi::xpath_node user : users) {
        // Get child elements
        for (pugi::xml_node child : user.node().children()) {
            const char* text = child.text().get();
            (void)text;
        }
    }
}

// ============================================================================
// Benchmark Runner
// ============================================================================

typedef void (*scenario_func_taurus_t)(const std::string&);
typedef void (*scenario_func_pugixml_t)(const std::string&);

static void run_scenario_benchmark(const char* name,
                                   scenario_func_taurus_t taurus_fn,
                                   scenario_func_pugixml_t pugixml_fn,
                                   const std::string& xml) {
    printf("\n=== %s (%.1f KB) ===\n", name, xml.length() / 1024.0);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; i++) {
        taurus_fn(xml);
        pugixml_fn(xml);
    }

    // Measure Taurus
    std::vector<double> taurus_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        taurus_fn(xml);
        long end = benchmark_time_us();
        taurus_times.push_back((double)(end - start));
    }
    benchmark_stats taurus_stats = benchmark_analyze(taurus_times.data(), ITERATIONS);

    // Measure pugixml
    std::vector<double> pugixml_times;
    for (int i = 0; i < ITERATIONS; i++) {
        long start = benchmark_time_us();
        pugixml_fn(xml);
        long end = benchmark_time_us();
        pugixml_times.push_back((double)(end - start));
    }
    benchmark_stats pugixml_stats = benchmark_analyze(pugixml_times.data(), ITERATIONS);

    // Print results
    double speedup = pugixml_stats.median / taurus_stats.median;
    printf("  Taurus:   %8.2f us (median)\n", taurus_stats.median);
    printf("  pugixml:  %8.2f us (median)\n", pugixml_stats.median);
    printf("  Speedup:  %.2fx %s\n", speedup,
           speedup >= 1.0 ? "PASS" : speedup >= 0.8 ? "OK" : "FAIL");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Initialize libxml2
    xmlInitParser();

    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║          Real-World Scenario Benchmarks (4 tests)         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Scenario 1: Configuration File
    std::string config_xml = generate_config_xml(100);
    run_scenario_benchmark("Config File Processing",
                          bench_config_taurus, bench_config_pugixml,
                          config_xml);

    // Scenario 2: RSS Feed
    std::string rss_xml = generate_rss_xml(50);
    run_scenario_benchmark("RSS Feed Processing",
                          bench_rss_taurus, bench_rss_pugixml,
                          rss_xml);

    // Scenario 3: SVG Processing
    std::string svg_xml = generate_svg_xml(100);
    run_scenario_benchmark("SVG Processing",
                          bench_svg_taurus, bench_svg_pugixml,
                          svg_xml);

    // Scenario 4: SOAP Response
    std::string soap_xml = generate_soap_xml(50);
    run_scenario_benchmark("SOAP Response Processing",
                          bench_soap_taurus, bench_soap_pugixml,
                          soap_xml);

    // Cleanup
    xmlCleanupParser();

    printf("\n");
    printf("Real-World Scenario Benchmarks Complete\n");
    printf("\n");

    return 0;
}
