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

By family: assertions 282/296 · cta 115/125 · override 87/87 ·
openContent 205/205 · core 1156/1493.

Re-measured 2026-11-27 (#2184 wave 2, branch `xsd11-tail-2184` — the
Override tails, the next-largest batched slice in the issue: the
redefine/override location graph must not cycle (§4.2.2/§4.3 — the
chase-time check matches a target against the ancestor chain and the
entry document BEFORE the visited dedup swallows the closing edge; a
target equal to the CONTAINING document is the legal self-reference
hook, and an EMPTY-BODY directive closes nothing — src-override.3's
mutual-override form over023 stays legal); the IBM cyclic-redefine
fixture s4_2_4si02c became judgeable at all by copying the pinned
snapshot's missing dependency document s4_2_4si02b.xsd into the
corpus (its redefine chain si02c→si02→si02b→si02c is now
reconstructible; the file rides the same manifest row); a-props-
correct (§3.2.2.1) NOTATION attributes must be ENUMERATED — a bare
xs:NOTATION type reference or a restriction of NOTATION whose facets
carry no enumeration fails the schema (over027, and the saxon Simple
schema-error rows simple091/094 flipped with the same rule); and ID
values are unique within the document (§3.3.5 — the validator
collects ID-typed attribute values per run, over008.n01's duplicate
id="b" fails, .v01's distinct ids stay valid); the manifest GATE also
gains the oracle-faithful arm: an instance row whose schema the SUITE
itself invalidates is judged with the suite's expected=invalid when
the engine rejects the schema too (over015.v01 vs over027) — scoped
to the suite-rejected set so the non-OK-status compile quirk handles
(complex003+, s3_8_6v01) keep validating normally; re-measured on the
8e05b358 base): **1845/2206 pass** — 1192 valid / 492 invalid / 161
schema-error (361 reds, zero regressions — the pinned 1.0 corpus
holds at 1041). The override family CLOSES: **87/87**. Spec tests:
test/xsd/test_xsd11_override.cpp (three red-first specs riding the
pinned corpus).

Re-measured 2026-11-27 (#2184, branch `xsd11-tail-b` — the wildcard
negative namespace/name sets, §3.10.2 + §3.10.6.3: every attribute
wildcard canonizes at capture (##targetNamespace resolved against
its declaring unit's tns, ##other folded into ns=##any plus the
{tns, ##local} exclusions, absent @namespace spelled ##any per
leptris-rs#73, @notNamespace's ##targetNamespace resolving to
"##local" when the declaring unit has none) and a type's effective
wildcard is the §3.10.6.3 INTERSECTION over its direct
xs:anyAttribute and every wildcard carried by its attributeGroup
refs, while an extension UNIONS the base's effective wildcard in at
the fold — one canon wildcard per row, no separate base arm; the
instance side admits through the positive grammar minus the
notNamespace and notQName exclusions; @notQName prefixed tokens
canonize to {uri}local against the declaring element's scope (the
xml prefix prebound per XML Namespaces 1.0 — the shared DOM
namespace lookup now prebinds it, the attribute accessors already
did); the src-wildcard compile sweep rejects @namespace +
@notNamespace co-occurrence, malformed notQName spellings
(`xml:xml:lang`, `:stylesheet`) and undeclared prefixes, and
vacuous exclusions whose namespace the wildcard could never admit
(wild031-035); derivation-ok-restriction.4.1 runs the §3.10.6.3
subset law over the captured canon rows (wild020-022/057); the
element wildcard's notQName canonizes the same way and
##definedSibling expands to the concrete sibling names —
substitution-group heads AND members (wild071) — with the borrowed
schema-default wildcard privatized per type before expansion
(wild074); the §3.8.6.4 UPA relaxation arms on the ##definedSibling
spelling ONLY (all305/s3_3_6si03/s3_4_6si03's overlap invalidity
depends on the strict posture); re-measured on the c0da7a9e base
(the wave-2 override tails): **1909/2206 pass** — 1198 valid / 535
invalid / 176 schema-error (297 reds, ZERO regressions against the
1845 base — 64 rows flip: 43 accept-side predicate rows, 15
src-wildcard schema-error rows, 6 valid rows including the two ibm
s3_10_1 ##definedSibling pairs). The pinned 1.0 corpus holds at
1041; full suite 2503/2503. Spec tests:
test/xsd/test_xsd11_wildcard.cpp (24 specs, red-first; two of the
predecessor's red pins corrected against the manifest oracle — the
union's disallowed set keeps only names EVERY arm rejects, per the
wild045/wild046 manifest rows).

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

Re-measured 2026-11-26 (#2185, branch `xsd-sweep-2185` — the
derivation-ok attribute-use clauses over the POST-capture tables:
an extension's own row re-declaring a folded base use by expanded
name fails the schema unless it spells use="prohibited" (which
contributes nothing to the union), a restriction's derived rows
must match a non-prohibited base use (a required base use cannot
weaken; optional/required with no matching base use fails unless
the base's attribute wildcard admits the row's namespace),
attribute rows capture use="prohibited" and an explicit
@targetNamespace (1.1's local-declaration form), and the instance
side rejects an attribute a governing prohibited row names; the
wildcard arm rides validate.c's namespace-set matcher exported as
xsd_any_attr_matches; re-measured on the #2193 base):
**1813/2206 pass** — 1192 valid / 479 invalid / 142 schema-error
(393 reds, zero regressions). The 1.0 lane rides the same engine
and gains 16 rows (1025 → 1041, see xsd-dispositions/README.md).
Four 1.1 rows flip: cta0044.n01 (the selected aType_f prohibits
@r; the instance carries it), target003.n1/n2 (the att row binds
its declared @targetNamespace — the foreign-ns and no-namespace
spellings are no longer admitted), s3_2_3ii01 (an unprefixed
attribute against a @targetNamespace-qualified local declaration).

`XsdConformance11.ManifestExpectationsMatch` (test/xsd/
test_xsd11_conformance.cpp) is the gate; this file is the disposition
record for every row left in it. The floor (now 1813) is pinned and
monotonic — same law as the 1.0 lane. Scaffolding context: the engine
already captures xs:assert / xs:assertion / xs:alternative /
xs:openContent / xs:override (moxml#556 wired xs:assert through the
XQuery substrate); what the classes below measure is how much of the
1.1 SEMANTICS each capture actually enforces.

Re-measured 2026-11-27 (#2184, branch `xsd11-tail-2184` — the
alternative-table compile constraints, the largest OPEN cta class in
the tail: §3.12.2 XPath-valid alternative tests (a test that does not
PARSE fails the SCHEMA — only evaluation errors take the §3.12.4
"false" channel; the #2149 TestEvaluationErrorIsFalse fixture is
re-pinned to a dynamic error, `1 div 0 eq 1`, and the malformed-syntax
form moved to Xsd11Cta.MalformedTestSyntaxIsSchemaError), src-element.5
ordering (a test-less alternative mid-table fails the schema, flagged
at capture where the chain truncates), §3.3.6.1 cl.7 /
cos-ct-type-substitutable (every alternative's type derives from the
declared type — complex chains walk res_base/assert_base/text_type
plus a builtin-derivation table; a local UNION declared type is
substituted by any type deriving from a member, cta0014/15; xs:error
always substitutes; unresolved names stay posture), the parser's
cast-tail fold now takes comparison operators with proper precedence
(`@length cast as xs:int > @width cast as xs:int` — the shape three
ibm valid schemas use — parses, so the syntax gate is regression-free
there; QT3/xslt/xpath lanes ride unchanged), cos-element-consistent
type-table equality for same-name particles in one content model
(cta9009/9010), the particle-restriction table preservation
(cta0043's narrowed stamp table), src-ct.1 (a complexType with a
simpleContent/complexContent wrapper may not declare attributes at
the complexType level — si01), and §3.2.7.1 {inheritable} (malformed
lexical forms fail the schema, cta9006/9007; a restriction may not
flip the base use's inheritability, cta9004/9005); re-measured on the
#2185 base): **1839/2206 pass** — 1192 valid / 490 invalid / 157
schema-error (367 reds, zero regressions — the pinned 1.0 corpus
holds at 1041). The cta family goes 100/125 → **115/125**; the gate's
red split moves to 101 valid / 89 invalid / 177 schema-error. Spec
tests: test/xsd/test_xsd11_ctatable.cpp (15 specs, red-first). Two
moxml#579 fixtures (selector_admitted.xsd, only_referenced_exempt.xsd)
carry the non-substitutable xs:int/xs:string pair the new constraint
rejects — re-pinned to the substitutable xs:string/xs:token pair (the
specs' valid/invalid verdicts ride the strict-attribute gate and are
unchanged).

Re-measured 2026-11-27 (branch `xsd11-oc-interleave` — the
interleaved-mode GOVERNANCE refinement of the #2151 open-content
absorption, the residue the row-41 class closed with: the #2151
attribution trace was a single prefix cut — every child from the
particle path's first divergence rode the wildcard — which is exact
for SUFFIX mode but coarse for INTERLEAVED mode, where the walk
resumes after a wildcard span; §3.4.4.3 cl. 3.3's greedy law is
particle-first per child: a declared particle still available to the
model governs its own instance even across a span (a mis-typed
element of a still-open particle fails through its own type, not the
skip wildcard), while a name whose particle is past the model's
horizon (already consumed, or the model has moved on) rides the
wildcard untyped exactly as open025.v1/open047.v3 pin); the
attribution channel is per child (`oc_attr`), the suffix prefix-cut
law unchanged, the public `leptris_xsd_content_valid` path passes no
attribution and keeps its contract; re-measured: **1984/2206** —
zero corpus-row flips (the suite never exercises the
span-then-mistyped-declared shape; the refinement is spec-pinned) —
valid 1252 / invalid 543 / schema-error 189, families
assertions 282/296 · cta 118/125 · override 87/87 · openContent
205/205 · core 1292/1493; the pinned 1.0 corpus holds at 1044. Spec
tests: test/xsd/test_xsd11_opencontent.cpp
(InterleaveSpanBeforeParticleStillGoverned /
InterleaveConsumedParticleDoesNotGovern / InterleaveChoiceGoverned —
red-first: both mis-typed-after-span shapes false-validated under
the prefix cut).

Re-measured 2026-11-28 (#2184 residue tail, branch `xsd11-cta-ic` —
the CTA/IC batch the previous scoping left open, five small slices:
the SELECTED alternative type's TEXT typing runs — §3.4.3.3 cl. 1: a
simpleContent derivation's inline <xs:simpleType> child is the text
type, captured as the synthesized "complex:NAME" slot instead of
riding the base's (complex, therefore unresolved) name, and
xsd_capture_simple accepts a restriction element DIRECTLY so the
direct-facet restrictions capture their enumeration (cta0001.n01/n02,
s3_12ii01.xml, s3_12ii03.xml); CTA test prefixes bind in the SCHEMA
document — the §3.13.2 {namespace bindings} + xpathDefaultNamespace
prolog captured at the alternative element and compiled into the test,
exactly the assert lane's machinery (cta0002.n01: the instance binds
the same namespace with a different prefix and the test still selects
treq); a cast/castable target resolving to a COMPLEX type of the
schema fails the SCHEMA (XPST0080 — the capture scans the test for
the cast/castable SequenceTypes, resolves each through the host's
bindings, s3_12ii06); §4.2.2 conditional inclusion — every
XMLSchema-versioning attribute validates (two-part version decimals,
QName/facet lists with bound prefixes), components whose annotation
set is unsatisfied against the 1.1 processor DETACH at capture (an
unsatisfied xs:schema root empties the whole document, includes
included; unknown vc: names are IGNORED, vc009; malformed values fail
the schema, the vc90x.bad class), and one document's surviving
top-level element declarations may not repeat a name (src-element.2 —
s4_2_2si01's duplicate root pair after the minVersion="3.2" member
prunes); keyref tuples compare in the VALUE space (§3.11.4 — the
field terminal's ASSESSED type, recorded by the main walk at the
validate_local_uri chokepoint, drives a time/instant/duration/numeric
dispatch past the string lexical: xs:time normalizes the timezone
with 24:00:00 ≡ 00:00:00 and a tz-absent lexical never equaling a
tz-carrying one) — zone206.v01/v02, zone301.v01, zone302.v01):
re-measured on the ccc58dcb base: **2006/2206 pass** — 1258 valid /
553 invalid / 195 schema-error (200 reds, ZERO regressions by
red-set diff — 22 flips). The cta family CLOSES: **125/125**. The
valid-side saxon mixed/VC pair (test5/test6), the five-row
invalid-side saxon VC class (vc002.n1, vc010.v1 ×4) and the four
vc90x.bad schema rows flipped with the conditional-inclusion
mechanism in the same wave. Spec tests:
test/xsd/test_xsd11_ctatable.cpp (six new specs riding the pinned
corpus) + test/xsd/test_xsd11_ic.cpp (two new specs).

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
(`pdecimal001`) now compile and type. #2184 wave 3 retired the
xsi:type-with-default member: the built-in xsi declarations resolve
for attribute uses — `complex003/004/005/006/007/009/010.xsd` compile
clean. The #2184 all lane retired the restriction-of-all members
(`s3_3_6v01/04`, `s3_4_6v01/04`) and the mixed-extension members
(`s3_12v09`, `s3_12ii04`) — all six now compile clean.) Treat the
status artifact as part of each construct class below, not a
separate lane.

A second quirk — the four `##definedSibling` repeated-sibling
instance rows (wild070.n1/071.n1/074.n1/084.n1) failing only
in-process — is ROOT-CAUSED AND CLOSED by the #2184 all lane: the
##definedSibling expansion's dup check `strstr`ed an UNINITIALIZED
`names[4096]` buffer, so a spurious garbage match suppressed the
token rewrite and the raw spelling (which admits the defined
siblings) survived; the verdict rode the stack garbage of whichever
binary ran. The buffer is zero-initialized (all-lane commit), the
four rows validate invalid in-process and standalone alike, and the
1909-vs-1908 local/CI delta that row carried is recovered.

The corpus also carries 79 UTF-16 suite files (S3_2_3, S2_2_4, S3_3_4
families); the engine parses UTF-16 natively, so they are IN lane.

Per-row counts below are approximate (the #2148 classifier lineage);
the authoritative totals are the gate's printed verdict split
(#2155: 157 valid / 175 invalid / 202 schema-error reds; #2156
re-sliced the ic class row: 153 valid / 151 invalid / 200
schema-error reds; #2150 flips 8 override schema-error rows:
153 valid / 151 invalid / 192 schema-error reds; #2183 flips 11
assertion-facet rows invalid-side — the gate split moves
102 valid / 143 invalid / 166 schema-error reds → 102 valid /
132 invalid / 166 schema-error reds; #2184 flips 15 cta
schema-error rows: 101 valid / 89 invalid / 177 schema-error reds;
#2184 wave 2 flips 4 override schema-error rows + 2 override/core
invalid rows: 101 valid / 87 invalid / 173 schema-error reds;
#2184 wave 3 flips 18 defaulted-xsi:type rows valid-side (7 schema
+ 11 instance): 83 valid / 118 invalid / 142 schema-error reds
(re-measured split, manifest expectation field — the wave-2 line's
87/173 came from the pre-suite_rejected accounting); #2213 flips 64
wildcard rows; the #2184 all lane flips 55 rows (34 all-family,
17 wild riders, 4 via the #2214 names-buffer fix); the #2184
cta-ic wave flips 22 rows (11 cta/keyref targets + 7 VC riders
invalid-side, 4 Zone valid-side): 35 valid / 57 invalid /
108 schema-error reds.

## valid rejected by the engine (59)

| rows | class | disposition |
|---|---|---|
| 41 | saxon Open: openContent — allowed extra elements after (and with `mode="interleaved"`, among) the declared particles rejected ("children do not match the content model") | CLOSED by #2151 — suffix + interleave absorption over sequence/all/choice models (§3.4.4.3: the wildcard absorbs before ANY model element step), defaultOpenContent capture with per-document unit scoping, the empty-variety rules, extension inheritance/union; the family's only remaining reds are the defaultAttributes sub-family (new row in the invalid table) |
| 6 | saxon Wild valid: instances exercising 1.1 wildcard namespace sets (`notNamespace`, `notQName`, `namespace="a b"`) wrongly excluded ("excluded by notQName") | OPEN — 13 rows flipped by the #2184 all lane's riders (the relaxed-all UPA arming + the order-free all matcher let the xs:all-carrying members compile and validate: wild030.xsd+v1+v2, wild047.xsd+v1, wild052.xsd+v1+v2, wild073.xsd, wild072.v1 and the wild072 instances re-paired against wild073); the four `##definedSibling` repeated-sibling `.n1` rows flipped with the expansion buffer fix (see the quirk note) |
| 19 | saxon All + ibm S3_3_6/S3_4_6: `xs:all` with relaxed occurrences (min/max > 1) and RESTRICTION of xs:all rejected | CLOSED by the #2184 all lane — the order-free counting matcher (per-child min/max windows, wildcard children with weak-wildcard precedence, substitution-group members counting toward the head, group-ref flattening), the compile-side relaxed-all UPA arming (element-vs-wildcard overlap inside an all is legal §3.8.6), the all-to-all extension MERGE (§3.4.4.2: the particle sets union and the instance interleaves freely), the all-restriction sweep (§3.9.6: partition-with-summed-windows, substitution membership, wildcard subset, element-in-wildcard), and the mixed-flag source check reading the base's own @mixed. 18 rows: ibm s3_3_6/s3_4_6 v01/v04 pairs + saxon all002.v01, all004.v02, all005.v01, all006.v01, all001.v01-vs-all007, all301.v01, all304.v01, all306.xsd+v01, all314.v02 |
| 18 | saxon Complex: `<xs:attribute ref="xsi:type" default="..."/>` — instance-determined type via a DEFAULTED xsi:type attribute | CLOSED by #2184 wave 3 — the four BUILT-IN xsi declarations (§3.2.7) resolve for attribute uses without an import (§3.17.6.2 cl. 4.2.4: the `src-element.2` artifact — complex003/004/005/006/007/009/010.xsd compile clean) and capture as uses whose {value constraint} rides the USE (§3.2.2.3); the DEFAULT has NO effect at assessment (§3.4.4.2 cl. 4 of the defaulted-attribute definition excludes the built-in declarations — "the attribute uses would have no effect", §3.2.6.4 Note), so complex003/004 judge the instance against the declared decimal content type and complex006's defaulted xsi:nil never substitutes (n3's bare `<root/>` fails the lexical); the use's occurrence constraint still bites (§3.4.4.2 cl. 3 — complex009/complex010's use="required"); and an EXPLICIT xsi:type must override the selected type (§3.3.4.3 cl. 4) — a SIMPLE type is never substitutable for a COMPLEX selected type (§3.3.4.2 has no such arm), so every complex005 instance naming xs:integer/xs:short is invalid with assessment falling back to the selected type; complex-over-complex keeps the pinned lax direction (derivation-ok walking unentered) |
| 17 | saxon Id + ibm S3_3_4: multiple ID-typed attributes on one type — legal since 1.1, engine still enforces the 1.0 one-ID-per-element rule | OPEN — the one-ID gate needs an engine version flip (gate exists: "more than one ID-typed attribute (XSD 1.0 one-ID-per-element)") |
| 14 | saxon/oracle Zone: valid timezone-bearing dateTime/time lexicals (±14:00, explicitTimezone-compliant) rejected as "lexical value does not match its type" | CLOSED by #2155 — the ±14:00 lexical rows, the timezone-bound d3_3_9..14 rows and zone202's year-zero enumeration flipped (the temporal-key enumeration equality now admits "-0000-02" over the enumerated "0000-02") |
| 4 | saxon Zone zone206/301/302: time enumerations with instant-equal timezone-distinct values, and the keyref `t` failing to resolve ("keyref 't': reference does not resolve to any key") | CLOSED by the #2184 cta-ic wave — keyref tuples compare in the VALUE space (§3.11.4): the field terminal's assessed type (recorded by the main walk) drives the dispatch, xs:time normalizes the timezone with 24:00:00 ≡ 00:00:00 (zone206.v02) and a tz-absent lexical never equals a tz-carrying one (zone206.n01 stays red-free), the durations compare by total months/seconds (P1DT5H == PT29H0M0.000S == P01DT300M; P2Y4M == P01Y016M) — the four .v01/.v02 rows flipped, the .n01 rows keep their invalid verdicts |
| 11 | ibm S3_4_1 (9) + saxon Assert (2): documents whose assertions PASS are rejected — the ibm rows keep the content-model residue around complexContent+assert types ("children do not match the content model"); assert020/021 (content model again); assert-simple006 flipped in #2155 | OPEN — the assert capture rewrites the content model for complexContent extension (base content lost); the typing-shape residue remains |
| 9 | ibm S3_10_1/S3_8_6: weakened UPA sequences (`<element minOccurs="0"/><any/>`) and weak-wildcard lax typing | OPEN — the compile-side UPA gate fires where 1.1 relaxed it (see the engine quirk above); weak wildcard (§3.8.6) governing-type compatibility unevaluated |
| 5 | saxon Simple: fixed/default value semantics incl. defaultAttributes application | OPEN — 1.1 default semantics on attribute uses |
| 4 | ibm S3_4_2_4/S3_12: redefine/override — instances valid against the redefined/overridden type rejected | OPEN — overridden component not used for instance typing |
| 2 | saxon mixed/VC: documents valid under `vc:maxVersion` gating rejected ("attribute not declared (strict)") | CLOSED by the #2184 cta-ic wave — conditional inclusion prunes at capture: the vc:-gated duplicate declaration detaches (test5's my:myInt-gated Example, test6's typeAvailable="xs:int my:myInt" — myInt is not a declared type) and the satisfied xs:int form types the instance; #2149's assert condition fixed mixed/VC test4_2 earlier |
| 3 | ibm S3_12 s3_12v09/s3_12ii04: valid cta schema/instance rows — the mixed complexContent EXTENSION compile-status artifact (the two schema-only rows compile with a non-OK status, see the engine quirk) and the extension content model losing the base's lax wildcard (s3_12v09.xml) | CLOSED by the #2184 all lane — the mixed agreement source check now reads the BASE's own @mixed even for a direct (non-complexContent) particle model, so a mixed all base extends by a mixed derived type and the compile-status artifact dissolves (s3_12v09.xsd+v, s3_12ii04.xsd) |
| 4 | substitution groups: saxon Subsgroup (2, member of a substitution group where the head appears) + ibm S2_2_2 (2, valid instances against cyclic subsgroup heads) | OPEN — same substitution-group gap as the 1.0 lane |

## invalid documents accepted (67)

| rows | class | disposition |
|---|---|---|
| 15 | ibm S3_3_4 ii: instance-invalid ID/IDREF rows (fixed on ID-typed elements, ID value-space violations) | OPEN — the one-ID-per-element gate and the ID value-space checks; no assert semantics involved (the #2149-era table miscounted these into the 85-row class) |
| 21 | saxon Id id004-id017: instance-invalid ID-family rows accepted (ID list types, ID-typed complexTypes with simple content, ID attribute defaults) | OPEN — re-sliced out of #2155's ic class row (#2156): the one-ID gate engine flip + the ID value-space checks, same class as the ibm S3_3_4 ii row |
| 11 | assertion-facet residue (#2154's class, 66 of the 85 caught): assert tests the engine cannot ASSESS — the facet's ABSENT focus (assert-simple008/009/010: context item, position() and last() are undefined during simple-type validation; the engine evaluates with the owning element), fn:doc() relative loading inside assert tests (assert011 n+v), xs:integer division by zero must raise (assert012), recursive count asserts over derived contexts (d4_3_15ii14/15), the sequence-carrier decomposition for every-over-$value (listunion6_2), castable-as-double accepting '10f4' (assert-simple003/004) | CLOSED by #2183 — the XQuery semantics landed: §3.16.4 ABSENT FOCUS on the facet lane (position()/last()/. raise XPDY0002 → the facet does not hold; the $value-only positive controls stay green), fn:doc() resolving against the SCHEMA's compile-captured base URI (the `declare base-uri` XQuery channel), xs:integer/divisor-zero raising FOAR0001 under the XQuery spelling, §3.13.4.1 cl. 1.3 subtree scoping (relative descent roots at the validated element; '/' and '//' select NOTHING — ii14/ii31 pin both directions), fn:data over the itemized $value binding (listunion6_2's every-decomposition), and `castable as` resolving unprefixed builtin names through xpathDefaultNamespace (003.n1 ×2). Eleven rows flipped, zero regressions; the remaining saxon Assert reds (assert020/021) are the content-model class in the valid-rejected table |
| 13 | saxon Wild: wildcard namespace-set violations (`notNamespace`/`notQName`/multi-ns `namespace`) accepted | OPEN — 47 of the 60 CLOSED across #2213 (43, negative-set predicate end to end) and the #2184 all lane (wild004.n1×wild006 rides the cross-document lane's include-chain, flipped with the matcher; the four `##definedSibling` repeated-sibling `.n1` rows flipped with the expansion buffer fix); the residue is the cross-document wildcard lane (wild009/010 against include-chained schemas wild011/012), the multi-ns predicate rows (wild062/063/065), and wild042 |
| 4 | saxon CTA cta0001 + ibm s3_12ii01+03: invalid instances accepted — the SELECTED alternative types lack their TEXT typing (cta0040/41 flipped in #2155 when xs:dateTimeStamp registered; cta0044.n01 flipped in #2185 — the selected aType_f's use="prohibited" @r row rejects the instance's attribute) | CLOSED by the #2184 cta-ic wave — the selected type's TEXT typing runs: a simpleContent derivation's inline <xs:simpleType> child is the text type (§3.4.3.3 cl. 1 — it used to ride the base's complex name and text_valid accepted everything unresolved), and xsd_capture_simple now accepts a restriction element directly so the direct-facet enumeration (squareType/rectType) captures — the dispatch and selection were already correct (#2149) |
| 1 | saxon CTA cta0002: invalid instance accepted — the test expression `@c2:min=1` binds its prefix in the SCHEMA document (§3.13.2 {namespace bindings}); the instance binds a different prefix, the test evaluates false and the base type accepts | CLOSED by the #2184 cta-ic wave — the alternative test compiles under the §3.13.2 prolog captured at the alternative element ({namespace bindings} + xpathDefaultNamespace), the same machinery the assert/assertion lane uses; the instance's own prefix spellings never enter the test's context |
| 12 | saxon Open (11) + ibm s3_16_2ii06 (1): openContent `mode="strict"` violations accepted — strict-mode gate (unknown element must be declared) missing; `mode="sibling"` ordering likewise | CLOSED by #2151 — the violations these rows encode are namespace-set violations, and the open content wildcard now enforces its full grammar (@namespace lists, @notNamespace, prefix-resolved @notQName, greedy attribution so a declared particle still type-checks its own instances: open025.n1). Strict processContents on open-content-absorbed children remains tier-1 lax (§3.10.4.1 posture pinned by the moxml corpus); no remaining row depends on it |
| 5 | saxon VC: invalid-under-version-control documents accepted | CLOSED by the #2184 cta-ic wave — conditional inclusion prunes at capture: vc002.n1 rides vc006's vc:maxVersion="0.9" root (the whole document, includes included, contributes nothing and `temp` is undeclared), vc010.v1's `x` attribute loses its typeUnavailable/typeAvailable-pruned declaration (xs:integer available → the *Unavailable annotation is unsatisfied; the bananaSkin list in vc012 leaves typeAvailable unsatisfied) |
| 3 | ibm S3_4_2_4 + saxon Override: instance validity under overridden/redefined components | OPEN — overridden components captured (compile.c:55) but instance typing uses the pre-override definitions (#679 retired the three Override n02 no-namespace-instance rows via exact root binding) |
| 2 | ibm S3_2_3: element-decl clause violations (targetNamespace on local declarations) accepted (s3_2_3ii01 flipped in #2185 — the @targetNamespace-qualified local declaration rejects the unprefixed instance attribute) | OPEN |
| 1 | saxon TargetNS: targetNamespace violations accepted (target003.n1/n2 flipped in #2185 — the row-level @targetNamespace capture plus the base wildcard's namespace set reject the foreign-ns and no-namespace spellings; target001.n1 remains) | OPEN |
| 3 | saxon All: xs:all occurrence-limit violations accepted | CLOSED by the #2184 all lane — the counting matcher enforces the relaxed windows end to end: all004.n01 rides the xsi:nil="1" boolean spelling (a nil'ed element may carry no content), all314.n03/n04 fail the merged optional-all's particle floors (a present all satisfies its particles or nothing does) |
| 2 | saxon Simple: default/fixed violations accepted | OPEN — shares the default-semantics class |
| 1 | ibm D3_3_17 ii: base64Binary with minLength=4 octets accepts the unpadded "123456" (the suite's base64Binary lexical requires the RFC-2045 padding) | OPEN — binary lexical strictness (re-dispositioned here from #2148's datatype row; not a 1.1-datatype defect) |

(#2155 retired the datatype rows: the 27-row "precisionDecimal /
yearMonthDuration / dayTimeDuration / dateTimeStamp facet violations
accepted" class is GREEN — the 1.1 builtins validate their lexicals,
the ordered bounds compare in the value space, the precisionDecimal
scale window and NaN-enumeration rule are enforced, and
explicitTimezone is applied to values; the 18-row explicitTimezone
class and the 3-row cta0040/41 stamp class flipped with it.)

## schema-error schemas that compile clean (114)

| rows | class | disposition |
|---|---|---|
| 55 | ibm S3_3_4 si (11) + saxon Simple `.bad` (44): invalid simpleType derivations (bad facet values, non-numeric restriction of numerics, `final="extension"` misuse...) compile clean | OPEN — §3.3.4/§3.16.2 derivation-ok clause sweep for simple types; single largest schema-side class (#2155's applicability sweep flipped 9 of the ibm rows — si12–si20's inapplicable facets — and 1 saxon Simple row; #2184 wave 2's a-props-correct NOTATION rule flipped simple091/094 — NOTATION-typed attributes without an enumeration) |
| 8 | ibm S3_4_1: complexType derivation-ok clauses (cos-st-restricts / derivation-ok-restriction) compile clean | CLOSED by #2151 — the open-content derivation ladders (cos-ct-extends.1.4.3.2.2.3/4, cos-ns-subset, cos-content-act-restrict approximation), the extension mixed-match (si08), the src-ct.5 mixed-attribute conflict (si09), the defaultOpenContent placement rule (si02), and the src-ct.3 wildcard checks (si01/si03) now reject; the ibm S3_4_1 schema-invalid rows the classifier tagged openContent are all green |
| 17 | saxon All `.n`: xs:all compile constraints — group refs must be xs:all groups, occurrence/derivation rules | OPEN — 15 of the 32 CLOSED by the #2184 all lane (cos-particle-restrict for all-in-place-of-all: partition-with-summed-windows over substitution-group members and wildcard splits, wildcard subset narrowing, element-in-wildcard, no dropped required particle — all202-205, all223/224/229, all235/236/238/244), and 5 more by the extension particle grammar (all extends only by all, sequence by sequence, empty all by empty all — bug 6202: all308; sequence-extended-by-all: all309/312) plus the merged-all wildcard disjointness law (all305.n: distinct wildcards of one all need disjoint namespace sets); the residue is the SEQUENCE-permutation subsumption (all212-215 — each derived sequence particle must restrict a distinct base particle), the type-narrowing subsumption (all227: the derived element type must derive from the base's), the choice subsumption (all231-233 — all233), the group-ref-must-be-all rules (all008-011: a group ref inside xs:all resolves to an all group and carries minOccurs=maxOccurs=1), the all306/all307 sibling shapes (all302/303.n), all241-244's residue (all241-243), and all313; the all234 valid row needs the sequence-permutation acceptance |
| 5 | saxon Wild `.bad` (4) + ibm S3_3_6/S3_4_6 (1): the remaining wildcard schema-side constraints — the all-lane wildcards' relaxed-occurrence shapes (wild041/078/079/081.bad), notQName with processContents=strict in xs:all | OPEN — 17 of the 22 CLOSED: 15 by #2184's src-wildcard sweep (@namespace + @notNamespace co-occurrence rejects — wild007/008; a notQName token with an undeclared prefix or a malformed spelling rejects — wild036-039; a notQName exclusion whose namespace the wildcard could never admit rejects as vacuous — wild031-035; the attribute-wildcard RESTRICTION subset law rejects non-subset derivations — wild020-022/057) and 2 by the #2184 all-restriction sweep (s3_3_6si02/s3_4_6si02: the derived wildcard may not WIDEN — dropping the base's notQName exclusion admits the excluded names) |
| 6 | override/redefine cycles residue: ibm S4_2_4/S4_2_2 (2) + ibm S3_4_2_4 (3) + the s3_12ii04 mixed-extension compile-status artifact (1) | OPEN — cycle detection on redefine graphs beyond the override lane (#2150 flipped the eight saxon Override rows: over011/014/016/017/021/022/024/026 — src-override.1–4 plus the post-merge circular-derivation check, override-scoped); the s4_2_4si02c member CLOSED by #2184 wave 2 — the pinned snapshot's si02b was copied into the corpus and the redefine/override location-graph cycle check (with the empty-body exemption) now rejects the three-document cycle |
| 19 | saxon Open `.bad` (14) + ibm S3_16_2 (5): openContent derivation rules (derived adds open content over a non-open base; fixed-slot violations) | CLOSED by #2151 — the saxon `.bad` ladder enforces at compile (src-ct.3 any/none co-occurrence, annotation limit, ID form + uniqueness, occurrence attributes on the wildcard, simpleContent carriers, restriction no-add/subset/weaker-pc with the empty-model exemption, extension mode ladder + cos-ns-subset + §3.10.6.3 union); the ibm S3_16_2 rows were misclassified by the #2148 classifier — they are wildcard-UNION tests, not openContent, and flipped with the §3.10.6.3 machinery |
| 9 | saxon Open defaultAttributes sub-family: open035.n1 (required id from the default attribute group missing must be invalid), open044.n1/n2 + open045.n1/n2 (anyAttribute from the default group mis-admitted / not admitted), open202.n1 (defaultAttributesApply=false must exclude the group's attrs), open205.n1 (foreign-ns default group attrs), open203.bad/204.bad (unresolved + non-imported defaultAttributes refs must fail the schema) | OPEN — re-dispositioned by #2151 from the openContent classes: these nine exercise §3.4.2.5 DEFAULT ATTRIBUTE GROUPS (`xs:schema/@defaultAttributes`, `defaultAttributesApply`, the group's attribute rows + anyAttribute, cross-ns reference resolution) — a separate feature, not openContent |
| 15 | cta alternative-table SCHEMA constraints (saxon cta9001err/9004-9010err/cta0043 (9) + ibm s3_12si01-06 (6)): §3.3.6.1 cl.7 type-substitutability (every alternative/default type must derive from the declared type or be xs:error), cos-element-consistent type-table equivalence (cta9009/9010err, cta0043's restriction narrowing a table), src-ct.1 (cta9001err mixed+simpleContent), src-element.5 ordering | CLOSED by #2184 — the compile-side sweep for type tables (test/xsd/test_xsd11_ctatable.cpp): §3.12.2 XPath-valid tests reject at compile (si04/si05/si06 — the parser's cast-tail fold first learned comparison operators so the valid corpus's `cast as T > cast as T` shape keeps parsing), src-element.5 flags the mid-table test-less alternative (cta9001err), cos-ct-type-substitutable walks the declared type's derivation chain (complex: res_base/assert_base/text_type + builtin ancestry; union declared types substitute by member derivation, cta0014/15) and rejects si02/si03/cta9008err, cos-element-consistent demands equal tables for same-name particles (cta9009/9010), the particle restriction preserves the base table (cta0043), src-ct.1 rejects the wrapper-plus-direct-attributes shape (si01), and §3.2.7.1 {inheritable} rejects malformed lexicals (cta9006/9007) and restriction flips (cta9004/9005) |
| 10 | ibm S3_2_3 (9) + D3_3_15 (1): element-decl clauses (targetNamespace on LOCAL declarations) and gDay timezone-bound facets compile clean | OPEN |
| 5 | saxon/oracle Zone `.bad` (zone004/005/008 + zone408/409): explicitTimezone derivation (required→optional forbidden) | OPEN — the rest flipped under #2155's facet tables |
| 1 | saxon Complex `.bad`: complexType schema constraints (abstract/block misuse) | OPEN |
| 1 | ibm s4_2_2si01 (manifest-tagged cta; really conditional inclusion): duplicate root element declarations gated by malformed vc:minversion must be rejected | CLOSED by the #2184 cta-ic wave — the vc:minVersion="3.2" root prunes away (3.2 > the 1.1 processor) and the two surviving roots repeat the name in one document: src-element.2 rejects the schema (the duplicate-global check runs AFTER the prune, so test5/test6's vc-gated duplicates stay legal); the misspelt vc:minversion itself is IGNORED per vc009's law, never an error |
| 4 | ibm D4_3_15/D4_3_16 si: xs:assert with a syntactically-invalid test must fail the SCHEMA ("errors in XPath evaluation are treated as false" applies to VALUES, not compile) | OPEN — the lazy-compile guard treats compile failure as "assertion false"; 1.1 requires the schema itself be rejected |
| 5 | saxon VC/vc901 (4) + ibm S4_2_2 (1): invalid conditional-inclusion combos (vc:minVersion on a 1.0-only schema, invalid vc:type) | CLOSED by the #2184 cta-ic wave — vc: attribute validity is checked: a three-part version ("1.1.3", vc901) and a non-decimal spelling ("10g", vc902) fail the version lexical, an undeclared prefix ("vx:", vc904) and a non-QName token ("23", vc905) fail the QName/facet lists; the s4_2_2si01 member closed with the prune + duplicate-global mechanism |
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
- override family 87/87 (#2150 + #2184 wave 2): the override capture
  merges by the §4.3.2 semantics — the overridden document's merged
  component set (recursive through includes and its own override
  bodies), non-corresponding overriding components dropped,
  references re-resolved post-merge, tns alignment, set uniqueness,
  the mutual-override conflict and dangling-ref laws enforced at
  compile; the four remaining reds of #2150 (over008.n01's ID
  uniqueness, the over027 NOTATION pair, s4_2_4si02c's missing cycle
  document) closed under #2184 wave 2 — document-scoped ID
  uniqueness, the a-props-correct NOTATION enumeration rule, and the
  location-graph cycle check over the now-complete si02b corpus
  copy (the over015.v01 instance row rides the gate's
  suite-rejected arm).
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
