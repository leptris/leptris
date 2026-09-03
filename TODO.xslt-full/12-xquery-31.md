# 12 — XQuery 3.1/4.0 extensions (#684-B)

group by, count clauses, allowing empty, tumbling/sliding windows,
module imports + library modules, declare context item, map/array
surface (08), transform(), load-xquery-module (stub), higher-order
library (07), string templates (06). 4.0 previews behind a flag
probed against Saxon-HE 13 defaults.
Gate: qt3tests XQuery 3.1 subset + qt3extra green.

## Update 2026-09-03: group by + try/catch SHIPPED (v1.9.66/67 / PR #786, #789)

- try/catch (v1.9.66): XPATH_OP_TRY — catch * catches with
  $err:code/description/value bound; the XSLT rejection pin
  flipped (XPath 3.0 defines it). Named catches never match (no
  error-code model — pinned).
- group by (v1.9.67): first-appearance partition; clause vars
  rebound to whole-group member lists (nodes preserved); order
  keys evaluated POST-group so group by + order by composes.
  XqTuple generalized to per-var member lists. Banked:
  xpath_variable_set_nodeset OVERWRITES without freeing — always
  unbind before rebind (the 168B/query leak only Linux LSan
  sees); "group" must be in is_clause_word or the domain span
  swallows it.

Remaining for lane 12: windowing (for tumbling/sliding window),
typeswitch, the error-code model for named catches (thread the
leptris_error_code through try), qt3tests 3.1 subset.

## Update 2026-09-03 (later): windows SHIPPED (v1.9.69 / PR #795)

- for tumbling|sliding window $w in D start ... end ...: windows
  enumerate over the domain — $w = the member list (BORROWED from
  the domain nodeset; claiming ownership freed the members
  mid-iteration — the corrupted-output signature), boundary vars
  ($s/$sp/$e/$ep) as singles. Tumbling resumes after the end,
  sliding after the start. Tuple snapshots carry $w whole.
- XPath 2.0 value-comparison keywords (eq/ne/lt/le/gt/ge) in the
  relational parser — bare NCNames at operand boundaries only.
- Banked: xq_unbind_all must cover window names (the 2184B/query
  overwrite leak, Linux LSan only); scan_word needs scan_ws FIRST
  after a keyword token (the fifth bite of that invariant).

Remaining for lane 12: typeswitch, error-code model for named
catches, qt3tests 3.1 subset. Lane 11 tail: collection(),
qt3tests 1.0 subset, Windows CLI harness.

## Update 2026-09-03 (final): typeswitch + error codes + collection SHIPPED (v1.9.70 / PR #798)

- typeswitch (E) case T return R ... default return RD — dispatch
  through xpath_result_matches_type, the ONE SequenceType
  classifier shared with instance-of (the type-name parse must
  stop at 'return' — the keyword was swallowed into the type).
- Error-code model: XPathContext.error_code (local part only) —
  doc()/collection() set FODC0002, cast-lexical failures FORG0001;
  named catches match, $err:code binds, channels clear on catch.
- collection() = 'No default collection has been defined' (Saxon).
- Drive-by: FOR's scalar-domain branch marks numeric members;
  xpath_nodeset_deep_copy copies RAW content — its get_node_text
  round-trip stripped the \x03N marker ($v instance of xs:integer
  was false through any let/var deep-copy path — the typeswitch
  bisect exposed a latent instance-of bug).

Lane 12 language surface COMPLETE except qt3tests subset
adoption. Lane 11 tail: qt3tests 1.0 subset, Windows CLI harness
(collection() landed here).
