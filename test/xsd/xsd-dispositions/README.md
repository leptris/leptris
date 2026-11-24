# XSD conformance-lane dispositions — remaining oracle divergences

Measured 2026-11-24 (branch `fix/xsd-ns-qualification`, leptris-ruby#679,
oracle libxml 2.9.13, `MANIFEST.tsv` 1056 rows):
**1020/1056 pass** — 181 valid / 652 invalid / 187 schema-error classes.

History: 769 (2026-10-14 first measurement) → 795 → 803 → 839 → 898 →
903 (2026-10-22 session start) → 989 (2026-10-22 branch) →
1011 (main, pre-#679 re-measure) → **1020** (this branch: the
targetNamespace qualification fix retires the root-resolution +
attributeFormDefault classes wholesale, see "Retired" below).

Remaining 36 divergences, by class. `XsdConformance.
ManifestExpectationsMatchOracle` is the gate; this file is the
disposition record for every row left in it.

## valid rejected by the engine (6)

| rows | class | disposition |
|---|---|---|
| 3 | allsg: `xs:all` whose particles `ref` an abstract substitution-group HEAD; instances carry members, any order | OPEN — content-model gap: substitution-group member acceptance (and `xs:all` member sets) not implemented in `content_model.c` |
| 1 | include1_0: include-chained schema set (`include1_0.inc`) — the included unit's complexType for `foo:bar` is not applied ("children do not match the content model (at 'bar.B')") | OPEN — include-chain content-model merge for the included unit's local types |
| 1 | derivation-restriction-anyAttr_0: instance attribute in the base's wildcard after restriction | OPEN — attribute-wildcard inheritance across restriction |
| 1 | bug322411_0: duplicate `xs:unique` key tuples | DISPOSITIONED — the engine is spec-correct (duplicate keys must fail); the 2.9.13 oracle verdict "valid" is a libxml2 identity-constraint miss (field `.`). Do NOT chase this row; it flips with a newer oracle |

## schema-error schemas that compile clean (19)

| rows | class | disposition |
|---|---|---|
| 12 | derivation-ok-extension-err + derivation-ok-restriction-2-1-1 grids (six instance docs against each schema): duplicate attribute use inside one complexType; restriction attribute-use consistency (an `optional` derived use against a `required` base use, or no matching base use/wildcard at all) | OPEN — derivation-ok attribute clauses (slice 4 covered the compile-side checks; the instance-side derived-vs-base table comparison and the duplicate-@name check are missing) |
| 3 | restriction-attr1 grid | OPEN — same restriction attribute-use consistency class |
| 3 | include3: duplicate global element declaration across `xs:include` | OPEN — merged-schema duplicate detection |
| 1 | oss-fuzz-51295: circular substitution group (`e` substitutes `e`) | OPEN — cycle check on substitutionGroup refs |

## invalid documents accepted (11)

| rows | class | disposition |
|---|---|---|
| 2 | ns0_3/ns0_4 vs ns0_0.xsd: `attributeFormDefault="unqualified"` rows bound by QUALIFIED instance attributes | OPEN — kept open deliberately: the unqualified-form row keeps the pinned #1586 qualified-spelling leniency (OOXML wire documents qualify attributes their schemas declare unqualified — w:val & friends); enforcing this direction would regress the OOXML consumer corpus. The reverse direction (qualified-form row vs unqualified attribute) IS enforced since #679 |
| 1 | attruse_0_2: `xs:attribute use="prohibited"` present in the instance | OPEN — prohibited-use enforcement at capture/validate |
| 2 | bug141333 family vs bug323510_1.xsd: required child missing (min=1) accepted | OPEN — content-model occurrence floor at the element end |
| 1 | mixed1_0: undeclared attribute on an attribute-less `mixed="true"` complexType | OPEN — a type with NO captured attribute rows skips the strict attribute walk entirely (`validate_attributes` returns on `ta == NULL`) |
| 1 | anyAttr-processContents-err1_0: `processContents="strict"` wildcard admits undeclared attributes | OPEN — processContents enforcement on wildcards |
| 1 | idc-keyref-err1: keyref with no matching key | OPEN — keyref target lookup (capture exists, the check is missing) |
| 1 | changelog093: duplicate `xs:unique` keys (field on a sub-element path) | OPEN — unique over non-trivial field paths (bug322411 shows the `.` field case works) |
| 1 | date-whitespace_0: leading/trailing whitespace in date/dateTime/time/gMonth-family lexical spaces accepted | OPEN — the collapse posture is applied where the type's lexical space is strict (date-family builtins admit no surrounding whitespace) |
| 1 | xsd-simpleType-varieties vs xsd-list-itemType: character content in an element-only content model accepted | OPEN — element-only content-type gate |

## Retired by leptris-ruby#679 (this branch, for the record)

The root-resolution ladder binds the global element declaration whose
(namespaceURI, localName) pair matches the instance element EXACTLY —
a stamped declaration ({target namespace} = the declaring unit's
targetNamespace) names an element in that namespace, an unstamped
declaration names a NO-namespace element, and there is NO fallback
past a namespace mismatch. Form-qualified attribute declarations
(attributeFormDefault/@form) require the namespace on the instance
attribute.

- 5 root-resolution rows: po0_0/po1_0 ("instance root in a namespace
  the schema does not declare") + the bug141312/bug141333/bug309338
  cross-pairs ("root-namespace mismatches"), incl. ns0_2/ns0_0.xsd (a
  NO-namespace root against a targetNamespace schema — the report's
  cell from the moxml-routed validation suite).
- 3 attributeFormDefault rows: ns0_0/ns0_1/ns0_2 vs ns0_1.xsd — a
  qualified-form declared attribute used unprefixed is rejected; the
  qualified spelling validates.

Spec tests: test/xsd/test_xsd_ns_qualification.cpp.

## Retired earlier (for the record)

xsi-namespace exemption from the strict attribute gate (16), empty
content models reject element children (9), unprefixed builtin type
references (3) + strict attribute walk on simple-typed elements (2),
no-tns unit `##other` wildcards (4), xs:attribute 3.2.3 declaration
clauses (55), g-regional timezone + three-dash gDay (1), token-family
whitespace collapse (1), unknown xs:-builtin type references (3),
subst-group-1_1 + complex-type-extension_0 (xsi:type honoring) +
all_4/choice_4 occurrence floors + any5 wildcard sets +
anyAttr-derive2/processContents cross-pairs + cos-st-restricts facet
chain + one list-itemType pair (2026-10-22 → 2026-11-24 residue
sweeps between the 989 and 1011 measurements).
