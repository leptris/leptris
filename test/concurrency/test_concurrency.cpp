/* TODO.concurrency — public-API specs for the threading / error /
 * batch-copy pack (items 01, 03, 07, 08). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/error.h"
}
#include <string>
#include <thread>
#include <vector>

static const char* kDoc =
    "<library><book n='1'><title>A</title></book>"
    "<book n='4'><title>B</title></book>"
    "<book n='9'><title>C</title></book></library>";

/* TODO.concurrency/08: independent documents on independent threads
 * never interact — parse, evaluate, serialize, free, repeat. */
TEST(Threading, ConcurrentParseEvalSerializeFree) {
    std::vector<std::thread> threads;
    std::vector<int> failures(4, 0);
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&failures, t]() {
            for (int i = 0; i < 50; i++) {
                LeptrisStatus st = LEPTRIS_OK;
                LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), &st);
                if (!doc) { failures[t]++; continue; }
                LeptrisXPathResult r = leptris_xpath_eval(
                    doc, nullptr, "count(//book[@n > 3])");
                if (!r) { failures[t]++; leptris_document_free(doc); continue; }
                if (leptris_xpath_result_number(r) != 2.0) failures[t]++;
                leptris_xpath_result_free(r);
                char* xml = leptris_document_serialize(doc, nullptr);
                if (!xml || std::string(xml).find("<library>") != 0) failures[t]++;
                leptris_free_string(xml);
                leptris_document_free(doc);
            }
            /* TODO.concurrency/08: drain this thread's caches so the
             * worker exits without retaining free-list entries. */
            leptris_thread_cleanup();
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < 4; t++) EXPECT_EQ(failures[t], 0) << "thread " << t;
}

/* TODO.concurrency/01: leptris_last_error is thread-local — an error
 * recorded on the main thread is invisible to fresh threads, and each
 * thread records its own errors independently. */
TEST(Threading, LastErrorIsThreadLocal) {
    LeptrisStatus st = LEPTRIS_OK;
    leptris_parse_string("<unclosed>", 10, &st);
    EXPECT_NE(st, LEPTRIS_OK);
    ASSERT_NE(leptris_last_error(), nullptr);

    std::vector<std::string> seen(2);
    std::vector<int> unexpected(2, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&seen, &unexpected, t]() {
            if (leptris_last_error() != nullptr) unexpected[t]++;
            LeptrisStatus s = LEPTRIS_OK;
            char buf[32];
            snprintf(buf, sizeof(buf), "<a-%d", t);
            leptris_parse_string(buf, strlen(buf), &s);
            const char* msg = leptris_last_error();
            if (!msg || !*msg) unexpected[t]++;
            else seen[t] = msg;
            leptris_thread_cleanup();
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < 2; t++) EXPECT_EQ(unexpected[t], 0) << "thread " << t;
    EXPECT_FALSE(seen[0].empty());
    EXPECT_FALSE(seen[1].empty());

    /* Main-thread slot survived the workers untouched. */
    ASSERT_NE(leptris_last_error(), nullptr);
}

/* TODO.concurrency/01: leptris_document_last_error is per-document —
 * a failing evaluation on one document says nothing about another. */
TEST(Threading, PerDocumentErrorSlot) {
    LeptrisDocument doc1 = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    LeptrisDocument doc2 = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_TRUE(doc1 && doc2);

    EXPECT_EQ(leptris_document_last_error(doc1), nullptr);

    LeptrisXPathResult bad = leptris_xpath_eval(doc1, nullptr, "count(///bad[)");
    EXPECT_EQ(bad, nullptr);
    const char* msg = leptris_document_last_error(doc1);
    ASSERT_NE(msg, nullptr);
    EXPECT_NE(*msg, '\0');

    /* A GOOD evaluation clears nothing (last *failed* operation), but
     * doc2 was never involved — its slot stays empty. */
    LeptrisXPathResult good = leptris_xpath_eval(doc2, nullptr, "count(//book)");
    ASSERT_NE(good, nullptr);
    leptris_xpath_result_free(good);
    EXPECT_EQ(leptris_document_last_error(doc2), nullptr);
    EXPECT_NE(leptris_document_last_error(doc1), nullptr);

    /* NULL doc is not an error. */
    EXPECT_EQ(leptris_document_last_error(nullptr), nullptr);

    leptris_document_free(doc1);
    leptris_document_free(doc2);
}

/* TODO.concurrency/03: get_nodes_ex copies EVERY entry with its kind
 * — get_nodes copies elements only. */
TEST(PublicApi, MixedNodesetBatchCopy) {
    const char* xml = "<r><a x='1'/>text<a x='2'/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);

    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//node()");
    ASSERT_NE(r, nullptr);
    size_t total = leptris_xpath_result_count(r);
    ASSERT_GT(total, 3u);  /* r, a, text, a */

    /* Count-only mode: NULL arrays still count (capped by capacity). */
    EXPECT_EQ(leptris_xpath_result_get_nodes_ex(r, nullptr, nullptr, total), total);

    std::vector<LeptrisNodeRef> nodes(total);
    std::vector<LeptrisXPathNodeKind> kinds(total);
    size_t copied = leptris_xpath_result_get_nodes_ex(
        r, nodes.data(), kinds.data(), total);
    EXPECT_EQ(copied, total);

    bool saw_element = false, saw_text = false;
    for (size_t i = 0; i < copied; i++) {
        ASSERT_NE(nodes[i], nullptr);
        EXPECT_EQ(leptris_xpath_result_node_kind(r, i), kinds[i]);
        if (kinds[i] == LEPTRIS_XPATH_NODE_ELEMENT) saw_element = true;
        if (kinds[i] == LEPTRIS_XPATH_NODE_TEXT) saw_text = true;
    }
    EXPECT_TRUE(saw_element);
    EXPECT_TRUE(saw_text);

    /* Truncation is explicit. */
    EXPECT_EQ(leptris_xpath_result_get_nodes_ex(r, nullptr, nullptr, 2), 2u);

    /* Elements-only view misses the text node. */
    std::vector<LeptrisElement> elems(total);
    size_t nelem = leptris_xpath_result_get_nodes(r, elems.data(), total);
    EXPECT_LT(nelem, total);
    EXPECT_GT(nelem, 0u);
    leptris_xpath_result_free(r);

    /* Synthetic attribute nodes report their kind. */
    LeptrisXPathResult attrs = leptris_xpath_eval(doc, nullptr, "//@x");
    ASSERT_NE(attrs, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(attrs), 2u);
    EXPECT_EQ(leptris_xpath_result_node_kind(attrs, 0),
              LEPTRIS_XPATH_NODE_ATTRIBUTE);
    leptris_xpath_result_free(attrs);

    leptris_document_free(doc);
}

/* TODO.concurrency/07: ns_set_new_from_pairs — one FFI call for the
 * whole flat [prefix, URI, ...] array. */
TEST(PublicApi, NsSetFromPairs) {
    const char* xml =
        "<root xmlns:p='http://x'><p:title>T1</p:title>"
        "<child xmlns:p='http://x'><p:title>T2</p:title></child></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);

    const char* flat[] = {"p", "http://x"};
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new_from_pairs(flat, 1);
    ASSERT_NE(ns, nullptr);
    LeptrisXPathResult r = leptris_xpath_eval_ns(doc, nullptr, "count(//p:title)", ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);
    leptris_xpath_ns_set_free(ns);

    /* Two pairs. */
    const char* flat2[] = {"p", "http://x", "q", "http://y"};
    LeptrisXPathNsSet ns2 = leptris_xpath_ns_set_new_from_pairs(flat2, 2);
    ASSERT_NE(ns2, nullptr);
    leptris_xpath_ns_set_free(ns2);

    /* Invalid entries fail the whole call — no partial set. */
    const char* bad1[] = {"p", "http://x", "", "http://y"};
    EXPECT_EQ(leptris_xpath_ns_set_new_from_pairs(bad1, 2), nullptr);
    const char* bad2[] = {nullptr, "http://x"};
    EXPECT_EQ(leptris_xpath_ns_set_new_from_pairs(bad2, 1), nullptr);

    /* NULL / empty input rejected. */
    EXPECT_EQ(leptris_xpath_ns_set_new_from_pairs(nullptr, 1), nullptr);
    EXPECT_EQ(leptris_xpath_ns_set_new_from_pairs(flat, 0), nullptr);

    leptris_document_free(doc);
}

TEST(PublicApi, FunctionSupportAndVersion) {
    EXPECT_EQ(leptris_xpath_function_supported("concat"), 1);
    EXPECT_EQ(leptris_xpath_function_supported("str:replace"), 0);
    EXPECT_EQ(leptris_xpath_function_supported(nullptr), 0);

    const char** fns = leptris_xpath_supported_functions();
    ASSERT_NE(fns, nullptr);
    bool found = false;
    for (size_t i = 0; fns[i]; i++) {
        if (strcmp(fns[i], "concat") == 0) found = true;
    }
    EXPECT_TRUE(found);

    int major = -1, minor = -1, patch = -1;
    leptris_version_components(&major, &minor, &patch);
    EXPECT_GE(major, 1);
    EXPECT_GE(minor, 0);
    EXPECT_GE(patch, 0);
}
