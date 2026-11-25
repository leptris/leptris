// test/xsd/test_xsd11_conformance.cpp — #2148: the XSD 1.1 baseline
// lane. Walks test/xsd/xsd11-cases/MANIFEST.tsv, generated from the
// PINNED w3c/xsdtests snapshot (SHA in the manifest header): the
// SUITE is its own oracle — every row's expected verdict comes from
// the suite's <expected validity> metadata, never from libxml2,
// which supports little of XSD 1.1.
//
// Row shape (see scripts/gen_xsd11_manifest.py):
//   instance<TAB>schema<TAB>expected<TAB>family
//   - instance "-" = schema-only row (a suite schemaTest):
//       expected valid        -> the schema must compile clean
//       expected schema-error -> the schema must be rejected
//   - expected valid/invalid on instance rows = document verdict.
// The pass floor is pinned from the first measured run and is
// MONOTONIC: raising it is progress, lowering it is a regression.
// Every red is classified in test/xsd/xsd11-dispositions/README.md.
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "leptris.h"

TEST(XsdConformance11, ManifestExpectationsMatch) {
    const std::string manifest_path =
        std::string(LEPTRIS_XSD11_CASES_DIR) + "/MANIFEST.tsv";
    FILE* f = fopen(manifest_path.c_str(), "rb");
    ASSERT_NE(f, nullptr) << "manifest missing";
    const bool verbose = getenv("LEPTRIS_XSD11_VERBOSE") != nullptr;
    char line[1024];
    int pass = 0, fail = 0, total = 0;
    int pass_valid = 0, pass_invalid = 0, pass_schema_error = 0;
    /* families: assertions / cta / override / openContent / core */
    int fam_total[5] = {0, 0, 0, 0, 0};
    int fam_pass[5] = {0, 0, 0, 0, 0};
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char inst[512], schema[512], expect[32], family[32];
        if (std::sscanf(line, "%511[^\t]\t%511[^\t]\t%31[^\t]\t%31s", inst,
                        schema, expect, family) != 4)
            continue;
        int fam = -1;
        if (std::strcmp(family, "assertions") == 0) fam = 0;
        else if (std::strcmp(family, "cta") == 0) fam = 1;
        else if (std::strcmp(family, "override") == 0) fam = 2;
        else if (std::strcmp(family, "openContent") == 0) fam = 3;
        else if (std::strcmp(family, "core") == 0) fam = 4;
        ASSERT_GE(fam, 0) << "unknown family in manifest row: " << line;
        total++;
        fam_total[fam]++;
        std::string spath =
            std::string(LEPTRIS_XSD11_CASES_DIR) + "/" + schema;

        LeptrisStatus st = (LeptrisStatus)0;
        LeptrisXsdSchema s = leptris_xsd_compile_file(spath.c_str(), &st);
        bool ok;
        if (std::strcmp(expect, "schema-error") == 0) {
            /* the suite says the schema itself is invalid: the engine
             * must reject it (a clean parse-or-schema error, no crash) */
            ok = (s == (LeptrisXsdSchema)0) || st != LEPTRIS_OK;
            if (s != (LeptrisXsdSchema)0) leptris_xsd_free(s);
        } else if (std::strcmp(inst, "-") == 0) {
            /* schema-only row: the suite says this schema is valid,
             * so it must compile clean */
            ok = (s != (LeptrisXsdSchema)0) && st == LEPTRIS_OK;
            if (s != (LeptrisXsdSchema)0) leptris_xsd_free(s);
        } else if (s == (LeptrisXsdSchema)0) {
            ok = false; /* the suite compiled it; we must too */
        } else {
            std::string ipath =
                std::string(LEPTRIS_XSD11_CASES_DIR) + "/" + inst;
            LeptrisDocument d =
                leptris_parse_file(ipath.c_str(), &st);
            if (!d) {
                ok = false;
            } else {
                int v = leptris_xsd_validate(s, d);
                ok = (std::strcmp(expect, "valid") == 0) ? (v == 1)
                                                          : (v != 1);
                leptris_document_free(d);
            }
            leptris_xsd_free(s);
        }
        if (ok) {
            pass++;
            fam_pass[fam]++;
            if (std::strcmp(expect, "valid") == 0) pass_valid++;
            else if (std::strcmp(expect, "invalid") == 0) pass_invalid++;
            else pass_schema_error++;
        } else {
            fail++;
            if (verbose)
                printf("XSD11-RED %s\t%s\t%s\t%s\n", inst, schema, expect,
                       family);
        }
    }
    fclose(f);
    EXPECT_EQ(total, 2206) << "corpus shrank — regenerate the manifest";
    RecordProperty("pass", std::to_string(pass));
    RecordProperty("fail", std::to_string(fail));
    printf(
        "XSD11-CONF-BASELINE pass=%d fail=%d "
        "(valid=%d invalid=%d schema_error=%d)\n",
        pass, fail, pass_valid, pass_invalid, pass_schema_error);
    printf("XSD11-CONF-FAMILIES assertions=%d/%d cta=%d/%d override=%d/%d "
           "openContent=%d/%d core=%d/%d\n",
           fam_pass[0], fam_total[0], fam_pass[1], fam_total[1],
           fam_pass[2], fam_total[2], fam_pass[3], fam_total[3],
           fam_pass[4], fam_total[4]);
    /* The floor: pinned at the first measured run (2026-11-24,
     * w3c/xsdtests @ 7bc3365c652a322f3d762021b3879eb92dae7e30,
     * 2206 rows): 1439/2206 — 180 valid rejections, 302 invalid
     * acceptances, and 285 suite-invalid schemas compiling clean.
     * Raised by #2149 (xs:alternative wiring — conditional type
     * assignment dispatch on global decls and local particles,
     * test-less defaults, xs:error selections, inherited attributes
     * in test contexts, assert capture for alternative-carrying
     * schemas): 1455/2206. Raised by leptris-ruby#679
     * (targetNamespace qualification on validated documents — the
     * exact (namespaceURI, localName) root binding): 1458/2206,
     * three Override n02 rows (no-namespace instances against
     * tns-carrying overrides now correctly rejected; measured
     * 2026-11-24). Raised by #2154 (xs:assertion facets
     * enforced on simple types across the restriction ladder, with
     * list-item and union-member chains; the complexType assert
     * capture keyed on assert syntax presence; assert inheritance
     * down the derivation chain; §3.13.2 namespace bindings and
     * xpathDefaultNamespace; the §3.13.4.1 assertion data model
     * excluding comments/PIs): 1524/2206 (re-measured on the
     * #679-qualified base) — every red classified in
     * test/xsd/xsd11-dispositions/README.md. Monotonically rising.
     * Raised by #2155 (facet applicability on the 1.1-only
     * datatypes: xs:dateTimeStamp/xs:precisionDecimal/
     * xs:yearMonthDuration/xs:dayTimeDuration registered with their
     * own lexicals, unprefixed builtin refs canonized at capture,
     * the compile-time applicability sweep (inapplicable facets,
     * invalid facet values, cross-facet order clauses, narrowing +
     * the fixed law on the 1.1 builtins, bounds-on-lists), the
     * exact decimal comparator, the duration/date-time value-space
     * bound comparisons with year zero, the precisionDecimal scale
     * window (spelling-claimed precision) and its NaN-enumeration
     * rule, numeric/temporal enumeration equality, collapse for
     * restrictions declaring whiteSpace="collapse", the
     * precisionDecimal canonical digit counts,
     * explicitTimezone enforcement, and the pre-existing
     * fractional-seconds-with-timezone hole in the time tail):
     * 1676/2206 (measured 2026-11-25) — every red classified in
     * test/xsd/xsd11-dispositions/README.md. Raised by #2156
     * (the identity-constraint 1.1 extensions: selector/field
     * XPaths parsed into the §3.11.6 subset at compile time with
     * SCHEMA-side namespace resolution + xpathDefaultNamespace,
     * out-of-subset expressions and unresolved prefixes failing the
     * schema, @ref reuse with the §3.11.3 category match, capture
     * reaching local element particles, §3.10.4.1 skipped subtrees
     * omitted from target node sets, per-scoping-element
     * uniqueness, vacuous node tables, strict keyref target
     * tables): 1702/2206 — 18 invalid Id/S3_11_2 rows + 2
     * schema-error rows + 6 wildcard-family key rows flipped, zero
     * regressions (measured 2026-11-25 on the #2155 base).
     * Monotonically rising. */
    EXPECT_GE(pass, 1702) << "conformance floor regressed";
    EXPECT_GE(pass_valid, 1140);
    EXPECT_GE(pass_invalid, 459);
    EXPECT_GE(pass_schema_error, 103);
    RecordProperty("pass_valid", std::to_string(pass_valid));
    RecordProperty("pass_invalid", std::to_string(pass_invalid));
    RecordProperty("pass_schema_error", std::to_string(pass_schema_error));
}
