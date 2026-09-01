# 15 — Engine support for binding asks (#683)

Versioned eval family the bindings requested:
- leptris_xpath_eval_versioned(doc, ctx, expr, LEPTRIS_XPATH_VER)
  switching 1.0 strict vs 3.x surface (the grammar from 06/07/08
  is version-gated OFF for 1.0 callers — 438/438 stays green)
- leptris_xquery_* entries from 11
- result-type extensions from 07 (mirrors in binding PRs)
Gate: abi spec + mirrors drift check green.
