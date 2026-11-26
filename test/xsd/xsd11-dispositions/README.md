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

Re-measured 2026-11-24 (leptris-ruby#679, branch
`fix/xsd-ns-qualification` — targetNamespace qualification on
validated documents: the root-resolution ladder binds the global
declaration whose (namespaceURI, localName) pair matches EXACTLY,
no fallback past a namespace mismatch): **1458/2206 pass** —
1117 valid / 323 invalid / 18 schema-error (748 reds). The three
flips are saxon Override n02 rows (no-namespace instances against
targetNamespace-carrying override schemas, over018.n02 + over019.n02
against over019/over020) — the "instance validity under
overridden/redefined components" class drops 11 → 8; spec tests:
test/xsd/test_xsd_ns_qualification.cpp.

Re-measured 2026-11-24 (#2154, branch `xsd11-assertion-facets` —
xs:assertion facets enforced on simple types across the restriction
ladder with list-item and union-member chains, the complexType
assert capture keyed on assert SYNTAX presence, assert inheritance
down the derivation chain, §3.13.2 namespace bindings +
xpathDefaultNamespace wrapped into the test prolog, the §3.13.4.1
assertion data model excluding comments/PIs, typed-temporal
comparisons against $value; re-measured on the #679-qualified
base): **1524/2206 pass** — 1117 valid / 389 invalid / 18
schema-error (682 reds, zero regressions).

Re-measured 2026-11-25 (#2155, branch `xsd11-datatype-facets` —
facet applicability on the 1.1-only datatypes: xs:dateTimeStamp /
xs:precisionDecimal / xs:yearMonthDuration / xs:dayTimeDuration
registered with their own lexicals, unprefixed builtin references
canonized at capture, the compile-time applicability sweep
(inapplicable facets, invalid facet values, cross-facet order
clauses, narrowing + the fixed law on the 1.1 builtins, bounds on
lists), the exact decimal comparator, duration/date-time
value-space bound comparisons with year zero, the precisionDecimal
scale window and its NaN-enumeration rule, numeric/temporal
enumeration equality, collapse for restrictions declaring
whiteSpace="collapse", explicitTimezone enforcement, and the
pre-existing fractional-seconds-with-timezone hole in the time
tail): **1676/2206 pass** — 1140 valid / 435 invalid / 101
schema-error (530 reds, zero regressions). The 71-row
facet-applicability class is GREEN: all 68 datatype rows flipped;
the three non-datatype rows the #2148 classifier lumped in
(s2_2_2si01/02 — substitution-group cycles; s2_2_4si01 — the ic
ref clause) are re-dispositioned below.

By family: assertions 270/296 · cta 99/125 · override 75/87 ·
openContent 122/205 · core 1110/1493 (#2155: cta 97→99 — the
xs:dateTimeStamp selections type their text; core +149 — the
datatype classes 13/27/3/14/18 retired and the 71-row
facet-applicability class green; 9 of the 66-class ibm S3_3_4
schema rows (facets the applicability sweep now rejects at
compile: si12–si20) and 1 saxon Simple row flipped as a side
effect; the tables below carry the #2155 counts).

Re-measured 2026-11-25 (#2156, branch `xsd11-ic-extensions` — the
identity-constraint 1.1 extensions: selector/field XPaths parsed
into the §3.11.6 subset at COMPILE time with SCHEMA-side namespace
resolution + xpathDefaultNamespace (§3.13.2), out-of-subset
expressions and unresolved schema prefixes failing the schema,
@ref reuse with the §3.11.3 category match, capture reaching local
element particles, §3.10.4.1 skipped subtrees omitted from target
node sets, per-scoping-element uniqueness (§3.11.4), vacuous node
tables (§3.11.5), strict keyref target tables; re-measured on the
#2155 base): **1702/2206 pass** — 1140 valid / 459 invalid / 103
schema-error (504 reds, zero regressions). The identity-constraint
class CLOSES: 18 invalid instance rows + 2 schema-error rows flip
green, and 6 wildcard-family key rows (wild101/wild103 — their
schemas carry an xs:key the old engine never evaluated) flip with
them. Spec tests: test/xsd/test_xsd11_ic.cpp. The 1.0 lane rides
the same engine and gains 3 rows (1020 → 1022, see
xsd-dispositions/README.md).

By family (#2156): core 1110→1136; the other families unchanged.

Re-measured 2026-11-25 (#2150, branch `xsd11-override` — xs:override
(§4.3 component replacement): the overridden document's MERGED
component set collected recursively (its own named top level, the
documents it includes, and the components its own override/redefine
bodies contribute — the over009 shape, where a document that is
itself an overriding document still declares the component), an
overriding component outside that set overrides nothing and is
IGNORED — dropped from the merged schema (§4.3.2), overriding
references re-resolve against the POST-merge set (a replacement
deriving from its own former name is a circular derivation —
override does NOT get the 1.0 redefine hook's original-naming
semantics, and the #1958 alias re-point is redefine-only now),
targetNamespace alignment (src-override.1: equal, or the overridden
document declares none), overriding-set uniqueness (src-override.2:
one (kind, name) pair twice within a body or across bodies), the
mutual-override conflict (src-override.3: an inner override of the
overriding document is an error exactly when it would REPLACE one
of its components — the empty-body mutual form stays legal,
over023), and src-override.4: a kept component's dangling type
reference fails the schema; re-measured on the #2156 base):
**1710/2206 pass** — 1140 valid / 459 invalid / 111 schema-error
(496 reds, zero regressions). The eight saxon Override schema-error
rows flip (over011/014/016/017/021/022/024/026). Spec tests:
test/xsd/test_xsd11_override.cpp.

By family (#2150): override 75→83; the other families unchanged.

`XsdConformance11.ManifestExpectationsMatch` (test/xsd/
test_xsd11_conformance.cpp) is the gate; this file is the disposition
record for every row left in it. The floor (now 1710) is pinned and

Re-measured 2026-11-25 (#2151, branch `xsd11-opencontent` — the
xs:openContent / xs:defaultOpenContent lane: the unmarked-schema
syntax gate (the whole saxonData/Open corpus carries no version
marker), the S4SD interleave default for a mode-less openContent,
the defaultOpenContent precedence chain — declared beats default
(mode="none" declares nothing AND blocks the default, open031),
the default beats the base for extensions (open030), per-document
unit scoping (a document's default applies to its own types:
redefine bodies ride the includer's default, open042; override
bodies and included documents do not, open043/open040/open041),
suffix + interleave absorption over sequence / xs:all / xs:choice
models (§3.4.4.3 — the wildcard absorbs before ANY model element
step, and a particle-only trace attributes children greedily so
declared particles keep governing their instances: open025.n1's
mis-typed integer is caught, open047.v3's second `<b>` rides the
skip wildcard untyped), the §3.4.2.3.3 clause 6.2 empty-variety
rules (variety empty forbids all text — open012.n3; an applied
wildcard upgrades to element-only, open010), extension
inheritance + mode="none" inheritance + the §3.10.6.3 wildcard
union + the cos-ct-extends.1.4.3.2.2.3/4 mode ladder + subset
check, the restriction subset/no-add/weaker-pc checks with the
suite's empty-model exemption (open020/021/022), the §3.8.6.4
element-vs-wildcard UPA relaxation for 1.1-evidence schemas
(open034's wildcard after a+ is legal competition), the open
content wildcard's @notNamespace (open047) and prefix-resolved
@notQName (s3_4_1ii04), and the schema-error class (src-ct.3
any/none co-occurrence, non-any children, the annotation limit,
ID form + uniqueness, occurrence attributes on the wildcard,
ct-props-correct.5 simpleContent carriers, src-ct.5 mixed
agreement, the mixed-mismatch extension, the defaultOpenContent
placement rule, si04/05/06 derivation ladders); re-measured on
the #2156 base): **1787/2206 pass** — 1191 valid / 467 invalid /
129 schema-error (419 reds, zero regressions — the pinned 1.0
corpus holds at 175 with its environment-gated skip). The
openContent family goes 122/205 → **196/205**. Spec tests:
test/xsd/test_xsd11_opencontent.cpp (27 specs, red-first).

By family (#2151): openContent 122→196; core 1136→1147; valid
1140→1191; invalid 459→467; schema-error 103→129 (the Open
`.bad` + ibm S3_4_1 derivation classes close; the ibm S3_16_2
rows the #2148 classifier lumped into openContent are UNION
tests that flipped with the wildcard-namespace machinery).

By family: assertions 270/296 · cta 99/125 · override 75/87 ·
openContent 196/205 · core 1147/1493.

Re-measured 2026-11-26 (#2182, branch `xsd11-defattr` — the
§3.4.2.5 default attribute groups: xs:schema/@defaultAttributes
captured per schema DOCUMENT unit as resolved QNames (unprefixed
rides the default namespace, §3.17.6.2), the groups' attribute uses
appended to each applying complexType's table AFTER its own uses
(§3.4.2.4 — computed as if an <attributeGroup ref> child appeared
last), defaultAttributesApply="false" opting a type out, per-unit
scoping (an included document's types ride their own document's
list; redefine bodies stay in the including unit and DO receive the
defaults, open044; override bodies capture suppressed — §4.2.5,
open045), the group's xs:anyAttribute joining the type's wildcard
union, the schema-level ref resolution independent of any opt-out
(open203.bad), cross-namespace group refs resolved by (ns, name)
through the declaring unit's targetNamespace (open204.bad vs
open205), the c-props-correct.4 duplicate-use rejection where the
synthetic ref collides with the type's OWN use (si02, and si03's
circular re-introduction) while a collision with a BASE-derived use
merges per cos-st-extends (from_base provenance on the extension
fold's row copies); plus the strict attribute walk now running on
attribute-less complexTypes (every root registers its row slot —
the open044.n1 shape: the included doc's bare alpha rejects
xml:lang); re-measured on the release-447 base): **1809/2206
pass** — 1192 valid / 475 invalid / 142 schema-error (397 reds,
zero regressions). The 1.0 lane rides the same engine and gains 3
rows (1022 → 1025, see xsd-dispositions/README.md). Two pre-#2182
local pins rode the old attribute-less leniency and were
re-pinned to the strict posture: XsdAlternative11.
FirstMatchingAlternativeTypes (the no-match fallback asserts its
content typing attribute-free; the unreferenced selector on the
fallback is now invalid) and Xsd11Cta.XsiTypeOverridesSelected
Alternative (xsi_wins.xsd's base declares the selector k so the
override discriminator survives strict assessment). Spec tests:
test/xsd/test_xsd11_defattr.cpp (13 specs, red-first).

`XsdConformance11.ManifestExpectationsMatch` (test/xsd/
test_xsd11_conformance.cpp) is the gate; this file is the disposition
record for every row left in it. The floor (now 1809) is pinned and
>>>>>>> 39f81619 (xsd11: xs:openContent phase-3 rules (#2151))
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
(`s3_8_6v01`), xsi:type-with-default (`complex003+`), override
(`s3_12v09`, `s3_12ii04`). (#2155 retired the two datatype members of
this list: `xs:dateTimeStamp` (`zone101`) and `xs:precisionDecimal`
(`pdecimal001`) now compile and type.) Treat the status artifact as
part of each construct class below, not a separate lane.

The corpus also carries 79 UTF-16 suite files (S3_2_3, S2_2_4, S3_3_4
families); the engine parses UTF-16 natively, so they are IN lane.

Per-row counts below are approximate (the #2148 classifier lineage);
the authoritative totals are the gate's printed verdict split
(#2155: 157 valid / 175 invalid / 202 schema-error reds; #2156
re-sliced the ic class row: 153 valid / 151 invalid / 200
schema-error reds; #2150 flips 8 override schema-error rows:
153 valid / 151 invalid / 192 schema-error reds).

## valid rejected by the engine (153)

| rows | class | disposition |
|---|---|---|
| 41 | saxon Open: openContent — allowed extra elements after (and with `mode="interleaved"`, among) the declared particles rejected ("children do not match the content model") | CLOSED by #2151 — suffix + interleave absorption over sequence/all/choice models (§3.4.4.3: the wildcard absorbs before ANY model element step), defaultOpenContent capture with per-document unit scoping, the empty-variety rules, extension inheritance/union; the family's only remaining reds are the defaultAttributes sub-family (new row in the invalid table) |
| 19 | saxon Wild valid: instances exercising 1.1 wildcard namespace sets (`notNamespace`, `notQName`, `namespace="a b"`) wrongly excluded ("excluded by notQName") | OPEN — the wildcard predicate inverts acceptance for 1.1-only set forms on the INSTANCE side |
| 19 | saxon All + ibm S3_3_6/S3_4_6: `xs:all` with relaxed occurrences (min/max > 1) and RESTRICTION of xs:all rejected | OPEN — content model for relaxed xs:all (1.1 allows 0..N particles) not built |
| 18 | saxon Complex: `<xs:attribute ref="xsi:type" default="..."/>` — instance-determined type via a DEFAULTED xsi:type attribute | OPEN — xsi:type honoring missing wholesale (same class as the 1.0 lane's complex-type-extension row); the defaulted form is 1.1-only syntax |
| 17 | saxon Id + ibm S3_3_4: multiple ID-typed attributes on one type — legal since 1.1, engine still enforces the 1.0 one-ID-per-element rule | OPEN — the one-ID gate needs an engine version flip (gate exists: "more than one ID-typed attribute (XSD 1.0 one-ID-per-element)") |
| 14 | saxon/oracle Zone: valid timezone-bearing dateTime/time lexicals (±14:00, explicitTimezone-compliant) rejected as "lexical value does not match its type" | CLOSED by #2155 — the ±14:00 lexical rows, the timezone-bound d3_3_9..14 rows and zone202's year-zero enumeration flipped (the temporal-key enumeration equality now admits "-0000-02" over the enumerated "0000-02") |
| 4 | saxon Zone zone206/301/302: time enumerations with instant-equal timezone-distinct values, and the keyref `t` failing to resolve ("keyref 't': reference does not resolve to any key") | OPEN — zone206's enumeration equality is in place; the remaining blocker for the keyref rows is VALUE-space tuple comparison (§3.11.4: "equal or identical ... on the values") — the constraint now RUNS (#2156 fixed the capture + scoping) but tuple equality is still string-based, so PT29H never equals P1DT5H |
| 11 | ibm S3_4_1 (9) + saxon Assert (2): documents whose assertions PASS are rejected — the ibm rows keep the content-model residue around complexContent+assert types ("children do not match the content model"); assert020/021 (content model again); assert-simple006 flipped in #2155 | OPEN — the assert capture rewrites the content model for complexContent extension (base content lost); the typing-shape residue remains |
| 9 | ibm S3_10_1/S3_8_6: weakened UPA sequences (`<element minOccurs="0"/><any/>`) and weak-wildcard lax typing | OPEN — the compile-side UPA gate fires where 1.1 relaxed it (see the engine quirk above); weak wildcard (§3.8.6) governing-type compatibility unevaluated |
| 5 | saxon Simple: fixed/default value semantics incl. defaultAttributes application | OPEN — 1.1 default semantics on attribute uses |
| 4 | ibm S3_4_2_4/S3_12: redefine/override — instances valid against the redefined/overridden type rejected | OPEN — overridden component not used for instance typing |
| 2 | saxon mixed/VC: documents valid under `vc:maxVersion` gating rejected ("attribute not declared (strict)") | OPEN — conditional inclusion (§4.2.2) not honored: version-gated declarations are evaluated (or included) when they should be pruned (#2149's assert condition fixed mixed/VC test4_2; the vc-gated declaration rows remain) |
| 3 | ibm S3_12 s3_12v09/s3_12ii04: valid cta schema/instance rows — the mixed complexContent EXTENSION compile-status artifact (the two schema-only rows compile with a non-OK status, see the engine quirk) and the extension content model losing the base's lax wildcard (s3_12v09.xml) | OPEN — alternative DISPATCH itself fires and selects correctly; the blockers are the extension-model classes above |
| 4 | substitution groups: saxon Subsgroup (2, member of a substitution group where the head appears) + ibm S2_2_2 (2, valid instances against cyclic subsgroup heads) | OPEN — same substitution-group gap as the 1.0 lane |

## invalid documents accepted (151)

| rows | class | disposition |
|---|---|---|
| 15 | ibm S3_3_4 ii: instance-invalid ID/IDREF rows (fixed on ID-typed elements, ID value-space violations) | OPEN — the one-ID-per-element gate and the ID value-space checks; no assert semantics involved (the #2149-era table miscounted these into the 85-row class) |
| 21 | saxon Id id004-id017: instance-invalid ID-family rows accepted (ID list types, ID-typed complexTypes with simple content, ID attribute defaults) | OPEN — re-sliced out of #2155's ic class row (#2156): the one-ID gate engine flip + the ID value-space checks, same class as the ibm S3_3_4 ii row |
| 11 | assertion-facet residue (#2154's class, 66 of the 85 caught): assert tests the engine cannot ASSESS — the facet's ABSENT focus (assert-simple008/009/010: context item, position() and last() are undefined during simple-type validation; the engine evaluates with the owning element), fn:doc() relative loading inside assert tests (assert011 n+v), xs:integer division by zero must raise (assert012), recursive count asserts over derived contexts (d4_3_15ii14/15), the sequence-carrier decomposition for every-over-$value (listunion6_2), castable-as-double accepting '10f4' (assert-simple003/004) | OPEN — each names an ENGINE gap (focus semantics, doc loading, arithmetic error semantics, carrier decomposition, castable lexical validators), not a facet-capture gap |
| 60 | saxon Wild: wildcard namespace-set violations (`notNamespace`/`notQName`/multi-ns `namespace`) accepted | OPEN — same wildcard-predicate class as the valid table, accept-side (#2156 retired the six wild101/wild103 key rows whose real blocker was an unevaluated xs:key) |
| 5 | saxon CTA cta0001/cta0044 + ibm s3_12ii01+03: invalid instances accepted — the SELECTED alternative types lack their TEXT typing (cta0040/41 flipped in #2155 when xs:dateTimeStamp registered; cta0044's residue is the complexContent content-model gap — the selected aType_f requires an `f` child the instance lacks) | OPEN — mirror of the valid-side restriction-facet class; the dispatch and selection themselves are correct (#2149) |
| 1 | saxon CTA cta0002: invalid instance accepted — the test expression `@c2:min=1` binds its prefix in the SCHEMA document (§3.13.2 {namespace bindings}); the instance binds a different prefix, the test evaluates false and the base type accepts | OPEN — schema-side namespace bindings for CTA tests |
| 12 | saxon Open (11) + ibm s3_16_2ii06 (1): openContent `mode="strict"` violations accepted — strict-mode gate (unknown element must be declared) missing; `mode="sibling"` ordering likewise | CLOSED by #2151 — the violations these rows encode are namespace-set violations, and the open content wildcard now enforces its full grammar (@namespace lists, @notNamespace, prefix-resolved @notQName, greedy attribution so a declared particle still type-checks its own instances: open025.n1). Strict processContents on open-content-absorbed children remains tier-1 lax (§3.10.4.1 posture pinned by the moxml corpus); no remaining row depends on it |
| 5 | saxon VC: invalid-under-version-control documents accepted | OPEN — conditional inclusion: the version test prunes NOTHING (all branches evaluated/none pruned), so vc-invalid instances sail through |
| 3 | ibm S3_4_2_4 + saxon Override: instance validity under overridden/redefined components | OPEN — overridden components captured (compile.c:55) but instance typing uses the pre-override definitions (#679 retired the three Override n02 no-namespace-instance rows via exact root binding) |
| 3 | ibm S3_2_3: element-decl clause violations (targetNamespace on local declarations) accepted | OPEN |
| 3 | saxon TargetNS: targetNamespace violations accepted | OPEN |
| 3 | saxon All: xs:all occurrence-limit violations accepted | OPEN — mirror of the valid-side relaxed-occurrence gap: the RELAXED limits are neither enforced nor bounded |
| 2 | saxon Simple: default/fixed violations accepted | OPEN — shares the default-semantics class |
| 1 | ibm D3_3_17 ii: base64Binary with minLength=4 octets accepts the unpadded "123456" (the suite's base64Binary lexical requires the RFC-2045 padding) | OPEN — binary lexical strictness (re-dispositioned here from #2148's datatype row; not a 1.1-datatype defect) |

(#2155 retired the datatype rows: the 27-row "precisionDecimal /
yearMonthDuration / dayTimeDuration / dateTimeStamp facet violations
accepted" class is GREEN — the 1.1 builtins validate their lexicals,
the ordered bounds compare in the value space, the precisionDecimal
scale window and NaN-enumeration rule are enforced, and
explicitTimezone is applied to values; the 18-row explicitTimezone
class and the 3-row cta0040/41 stamp class flipped with it.)

## schema-error schemas that compile clean (200)

| rows | class | disposition |
|---|---|---|
| 57 | ibm S3_3_4 si (11) + saxon Simple `.bad` (46): invalid simpleType derivations (bad facet values, non-numeric restriction of numerics, `final="extension"` misuse...) compile clean | OPEN — §3.3.4/§3.16.2 derivation-ok clause sweep for simple types; single largest schema-side class (#2155's applicability sweep flipped 9 of the ibm rows — si12–si20's inapplicable facets — and 1 saxon Simple row) |
| 8 | ibm S3_4_1: complexType derivation-ok clauses (cos-st-restricts / derivation-ok-restriction) compile clean | CLOSED by #2151 — the open-content derivation ladders (cos-ct-extends.1.4.3.2.2.3/4, cos-ns-subset, cos-content-act-restrict approximation), the extension mixed-match (si08), the src-ct.5 mixed-attribute conflict (si09), the defaultOpenContent placement rule (si02), and the src-ct.3 wildcard checks (si01/si03) now reject; the ibm S3_4_1 schema-invalid rows the classifier tagged openContent are all green |
| 32 | saxon All `.n`: xs:all compile constraints — group refs must be xs:all groups, occurrence/derivation rules | OPEN |
| 22 | saxon Wild `.bad` (19) + ibm S3_3_6/S3_4_6 (3): wildcard namespace-set exclusivity (namespace= + notNamespace= combined; notQName with processContents=strict in xs:all) must be rejected | OPEN — the 1.1 wildcard ATTRIBUTES are parsed but their mutual-exclusion constraints unchecked |
| 7 | override/redefine cycles residue: ibm S4_2_4/S4_2_2 (2) + ibm S3_4_2_4 (3) + the s3_12ii04 mixed-extension compile-status artifact (1) + s4_2_4si02c (1 — the mutual-redefine cycle's closing document si02b was never copied into the corpus, so the document graph the oracle sees is not reconstructible) | OPEN — cycle detection on redefine graphs beyond the override lane (#2150 flipped the eight saxon Override rows: over011/014/016/017/021/022/024/026 — src-override.1–4 plus the post-merge circular-derivation check, override-scoped) |
| 19 | saxon Open `.bad` (14) + ibm S3_16_2 (5): openContent derivation rules (derived adds open content over a non-open base; fixed-slot violations) | OPEN |

| 15 | override/redefine cycles: saxon Override/over011 (9) + ibm S4_2_4/S4_2_2 (2) + ibm S3_4_2_4 (3) + the s3_12ii04 mixed-extension compile-status artifact (1) | OPEN — cycle detection on override/redefine graphs |
| 19 | saxon Open `.bad` (14) + ibm S3_16_2 (5): openContent derivation rules (derived adds open content over a non-open base; fixed-slot violations) | CLOSED by #2151 — the saxon `.bad` ladder enforces at compile (src-ct.3 any/none co-occurrence, annotation limit, ID form + uniqueness, occurrence attributes on the wildcard, simpleContent carriers, restriction no-add/subset/weaker-pc with the empty-model exemption, extension mode ladder + cos-ns-subset + §3.10.6.3 union); the ibm S3_16_2 rows were misclassified by the #2148 classifier — they are wildcard-UNION tests, not openContent, and flipped with the §3.10.6.3 machinery |
| 9 | saxon Open defaultAttributes sub-family: open035.n1 (required id from the default attribute group missing must be invalid), open044.n1/n2 + open045.n1/n2 (anyAttribute from the default group mis-admitted / not admitted), open202.n1 (defaultAttributesApply=false must exclude the group's attrs), open205.n1 (foreign-ns default group attrs), open203.bad/204.bad (unresolved + non-imported defaultAttributes refs must fail the schema) | OPEN — re-dispositioned by #2151 from the openContent classes: these nine exercise §3.4.2.5 DEFAULT ATTRIBUTE GROUPS (`xs:schema/@defaultAttributes`, `defaultAttributesApply`, the group's attribute rows + anyAttribute, cross-ns reference resolution) — a separate feature, not openContent |
>>>>>>> 39f81619 (xsd11: xs:openContent phase-3 rules (#2151))
| 15 | cta alternative-table SCHEMA constraints (saxon cta9001err/9004-9010err/cta0043 (9) + ibm s3_12si01-06 (6)): §3.3.6.1 cl.7 type-substitutability (every alternative/default type must derive from the declared type or be xs:error), cos-element-consistent type-table equivalence (cta9009/9010err, cta0043's restriction narrowing a table), src-ct.1 (cta9001err mixed+simpleContent), src-element.5 ordering | OPEN — compile-side constraint sweep for type tables; the dispatch validates fine on the legal schemas (#2149 fixed the instance side) |
| 10 | ibm S3_2_3 (9) + D3_3_15 (1): element-decl clauses (targetNamespace on LOCAL declarations) and gDay timezone-bound facets compile clean | OPEN |
| 5 | saxon/oracle Zone `.bad` (zone004/005/008 + zone408/409): explicitTimezone derivation (required→optional forbidden) | OPEN — the rest flipped under #2155's facet tables |
| 1 | saxon Complex `.bad`: complexType schema constraints (abstract/block misuse) | OPEN |
| 1 | ibm s4_2_2si01 (manifest-tagged cta; really conditional inclusion): duplicate root element declarations gated by malformed vc:minversion must be rejected | OPEN — vc: attribute validity unchecked (same class as the vc901 rows) |
| 4 | ibm D4_3_15/D4_3_16 si: xs:assert with a syntactically-invalid test must fail the SCHEMA ("errors in XPath evaluation are treated as false" applies to VALUES, not compile) | OPEN — the lazy-compile guard treats compile failure as "assertion false"; 1.1 requires the schema itself be rejected |
| 5 | saxon VC/vc901 (4) + ibm S4_2_2 (1): invalid conditional-inclusion combos (vc:minVersion on a 1.0-only schema, invalid vc:type) | OPEN — vc: attribute validity unchecked |
| 3 | saxon TargetNS `.n`: targetNamespace constraints | OPEN |
| 4 | saxon Subsgroup (2) + ibm S2_2_2 (2): substitution-group cycles (structures 3.3.6.1.4/5 — circular subsgroup heads, type substitutability) | OPEN — re-dispositioned here from #2148's datatype row (not datatype defects); substitution-group cycle detection |
| 1 | ibm S2_2_4 (1): ic schema constraints (3.11.3.4 — ref excludes non-annotation children and element children) | OPEN — re-dispositioned here from #2148's datatype row; the ic ref allowlist (#2156 closed the saxon Id `.bad` pair — missing referent + category mismatch — via the post-capture @ref resolution) |
| 1 | ibm D3_1: the deprecated `http://www.w3.org/2001/XMLSchema-datatypes` namespace must be rejected | OPEN — trivial prefix-map check |

(#2155 retired the 71-row facet-applicability class: all 68 datatype
rows — inapplicable facets (length on the duration family and
precisionDecimal, fractionDigits on precisionDecimal, bounds on
lists), invalid facet values (bounds and enumerations that are not
lexicals of the terminal builtin, negative totalDigits, malformed
duration/stamp spellings), cross-facet order violations (min > max,
both-forms collisions, minScale > maxScale, the empty effective
scale window), narrowing violations (widened bounds, increased
totalDigits, relaxed scales), fixed-facet re-specification,
whiteSpace != collapse and explicitTimezone != required on
dateTimeStamp — now REJECT the schema at compile time.)

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
- assertions family 269/296 (#2154): xs:assertion facets enforced on
  simple types — captured as an accumulating list (a restriction may
  carry several), enforced across the restriction ladder, per list
  ITEM and per union MEMBER; $value binds the typed value (numerics
  bare, list values itemized, everything else the lexical). The
  complexType xs:assert capture keys on assert SYNTAX presence
  (version_11 || saw_alternative || saw_assert), inherits down the
  derivation chain (§3.13.6), resolves its prefixes through the
  schema document's §3.13.2 bindings + xpathDefaultNamespace, runs
  in the §3.13.4.1 assertion data model (comments and PIs invisible),
  and compares temporally against typed scalars. Spec tests:
  test/xsd/test_xsd11_assertions.cpp.
- override family 83/87 (#2150): the override capture merges by the
  §4.3.2 semantics — the overridden document's merged component set
  (recursive through includes and its own override bodies),
  non-corresponding overriding components dropped, references
  re-resolved post-merge, tns alignment, set uniqueness, the
  mutual-override conflict and dangling-ref laws enforced at
  compile; the four remaining reds are adjacent classes
  (over008.n01's ID uniqueness, the over027 NOTATION pair,
  s4_2_4si02c's missing cycle document — classified above).
  Spec tests: test/xsd/test_xsd11_override.cpp.
- datatypes family (#2155): the four 1.1 builtins carry their own
  lexicals (dateTimeStamp's required timezone, precisionDecimal's
  arithmetic space with exponent spellings and INF/NaN, the strict
  yearMonthDuration/dayTimeDuration component sets); the compile
  rejects inapplicable facets, invalid facet values, cross-facet
  order violations, base-widening restrictions and fixed-facet
  re-specifications on the 1.1 terminals; the ordered bounds
  compare in the VALUE space (exact decimals, duration
  months/seconds, timezone-normalized date/time instants with year
  zero); explicitTimezone applies to values; precisionDecimal's
  scale rides the spelling and its enumerated NaN matches nothing.
  Spec tests: test/xsd/test_xsd11_datatypes.cpp (fixtures:
  test/xsd/xsd11-datatypes/).
- identity constraints 1.1 (#2156): the saxon Id + ibm S3_11_2
  class is closed end to end — selector/field XPaths are parsed at
  compile time into the §3.11.6 selector/field subset (child
  chains, leading `.//`, `.`-steps, a final `@`-step, selector
  unions; explicit axes, predicates, absolute paths, mid-path
  `//`, non-final `@`, field unions, and unbound schema prefixes
  are compile-time schema errors) with every name test resolved
  against the SCHEMA document's in-scope bindings + the four
  xpathDefaultNamespace forms (§3.13.2); `@ref` reuse copies the
  target's shape with the §3.11.3 category match (id041/id042);
  capture reaches local element particles; §3.10.4.1 skipped
  subtrees leave the target node sets; uniqueness is judged per
  scoping element instance; every key/unique contributes its
  (possibly EMPTY) §3.11.5 node table so keyrefs against it fail;
  keyref target tables must exist. Spec tests:
  test/xsd/test_xsd11_ic.cpp.
