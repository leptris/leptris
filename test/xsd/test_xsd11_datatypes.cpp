// test/xsd/test_xsd11_datatypes.cpp — #2155: facet applicability on
// the XSD 1.1-only datatypes. The 1.1 builtins (xs:dateTimeStamp,
// xs:precisionDecimal, xs:yearMonthDuration, xs:dayTimeDuration)
// register with their own lexical/value spaces, the facet
// applicability table rejects inapplicable facets at COMPILE time
// (the d3_3_4/D3_4_26/D3_4_27/D3_4_28/PDecimal schema-error class),
// the ordered-facet values must be valid lexicals of the terminal
// builtin, and the min/max comparisons run in the VALUE space
// (instants for the date/time family, total months/seconds for the
// duration family — year zero included).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD11_DATATYPES_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

/* Schema-error posture: the compile must fail (NULL handle or a
 * non-OK status), never crash. */
void expect_schema_error(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_TRUE(s == (LeptrisXsdSchema)0 || st != LEPTRIS_OK)
        << schema << " must be rejected";
    if (s) leptris_xsd_free(s);
}

/* Schema-valid posture: a clean compile. */
void expect_schema_ok(const char* schema) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (s) leptris_xsd_free(s);
}

/* Document verdict against a compiled schema. */
int verdict_of(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s =
        leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (!s) return -1;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(path(instance).c_str(), &pst);
    EXPECT_NE(d, (LeptrisDocument)0) << instance << " must parse";
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

}  // namespace

/* ---- facet applicability (compile-time) --------------------------- */

/* The length family does not apply to the duration family
 * (d3_4_26si01 / d3_4_27si01) nor to precisionDecimal
 * (d3_3_4si09). */
TEST(Xsd11Datatypes, LengthFamilyInapplicable) {
    expect_schema_error("length_ymd.xsd");
    expect_schema_error("length_dtd.xsd");
    expect_schema_error("length_pdec.xsd");
}

/* fractionDigits is not a precisionDecimal facet — its scale is
 * spelled minScale/maxScale (d3_3_4si20 / pdecimal009.n). */
TEST(Xsd11Datatypes, FractionDigitsInapplicableOnPrecisionDecimal) {
    expect_schema_error("fractiondigits_pdec.xsd");
}

/* A LIST value space cannot be bounded — the ordered facets do not
 * apply (d2_4_1_2si01). */
TEST(Xsd11Datatypes, OrderedBoundsInapplicableOnLists) {
    expect_schema_error("bounds_on_list.xsd");
}

/* minScale/maxScale are decimal-family facets; explicitTimezone is
 * a date/time-family facet. Cross-family uses are inapplicable. */
TEST(Xsd11Datatypes, ScaleAndTimezoneFacetApplicability) {
    expect_schema_error("scale_on_string.xsd");
    expect_schema_error("tz_on_pdec.xsd");
}

/* The legal facet surfaces compile clean. */
TEST(Xsd11Datatypes, LegalFacetSurfacesCompile) {
    expect_schema_ok("legal_pdec.xsd");
    expect_schema_ok("legal_stamp.xsd");
    expect_schema_ok("legal_ymd.xsd");
    expect_schema_ok("legal_list_length.xsd");
}

/* ---- facet value validity (compile-time) -------------------------- */

/* The ordered-facet values must be valid lexicals of the terminal
 * builtin: "--2"/"2e13" are not precisionDecimal (d3_3_4si10);
 * "P1Y1m"/"P-1Y1M"/"P1y1M"/"P1Y1.1M" are not yearMonthDuration
 * (d3_4_26si02); unzoned/undashed values are not dateTimeStamp
 * (d3_4_28si01). */
TEST(Xsd11Datatypes, BoundValuesMustBeValidLexicals) {
    expect_schema_error("bad_bound_values_pdec.xsd");
    expect_schema_error("bad_bound_values_ymd.xsd");
    expect_schema_error("bad_bound_values_stamp.xsd");
}

/* Digit facet values are nonNegativeInteger — a negative
 * totalDigits is an invalid facet value (d3_3_4si18). Scale values
 * are INTEGER (negative scales legal, pdecimal016), but the
 * effective window must stay non-empty — a derived minScale above
 * the inherited maxScale leaves no value space (pdecimal015.n). */
TEST(Xsd11Datatypes, DigitScaleValues) {
    expect_schema_error("totaldigits_negative.xsd");
    expect_schema_error("scale_negative.xsd");
}

/* Enumeration values are typed — "strings" is neither a
 * precisionDecimal (d3_3_4si21) nor a dateTimeStamp
 * (d3_4_28si13); the malformed duration spellings are not
 * yearMonthDuration (d3_4_26si09). */
TEST(Xsd11Datatypes, EnumerationValuesAreTyped) {
    expect_schema_error("enum_bad_values_pdec.xsd");
    expect_schema_error("enum_bad_values_stamp.xsd");
    expect_schema_error("enum_bad_values_ymd.xsd");
}

/* whiteSpace on the non-string builtins is fixed at collapse
 * (d3_3_4si22 / d3_4_26si10 / d3_4_28si14). */
TEST(Xsd11Datatypes, WhiteSpaceFixedAtCollapse) {
    expect_schema_error("whitespace_replace_pdec.xsd");
    expect_schema_error("whitespace_replace_ymd.xsd");
    expect_schema_error("whitespace_replace_stamp.xsd");
}

/* explicitTimezone on dateTimeStamp admits only "required"
 * (d3_4_28si11). */
TEST(Xsd11Datatypes, TimezoneRequiredOnDateTimeStamp) {
    expect_schema_error("tz_optional_stamp.xsd");
}

/* ---- cross-facet constraints (compile-time) ----------------------- */

/* minInclusive > maxInclusive, minExclusive > maxExclusive,
 * minInclusive > maxExclusive and the both-forms collisions are
 * schema errors (d3_3_4si11-13 / d3_4_26si03-04 / d3_4_28si02,04). */
TEST(Xsd11Datatypes, CrossFacetOrderConstraints) {
    expect_schema_error("cross_minmax_pdec.xsd");
    expect_schema_error("cross_minmax_ymd.xsd");
    expect_schema_error("cross_minmax_stamp.xsd");
    expect_schema_error("cross_tz_stamp.xsd");
    expect_schema_error("both_min_forms.xsd");
    expect_schema_error("both_forms_ymd.xsd");
}

/* minScale > maxScale in one restriction is a schema error
 * (d3_3_4si26 / pdecimal011.n). */
TEST(Xsd11Datatypes, ScaleWindowNonEmpty) {
    expect_schema_error("scale_cross.xsd");
}

/* ---- restriction narrowing (compile-time) ------------------------- */

/* A derived restriction may not widen: minInclusive below the base
 * (d3_3_4si14 / d3_4_26si05), maxInclusive above the base, and the
 * totalDigits/scale monotonicity clauses (d3_3_4si19/si27/si28,
 * pdecimal012.n/pdecimal013.n). */
TEST(Xsd11Datatypes, DerivedMustNarrow) {
    expect_schema_error("narrow_min_pdec.xsd");
    expect_schema_error("narrow_min_ymd.xsd");
    expect_schema_error("narrow_min_stamp.xsd");
    expect_schema_error("totaldigits_narrow.xsd");
    expect_schema_error("scale_narrow_min.xsd");
    expect_schema_error("scale_narrow_max.xsd");
}

/* The EFFECTIVE scale window must stay non-empty — a derived
 * maxScale under an inherited minScale leaves no value space
 * (pdecimal014.n). */
TEST(Xsd11Datatypes, EffectiveScaleWindowNonEmpty) {
    expect_schema_error("scale_empty.xsd");
}

/* A fixed base facet cannot be re-specified with a different value
 * (d3_3_4si24 / d3_4_26si11 / d3_4_28si12). */
TEST(Xsd11Datatypes, FixedFacetRespecification) {
    expect_schema_error("fixed_totaldigits.xsd");
    expect_schema_error("fixed_minmax_ymd.xsd");
    expect_schema_error("fixed_minmax_stamp.xsd");
}

/* ---- the 1.1 datatypes' own value spaces (instance side) ---------- */

/* yearMonthDuration: only signed PnYnM forms — a day/time tail or
 * an interior sign is invalid (d3_4_26ii01). */
TEST(Xsd11Datatypes, YearMonthDurationLexical) {
    EXPECT_EQ(verdict_of("inst_ymd.xsd", "inst_ymd_v.xml"), 1);
    EXPECT_NE(verdict_of("inst_ymd.xsd", "inst_ymd_n.xml"), 1);
    EXPECT_NE(verdict_of("inst_ymd.xsd", "inst_ymd_n2.xml"), 1);
}

/* dayTimeDuration: no month components. */
TEST(Xsd11Datatypes, DayTimeDurationLexical) {
    EXPECT_EQ(verdict_of("inst_dtd.xsd", "inst_dtd_v.xml"), 1);
    EXPECT_NE(verdict_of("inst_dtd.xsd", "inst_dtd_n.xml"), 1);
}

/* dateTimeStamp: the timezone is REQUIRED on every value
 * (cta0040/41/44 class). */
TEST(Xsd11Datatypes, DateTimeStampRequiresTimezone) {
    EXPECT_EQ(verdict_of("inst_stamp.xsd", "inst_stamp_v.xml"), 1);
    EXPECT_NE(verdict_of("inst_stamp.xsd", "inst_stamp_n.xml"), 1);
}

/* precisionDecimal: the arithmetic value space with ordered
 * bounds enforced (the PDecimal instance rows). */
TEST(Xsd11Datatypes, PrecisionDecimalBoundsEnforced) {
    EXPECT_EQ(verdict_of("inst_pdec.xsd", "inst_pdec_v.xml"), 1);
    EXPECT_NE(verdict_of("inst_pdec.xsd", "inst_pdec_n.xml"), 1);
}

/* Duration bounds compare in the VALUE space — "P12M" instant-
 * equals the "P1Y" bound; "P11M" is below it. */
TEST(Xsd11Datatypes, DurationBoundsValueSpace) {
    EXPECT_EQ(verdict_of("inst_durbound.xsd", "inst_durbound_v.xml"),
              1);
    EXPECT_NE(verdict_of("inst_durbound.xsd", "inst_durbound_n.xml"),
              1);
}

/* ---- yearZero (#2155 work item 2) ---------------------------------- */

/* XSD 1.1's dateTime value space has the year zero: the bounds
 * compare across the era boundary (-0001 < 0000 < 0001), and the
 * compile-side facets around the boundary are consistent. */
TEST(Xsd11Datatypes, YearZeroValueSpace) {
    expect_schema_ok("yearzero_bounds.xsd");
    EXPECT_EQ(verdict_of("yearzero_bounds.xsd", "yearzero_v.xml"), 1);
    EXPECT_NE(verdict_of("yearzero_bounds.xsd", "yearzero_n.xml"), 1);
}

/* ---- duplicate facets (#2184 residue) ------------------------------ */

/* A single-occurrence facet may appear at most once in one
 * restriction (§4.3 XML representation; the suite's simple060-084
 * "Facet must not appear more than once" class — 25 rows): a
 * repetition fails the SCHEMA. The capture overwrote silently. */
TEST(Xsd11Datatypes, DuplicateFacetRejected) {
    expect_schema_error("dup_facet_each.xsd");
}

TEST(Xsd11Datatypes, DuplicateWhiteSpaceDifferentValueRejected) {
    expect_schema_error("dup_facet_whitespace.xsd");
}

/* The guard: one of each single-occurrence facet compiles clean,
 * and the multi-occurrence facets still repeat legally. */
TEST(Xsd11Datatypes, DuplicateFacetGuardStillCompiles) {
    expect_schema_ok("dup_facet_guard.xsd");
}

/* ---- the explicitTimezone derivation law (#2184 residue) ---------- */

/* Once explicitTimezone is set anywhere up the chain the value is
 * LOCKED — the derived restriction carries the same posture
 * (zone004/zone005/zone408/zone409). */
TEST(Xsd11Datatypes, TimezoneFacetDerivationLocked) {
    expect_schema_error("tz_derive_optional_from_required.xsd");
    expect_schema_error("tz_derive_optional_from_prohibited.xsd");
    expect_schema_error("tz_derive_required_from_prohibited.xsd");
    expect_schema_error("tz_derive_prohibited_from_required.xsd");
}

/* explicitTimezone on a UNION base — the union admits only the
 * common facets (zone008). */
TEST(Xsd11Datatypes, TimezoneFacetOnUnionRejected) {
    expect_schema_error("tz_union_base.xsd");
}

/* The guard: same-posture derivations and first choices from the
 * bare builtin compile clean. */
TEST(Xsd11Datatypes, TimezoneFacetDerivationGuard) {
    expect_schema_ok("tz_derive_same_guard.xsd");
}

/* ---- NOTATION / anyAtomicType placement (#2184 residue) ----------- */

/* A bare xs:NOTATION element type fails the schema (simple090);
 * NOTATION is not a legal list item type (simple092) nor union
 * member (simple093). */
TEST(Xsd11Datatypes, NotationPlacementRejected) {
    expect_schema_error("notation_element_type.xsd");
    expect_schema_error("notation_list_item.xsd");
    expect_schema_error("notation_union_member.xsd");
}

/* An enumerated NOTATION restriction names only DECLARED notations
 * (simple095); the declared-symbol form compiles clean. */
TEST(Xsd11Datatypes, NotationEnumerationMustNameDeclaredNotations) {
    expect_schema_error("notation_enum_undeclared.xsd");
    expect_schema_ok("notation_enum_declared_guard.xsd");
}

/* xs:anyAtomicType is abstract: no restriction base (simple051),
 * no list item type (simple052), no union member (simple053). */
TEST(Xsd11Datatypes, AnyAtomicTypePlacementRejected) {
    expect_schema_error("anyatomic_restriction_base.xsd");
    expect_schema_error("anyatomic_list_item.xsd");
    expect_schema_error("anyatomic_union_member.xsd");
}

/* ---- value constraints must be valid lexicals (#2184 residue) ----- */

/* An attribute/element default or fixed value is a LEXICAL of the
 * declared type — an enumeration mismatch fails the schema
 * (s3_3_4si01/si04/si07). */
TEST(Xsd11Datatypes, DeclaredValueConstraintInvalidRejected) {
    expect_schema_error("default_enum_mismatch.xsd");
    expect_schema_error("fixed_enum_mismatch.xsd");
    expect_schema_error("default_list_enum_mismatch.xsd");
}

/* Valid value constraints — builtin atomics, the list builtins,
 * enumerated locals with matching values — compile clean. */
TEST(Xsd11Datatypes, DeclaredValueConstraintValidGuard) {
    expect_schema_ok("default_values_valid_guard.xsd");
}
