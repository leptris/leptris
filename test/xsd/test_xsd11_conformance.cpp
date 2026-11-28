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
#include <set>
#include <string>

#include "leptris.h"

TEST(XsdConformance11, ManifestExpectationsMatch) {
    const std::string manifest_path =
        std::string(LEPTRIS_XSD11_CASES_DIR) + "/MANIFEST.tsv";
    FILE* f = fopen(manifest_path.c_str(), "rb");
    ASSERT_NE(f, nullptr) << "manifest missing";
    const bool verbose = getenv("LEPTRIS_XSD11_VERBOSE") != nullptr;
    char line[1024];
    /* pass 1: the schemas the SUITE itself invalidates (#2184) — an
     * instance row pinned "invalid" against such a schema is
     * satisfied by the failed validation (over015.v01 vs over027:
     * the suite's schemaTest rejects the schema, so no document can
     * validate against it). */
    std::set<std::string> suite_rejected;
    while (fgets(line, sizeof(line), f))
        if (line[0] != '#' && line[0] != '\n') {
            char inst[512], schema[512], expect[32], family[32];
            if (std::sscanf(line, "%511[^\t]\t%511[^\t]\t%31[^\t]\t%31s",
                            inst, schema, expect, family) == 4 &&
                std::strcmp(expect, "schema-error") == 0)
                suite_rejected.insert(schema);
        }
    rewind(f);
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
        } else if (suite_rejected.count(schema) > 0 &&
                   ((s == (LeptrisXsdSchema)0) || st != LEPTRIS_OK)) {
            /* #2184: the SUITE invalidates this schema (the schema is
             * one of the schema-error rows) and the engine rejects it
             * too — the instance cannot validate against it, and the
             * suite's expected=invalid is exactly that verdict
             * (over015.v01 vs over027). Scoped to the suite-rejected
             * set: the non-OK-status-with-empty-error compile quirk
             * (complex003+, s3_8_6v01) produces USABLE handles whose
             * instance rows keep validating normally. Only the
             * invalid expectation is satisfied this way: an expected
             * valid row against a rejected schema stays red (the
             * suite compiled it; we must too). */
            ok = std::strcmp(expect, "invalid") == 0;
            if (s != (LeptrisXsdSchema)0) leptris_xsd_free(s);
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
     * Monotonically rising. Raised by #2150 (xs:override, §4.3
     * component replacement: the overridden document's merged
     * component set collected recursively (its own top level, its
     * includes, and the components its own override/redefine
     * bodies contribute) so an overriding component that overrides
     * nothing is IGNORED and dropped; overriding references
     * re-resolve against the POST-merge set, so a replacement
     * deriving from its own former name is a circular derivation
     * (the redefine hook keeps the original-naming semantics);
     * targetNamespace alignment (equal, or the overridden document
     * has none); overriding-set uniqueness across bodies; the
     * mutual-override conflict (the inner override must replace
     * nothing — the empty-body mutual form stays legal); and a
     * kept component's dangling type reference failing the schema):
     * 1710/2206 — the eight saxon Override schema-error rows
     * (over011/014/016/017/021/022/024/026) flipped, zero
     * regressions (measured 2026-11-25 on the #2156 base);
     * Raised by #2151 (xs:openContent / xs:defaultOpenContent —
     * the unmarked-schema syntax gate, the S4SD interleave default,
     * the defaultOpenContent precedence chain with
     * appliesToEmpty and per-document unit scoping, suffix +
     * interleave absorption over sequence/all/choice models
     * (§3.4.4.3 — the wildcard absorbs before ANY model element
     * step and the particle-only trace attributes children
     * greedily), the §3.4.2.3.3 clause 6.2 empty-variety rules
     * (empty forbids all text; an applied wildcard upgrades to
     * element-only), extension inheritance / mode="none"
     * inheritance / §3.10.6.3 wildcard union / the
     * cos-ct-extends.1.4.3.2.2.3/4 ladder, the restriction
     * subset+no-add checks with the empty-model exemption, the
     * §3.8.6.4 element-vs-wildcard UPA relaxation, the
     * openContent wildcard's @notNamespace + prefix-resolved
     * @notQName, xsd_bool_attr whitespace tolerance, and the
     * schema-error class (src-ct.3 any/none co-occurrence,
     * non-any children, annotation limit, ID form + uniqueness,
     * occurrence attributes on the wildcard, simpleContent
     * carriers, src-ct.5 mixed agreement, the mixed-mismatch
     * extension, the defaultOpenContent placement rule,
     * s3_4_1si04/05/06 derivation ladders; open042's redefine
     * body rides the includer's default while open043's override
     * body does not)): 1787/2206 — openContent 122/205 → 196/205,
     * the nine remaining reds are the saxon defaultAttributes
     * sub-family (open035/044/045/202-205 — §3.4.2.5 default
     * attribute groups are not implemented; not openContent),
     * re-dispositioned in test/xsd/xsd11-dispositions/README.md
     * (measured 2026-11-25 on the #2156 base). Monotonically
 * UNION RE-MEASURE (#2150+#2151 together on the rebased base):
 * floor pinned to the measured union value below.
 * UNION RE-MEASURE (#2150+#2151 together on the rebased base):
 * floor pinned to the measured union value below.
 * Monotonically rising. Raised by #2183 (assertion-facet XQuery
 * semantics: §3.16.4 ABSENT FOCUS on the simple-type facet lane —
 * position()/last()/context dereference raise XPDY0002 and the
 * facet does not hold; fn:doc() inside assert tests resolving
 * against the SCHEMA's compile-captured base URI via the XQuery
 * `declare base-uri` channel; xs:integer/divisor-zero raising
 * FOAR0001 under the XQuery spelling (XPath 1.0 keeps INF); the
 * §3.13.4.1 cl. 1.3 subtree scoping — relative descent roots at
 * the validated element, absolute paths select NOTHING
 * (d4_3_15ii14/ii31 pin both directions); the list-of-items
 * carrier decomposition riding fn:data over the itemized $value
 * binding; `castable as` resolving unprefixed builtin names
 * through xpathDefaultNamespace): 1806/2206 on the #2151 base —
 * assertions family 270/296 → 281/296, eleven invalid-expected
 * rows flipped. Raised by #2185 (the derivation-ok attribute-use
 * clauses ride the shared engine: duplicate attribute use on the
 * extension-folded table, restriction derived-vs-base consistency
 * with the 2.2 wildcard arm, the row-level @targetNamespace
 * capture, and the prohibited instance-side enforcement):
 * 1813/2206 — cta0044.n01, target003.n1/n2, s3_2_3ii01 flip
 * (measured 2026-11-26 on the #2193 base). Raised by #2184 (the
 * alternative-table compile constraints: §3.12.2 XPath-valid
 * alternative tests fail the SCHEMA — only evaluation errors take
 * the §3.12.4 false channel; src-element.5 flags the mid-table
 * test-less default; §3.3.6.1 cl.7 cos-ct-type-substitutable walks
 * the declared type's derivation chain (res_base/assert_base/
 * text_type + builtin ancestry, union member substitution);
 * cos-element-consistent type-table equality for same-name
 * particles; the particle-restriction table preservation; src-ct.1
 * wrapper-plus-direct-attributes; §3.2.7.1 {inheritable} lexical +
 * restriction-match — the parser's cast-tail fold learned comparison
 * operators so the valid corpus's cast-into-comparison tests keep
 * parsing): 1839/2206 — the fifteen cta schema-error rows flip
 * (s3_12si01-06, cta0043, cta9001err, cta9004-9008err, cta9009err,
 * cta9010err; measured 2026-11-27 on the #2185 base). Raised by
 * #2184 wave 2 (the Override tails: the redefine/override location
 * graph must not cycle — flagged at chase time with the
 * empty-body/mutual-override exemption, and si02c's missing corpus
 * document si02b copied from the pinned snapshot; a-props-correct
 * NOTATION attributes must be enumerated — over027 plus the
 * simple091/094 simpleType shapes; document-scoped ID uniqueness —
 * over008.n01; and the gate now judges an instance row whose schema
 * the SUITE rejects with the suite's own invalid expectation when
 * the engine rejects it too — over015.v01 vs over027, scoped to the
 * suite-rejected set so the non-OK-status compile quirk handles keep
 * validating): 1845/2206 — the override family closes 87/87,
 * simple091/094 flip with the NOTATION rule (measured 2026-11-27 on
 * the 8e05b358 base). Raised by #2207 (moxml#588; the float/double
 * fixed-value constraint compares in the datatype's VALUE space —
 * the infinity family's spellings and the overflow forms are one
 * value, so the lexical strcmp no longer false-rejects):
 * 1847/2206 — simple001.v01/simple002 (saxonData/Simple) flip to
 * their oracle verdicts, attribute- and element-side alike
 * (measured 2026-11-27 on the c0da7a9e base). Raised by #2213
 * (the wildcard negative namespace/name sets — §3.10.2 + §3.10.6.3:
 * fold-at-capture canonization, the intersection-over-refs effective
 * wildcard, extension unions, notNamespace/notQName enforcement with
 * the xml prefix prebound, ##definedSibling expansion, the
 * restriction-subset law, the UPA-relaxation arming scoped to
 * ##definedSibling): 1909/2206 — 64 rows flip (43 accept-side
 * predicate rows, 15 src-wildcard schema-error rows, 6 valid rows),
 * zero regressions by red-set diff (measured 2026-11-27 on the
 * #2207-merged base; the PR's pin hunk lost its rebase — this rider
 * re-measured and raised). The aggregate pins the CROSS-PLATFORM
 * stable minimum **1908**: CI measures 1908 uniformly on every
 * platform (linux amd64/arm64, s390x qemu, windows, amalgamated);
 * a local macOS run passes at 1909 — the one-row delta is the
 * #2214 process-context quirk, tracked there. Raised by #2184 wave 3
 * (the defaulted-xsi:type class: the four BUILT-IN xsi declarations
 * resolve for attribute uses without an import (§3.17.6.2 cl. 4.2.4
 * — the src-element.2 artifact retires for complex003-010) and
 * capture with their {value constraint} riding the USE (§3.2.2.3);
 * the default has NO effect at assessment (§3.4.4.2 cl. 4 of the
 * defaulted-attribute definition — §3.2.6.4's "would have no effect"
 * note) so the defaulted forms judge against the declared type; the
 * use's occurrence constraint bites (cl. 3 — complex009/complex010
 * use="required"); and an explicit xsi:type must OVERRIDE the
 * selected type (§3.3.4.3 cl. 4) — a SIMPLE named type is never
 * substitutable for a COMPLEX selected type (§3.3.4.2 has no such
 * arm), so the complex005 explicit forms are invalid with assessment
 * falling back to the selected type (§3.3.4.3 Note), complex-over-
 * complex keeping the pinned lax direction; spec tests
 * test/xsd/test_xsd11_xitype.cpp (10 specs, red-first)): +18 rows on
 * the wildcard-union base, disjoint classes → **1926/2206**. Raised
 * by the #2184 all lane (relaxed xs:all, §3.8.3: the order-free
 * counting matcher — per-child min/max windows, wildcard children
 * with weak-wildcard precedence, substitution-group members counting
 * toward the head, group refs flattening — the compile-side
 * relaxed-all UPA arming, the all-to-all extension MERGE with the
 * §3.8.3 wildcard-disjointness law and the cos-ct-extends particle
 * grammar (all extends only by all, sequence by sequence, empty all
 * by empty all), the all-restriction sweep (§3.9.6
 * partition-with-summed-windows, substitution membership, wildcard
 * subset, element-in-wildcard, §3.10.6.3's no-wider-wildcard
 * clause), the mixed-flag source check reading the base's own
 * @mixed, the xsi:nil="1" boolean spelling, the element-only
 * character-content gate, and the reserved-namespace wildcard gate
 * — the XML Schema/xsi namespaces are never wildcard-admitted):
 * 55 flips (34 all-family rows: the 18-row ibm S3_3_6/S3_4_6 +
 * saxon All valid class, the 3-row occurrence-limit class, the
 * 11-row subsumption subset, the si02 pair, and the 3-row s3_12
 * mixed-extension artifact; 17 wild riders; 4 wild070/071/074/084
 * rows from the #2214 names-buffer fix), zero regressions by
 * red-set diff against main. #2214's root cause — the
 * uninitialized ##definedSibling expansion buffer — is fixed in the
 * same wave, so the 1909-vs-1908 platform delta it carried should
 * be gone; re-pin on the first cross-platform measure if any row
 * still straddles. Re-measured on the MERGED base (the wave-3
 * xitype class + this wave): **1984/2206** — 1252 valid / 543
 * invalid / 189 schema-error (measured 2026-11-27). Raised by the
 * CTA/IC residue tail (branch `xsd11-cta-ic`, five small slices —
 * the SELECTED alternative type's TEXT typing runs (§3.4.3.3: a
 * simpleContent derivation's inline <xs:simpleType> child is the
 * text type, captured as the synthesized slot — cta0001.n01/n02,
 * s3_12ii01.xml; xsd_capture_simple accepts a restriction element
 * directly so direct-facet restrictions capture their enumeration,
 * s3_12ii03.xml); CTA test prefixes bind in the SCHEMA document
 * (§3.13.2 {namespace bindings} + xpathDefaultNamespace prolog,
 * cta0002.n01); a cast/castable target resolving to a COMPLEX type
 * of the schema fails the schema (XPST0080, s3_12ii06); §4.2.2
 * conditional inclusion — vc:-gated components prune at capture,
 * malformed vc: values fail the schema, an unsatisfied xs:schema
 * root empties the document, and one document's surviving globals
 * may not repeat a name (src-element.2) — s4_2_2si01, the
 * vc90x.bad class, plus vc002.n1, vc010.v1 x4 and ibm test5/test6
 * riding the same mechanism; keyref tuples compare in the VALUE
 * space (§3.11.4 — the field terminal's assessed type drives a
 * time/instant/duration/numeric dispatch past the string lexical)
 * — zone206.v01/v02, zone301.v01, zone302.v01): +22 flips by
 * red-set diff, ZERO regressions — **2006/2206**, 1258 valid /
 * 553 invalid / 195 schema-error; the cta family CLOSES at
 * **125/125** (measured 2026-11-28 on the ccc58dcb base). Raised by
 * the #2184 multi-ID wave (the ID bookkeeping): the 1.0-only one-ID
 * gate now yields to DECLARED multi-ID tables on unmarked schemas
 * (the type's own >=2 ID-typed uses are 1.1 syntax evidence — the
 * #2149 posture; a @version/vc:maxVersion="1.0" pin keeps the gate,
 * and the rs#73 wildcard shapes keep their 1.0 verdicts), and the
 * §C.4 ID/IDREF integrity runs end-to-end — atomic ID values
 * (attributes, element content, defaulted/fixed-filled §3.4.4.2
 * constraints, wildcard-admitted globals) register in a
 * document-scoped uniqueness table, list-of-ID values itemize into
 * the resolution pool with the first item carrying uniqueness, union
 * values register only when the FIRST accepting member is
 * ID-derived (id006/id007/ii17 — "true" riding the boolean arm is
 * not an ID), token-family values collapse before the tables see
 * them (v16/id001.n02), and every IDREF/IDREFS value must resolve at
 * end of run. Spec tests test/xsd/test_xsd11_multid.cpp (24 specs,
 * red-first). 41 rows flip, DISJOINT from the CTA/IC wave: ibm
 * S3_3_4 valid v12/v22/v23/v24/v25/v29 (the multi-ID tables),
 * ii04-ii18+ii23 (the IDREF integrity family), and the saxon Id
 * riders id004-id008/id010-id016's dup + resolution pairs. Residue:
 * ibm v02 + saxon id003.v01/id001.v01-vs-id002 (ONE declared ID + a
 * wildcard-admitted global ID — the unmarked shape the 1.0 suite
 * pins INVALID at attZ014b; the gate MUST keep firing there, and
 * test_xsd11_multid's WildcardAdmittedSecondIdStillInvalid pins it)
 * and ibm ii26/ii27 (IBM's "ID on root does not denote any element"
 * annotation is irreconcilable with the valid twins v15/v21).
 * Zero regressions by red-set diff against the base engine (1.0 red
 * set byte-identical). Union re-measure on the #2226-merged base:
 * **2047/2206** — 1265 valid / 587 invalid / 195 schema-error
 * (measured 2026-11-28). Raised by the #2184 final-consolidation
 * wave (branch `xsd11-final`): the duplicate-facet law (a
 * single-occurrence facet repeated in one restriction fails the
 * schema — simple060-084's 25 rows; the capture used to overwrite
 * silently), the explicitTimezone posture law (required/prohibited
 * LOCK the chain posture — zone004/005/408/409; "optional" stays
 * open, the reading IBM's D4_3_16 v05/ii05 valid derivations force;
 * the facet is not applicable to a union base — zone008), and the
 * NOTATION/xs:anyAtomicType placement laws (the abstract builtin in
 * no base/list-item/union-member slot — simple051-053; the bare
 * NOTATION element type — simple090; NOTATION as list item —
 * simple092 — and union member — simple093; the NOTATION-terminal
 * enumeration names only DECLARED notations — simple095, with the
 * xs:notation symbol table captured for the check). 40 rows flip,
 * zero regressions by red-set diff: **2087/2206** — 1265 valid /
 * 587 invalid / 235 schema-error (measured 2026-11-28).
 *
 * The #2184 final-consolidation branch (`xsd11-final`, five more
 * slices, every one red-set-diffed clean, 1.0 corpus held at 1044
 * throughout):
 *  - value constraints are lexicals (§3.2.2/§3.3.4): an attribute
 *    or element default/fixed value violating the declared type's
 *    facets fails the schema (s3_3_4si01-11, 11 rows; local
 *    particles walk their content-model rows; builtin list-typed
 *    values itemize) — plus the §3.2.5 unparsed-entity integrity
 *    (ENTITY/ENTITIES values name declared unparsed entities,
 *    id018/id021) and the src-attribute.4 existence probe moving to
 *    the builtin-EXISTS table (the list builtins are real
 *    definitions; id018.xsd/id021.xsd had been losing their
 *    compiles to the old tier-1 probe);
 *  - union circularity (st-props-correct): a union's transitive
 *    membership — members, their bases, their item types, their
 *    union members — never reaches the union itself (simple017-020,
 *    s3_16_2si01-05, 9 rows);
 *  - the targetNamespace placement rules (§3.2.3.2/§3.3.3.2): a
 *    top-level declaration's tns equals the schema's, a local one
 *    never carries @form and outside a restriction equals the
 *    schema's tns (target001's restriction form compiles),
 *    plus src-import agreement (target007) — 11 rows;
 *  - RecurseAsSum (§3.9.6): the all restriction sweep grows the
 *    SEQUENCE and CHOICE arms (per-owner window sums, base-required
 *    coverage, the re-declared element's type derives, every branch
 *    independently — all212-215/227/231/232/233/234) and the UPA
 *    sequence arm routes FOLLOW through nullability (a non-nullable
 *    sibling stops the walk) — 8 rows;
 *  - the §3.4.4.3 dynamic EDC (a wildcard-admitted element resolving
 *    to a global decl derives from the governing chain's same-name
 *    particle's type) + §3.3.4.3 cl. 4.2 xsi:type substitutability
 *    (a facet-free union-member path substitutes, a member behind a
 *    restriction facet does not, in-window overrides keep the cast
 *    arm for the string family, a wildcard-slot excess stays
 *    strict) — wild062/065/068, simple016.n01 — 7 rows;
 *  - multi-head substitution groups (§3.3.6, the member sits in
 *    several heads) + the §3.3.4 {abstract} law (an abstract
 *    declaration is never validated against directly) —
 *    subsgroup001/s2_2_2, 6 rows.
 * Union re-measure: **2148/2206** — 1278 valid / 597 invalid /
 * 273 schema-error (measured 2026-11-28). Raised by the
 * substitution-group / weak-wildcard endgame wave (branch
 * `xsd11-subst`, #2184/#2234 clusters 5/8/9): the weak-wildcard
 * dialect evidence (a SEQUENCE carrying an element-vs-wildcard
 * namespace overlap cannot be a legal 1.0 schema — the ibm
 * S3_8_6/S3_10_1 schemas compile clean; a 1.0 pin keeps the gate and
 * the choice-branch overlap stays rejected) + the NFA wildcard
 * attribution trace and the weak-wildcard assessment law (the
 * wildcard-admitted element resolves its GLOBAL declaration, whose
 * type must be validly derived from the shadowed particle's through
 * the facet-free union member path — s3_8_6v01/ii01, s3_10_1v04,
 * wild063 rider); the substitution-group compile constraints (the
 * head graph acyclic — s2_2_2si01; the member's type substituting
 * EVERY head's — s2_2_2si02; cos-element-consistent against a local
 * particle — subsgroup901; the UPA an all inherits from shared
 * members — subsgroup903, riders all242/all302/all303 + the 1.0
 * lane's oss-fuzz-51295); union substitutability (union(x,y)
 * derives from no atomic member; a member path through a
 * facet-carrying union hop is blocked — simple011/014/015); the
 * base64Binary quantum closing (d3_3_17ii01) and union memberTypes
 * resolving by local name (s3_16_2ii06). 18 rows flip, zero
 * regressions by red-set diff; s3_3_4v02 stays RED (the attZ014b
 * owner-gated wildcard-second-ID shape). **2166/2206** — 1283 valid
 * / 600 invalid / 283 schema-error (measured 2026-11-28). */
    EXPECT_GE(pass, 2166) << "conformance floor regressed - re-measure the union";
    EXPECT_GE(pass_valid, 1283);
    EXPECT_GE(pass_invalid, 600);
    EXPECT_GE(pass_schema_error, 283);
    RecordProperty("pass_valid", std::to_string(pass_valid));
    RecordProperty("pass_invalid", std::to_string(pass_invalid));
    RecordProperty("pass_schema_error", std::to_string(pass_schema_error));
}
