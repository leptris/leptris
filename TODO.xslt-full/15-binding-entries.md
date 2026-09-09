# 15 — Engine support for binding asks (#683)

Versioned eval family the bindings requested:
- leptris_xpath_eval_versioned(doc, ctx, expr, LEPTRIS_XPATH_VER)
  switching 1.0 strict vs 3.x surface (the grammar from 06/07/08
  is version-gated OFF for 1.0 callers — 438/438 stays green)
- leptris_xquery_* entries from 11
- result-type extensions from 07 (mirrors in binding PRs)
## Status 2026-09-10 — versioned eval shipped

leptris_xpath_eval_versioned(doc, ctx, expr, LEPTRIS_XPATH_10 |
LEPTRIS_XPATH_31, status) shipped: 1.0 mode rejects 3.x-only
SYNTAX (arrow =>, bang !, lookup ?, let, inline function,
string templates) via a literal-aware lexical scan with
LEPTRIS_ERROR_INVALID_ARG; 3.1 mode = leptris_xpath_eval
verbatim. Spec: PublicSurface.VersionedXPathEval (violation
cases per falsifiability). Remaining: the xquery_* entries from
lane 11 (qt3tests adoption) + binding mirror PRs (user merges).
Gate: abi spec + mirrors drift check green.
