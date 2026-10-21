# XSD conformance-lane dispositions — remaining oracle divergences

Measured 2026-10-22 (worktree `wt/xsd-zero`, branch `feat/xsd-queue-zero`,
oracle libxml 2.9.13, `MANIFEST.tsv` 1056 rows):
**989/1056 pass** — 187 valid / 663 invalid / 206 schema-error classes.

History: 769 (2026-10-14 first measurement) → 795 → 803 → 839 → 898 →
903 (2026-10-22 session start) → **989** (this branch: 9 commits, each
one root-caused class).

Remaining 67 divergences, by class. `XsdConformance.
ManifestExpectationsMatchOracle` is the gate; this file is the
disposition record for every row left in it.

## valid rejected by the engine (7)

| rows | class | disposition |
|---|---|---|
| 3 | allsg: `xs:all` whose particles `ref` an abstract substitution-group HEAD; instances carry members, any order | OPEN — content-model gap: substitution-group member acceptance (and `xs:all` member sets) not implemented in `content_model.c` |
| 1 | subst-group-1_1: member (`TestEvent`) where the head (`SimpleEvent`) is referenced in an `xs:choice` | OPEN — same substitution-group gap |
| 1 | complex-type-extension_0: instance carries `xsi:type="ItemWithNumber"` | OPEN — instance-determined type (xsi:type honoring) not implemented; §3.2.7 exemption covers the ATTRIBUTES only |
| 1 | derivation-restriction-anyAttr_0: instance attribute in the base's wildcard after restriction | OPEN — attribute-wildcard inheritance across restriction |
| 1 | bug322411_0: duplicate `xs:unique` key tuples | DISPOSITIONED — the engine is spec-correct (duplicate keys must fail); the 2.9.13 oracle verdict "valid" is a libxml2 identity-constraint miss (field `.`). Do NOT chase this row; it flips with a newer oracle |

## schema-error schemas that compile clean (19)

| rows | class | disposition |
|---|---|---|
| 9 | derivation-ok-restriction-2-1-1 + restriction-attr1: restriction attribute-use consistency — an `optional` derived use against a `required` base use, or no matching base use/wildcard at all | OPEN — derivation-ok attribute clauses (slice 4 covered the compile-side checks; the instance-side derived-vs-base table comparison is missing) |
| 6 | derivation-ok-extension-err: duplicate attribute use inside one complexType | OPEN — mechanical duplicate-@name check at capture |
| 3 | include3: duplicate global element declaration across `xs:include` | OPEN — merged-schema duplicate detection |
| 1 | oss-fuzz-51295: circular substitution group (`e` substitutes `e`) | OPEN — cycle check on substitutionGroup refs |

## invalid documents accepted (41)

| rows | class | disposition |
|---|---|---|
| 8 | ns0/attruse: `attributeFormDefault="qualified"` — a qualified-form declared attribute used unprefixed (or vice versa) accepted | OPEN — form enforcement on attribute declarations (the declared-row lookup is name-only; it must be namespace-aware per the decl's form) |
| 6 | po0/po1 + derivation-restriction-anyAttr cross-pairs: instance root in a namespace the schema does not declare ("no matching global declaration for the validation root") | OPEN — root-resolution ladder falls back past the strict (ns, name) pair |
| 4 | bug323510 family: required child missing (min=1) accepted | OPEN — content-model occurrence floor at the element end |
| 2 | all_4: `xs:all` missing one of its required members | OPEN — same occurrence-floor class |
| 2 | choice_4: `xs:choice` missing its required arm | OPEN — same |
| 3 | any5: `xs:any` namespace set matches a foreign element | OPEN — element-wildcard namespace sets |
| 5 | anyAttr-derive2/processContents cross-pairs: `processContents="strict"` wildcard admits undeclared attributes | OPEN — processContents enforcement on wildcards |
| 1 | idc-keyref-err1: keyref with no matching key | OPEN — keyref target lookup (capture exists, the check is missing) |
| 1 | changelog093: duplicate `xs:unique` keys (field on a sub-element path) | OPEN — unique over non-trivial field paths (bug322411 shows the `.` field case works) |
| 2 | cos-st-restricts-1-2-err: length facet on extended simple content not enforced | OPEN — facet walk through the complexContent extension chain |
| 2 | xsd-list-itemType/simpleType-varieties cross-pairs: character content in an element-only content model accepted | OPEN — element-only content-type gate |
| 1 | mixed1_0: undeclared attribute on a mixed-content type | OPEN — shares the attributeFormDefault/strict-walk class |
| 4 | bug141312/bug141333/bug309338 cross-pairs: root-namespace mismatches | OPEN — shares the root-resolution class |

## Already retired by this branch (for the record)

xsi-namespace exemption from the strict attribute gate (16), empty
content models reject element children (9), unprefixed builtin type
references (3) + strict attribute walk on simple-typed elements (2),
no-tns unit `##other` wildcards (4), xs:attribute 3.2.3 declaration
clauses (55), g-regional timezone + three-dash gDay (1), token-family
whitespace collapse (1), unknown xs:-builtin type references (3).
