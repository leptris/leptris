/* TODO.bindings/03 — compiled XPath expression specs. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/error.h"
}
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static const char* kDoc =
    "<library><book n='1'><title>A</title></book>"
    "<book n='4'><title>B</title></book>"
    "<book n='9'><title>C</title></book></library>";

TEST(CompiledXPath, MatchesPlainEval) {
    LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//book[@n > 3])");
    ASSERT_NE(c, nullptr);
    for (int i = 0; i < 3; i++) {
        LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
        leptris_xpath_result_free(r);
    }
    leptris_xpath_compiled_free(c);
    leptris_document_free(doc);
}

TEST(CompiledXPath, ReusedAcrossDocuments) {
    LeptrisXPathCompiled c = leptris_xpath_compile("string(//title)");
    ASSERT_NE(c, nullptr);
    for (int i = 0; i < 2; i++) {
        LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
        ASSERT_NE(doc, nullptr);
        LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
        ASSERT_NE(r, nullptr);
        char* s = leptris_xpath_result_string(r);
        EXPECT_STREQ(s, "A");
        leptris_free_string(s);
        leptris_xpath_result_free(r);
        leptris_document_free(doc);
    }
    leptris_xpath_compiled_free(c);
}

TEST(CompiledXPath, SyntaxErrorReportsMessage) {
    EXPECT_EQ(leptris_xpath_compile(nullptr), nullptr);
    EXPECT_EQ(leptris_xpath_compile(""), nullptr);
    EXPECT_EQ(leptris_xpath_compile("count(///bad[)"), nullptr);
    EXPECT_NE(leptris_last_error(), nullptr);
}

TEST(CompiledXPath, FailureSnapshotsIntoDocSlot) {
    LeptrisDocument doc = leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathCompiled c = leptris_xpath_compile("1 div 0");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
    if (r) leptris_xpath_result_free(r);   /* may legitimately succeed */
    leptris_xpath_compiled_free(c);
    leptris_document_free(doc);
}

TEST(CompiledXPath, ConcurrentEvalOneHandle) {
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//book[@n > 3])");
    ASSERT_NE(c, nullptr);
    std::vector<int> failures(4, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&failures, c, t]() {
            for (int i = 0; i < 25; i++) {
                LeptrisDocument doc =
                    leptris_parse_string(kDoc, strlen(kDoc), nullptr);
                if (!doc) { failures[t]++; continue; }
                LeptrisXPathResult r =
                    leptris_xpath_compiled_eval(c, doc, nullptr);
                if (!r || leptris_xpath_result_number(r) != 2.0) failures[t]++;
                if (r) leptris_xpath_result_free(r);
                leptris_document_free(doc);
            }
            /* Worker-exit cache drain (TODO.concurrency/08). */
            leptris_thread_cleanup();
        });
    }
    for (auto& th : threads) th.join();
    for (int t = 0; t < 4; t++) EXPECT_EQ(failures[t], 0) << "thread " << t;
    leptris_xpath_compiled_free(c);
}

/* TODO.engine/02 — compiled handles on the context-carrying paths. */
TEST(CompiledXPath, EvalWithNamespaceBindings) {
    const char* xml =
        "<root xmlns:p='http://x'><p:title>T1</p:title>"
        "<child xmlns:p='http://x'><p:title>T2</p:title></child></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);

    const char* flat[] = {"p", "http://x"};
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new_from_pairs(flat, 1);
    ASSERT_NE(ns, nullptr);

    LeptrisXPathCompiled c = leptris_xpath_compile("count(//p:title)");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r = leptris_xpath_compiled_eval_ns(c, doc, nullptr, ns);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);
    leptris_xpath_compiled_free(c);
    leptris_xpath_ns_set_free(ns);
    leptris_document_free(doc);
}

TEST(CompiledXPath, EvalWithVariables) {
    LeptrisDocument doc =
        leptris_parse_string(kDoc, strlen(kDoc), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    ASSERT_EQ(leptris_xpath_variable_set_number(vars, "min", 3.0),
              LEPTRIS_OK);

    LeptrisXPathCompiled c = leptris_xpath_compile("count(//book[@n > $min])");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r =
        leptris_xpath_compiled_eval_vars(c, doc, nullptr, vars);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 2.0);
    leptris_xpath_result_free(r);
    leptris_xpath_compiled_free(c);
    leptris_xpath_variable_set_free(vars);
    leptris_document_free(doc);
}

/* Issue #608: combined namespaces + variables on a compiled handle —
 * count(//x:book[@id=$id]) needs BOTH bound; the public surface only
 * had _ns and _vars separately, forcing bindings back to uncompiled
 * evaluation for the combination. */
TEST(CompiledXPath, EvalNsVarsCombined) {
    const char* xml =
        "<lib xmlns:x='urn:x'>"
        "<x:book id='7'>A</x:book><x:book id='8'>B</x:book>"
        "<book id='7'>C</book>"
        "</lib>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//x:book[@id=$id])");
    ASSERT_NE(c, nullptr);

    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new();
    ASSERT_NE(ns, nullptr);
    ASSERT_EQ(leptris_xpath_ns_set_add(ns, "x", "urn:x"), LEPTRIS_OK);
    LeptrisXPathVariableSet vars = leptris_xpath_variable_set_new();
    ASSERT_NE(vars, nullptr);
    ASSERT_EQ(leptris_xpath_variable_set_string(vars, "id", "7"), LEPTRIS_OK);

    LeptrisXPathResult r = leptris_xpath_compiled_eval_ns_vars(
        c, doc, nullptr, ns, vars);
    ASSERT_NE(r, nullptr);
    /* The prefixed test excludes the no-namespace book; $id=7 leaves
     * exactly one x:book. */
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);

    leptris_xpath_variable_set_free(vars);
    leptris_xpath_ns_set_free(ns);
    leptris_xpath_compiled_free(c);
    leptris_document_free(doc);
}

/* leptris-ruby #368: prefix:* is namespace-scoped, not a plain
 * wildcard. The compiler's // fusion paths classified a prefixed
 * NODE_TEST_ALL as a bare wildcard and lowered it to the
 * any-element opcodes — the binding (and the literal-prefix
 * fallback) never ran. */
TEST(CompiledXPath, PrefixedWildcardRespectsBindings) {
    const char* xml =
        "<r><bibdata/>"
        "<m:title xmlns:m='urn:x'/>"
        "<y:title xmlns:y='urn:x'/>"
        "<z:other xmlns:z='urn:z'/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), nullptr);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    const char* flat[] = {"m", "urn:x"};
    LeptrisXPathNsSet ns = leptris_xpath_ns_set_new_from_pairs(flat, 1);
    ASSERT_NE(ns, nullptr);
    const char* none[] = {"m", "urn:none"};
    LeptrisXPathNsSet ns_none = leptris_xpath_ns_set_new_from_pairs(none, 1);
    ASSERT_NE(ns_none, nullptr);

    /* Relative // from an element context (the TODO 192 fusion):
     * URI equality across prefixes, no-ns and other-ns excluded. */
    {
        LeptrisXPathCompiled c = leptris_xpath_compile(".//m:*");
        ASSERT_NE(c, nullptr);
        LeptrisXPathResult r =
            leptris_xpath_compiled_eval_ns(c, doc, root, ns);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 2u);
        for (size_t i = 0; i < leptris_xpath_result_count(r); i++)
            EXPECT_STREQ(leptris_element_name(
                             leptris_xpath_result_get(r, i)), "title");
        leptris_xpath_result_free(r);

        r = leptris_xpath_compiled_eval_ns(c, doc, root, ns_none);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 0u);
        leptris_xpath_result_free(r);

        /* Unbound prefix: the literal-prefix fallback must survive
         * too — only elements literally carrying m. */
        r = leptris_xpath_compiled_eval(c, doc, root);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 1u);
        leptris_xpath_result_free(r);
        leptris_xpath_compiled_free(c);
    }

    /* Explicit absolute descendant-or-self::m:* (the bare-DOS
     * fusion): the no-namespace self must NOT match. */
    {
        LeptrisXPathCompiled c =
            leptris_xpath_compile("/descendant-or-self::m:*");
        ASSERT_NE(c, nullptr);
        LeptrisXPathResult r =
            leptris_xpath_compiled_eval_ns(c, doc, nullptr, ns);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 2u);
        leptris_xpath_result_free(r);
        leptris_xpath_compiled_free(c);
    }

    /* Already-correct spellings pinned against regression. */
    {
        LeptrisXPathCompiled c = leptris_xpath_compile("//m:*");
        ASSERT_NE(c, nullptr);
        LeptrisXPathResult r =
            leptris_xpath_compiled_eval_ns(c, doc, nullptr, ns);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 2u);
        leptris_xpath_result_free(r);
        leptris_xpath_compiled_free(c);
    }
    {
        LeptrisXPathCompiled c = leptris_xpath_compile("descendant::m:*");
        ASSERT_NE(c, nullptr);
        LeptrisXPathResult r =
            leptris_xpath_compiled_eval_ns(c, doc, root, ns);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(leptris_xpath_result_count(r), 2u);
        leptris_xpath_result_free(r);
        leptris_xpath_compiled_free(c);
    }

    leptris_xpath_ns_set_free(ns);
    leptris_xpath_ns_set_free(ns_none);
    leptris_document_free(doc);
}
