// test/parser/test_large_documents.cpp — large-document survivability.
//
// Every regression in the v0.26.x line that reached users was
// size- or layout-dependent (#450: sibling links truncated once the
// parse-time block gap crossed 256 KB; stale-arena reads only
// visible when retained blocks recycled dirty pages). Small-fixture
// suites cannot see those. These specs generate documents at sizes
// that cross every known boundary — including tens of megabytes —
// and run the full lifecycle on each: parse → complete tree walk →
// XPath → serialize → reparse → idempotence check → free, twice
// (the second cycle exercises retained-block reuse). A mutation
// spec builds thousands of nodes inside a boundary-crossing tree.
//
// Under sanitizers the top sizes are cut down (memory + time); the
// boundary-crossing sizes that reproduced #450 stay in every build.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

/* Public API documents node types as integers. */
constexpr int kNodeTypeElement = 0;

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#    define LEPTRIS_STRESS_SANITIZER 1
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define LEPTRIS_STRESS_SANITIZER 1
#endif

/* Byte sizes to sweep. 90 KB is the #450 reproduction threshold
 * (block gap crosses cp16 range); 300 KB/1 MB add margin; the big
 * ones prove nothing dies at scale. */
std::vector<size_t> stress_sizes() {
#ifdef LEPTRIS_STRESS_SANITIZER
    return {30u << 10, 90u << 10, 300u << 10, 1u << 20};
#else
    return {30u << 10, 90u << 10, 300u << 10,
            1u << 20, 4u << 20, 16u << 20, 48u << 20};
#endif
}

/* ---- deterministic generators (no fixtures: CI boxes vary) ---- */

std::string gen_pretty_mixed(size_t target) {
    /* The #450 shape: whitespace text nodes followed by element
     * siblings, declaration with encoding. */
    std::string b = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<users>\n";
    b.reserve(target + 256);
    int i = 0;
    while (b.size() < target) {
        char buf[192];
        std::snprintf(buf, sizeof buf,
            "<user id=\"%d\"><name>User %d</name>"
            "<email>user%d@example.com</email>"
            "<created>2023-01-%02dT10:00:00Z</created>"
            "<profile><age>%d</age><city>City %d</city></profile></user>\n",
            i, i, i, (i % 28) + 1, 20 + i % 50, i % 100);
        b += buf;
        i++;
    }
    b += "</users>\n";
    return b;
}

std::string gen_attr_heavy(size_t target) {
    std::string b = "<r>";
    b.reserve(target + 256);
    int e = 0;
    while (b.size() < target) {
        char buf[256];
        int n = std::snprintf(buf, sizeof buf, "<e");
        for (int a = 0; a < 8; a++)
            n += std::snprintf(buf + n, sizeof buf - (size_t)n,
                               " k%d='v%d'", a, (e + a) % 97);
        n += std::snprintf(buf + n, sizeof buf - (size_t)n, "/>");
        b += buf;
        e++;
    }
    b += "</r>";
    return b;
}

std::string gen_text_heavy(size_t target) {
    std::string b = "<r>";
    b.reserve(target + 64);
    const char* word = "lorem_ipsum_dolor_sit_amet_";
    while (b.size() < target) { b += word; b += ' '; }
    b += "</r>";
    return b;
}

std::string gen_deep(size_t target) {
    /* Wide-shallow with periodic deep spines up to the parser's
     * depth limit (DP_MAX_DEPTH = 256; deeper input is rejected by
     * design). */
    std::string b = "<r>";
    b.reserve(target + 256);
    int i = 0;
    while (b.size() < target) {
        int depth = 1 + (i % 200);
        for (int d = 0; d < depth; d++) b += "<d>";
        b += "<leaf/>";
        for (int d = 0; d < depth; d++) b += "</d>";
        i++;
    }
    b += "</r>";
    return b;
}

std::string gen_markup_rich(size_t target) {
    /* Comments, CDATA and PIs interleaved with text — the node
     * types whose sibling edges are STILL cp16 (encoder-routed
     * since #450): large sizes prove the far-pair path holds. */
    std::string b = "<r>";
    b.reserve(target + 256);
    int i = 0;
    while (b.size() < target) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
            "<e n=\"%d\">text %d<!-- comment %d -->"
            "<![CDATA[raw < > & %d]]><?pi data%d?>tail</e>",
            i, i, i, i, i);
        b += buf;
        i++;
    }
    b += "</r>";
    return b;
}

std::string gen_entities(size_t target) {
    std::string b = "<r>";
    b.reserve(target + 256);
    int i = 0;
    while (b.size() < target) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
            "<e a=\"%d &amp; %d\" b='x &lt;%d&gt;'>"
            "%d &amp; more &lt;text&gt; %d</e>", i, i, i, i, i);
        b += buf;
        i++;
    }
    b += "</r>";
    return b;
}

/* ---- lifecycle ---- */

/* Iterative full walk: every element's whole child chain, element
 * children pushed to a work stack. This is the traversal that died
 * in #450 — a truncated sibling edge walks into garbage (crash,
 * caught by ASAN/segv) or loops forever (bounded by the guard). */
size_t walk_tree(LeptrisElement root, size_t node_limit) {
    size_t visited = 0;
    std::vector<LeptrisElement> work;
    work.push_back(root);
    while (!work.empty()) {
        LeptrisElement e = work.back();
        work.pop_back();
        visited++;
        if (visited > node_limit) return visited; /* cycle guard */
        for (LeptrisNodeRef c = leptris_node_first_child((LeptrisNodeRef)e); c;
             c = leptris_node_next_sibling(c)) {
            visited++;
            if (visited > node_limit) return visited;
            if (leptris_node_get_type(c) == kNodeTypeElement)
                work.push_back((LeptrisElement)c);
        }
    }
    return visited;
}

void run_lifecycle_exact(const std::string& xml,
                         const std::string& expected) {
    for (int cycle = 0; cycle < 2; cycle++) {
        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
        ASSERT_NE(doc, nullptr) << "cycle " << cycle << ": parse failed";
        LeptrisElement root = leptris_document_root(doc);
        ASSERT_NE(root, nullptr);
        ASSERT_GE(walk_tree(root, 100u << 20), 2u);
        char* out = leptris_document_serialize(doc, NULL);
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(std::string(out), expected);
        leptris_free_string(out);
        leptris_document_free(doc);
    }
}

void run_lifecycle(const std::string& xml, bool byte_exact_round_trip) {
    /* Two cycles: the second parse reuses retained arena blocks —
     * the dirty-memory class behind #450's stale reads. */
    for (int cycle = 0; cycle < 2; cycle++) {
        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
        ASSERT_NE(doc, nullptr) << "cycle " << cycle << ": parse failed";

        LeptrisElement root = leptris_document_root(doc);
        ASSERT_NE(root, nullptr);

        size_t visited = walk_tree(root, 100u << 20);
        ASSERT_GE(visited, 2u) << "tree walk lost the tree";

        /* XPath over the full tree. */
        LeptrisXPathResult r = leptris_xpath_eval(doc, NULL, "//*");
        ASSERT_NE(r, (LeptrisXPathResult)0);
        leptris_xpath_result_free(r);

        /* Serialize, reparse, re-serialize: output must be
         * idempotent — catches content corruption that survives a
         * single round trip. */
        char* out1 = leptris_document_serialize(doc, NULL);
        ASSERT_NE(out1, nullptr);
        LeptrisDocument doc2 =
            leptris_parse_string(out1, std::strlen(out1), NULL);
        ASSERT_NE(doc2, nullptr) << "reparse of serialized output failed";
        char* out2 = leptris_document_serialize(doc2, NULL);
        ASSERT_NE(out2, nullptr);
        EXPECT_STREQ(out1, out2) << "serialize is not idempotent";

        if (byte_exact_round_trip) {
            /* Entity-free documents round-trip byte-exactly. */
            EXPECT_EQ(std::string(out1), xml);
        }

        leptris_free_string(out2);
        leptris_free_string(out1);
        leptris_document_free(doc2);
        leptris_document_free(doc);
    }
}

using GenFn = std::string (*)(size_t);

void run_shape_sweep(const char* name, GenFn gen, bool byte_exact) {
    for (size_t sz : stress_sizes()) {
        SCOPED_TRACE(std::string(name) + " @ " + std::to_string(sz) + " bytes");
        ASSERT_NO_FATAL_FAILURE(run_lifecycle(gen(sz), byte_exact));
    }
}

}  // namespace

// ---- Size sweeps per shape ---------------------------------------------

TEST(LargeDocuments, PrettyMixedSurvivesAllSizes) {
    /* Serializer drops the whitespace after the declaration and the
     * trailing top-level newline — expected is input minus both. */
    for (size_t sz : stress_sizes()) {
        std::string xml = gen_pretty_mixed(sz);
        SCOPED_TRACE("pretty-mixed @ " + std::to_string(sz));
        std::string trimmed = xml;
        trimmed.erase(strlen("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), 1);
        if (!trimmed.empty() && trimmed.back() == '\n') trimmed.pop_back();
        ASSERT_NO_FATAL_FAILURE(run_lifecycle_exact(xml, trimmed));
    }
}

TEST(LargeDocuments, AttrHeavySurvivesAllSizes) {
    /* Single-quoted attributes re-emit double-quoted — idempotence
     * only; round-trip identity is covered by the reparse. */
    run_shape_sweep("attr-heavy", gen_attr_heavy, false);
}

TEST(LargeDocuments, TextHeavySurvivesAllSizes) {
    run_shape_sweep("text-heavy", gen_text_heavy, true);
}

TEST(LargeDocuments, DeepSpinesSurviveAllSizes) {
    run_shape_sweep("deep", gen_deep, true);
}

TEST(LargeDocuments, MarkupRichSurvivesAllSizes) {
    /* Comments and CDATA emit verbatim — byte-exact holds. */
    run_shape_sweep("markup-rich", gen_markup_rich, true);
}

TEST(LargeDocuments, EntitiesSurviveAllSizes) {
    /* Entity-bearing input re-escapes on output — idempotence only. */
    run_shape_sweep("entities", gen_entities, false);
}

// ---- Mutation at scale (the #450 shape, boundary-crossing size) --------

TEST(LargeDocuments, MutateSerializeAndWalkLargeTree) {
    std::string xml = gen_pretty_mixed(300u << 10);
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Build 2000 fresh elements and attach them to the tree. */
    for (int i = 0; i < 2000; i++) {
        LeptrisElement c = leptris_element_create(doc, "added");
        ASSERT_NE(c, nullptr);
        char n[16], v[16];
        std::snprintf(n, sizeof n, "i%d", i);
        std::snprintf(v, sizeof v, "v%d", i);
        ASSERT_EQ(leptris_element_set_attribute(c, n, v), LEPTRIS_OK);
        ASSERT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);
    }
    EXPECT_GT(leptris_element_child_count(root), 2000u);

    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_GT(std::strlen(out), xml.size());

    /* Walk the mutated tree — mutation-created nodes link across
     * the bump-block/parse-arena gap. */
    EXPECT_GT(walk_tree(root, 100u << 20), 2000u);

    leptris_free_string(out);
    leptris_document_free(doc);
}

// ---- inplace variant at the boundary size ------------------------------

TEST(LargeDocuments, InplaceParseSurvivesBoundarySize) {
    std::string xml = gen_pretty_mixed(90u << 10);
    std::vector<char> buf(xml.begin(), xml.end());
    buf.push_back('\0');
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisDocument doc =
        leptris_parse_string_inplace(buf.data(), buf.size() - 1, &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_GT(walk_tree(root, 100u << 20), 100u);
    char* out = leptris_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    leptris_free_string(out);
    leptris_document_free(doc);
}
