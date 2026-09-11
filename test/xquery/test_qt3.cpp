// test/xquery/test_qt3.cpp — lanes 11/12 residual: W3C QT3 test
// suite subset adoption. First batch: fn/substring (vendored
// verbatim from github.com/w3c/qt3tests into test/xquery/qt3/).
//
// The runner parses the test-set with libleptris itself, adopts
// every test-case whose assertions are within the supported
// kinds, evaluates each query through the public XQuery API, and
// gates EXACT: agree == adopted == kExpected. Every future
// divergence is an immediate red. New assertion kinds are added
// to the runner as the engine surface grows (XTH connector
// shape).
//
// Supported assertions (F&O test assertion language):
//   assert-string-value  space-joined string of the result
//   assert-eq            scalar compare (quoted strings verbatim;
//                        numeric literals compare numerically)
//   assert-true/false    boolean of the result
//   all-of               every child assertion must hold
// (assert-type and any-of wrappers exclude the case from
// adoption for now — the count documents that.)

#include "leptris.h"
#include "leptris/xquery/xquery.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef LEPTRIS_QT3_DIR
#define LEPTRIS_QT3_DIR "test/xquery/qt3"
#endif

namespace {

std::string slurp(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

const char* local_name(const char* name) {
    const char* c = name ? strchr(name, ':') : NULL;
    return c ? c + 1 : name;
}

LeptrisElement first_child_elem(LeptrisElement e) {
    for (LeptrisElement c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        if (leptris_node_get_type((LeptrisNodeRef)c) == LEPTRIS_NODE_TYPE_ELEMENT)
            return c;
    }
    return NULL;
}

LeptrisElement next_elem(LeptrisElement e) {
    for (LeptrisElement c = leptris_element_next_sibling_any(e); c;
         c = leptris_element_next_sibling_any(c)) {
        if (leptris_node_get_type((LeptrisNodeRef)c) == LEPTRIS_NODE_TYPE_ELEMENT)
            return c;
    }
    return NULL;
}

LeptrisElement find_child(LeptrisElement e, const char* ln) {
    for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c))
        if (strcmp(local_name(leptris_element_name(c)), ln) == 0) return c;
    return NULL;
}

/* Space-joined string of the result sequence (the value-of rule
 * the XQuery surface established). */
std::string result_string(LeptrisXPathResult r) {
    std::string out;
    if (!r) return out;
    if (leptris_xpath_result_type(r) == LEPTRIS_XPATH_NODESET) {
        size_t n = leptris_xpath_result_count(r);
        for (size_t i = 0; i < n; i++) {
            const char* v = leptris_xpath_result_node_value(r, i);
            if (i) out += ' ';
            out += v ? v : "";
        }
    } else {
        char* s = leptris_xpath_result_string(r);
        out = s ? s : "";
        leptris_free_string(s);
    }
    return out;
}

struct Assertion {
    std::string kind;
    std::string text;
};

/* Flatten all-of; mark unsupported with kind "?". */
void collect(LeptrisElement e, std::vector<Assertion>* out) {
    const char* ln = local_name(leptris_element_name(e));
    if (strcmp(ln, "all-of") == 0) {
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c))
            collect(c, out);
        return;
    }
    Assertion a;
    a.kind = ln;
    const char* t = leptris_element_text(e);
    a.text = t ? t : "";
    out->push_back(a);
}

bool is_supported(const Assertion& a) {
    return a.kind == "assert-string-value" || a.kind == "assert-eq" ||
           a.kind == "assert-true" || a.kind == "assert-false";
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    char* end = NULL;
    strtod(s.c_str(), &end);
    return end && *end == '\0';
}

bool check(const Assertion& a, LeptrisXPathResult r) {
    if (a.kind == "assert-true")
        return r && leptris_xpath_result_boolean(r);
    if (a.kind == "assert-false")
        return r && !leptris_xpath_result_boolean(r);
    std::string got = result_string(r);
    if (a.kind == "assert-string-value")
        return got == a.text;
    /* assert-eq: the expected is an XPath literal. Quoted -> exact
     * string; bare numeric -> numeric compare. */
    if (a.text.size() >= 2 && (a.text[0] == '"' || a.text[0] == '\'') &&
        a.text.back() == a.text[0]) {
        return got == a.text.substr(1, a.text.size() - 2);
    }
    if (is_number(a.text) && is_number(got))
        return strtod(a.text.c_str(), NULL) == strtod(got.c_str(), NULL);
    return got == a.text;
}

}  // namespace

/* Run one vendored test-set. env_sources maps an environment ref
 * id to its context document path (relative to LEPTRIS_QT3_DIR);
 * refs without an entry are unsupported and skip their case.
 * expected_adopted is the exact-N gate. */
void run_test_set(const char* set_path,
                  const std::vector<std::pair<const char*, const char*>>&
                      env_sources,
                  int expected_adopted) {
    std::string xml = slurp(std::string(LEPTRIS_QT3_DIR) + "/" + set_path);
    ASSERT_FALSE(xml.empty());
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument ts = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(ts, nullptr);

    std::vector<std::pair<std::string, LeptrisDocument>> envs;
    for (const auto& es : env_sources) {
        std::string src = slurp(std::string(LEPTRIS_QT3_DIR) + "/" +
                                es.second);
        if (!src.empty()) {
            LeptrisDocument d = leptris_parse_string(
                src.data(), src.size(), &st);
            if (d) envs.emplace_back(es.first, d);
        }
    }

    LeptrisDocument scratch = leptris_parse_string("<e/>", 4, &st);
    ASSERT_NE(scratch, nullptr);

    int run = 0, agree = 0, skipped = 0;
    std::vector<std::string> red;
    for (LeptrisElement tc =
             find_child(leptris_document_root(ts), "test-case");
         tc;
         tc = next_elem(tc)) {
        if (strcmp(local_name(leptris_element_name(tc)), "test-case") != 0)
            continue;
        LeptrisElement test = find_child(tc, "test");
        LeptrisElement result = find_child(tc, "result");
        LeptrisElement env = find_child(tc, "environment");
        if (!test || !result) continue;
        const char* q = leptris_element_text(test);
        if (!q || !q[0]) continue;

        std::vector<Assertion> asserts;
        for (LeptrisElement c = first_child_elem(result); c; c = next_elem(c))
            collect(c, &asserts);
        bool ok_kinds = !asserts.empty();
        for (const Assertion& a : asserts)
            if (!is_supported(a)) ok_kinds = false;
        if (ok_kinds && strstr(q, "collation/UCA"))
            ok_kinds = false;  /* UCA collation args are not in the
                                * engine surface; the codepoint and
                                * html-ascii-case-insensitive URIs
                                * ARE (3-arg contains) */
        if (!ok_kinds) {
            skipped++;
            continue;
        }

        /* environment: known refs ride their source document as
         * context; anything else is unsupported -> skip. */
        LeptrisDocument doc = scratch;
        if (env) {
            const char* ref = leptris_element_attribute(env, "ref");
            LeptrisDocument found = NULL;
            if (ref)
                for (const auto& e : envs)
                    if (e.first == ref) { found = e.second; break; }
            if (found) {
                doc = found;
            } else {
                skipped++;
                continue;
            }
        }

        run++;
        LeptrisXQuery xq = leptris_xquery_parse(q, strlen(q));
        LeptrisXPathResult r =
            xq ? leptris_xquery_eval(xq, doc, NULL) : NULL;
        std::string got = r ? result_string(r) : "(no result)";
        bool pass = r != NULL;
        for (const Assertion& a : asserts)
            if (!check(a, r)) pass = false;
        if (r) leptris_xpath_result_free(r);
        if (xq) leptris_xquery_free(xq);
        if (pass) {
            agree++;
        } else {
            const char* nm = leptris_element_attribute(tc, "name");
            red.push_back(std::string(nm ? nm : "?") + ": [" + q +
                          "] got [" + got + "]");
        }
    }
    (void)skipped;
    for (const std::string& r : red) ADD_FAILURE() << r;
    EXPECT_EQ(agree, run);
    EXPECT_EQ(run, expected_adopted);
    for (auto& e : envs) leptris_document_free(e.second);
    leptris_document_free(scratch);
    leptris_document_free(ts);
}

TEST(Qt3Subset, FnSubstring) {
    run_test_set("fn/substring.xml",
                 {{"concepts", "fn/substring/concepts.xml"}},
                 45);
}

TEST(Qt3Subset, FnContains) {
    /* The 8 "-dyn" cases declare external variables (param
     * binding) — unsupported, they skip; the adopted set is the
     * rest. */
    run_test_set("fn/contains.xml", {}, 41);
}

TEST(Qt3Subset, FnStartsWith) {
    /* UCA-collation cases (15) and error-assertion cases (6) skip. */
    run_test_set("fn/starts-with.xml", {}, 43);
}

TEST(Qt3Subset, FnEndsWith) {
    /* UCA-collation cases (15) and error-assertion cases (6) skip. */
    run_test_set("fn/ends-with.xml", {}, 34);
}
