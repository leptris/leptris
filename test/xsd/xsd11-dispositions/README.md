# XSD 1.1 baseline-lane dispositions — every red classified

Measured 2026-11-24 (#2148, branch `xsd11-baseline`), oracle = the
w3c/xsdtests suite metadata itself, snapshot pinned at
`7bc3365c652a322f3d762021b3879eb92dae7e30` (recorded in the manifest
header), `test/xsd/xsd11-cases/MANIFEST.tsv` 2206 rows:
**1439/2206 pass** — 1113 valid / 308 invalid / 18 schema-error classes
(180 + 302 + 285 = 767 reds).

Re-measured 2026-11-24 (#2149, branch `xsd11-alternatives` —
conditional type assignment wired end to end: alternative capture
keyed on syntax presence rather than @version, chains on global
declarations AND local particles, test-less default alternatives,
xs:error selections, xsi:type precedence, inherited attributes in
test contexts, assert capture + $value binding for
alternative-carrying schemas): **1455/2206 pass** — 1117 valid /
320 invalid / 18 schema-error (751 reds).

By family: assertions 214/296 · cta 97/125 · override 71/87 ·
openContent 122/205 · core 951/1493 (#2149: cta 83→97, +1 assertions
po_sample, +1 core VC — the cta reds left are all OUTSIDE the
dispatch class; the tables below carry the #2149 counts).

`XsdConformance11.ManifestExpectationsMatch` (test/xsd/
test_xsd11_conformance.cpp) is the gate; this file is the disposition
record for every row left in it. The floor (now 1455) is pinned and
monotonic — same law as the 1.0 lane. Scaffolding context: the engine
already captures xs:assert / xs:assertion / xs:alternative /
xs:openContent / xs:override (moxml#556 wired xs:assert through the
XQuery substrate); what the classes below measure is how much of the
1.1 SEMANTICS each capture actually enforces.

## Engine quirk worth knowing while reading the tables

`leptris_xsd_compile_file` on some schema-valid-in-the-suite documents
returns a NON-NULL handle with status `LEPTRIS_ERROR_PARSE` and an
EMPTY schema error list (the gate, rightly, requires a clean
LEPTRIS_OK compile). Every affected schema uses a 1.1-only construct —
weakened-UPA sequences (`s3_10_1v04/05/06`), restriction of `xs:all`
(`s3_3_6v01/04`, `s3_4_6v01/04`), weak wildcard lax typing
(`s3_8_6v01`), `xs:dateTimeStamp` (`zone101`), `xs:precisionDecimal`
(`pdecimal001`), xsi:type-with-default (`complex003+`), override
(`s3_12v09`, `s3_12ii04`). Treat the status artifact as part of each
construct class below, not a separate lane.

The corpus also carries 79 UTF-16 suite files (S3_2_3, S2_2_4, S3_3_4
families); the engine parses UTF-16 natively, so they are IN lane.

## valid rejected by the engine (176)

| rows | class | disposition |
|---|---|---|
| 41 | saxon Open: openContent — allowed extra elements after (and with `mode="interleaved"`, among) the declared particles rejected ("children do not match the content model") | OPEN — `open_any` feeds the NFA as a suffix self-loop (content_model.c:92); suffix openness works for some shapes but the particle-then-any ordering still rejects. Largest single 1.1 gap. |
| 19 | saxon Wild valid: instances exercising 1.1 wildcard namespace sets (`notNamespace`, `notQName`, `namespace="a b"`) wrongly excluded ("excluded by notQName") | OPEN — the wildcard predicate inverts acceptance for 1.1-only set forms on the INSTANCE side |
| 19 | saxon All + ibm S3_3_6/S3_4_6: `xs:all` with relaxed occurrences (min/max > 1) and RESTRICTION of xs:all rejected | OPEN — content model for relaxed xs:all (1.1 allows 0..N particles) not built |
| 18 | saxon Complex: `<xs:attribute ref="xsi:type" default="..."/>` — instance-determined type via a DEFAULTED xsi:type attribute | OPEN — xsi:type honoring missing wholesale (same class as the 1.0 lane's complex-type-extension row); the defaulted form is 1.1-only syntax |
| 17 | saxon Id + ibm S3_3_4: multiple ID-typed attributes on one type — legal since 1.1, engine still enforces the 1.0 one-ID-per-element rule | OPEN — the one-ID gate needs an engine version flip (gate exists: "more than one ID-typed attribute (XSD 1.0 one-ID-per-element)") |
| 14 | saxon/oracle Zone: valid timezone-bearing dateTime/time lexicals (±14:00, explicitTimezone-compliant) rejected as "lexical value does not match its type" | OPEN — 1.1 timezone lexical space (offsets beyond ±13:00) + explicitTimezone-aware lexical checking |
| 13 | ibm D3_3_4/S2_2_2/S2_2_4/D4_3_6 + saxon PDecimal: valid precisionDecimal / duration-family values rejected | OPEN — xs:precisionDecimal not a recognized builtin; derived lexical validation misfires |
| 12 | ibm S3_4_1/D4_3_15 + mixed/assertions + saxon Assert: documents whose assertions PASS are rejected (content-model empty / children mismatch around complexContent+assert types) | OPEN — the assert capture rewrites the content model for complexContent extension (base content lost); xs:assertion-facet value paths lose simpleContent (#2149 fixed po_sample via the $value binding; the typing-shape residue remains) |
| 9 | ibm S3_10_1/S3_8_6: weakened UPA sequences (`<element minOccurs="0"/><any/>`) and weak-wildcard lax typing | OPEN — the compile-side UPA gate fires where 1.1 relaxed it (see the engine quirk above); weak wildcard (§3.8.6) governing-type compatibility unevaluated |
| 6 | saxon Simple: fixed/default value semantics incl. defaultAttributes application | OPEN — 1.1 default semantics on attribute uses |
| 4 | ibm S3_4_2_4/S3_12: redefine/override — instances valid against the redefined/overridden type rejected | OPEN — overridden component not used for instance typing |
| 3 | saxon VC + mixed/VC: documents valid under `vc:maxVersion` gating rejected ("attribute not declared (strict)") | OPEN — conditional inclusion (§4.2.2) not honored: version-gated declarations are evaluated (or included) when they should be pruned (#2149's assert condition fixed mixed/VC test4_2; the vc-gated declaration rows remain) |
| 3 | ibm S3_12 s3_12v09/s3_12ii04: valid cta schema/instance rows — the mixed complexContent EXTENSION compile-status artifact (the two schema-only rows compile with a non-OK status, see the engine quirk) and the extension content model losing the base's lax wildcard (s3_12v09.xml) | OPEN — alternative DISPATCH itself fires and selects correctly; the blockers are the extension-model classes above |
| 2 | saxon Subsgroup: member of a substitution group where the head appears | OPEN — same substitution-group gap as the 1.0 lane |

## invalid documents accepted (290)

| rows | class | disposition |
|---|---|---|
| 85 | ibm D4_3_15/D4_3_16/S3_3_4 + ibm/saxon assertion sets: simpleType `xs:assertion` facet and complexType `xs:assert` violations accepted (verdict=valid, zero errors) | OPEN — the largest instance-side gap. The XQuery substrate is wired (moxml#556) but the facet is not reached for these shapes: assertion lists on RESTRICTION-derived simple types, D4_3_16 derivation-consistency assertions, assert-on-attributes |
| 66 | saxon Wild: wildcard namespace-set violations (`notNamespace`/`notQName`/multi-ns `namespace`) accepted | OPEN — same wildcard-predicate class as the valid table, accept-side |
| 45 | saxon Id + ibm S3_11_2: identity-constraint 1.1 extensions (fields on attributes/paths, xs:unique scoping, keyref targets) not evaluated | OPEN — the 1.0 lane's capture exists but 1.1 field/selector syntax unsupported → constraint silently skipped |
| 27 | ibm D3_3_4/D3_3_16/D3_3_17/D3_4_26/27/28 + PDecimal: precisionDecimal / yearMonthDuration / dayTimeDuration / dateTimeStamp facet violations accepted | OPEN — same missing-builtin class; facets captured, values unchecked |
| 4 | saxon CTA cta0001/s3_12ii01+03: invalid instances accepted — the SELECTED alternative types lack their TEXT typing (messageTypeDate/floatType-style complexContent+simpleContent restrictions whose inline simpleType facet never reaches the text) | OPEN — mirror of the valid-side restriction-facet class; the dispatch and selection themselves are correct (#2149) |
| 3 | saxon CTA cta0040/41/44: invalid instances accepted — the selected types reference xs:dateTimeStamp (timezone-required) and the builtin is unrecognized, so the text passes unvalidated | OPEN — same missing-builtin class as the zone rows |
| 1 | saxon CTA cta0002: invalid instance accepted — the test expression `@c2:min=1` binds its prefix in the SCHEMA document (§3.13.2 {namespace bindings}); the instance binds a different prefix, the test evaluates false and the base type accepts | OPEN — schema-side namespace bindings for CTA tests |
| 15 | saxon Open: openContent `mode="strict"` violations accepted | OPEN — strict-mode gate (unknown element must be declared) missing; `mode="sibling"` ordering likewise |
| 15 | saxon/oracle Zone: explicitTimezone facet violations (missing/forbidden timezone) accepted | OPEN — facet captured (zone sets compile) but not applied to values |
| 11 | saxon VC + mixed/VC: invalid-under-version-control documents accepted | OPEN — conditional inclusion: the version test prunes NOTHING (all branches evaluated/none pruned), so vc-invalid instances sail through |
| 11 | ibm S3_12/S3_4_2_4 + saxon Override: instance validity under overridden/redefined components | OPEN — overridden components captured (compile.c:55) but instance typing uses the pre-override definitions |
| 3 | ibm S3_2_3: element-decl clause violations (targetNamespace on local declarations) accepted | OPEN |
| 3 | saxon TargetNS: targetNamespace violations accepted | OPEN |
| 3 | saxon All: xs:all occurrence-limit violations accepted | OPEN — mirror of the valid-side relaxed-occurrence gap: the RELAXED limits are neither enforced nor bounded |
| 2 | saxon Simple: default/fixed violations accepted | OPEN — shares the default-semantics class |

## schema-error schemas that compile clean (285)

| rows | class | disposition |
|---|---|---|
| 71 | ibm D3_4_26/27/28, D3_3_4, D2_4_1_2, S2_2_2/S2_2_4 + saxon PDecimal `.n/.bad`: inapplicable facets (length on yearMonthDuration/dayTimeDuration/precisionDecimal, bounds on LISTS) must be compile errors | OPEN — facet-applicability table for the 1.1 datatypes missing (lists cannot be bounded at all; duration family has no length/maxLength) |
| 66 | ibm S3_3_4 si + saxon Simple `.bad`: invalid simpleType derivations (bad facet values, non-numeric restriction of numerics, `final="extension"` misuse...) compile clean | OPEN — §3.3.4/§3.16.2 derivation-ok clause sweep for simple types; single largest schema-side class |
| 32 | saxon All `.n`: xs:all compile constraints — group refs must be xs:all groups, occurrence/derivation rules | OPEN |
| 22 | saxon Wild `.bad` + ibm S3_3_6/S3_4_6: wildcard namespace-set exclusivity (namespace= + notNamespace= combined; notQName with processContents=strict in xs:all) must be rejected | OPEN — the 1.1 wildcard ATTRIBUTES are parsed but their mutual-exclusion constraints unchecked |
| 19 | saxon Override/over011 + ibm S3_12/S4_2_4/S4_2_2-cyclic: override/redefine cycles (restricting a type by its former self, cyclic include+override chains) | OPEN — cycle detection on override/redefine graphs |
| 19 | saxon Open `.bad` + ibm S3_16_2: openContent derivation rules (derived adds open content over a non-open base; fixed-slot violations) | OPEN |
| 10 | ibm S3_2_3/D3_3_15: element-decl clauses (targetNamespace on LOCAL declarations) and gDay timezone-bound facets compile clean | OPEN |
| 9 | saxon Zone `.bad`: explicitTimezone derivation (required→optional forbidden) | OPEN |
| 9 | saxon Complex `.bad`: complexType schema constraints (abstract/block misuse in Complex set) | OPEN |
| 16 | cta alternative-table SCHEMA constraints (saxon cta9001err/9004-9010err/cta0043 + ibm s3_12si01-06/s3_12ii06): §3.3.6.1 cl.7 type-substitutability (every alternative/default type must derive from the declared type or be xs:error), cos-element-consistent type-table equivalence (cta9009/9010err, cta0043's restriction narrowing a table), src-ct.1 (cta9001err mixed+simpleContent), src-element.5 ordering | OPEN — compile-side constraint sweep for type tables; the dispatch validates fine on the legal schemas (#2149 fixed the instance side) |
| 1 | ibm s4_2_2si01 (manifest-tagged cta; really conditional inclusion): duplicate root element declarations gated by malformed vc:minversion must be rejected | OPEN — vc: attribute validity unchecked (same class as the vc901 rows) |
| 5 | ibm D4_3_15/D4_3_16 si: xs:assert with a syntactically-invalid test must fail the SCHEMA ("errors in XPath evaluation are treated as false" applies to VALUES, not compile) | OPEN — the lazy-compile guard treats compile failure as "assertion false"; 1.1 requires the schema itself be rejected |
| 5 | saxon VC/vc901 + ibm S4_2_2: invalid conditional-inclusion combos (vc:minVersion on a 1.0-only schema, invalid vc:type) | OPEN — vc: attribute validity unchecked |
| 3 | saxon TargetNS `.n`: targetNamespace constraints | OPEN |
| 2 | saxon Id `.bad`: ic schema constraints | OPEN |
| 1 | ibm D3_1: the deprecated `http://www.w3.org/2001/XMLSchema-datatypes` namespace must be rejected | OPEN — trivial prefix-map check |

## Already satisfied by this baseline (for the record)

- 18/303 suite-invalid schema documents are correctly rejected, and
  1113/1293 suite-valid documents correctly accepted — the 1.0-core
  substrate (derivation, wildcards 1.0, identity constraints 1.0,
  builtin facets) carries the bulk of the 1.1 core surface.
- assertions family 213/296: complexType xs:assert evaluation through
  the XQuery substrate passes the majority of the ibm/saxon assert
  sets (moxml#556 groundwork) — the reds above are the derivation-shape
  and facet-side residue.
- cta family 97/125 (#2149): conditional type assignment is wired
  end to end — chains captured on global declarations and local
  element particles keyed on syntax presence (not @version), first
  true test selects, the test-less final alternative is the default,
  xs:error selections invalidate, xsi:type overrides, inherited
  attributes join the test context, and the selected type's asserts
  evaluate (with $value bound). Spec tests:
  test/xsd/test_xsd11_cta.cpp.
- override family 71/87: the override capture swaps same-named
  components well enough to type the majority of the saxon Override
  instances.
