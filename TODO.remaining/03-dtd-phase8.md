# 03 — DTD validator Phase 8 (TODO 91 remainder)

Shipped so far: EMPTY/ANY/Mixed/Element content models,
#REQUIRED/#IMPLIED/#FIXED attribute checking, attribute type
validation, eager internal-subset entity substitution (Phase 8b in
dtd/parser.c).

Remaining (marked in source):
- ENTITY/ENTITIES attribute-type resolution (unparsed entities).
- Parameter entities (%pe;) in the internal/external subset —
  parser.c:532 gates conditional sections behind Phase 8d.
- Choice-model backtracking: the content-checker's choice operator
  is not memoized for ambiguous models (docs/119-dtd-choice-
  memoization.md has the design).
- External subset (systemid) loading hooks.

Acceptance: W3C DTD conformance suite green on the new cases;
no regression in the existing dtd specs.
