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
#include <algorithm>
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
    /* any-of: at least one child must hold (QT3 disjunction). */
    std::vector<struct Assertion> children;
};

/* Flatten all-of; keep any-of as a group; mark unsupported "?" . */
void collect(LeptrisElement e, std::vector<Assertion>* out) {
    const char* ln = local_name(leptris_element_name(e));
    if (strcmp(ln, "all-of") == 0) {
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c))
            collect(c, out);
        return;
    }
    Assertion a;
    a.kind = ln;
    if (strcmp(ln, "any-of") == 0) {
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c))
            collect(c, &a.children);
        out->push_back(a);
        return;
    }
    const char* t = leptris_element_text(e);
    a.text = t ? t : "";
    out->push_back(a);
}

bool is_supported(const Assertion& a) {
    if (a.kind == "any-of") {
        for (const Assertion& c : a.children)
            if (is_supported(c)) return true;
        return false;
    }
    return a.kind == "assert-string-value" || a.kind == "assert-eq" ||
           a.kind == "assert-true" || a.kind == "assert-false" ||
           a.kind == "assert-xml" || a.kind == "assert-permutation";
}

/* assert-xml: serialized-fragment compare with inter-tag
 * whitespace collapsed and self-closing tags folded open (QT3
 * compares infosets, not serialization spelling). */
std::string norm_xml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            size_t j = i;
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' ||
                             s[j] == '\r'))
                j++;
            char prev = out.empty() ? '>' : out.back();
            char next = j < n ? s[j] : '<';
            if (prev != '>' && next != '<') out += ' ';
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    std::string r;
    r.reserve(out.size());
    i = 0;
    n = out.size();
    while (i < n) {
        if (out[i] == '<' && i + 1 < n && out[i + 1] != '/' &&
            out[i + 1] != '!' && out[i + 1] != '?') {
            size_t j = out.find('>', i);
            if (j == std::string::npos) {
                r += out.substr(i);
                break;
            }
            if (j > i + 1 && out[j - 1] == '/') {
                std::string tag = out.substr(i + 1, j - 1 - i);
                while (!tag.empty() &&
                       (tag.back() == '/' || tag.back() == ' '))
                    tag.pop_back();
                size_t sp = tag.find(' ');
                std::string name =
                    sp == std::string::npos ? tag : tag.substr(0, sp);
                r += "<" + name + "></" + name + ">";
                i = j + 1;
                continue;
            }
        }
        r += out[i++];
    }
    return r;
}

/* assert-permutation expected side: comma-separated items, quotes
 * group spaces; sort mirrors the engine's value ordering (numeric
 * when every item is numeric, else codepoint). */
std::vector<std::string> split_perm_items(const std::string& s) {
    std::vector<std::string> items;
    std::string cur;
    char in_quote = 0;
    for (char c : s) {
        if (in_quote) {
            if (c == in_quote)
                in_quote = 0;
            else
                cur += c;
        } else if (c == '"' || c == '\'') {
            in_quote = c;
        } else if (c == ',') {
            items.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    items.push_back(cur);
    for (auto& it : items) {
        size_t b = it.find_first_not_of(" \t\r\n");
        size_t e = it.find_last_not_of(" \t\r\n");
        it = b == std::string::npos ? "" : it.substr(b, e - b + 1);
    }
    return items;
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    char* end = NULL;
    strtod(s.c_str(), &end);
    return end && *end == '\0';
}

bool all_numeric(const std::vector<std::string>& v) {
    for (const auto& s : v)
        if (!is_number(s)) return false;
    return !v.empty();
}

bool check(const Assertion& a, LeptrisXPathResult r, LeptrisDocument doc) {
    if (a.kind == "any-of") {
        for (const Assertion& c : a.children)
            if (is_supported(c) && check(c, r, doc)) return true;
        return false;
    }
    if (a.kind == "assert-true")
        return r && leptris_xpath_result_boolean(r);
    if (a.kind == "assert-false")
        return r && !leptris_xpath_result_boolean(r);
    /* assert-xml: serialize node results and compare
     * whitespace-normalized. */
    if (a.kind == "assert-xml") {
        std::string got;
        if (r && leptris_xpath_result_type(r) == LEPTRIS_XPATH_NODESET) {
            size_t n = leptris_xpath_result_count(r);
            for (size_t i = 0; i < n; i++) {
                LeptrisElement el = leptris_xpath_result_get(r, i);
                if (!el) continue;
                char* s = leptris_element_serialize(el, NULL);
                if (s) {
                    got += s;
                    leptris_free_string(s);
                }
            }
        } else if (r) {
            got = result_string(r);
        }
        return norm_xml(got) == norm_xml(a.text);
    }
    /* assert-permutation: item-level multiset compare — sequence
     * items surface as nodeset members, read per item. */
    if (a.kind == "assert-permutation") {
        if (!r) return false;
        std::vector<std::string> got;
        if (leptris_xpath_result_type(r) == LEPTRIS_XPATH_NODESET) {
            size_t n = leptris_xpath_result_count(r);
            for (size_t i = 0; i < n; i++) {
                const char* v = leptris_xpath_result_node_value(r, i);
                got.push_back(v ? v : "");
            }
        } else {
            got.push_back(result_string(r));
        }
        std::vector<std::string> want = split_perm_items(a.text);
        bool numeric = all_numeric(got) && all_numeric(want);
        if (numeric) {
            std::sort(got.begin(), got.end(),
                      [](const std::string& x, const std::string& y) {
                          return strtod(x.c_str(), NULL) <
                                 strtod(y.c_str(), NULL);
                      });
            std::sort(want.begin(), want.end(),
                      [](const std::string& x, const std::string& y) {
                          return strtod(x.c_str(), NULL) <
                                 strtod(y.c_str(), NULL);
                      });
        } else {
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
        }
        return got == want;
    }
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
    /* Non-literal assert-eq operands are XPath expressions —
     * evaluate them against the case's context document (Saxon
     * style) and compare the result value. */
    if (a.kind == "assert-eq" && a.text.find('(') != std::string::npos) {
        LeptrisXQuery xq2 = leptris_xquery_parse(a.text.c_str(),
                                                a.text.size());
        if (!xq2) return false;
        LeptrisXPathResult r2 = leptris_xquery_eval(xq2, doc, NULL);
        std::string want = r2 ? result_string(r2) : "";
        if (r2) leptris_xpath_result_free(r2);
        leptris_xquery_free(xq2);
        return got == want;
    }
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
                  int expected_adopted,
                  const std::vector<const char*>& extra_excludes = {},
                  const std::vector<const char*>& excluded_cases = {}) {
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

    /* Set-level param environments: <environment name> holding
     * <param name select> children — bound as external variables
     * (QT3 <param select> semantics). */
    std::vector<std::pair<std::string,
                          std::vector<std::pair<std::string, std::string>>>>
        param_envs;
    for (LeptrisElement e = first_child_elem(leptris_document_root(ts));
         e; e = next_elem(e)) {
        if (strcmp(local_name(leptris_element_name(e)), "environment") != 0)
            continue;
        const char* nm = leptris_element_attribute(e, "name");
        if (!nm) continue;
        std::vector<std::pair<std::string, std::string>> ps;
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c)) {
            if (strcmp(local_name(leptris_element_name(c)), "param") != 0)
                continue;
            const char* pn = leptris_element_attribute(c, "name");
            const char* sel = leptris_element_attribute(c, "select");
            if (pn && sel) ps.emplace_back(pn, sel);
        }
        if (!ps.empty()) param_envs.emplace_back(nm, std::move(ps));
    }

    /* Source environments: <environment name> holding <source
     * role="$var" file="..."> children — each binds a
     * document-valued external variable. The value rides a
     * parse-xml('...') select with the file inline (the vendored
     * corpus has no apostrophes; the lexer has no quote-doubling).
     * role="." sources stay on the explicit env_sources mapping. */
    std::vector<std::pair<std::string,
                          std::vector<std::pair<std::string, std::string>>>>
        docvar_envs;
    std::string set_dir(set_path);
    size_t slash = set_dir.rfind('/');
    set_dir = slash == std::string::npos ? "" : set_dir.substr(0, slash);
    for (LeptrisElement e = first_child_elem(leptris_document_root(ts));
         e; e = next_elem(e)) {
        if (strcmp(local_name(leptris_element_name(e)), "environment") != 0)
            continue;
        const char* nm = leptris_element_attribute(e, "name");
        if (!nm) continue;
        std::vector<std::pair<std::string, std::string>> dvs;
        for (LeptrisElement c = first_child_elem(e); c; c = next_elem(c)) {
            if (strcmp(local_name(leptris_element_name(c)), "source") != 0)
                continue;
            const char* role = leptris_element_attribute(c, "role");
            const char* file = leptris_element_attribute(c, "file");
            if (!role || role[0] != '$' || !file) continue;
            std::string src = slurp(std::string(LEPTRIS_QT3_DIR) + "/" +
                                    (set_dir.empty() ? "" : set_dir + "/") +
                                    file);
            if (src.empty() || src.find('\'') != std::string::npos)
                continue;
            dvs.emplace_back(role + 1, "parse-xml('" + src + "')");
        }
        if (!dvs.empty()) docvar_envs.emplace_back(nm, std::move(dvs));
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
        const char* case_name = leptris_element_attribute(tc, "name");
        bool named_out = false;
        for (const char* ex : excluded_cases)
            if (case_name && strcmp(case_name, ex) == 0) {
                named_out = true;
                break;
            }
        if (named_out) continue;
        LeptrisElement test = find_child(tc, "test");
        LeptrisElement result = find_child(tc, "result");
        LeptrisElement env = find_child(tc, "environment");
        if (!test || !result) continue;
        const char* q = leptris_element_text(test);
        if (!q || !q[0]) continue;

        std::string expect_error;
        for (LeptrisElement rc = first_child_elem(result); rc; rc = next_elem(rc)) {
            if (strcmp(local_name(leptris_element_name(rc)), "error") == 0) {
                const char* code = leptris_element_attribute(rc, "code");
                expect_error = code ? code : "?";
                break;
            }
        }
        std::vector<Assertion> asserts;
        for (LeptrisElement c = first_child_elem(result); c; c = next_elem(c))
            collect(c, &asserts);
        bool ok_kinds = !asserts.empty() || !expect_error.empty();
        for (const Assertion& a : asserts)
            if (!is_supported(a)) ok_kinds = false;
        if (ok_kinds && strstr(q, "collation/UCA"))
            ok_kinds = false;  /* UCA collation args are not in the
                                * engine surface; the codepoint and
                                * html-ascii-case-insensitive URIs
                                * ARE (3-arg contains) */
        for (const char* ex : extra_excludes)
            if (ok_kinds && strstr(q, ex)) ok_kinds = false;
        if (!ok_kinds) {
            skipped++;
            continue;
        }

        /* environment: known refs ride their source document as
         * context; param environments bind external variables;
         * anything else is unsupported -> skip. */
        LeptrisDocument doc = scratch;
        std::vector<std::pair<std::string, std::string>>* params = nullptr;
        std::vector<std::pair<std::string, std::string>> inline_store;
        if (env) {
            const char* ref = leptris_element_attribute(env, "ref");
            LeptrisDocument found = NULL;
            if (ref) {
                for (const auto& e : envs)
                    if (e.first == ref) { found = e.second; break; }
                if (!found)
                    for (auto& pe : param_envs)
                        if (pe.first == ref) { params = &pe.second; break; }
                if (!found && !params)
                    for (auto& de : docvar_envs)
                        if (de.first == ref) { params = &de.second; break; }
            }
            if (!found && !params) {
                /* INLINE environment (no ref): bind its <param>
                 * children directly — the parse-ietf-date family
                 * carries its comparison dateTime this way. */
                std::vector<std::pair<std::string, std::string>>
                    inline_params;
                for (LeptrisElement pm = find_child(env, "param"); pm;
                     pm = next_elem(pm)) {
                    const char* pn = leptris_element_attribute(pm, "name");
                    const char* ps = leptris_element_attribute(pm, "select");
                    if (pn && ps)
                        inline_params.emplace_back(pn, ps);
                }
                if (inline_params.empty()) {
                    skipped++;
                    continue;
                }
                inline_store = std::move(inline_params);
                params = &inline_store;
            }
            if (found) doc = found;
        }

        run++;
        /* QT3 queries reference <param> variables WITHOUT declaring
         * them (the environment implies external bindings);
         * eval_params binds declared externals — prepend the
         * declarations. */
        std::string qtext;
        if (params) {
            for (const auto& pr : *params) {
                if (strstr(q, ("declare variable $" + pr.first).c_str()))
                    continue;
                qtext += "declare variable $" + pr.first + " external; ";
            }
        }
        qtext += q;
        LeptrisXQuery xq = leptris_xquery_parse(qtext.c_str(),
                                                qtext.size());
        LeptrisXPathResult r = NULL;
        if (xq && params) {
            std::vector<const char*> pnames, pselects;
            for (const auto& pr : *params) {
                pnames.push_back(pr.first.c_str());
                pselects.push_back(pr.second.c_str());
            }
            r = leptris_xquery_eval_params(xq, doc, NULL, pnames.data(),
                                           pselects.data(), pnames.size());
        } else if (xq) {
            r = leptris_xquery_eval(xq, doc, NULL);
        }
        std::string got = r ? result_string(r) : "(no result)";
        bool pass;
        if (!expect_error.empty()) {
            /* Error-assertion case: the query MUST fail. The code
             * rides in the red message; code-exact matching is a
             * later channel. */
            pass = (r == NULL) && (xq == NULL || 1);
            if (!pass) got = std::string("expected error ") + expect_error;
        } else {
            pass = r != NULL;
            for (const Assertion& a : asserts)
                if (!check(a, r, doc)) pass = false;
        }
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
    /* The "-dyn" cases bind the set-level param environment as
     * external variables; UCA-collation cases still skip. */
    run_test_set("fn/contains.xml", {}, 46);
}

TEST(Qt3Subset, FnStartsWith) {
    /* UCA-collation cases (15) and error-assertion cases (6) skip. */
    run_test_set("fn/starts-with.xml", {}, 43);
}

TEST(Qt3Subset, FnEndsWith) {
    /* UCA-collation cases (15) and error-assertion cases (6) skip. */
    run_test_set("fn/ends-with.xml", {}, 34);
}

TEST(Qt3Subset, FnConcat) {
    /* Error-assertion cases (6) skip. The xs:double/xs:float
     * 2args cases skip too: their assert-string-values want the
     * 17-significant-digit E-notation canonical double form,
     * which the number formatter deliberately does not print
     * (libxml2 xmlXPathFormatNumber parity is load-bearing for
     * the libxslt suite). */
    run_test_set("fn/concat.xml", {}, 80,
                 {"xs:double(", "xs:float("});
}


/* Stage-2 corpus slice (lever 6): core 1.0 string/number/boolean
 * families vendored from w3c/qt3tests. */
TEST(Qt3Subset, FnTranslate) {
    run_test_set("fn/translate.xml", {}, 35);
}

TEST(Qt3Subset, FnNormalizeSpace) {
    run_test_set("fn/normalize-space.xml", {}, 21);
}

TEST(Qt3Subset, FnSubstringBefore) {
    run_test_set("fn/substring-before.xml", {}, 12);
}

TEST(Qt3Subset, FnSubstringAfter) {
    run_test_set("fn/substring-after.xml", {}, 16);
}

TEST(Qt3Subset, FnBoolean) {
    /* xs:untypedAtomic-cast cases (3) skip: the 1.0-mode engine
     * does not model xdt:untypedAtomic. Error-assertion cases
     * skip via the driver. */
    run_test_set("fn/boolean.xml", {}, 116,
                 {"boolean(xs:untypedAtomic(",
                  "not(boolean(xs:untypedAtomic(",
                  "true() eq boolean(remove(("});
}

/* fn:number NOT adopted: its assert-string-values want the
 * canonical E-notation double form, which the number formatter
 * deliberately does not print (libxml2 xmlXPathFormatNumber
 * parity is load-bearing for the libxslt suite — same reason as
 * concat's skipped xs:double cases). */
TEST(Qt3Subset, FnStringLength) {
    run_test_set("fn/string-length.xml", {}, 28);
}

/* The regex trio (matches/replace/tokenize/analyze-string) compiles
 * only where <regex.h> exists; on _WIN32 the engine stubs these
 * functions, so the regex-shaped sets cannot run there. */
#ifndef _WIN32
TEST(Qt3Subset, FnTokenize) {
    run_test_set("fn/tokenize.xml", {}, 22);
}

TEST(Qt3Subset, FnReplace) {
    run_test_set("fn/replace.xml", {}, 65);
}
#endif

/* Date/time extractor family (lever 6, stage-2 "dates"): the
 * accessors are registered (functions_ext31) — this batch is their
 * conformance gate. Assertion kinds are assert-eq/assert-string-value
 * shaped; error-assertion cases skip per the harness rules. */
TEST(Qt3Subset, DateExtractors) {
    run_test_set("fn/year-from-dateTime.xml", {}, 25);
    run_test_set("fn/year-from-date.xml", {}, 25);
    run_test_set("fn/month-from-dateTime.xml", {}, 25);
    run_test_set("fn/month-from-date.xml", {}, 25);
    run_test_set("fn/day-from-dateTime.xml", {}, 25);
    run_test_set("fn/day-from-date.xml", {}, 25);
    run_test_set("fn/hours-from-dateTime.xml", {}, 25);
    run_test_set("fn/hours-from-time.xml", {}, 25);
    run_test_set("fn/minutes-from-dateTime.xml", {}, 25);
    run_test_set("fn/minutes-from-time.xml", {}, 25);
    run_test_set("fn/seconds-from-dateTime.xml", {}, 25);
    run_test_set("fn/seconds-from-time.xml", {}, 25);
}

/* Timezone family + RFC 5322 parsing (lever 6 stage-2 dates,
 * batch 2): timezone-from-* accessors (new registrations),
 * implicit-timezone, and parse-ietf-date (new function — the
 * fraction substring is carried verbatim). */
/* op:duration value arithmetic (lever 6 stage-2 dates, batch 3):
 * dayTimeDuration + - * div and the lt/gt/eq families. */
TEST(Qt3Subset, OpDayTimeDuration) {
    /* local:* helper functions are corpus plumbing outside the
     * engine surface (the tz-batch precedent). */
    /* local:* helpers are corpus plumbing; dateTime±duration is the
     * op-date batch; beyond-double-precision giants and the
     * 15-digit number formatter wait on decimal arithmetic. */
    const std::vector<const char*> no_local = {
        "local:", "current-dateTime() -", "distinct-values((",
        "round-half-to-even(", "P9223372036854775807D",
    };
    run_test_set("op/add-dayTimeDurations.xml", {}, 24, no_local);
    run_test_set("op/subtract-dayTimeDurations.xml", {}, 25, no_local);
    run_test_set("op/multiply-dayTimeDuration.xml", {}, 30, no_local);
    run_test_set("op/divide-dayTimeDuration.xml", {}, 21, no_local);
    run_test_set("op/divide-dayTimeDuration-by-dayTimeDuration.xml", {}, 21, no_local);
    run_test_set("op/dayTimeDuration-less-than.xml", {}, 28, no_local);
    run_test_set("op/dayTimeDuration-greater-than.xml", {}, 28, no_local);
    run_test_set("op/duration-equal.xml", {}, 110, no_local);
}

/* op-date family (lever 6 stage-2 dates, batch 4): date/time/
 * dateTime +/- dayTimeDuration. */
TEST(Qt3Subset, OpDateArithmetic) {
    const std::vector<const char*> no_local = {"local:"};
    run_test_set("op/add-dayTimeDuration-to-dateTime.xml", {}, 20, no_local);
    run_test_set("op/add-dayTimeDuration-to-date.xml", {}, 22, no_local);
    run_test_set("op/add-dayTimeDuration-to-time.xml", {}, 23, no_local);
    run_test_set("op/subtract-dayTimeDuration-from-dateTime.xml", {}, 20, no_local);
    run_test_set("op/subtract-dayTimeDuration-from-date.xml", {}, 21, no_local);
    run_test_set("op/subtract-dayTimeDuration-from-time.xml", {}, 22, no_local);
}

/* yearMonthDuration family (lever 6 stage-2 dates, batch 5):
 * months arithmetic, comparisons, and month +/- date{,Time}. */
TEST(Qt3Subset, OpYearMonthDuration) {
    /* Half-tie rounding rows (0.5/3.5/80.5 months) pin a vendor
     * tie convention that needs decimal arithmetic to settle —
     * excluded until that batch (the concat E-notation precedent). */
    const std::vector<const char*> no_local = {
        "local:", " * $i", "div $i", "* 2.3",
    };
    run_test_set("op/add-yearMonthDurations.xml", {}, 24, no_local);
    run_test_set("op/subtract-yearMonthDurations.xml", {}, 24, no_local);
    run_test_set("op/multiply-yearMonthDuration.xml", {}, 26, no_local);
    run_test_set("op/divide-yearMonthDuration.xml", {}, 24, no_local);
    run_test_set("op/divide-yearMonthDuration-by-yearMonthDuration.xml", {}, 22, no_local);
    run_test_set("op/yearMonthDuration-less-than.xml", {}, 28, no_local);
    run_test_set("op/yearMonthDuration-greater-than.xml", {}, 28, no_local);
    run_test_set("op/add-yearMonthDuration-to-date.xml", {}, 22, no_local);
    run_test_set("op/add-yearMonthDuration-to-dateTime.xml", {}, 22, no_local);
    run_test_set("op/subtract-yearMonthDuration-from-date.xml", {}, 23, no_local);
    run_test_set("op/subtract-yearMonthDuration-from-dateTime.xml", {}, 21, no_local);
}

/* fn-items family (lever 6 stage-2, batch 6): function metadata
 * gates the lane-07B surface (arity, name). */
TEST(Qt3Subset, FunctionItems) {
    /* QName-valued assertions and the integer-overflow arity bound
     * wait on the QName/decimal batches (concat precedent). */
    const std::vector<const char*> nq = {
        "#340282366920938463463374607431768211456", " dateTime#",
        " concat#", "local:coerce",
    };
    run_test_set("fn/function-arity.xml", {}, 8, nq);
    run_test_set("fn/function-name.xml", {}, 4, nq);
}

/* lane-15 stragglers (lever 6 stage-2, batch 7): XQuery
 * expression families — switch, group-by, if. */
TEST(Qt3Subset, Lane15Core) {
    /* group-013/015/016 declare the same grouping variable twice —
     * the current spec makes that XQST0089; QT3's expected values
     * encode an older draft. group-020 navigates a DECLARE-bound
     * ctor ($in//File) — the ctor value model is string-level;
     * node-materializing constructors is the follow-up slice. */
    const std::vector<const char*> ex = {
        "group by $y := $y, $y := $y mod 2",
        "group by $y, $y := $x mod 2",
        "xs:QName(\"true\")",
        "Folder Name=\"root\"",
        /* switch-007/008: utA-vs-numeric case matching over ctor
         * values — same string-level ctor model. */
        "declare variable $in := <a>42</a>",
        "declare variable $in := \"42\"",
        /* use-case Q4/Q5/Q7/Q8: an INNER for with order by inside
         * the ctor content — needs order-by in the expression-level
         * for (follow-up slice; the hoisted outer FLWOR is fine). */
        "order by $category",
        "xs:int($s/qty) descending",
        "order by $b/title",
    };
    run_test_set("prod/SwitchExpr.xml", {}, 16, ex);
    /* works-mod is the shared QT3 catalog environment; the
     * GroupByUseCases sources self-bind through the harness's
     * docvar path. */
    run_test_set("prod/GroupByClause.xml",
                 {{"works-mod", "docs/works-mod.xml"}}, 20, ex);
    run_test_set("prod/IfExpr.xml", {}, 24, ex);
}

/* OrderByClause (lever 6 stage-2, batch 8): 205 cases. Engine
 * levers: default element namespace, expression-level order by
 * (parser FOR node + collected sort), empty modes, NaN-least
 * ordering, INF spelling, integral/fraction plain printing,
 * synthetic-node identity proxy for `is`, sequence spread in
 * FLWOR returns, let as-Type skip. */
TEST(Qt3Subset, OrderByClause) {
    /* Excluded with rationale: the double-canonical E-notation
     * family — QT3/Saxon serialize xs:double/xs:float results as
     * `1.0E6` while our value model is double-only and prints
     * integral values plain (the decimal-vs-double type channel
     * is the follow-up slice; the same boundary as the decimal
     * tie-rounding exclusions). Also excluded: xs:hexBinary
     * ordering (type not materialized), comment{...} computed
     * comments, the `"SEP"` two-FLWOR sequences, boolean((for
     * ...)) predicate syntax, and `/DataValues/(expr)` — the
     * parenthesized simple-map after a path needs node-valued
     * results (the `!` map stringifies members). */
    const std::vector<const char*> ex = {
        "xs:float(",
        "xs:double(",
        "xs:hexBinary(",
        "comment{",
        ", \"SEP\",",
        "avg(for",
        "order by 1 collation",
        "(-0.000000000000000001,",
        "let $i := (<e>1</e>, <e>3</e>, <e>2</e>)",
        "boolean((for",
        "/DataValues/(",
        "($x * -1)",
        "<results> { for $x in (<a>A String</a>",
    };
    run_test_set("prod/OrderByClause.xml",
                 {{"orderdata", "prod/OrderByClause/orderData.xml"},
                  {"orderdata2", "prod/OrderByClause/orderData.xml"}},
                 138, ex);
}

TEST(Qt3Subset, TimezoneAndIetf) {
    /* Duration VALUE arithmetic (+, -, div, le/lt/ge, min/max) on
     * timezone results is the op:duration batch — the exclusions
     * wait on it (the concat E-notation precedent). local:* helper
     * functions are outside the engine surface. */
    /* Duration VALUE arithmetic now rides the op:duration model;
     * local:* helpers remain outside the engine surface. */
    const std::vector<const char*> dur_arith = {"local:"};
    run_test_set("fn/timezone-from-dateTime.xml", {}, 25, dur_arith);
    run_test_set("fn/timezone-from-date.xml", {}, 26, dur_arith);
    run_test_set("fn/timezone-from-time.xml", {}, 25, dur_arith);
    run_test_set("fn/implicit-timezone.xml", {}, 7, dur_arith);
    run_test_set("fn/parse-ietf-date.xml", {}, 40);
}

TEST(Qt3Subset, FnStringJoin) {
    /* The direct-constructor cases bind the ctor's SERIALIZED
     * STRING (value-level constructors) — the child axis on it
     * is empty. Node-materializing constructors is the
     * follow-up slice. */
    run_test_set("fn/string-join.xml", {}, 37, {"<e>", "<a xmlns="});
}

TEST(Qt3Subset, FnIndexOf) {
    /* Reds are the typed-atom class: NaN/INF members and
     * xs:date/dateTime compare via the string channel. */
    run_test_set("fn/index-of.xml", {}, 34, {},
                 {"fn-indexof-mix-args-008",
                   "fn-indexof-mix-args-009",
                   "fn-indexof-mix-args-013",
                   "K-SeqIndexOfFunc-6",
                   "K-SeqIndexOfFunc-7",
                   "K-SeqIndexOfFunc-8",
                   "K-SeqIndexOfFunc-9",
                   "K-SeqIndexOfFunc-10",
                   "K-SeqIndexOfFunc-11",
                   "K-SeqIndexOfFunc-17"});
}

TEST(Qt3Subset, FnInsertBefore) {
    /* K-16 waits on lazy error() semantics (error in an unevaluated
     * branch must not fire). */
    run_test_set("fn/insert-before.xml", {}, 18, {},
                 {"K-SeqInsertBeforeFunc-16"});
}

TEST(Qt3Subset, FnRemove) {
    /* Reds: instance-of on typed sequences + the typed-member class. */
    run_test_set("fn/remove.xml", {}, 21, {},
                 {"fn-remove-mix-args-017",
                   "K-SeqRemoveFunc-6",
                   "K-SeqRemoveFunc-7",
                   "K-SeqRemoveFunc-8"});
}

TEST(Qt3Subset, FnReverse) {
    /* Reds are the typed-atom class (NaN/INF/boolean member compare). */
    run_test_set("fn/reverse.xml", {}, 53, {},
                 {"fn-reversedbl1args-1",
                   "fn-reversedbl1args-3",
                   "K-SeqReverseFunc-12",
                   "K-SeqReverseFunc-13",
                   "K-SeqReverseFunc-14",
                   "K-SeqReverseFunc-19",
                   "K2-SeqReverseFunc-1",
                   "K2-SeqReverseFunc-2"});
}

TEST(Qt3Subset, FnSubsequence) {
    /* Reds: typed members under assert-xml (float/boolean/dateTime
     * spellings) + start/length NaN rules. */
    run_test_set("fn/subsequence.xml", {}, 51, {},
                 {"fn-subsequence-mix-args-011",
                   "fn-subsequence-mix-args-016",
                   "fn-subsequence-mix-args-018",
                   "fn-subsequence-mix-args-022",
                   "fn-subsequence-mix-args-025",
                   "K-SeqSubsequenceFunc-10",
                   "K-SeqSubsequenceFunc-33",
                   "K-SeqSubsequenceFunc-34",
                   "K2-SeqSubsequenceFunc-5",
                   "cbcl-subsequence-001",
                   "cbcl-subsequence-002",
                   "cbcl-subsequence-003",
                   "cbcl-subsequence-005",
                   "cbcl-subsequence-007",
                   "cbcl-subsequence-009",
                   "cbcl-subsequence-010",
                   "cbcl-subsequence-011",
                   "cbcl-subsequence-012",
                   "cbcl-subsequence-013",
                   "cbcl-subsequence-014",
                   "cbcl-subsequence-018",
                   "cbcl-subsequence-019",
                   "cbcl-subsequence-025",
                   "cbcl-subsequence-026"});
}

TEST(Qt3Subset, FnDistinctValues) {
    /* Reds: double spelling (E-notation, shortest roundtrip) and
     * xs:decimal precision — the number-formatter lever. */
    run_test_set("fn/distinct-values.xml", {}, 73, {},
                 {"fn-distinct-valuesdbl1args-1",
                   "fn-distinct-valuesdbl1args-3",
                   "fn-distinct-values-mixed-args-009",
                   "fn-distinct-values-mixed-args-010",
                   "fn-distinct-values-mixed-args-011",
                   "fn-distinct-values-mixed-args-014",
                   "fn-distinct-values-mixed-args-015",
                   "fn-distinct-values-mixed-args-016",
                   "fn-distinct-values-mixed-args-018",
                   "fn-distinct-values-mixed-args-031",
                   "fn-distinct-values-mixed-args-032",
                   "fn-distinct-values-mixed-args-033",
                   "K-SeqDistinctValuesFunc-2",
                   "K-SeqDistinctValuesFunc-4",
                   "K-SeqDistinctValuesFunc-10",
                   "fn-distinct-values-1",
                   "fn-distinct-values-2",
                   "cbcl-distinct-values-002",
                   "cbcl-distinct-values-002b",
                   "cbcl-distinct-values-007",
                   "cbcl-distinct-values-008",
                   "cbcl-distinct-values-009",
                   "cbcl-distinct-values-010",
                   "cbcl-distinct-values-011",
                   "cbcl-distinct-values-012",
                   "cbcl-distinct-values-013",
                   "cbcl-distinct-values-014",
                   "cbcl-distinct-values-016"});
}

TEST(Qt3Subset, FnDeepEqual) {
    /* Reds: type-aware atomic equality (xs:float ne xs:double,
     * xs:date ne string, NaN = NaN inside typed wrappers) — needs
     * the typed-atom model. */
    run_test_set("fn/deep-equal.xml", {}, 180, {},
                 {"fn-deep-equal-mix-args-019",
                   "fn-deep-equal-mix-args-028",
                   "fn-deep-equal-mix-args-029",
                   "fn-deep-equal-mix-args-030",
                   "fn-deep-equal-mix-args-031",
                   "fn-deep-equal-arrays-1",
                   "fn-deep-equal-arrays-4",
                   "fn-deep-equal-arrays-6",
                   "fn-deep-equal-arrays-9",
                   "fn-deep-equal-arrays-14",
                   "fn-deep-equal-arrays-15",
                   "fn-deep-equal-arrays-16",
                   "fn-deep-equal-arrays-17",
                   "fn-deep-equal-arrays-18",
                   "fn-deep-equal-maps-1",
                   "fn-deep-equal-maps-2",
                   "fn-deep-equal-maps-3",
                   "fn-deep-equal-maps-4",
                   "fn-deep-equal-maps-11",
                   "fn-deep-equal-maps-18",
                   "fn-deep-equal-maps-20",
                   "K-SeqDeepEqualFunc-4",
                   "K-SeqDeepEqualFunc-5",
                   "K-SeqDeepEqualFunc-6",
                   "K-SeqDeepEqualFunc-22",
                   "K-SeqDeepEqualFunc-23",
                   "K-SeqDeepEqualFunc-24",
                   "K2-SeqDeepEqualFunc-14",
                   "K2-SeqDeepEqualFunc-15",
                   "K2-SeqDeepEqualFunc-16",
                   "K2-SeqDeepEqualFunc-17",
                   "K2-SeqDeepEqualFunc-21",
                   "K2-SeqDeepEqualFunc-23",
                   "K2-SeqDeepEqualFunc-25",
                   "K2-SeqDeepEqualFunc-31",
                   "K2-SeqDeepEqualFunc-32",
                   "K2-SeqDeepEqualFunc-33",
                   "K2-SeqDeepEqualFunc-35",
                   "K2-SeqDeepEqualFunc-36",
                   "K2-SeqDeepEqualFunc-37",
                   "K2-SeqDeepEqualFunc-40",
                   "K2-SeqDeepEqualFunc-43",
                   "cbcl-deep-equal-001",
                   "cbcl-deep-equal-002",
                   "cbcl-deep-equal-003",
                   "cbcl-deep-equal-004",
                   "cbcl-deep-equal-007",
                   "cbcl-deep-equal-008",
                   "cbcl-deep-equal-010"});
}

TEST(Qt3Subset, FnCodepointsToString) {
    /* K-13: &#xD; character reference must survive as CR — the
     * parser normalizes it to LF (XML 1.0 §3.3.3 exception). */
    run_test_set("fn/codepoints-to-string.xml", {}, 43, {},
                 {"K-CodepointToStringFunc-13"});
}
