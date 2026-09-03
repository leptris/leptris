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
