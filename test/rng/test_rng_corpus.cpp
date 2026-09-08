// test/rng/test_rng_corpus.cpp — #878 conformance gate: 19 schema/
// instance pairs vendored in test/rng/jing-cases/. Reference verdicts
// were recorded from Jing (rc 0 = valid, 1 = invalid) and are mirrored
// in the table below; leptris_rng_validate must agree on every pair.
//
// Public API only — this spec is a black-box gate, no rng_internal.h.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstdio>
#include <string>

#ifndef LEPTRIS_RNG_CASES_DIR
#define LEPTRIS_RNG_CASES_DIR "test/rng/jing-cases"
#endif

namespace {

struct RngCase {
    const char* stem;
    const char* name;
    bool ok_valid;   /* Jing verdict on {stem}.ok.xml */
    bool bad_valid;  /* Jing verdict on {stem}.bad.xml */
};

/* Generated from jing-cases/jing-ref.json — regen steps are in
 * jing-cases/README.md. */
const RngCase kCases[] = {
    {"000", "attr-required", true, false},
    {"001", "attr-undeclared", true, false},
    {"002", "elem-name", true, false},
    {"003", "choice-alt", true, false},
    {"004", "optional-child", true, false},
    {"005", "one-or-more", true, false},
    {"006", "group-order", true, false},
    {"007", "interleave-order", true, false},
    {"008", "text-content", true, false},
    {"009", "data-integer", true, false},
    {"010", "value-exact", true, false},
    {"011", "list-integers", true, false},
    {"012", "mixed-text", true, false},
    {"013", "ref-define", true, false},
    {"014", "ref-recursion", true, false},
    {"015", "combine-choice", true, false},
    {"016", "nested-elems", true, false},
    {"017", "attr-value", true, false},
    {"018", "two-attrs", true, false},
};

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

LeptrisRelaxNG parse_schema(const std::string& path) {
    std::string sch = slurp(path);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisRelaxNG rng = leptris_rng_parse(sch.data(), sch.size(), &st);
    if (!rng) ADD_FAILURE() << path << ": parse failed, status " << st;
    return rng;
}

bool validates(const LeptrisRelaxNG rng, const std::string& path) {
    std::string doc = slurp(path);
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(doc.data(), doc.size(), &st);
    if (!d) {
        ADD_FAILURE() << path << ": instance parse failed, status " << st;
        return false;
    }
    int ok = leptris_rng_validate(rng, d);
    leptris_document_free(d);
    return ok != 0;
}

TEST(RngCorpus, AllSchemasParse) {
    for (const RngCase& c : kCases) {
        LeptrisRelaxNG rng = parse_schema(std::string(LEPTRIS_RNG_CASES_DIR) +
                                          "/" + c.stem + ".rng");
        EXPECT_NE(rng, nullptr) << c.stem << " " << c.name;
        if (rng) leptris_rng_free(rng);
    }
}

TEST(RngCorpus, MatchesJingVerdicts) {
    int run = 0;
    int agree = 0;
    for (const RngCase& c : kCases) {
        const std::string dir = LEPTRIS_RNG_CASES_DIR;
        LeptrisRelaxNG rng = parse_schema(dir + "/" + c.stem + ".rng");
        ASSERT_NE(rng, nullptr) << c.stem << " " << c.name;

        const struct {
            const char* suffix;
            bool jing_valid;
        } inst[] = {
            {"ok.xml", c.ok_valid},
            {"bad.xml", c.bad_valid},
        };
        for (const auto& in : inst) {
            bool got = validates(rng, dir + "/" + c.stem + "." + in.suffix);
            run++;
            if (got == in.jing_valid) {
                agree++;
                continue;
            }
            ADD_FAILURE() << c.stem << " " << c.name << " " << in.suffix
                          << ": jing says "
                          << (in.jing_valid ? "VALID" : "INVALID")
                          << ", leptris says "
                          << (got ? "VALID" : "INVALID") << " — "
                          << (got ? "" : leptris_rng_error(rng));
        }
        leptris_rng_free(rng);
    }
    /* Full agreement: 38/38 Jing verdicts. Exact on purpose — every
     * future divergence is an immediate red. */
    EXPECT_EQ(agree, run);
    EXPECT_EQ(run, 38);
}

}  // namespace
