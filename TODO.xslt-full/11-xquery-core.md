# 11 — XQuery 1.0 core (#684-A)

New subsystem src/leptris/xquery/ reusing the XPath engine (SSOT):
- prolog: declare variable/function (compiled to the existing var/
  ufn machinery), declare namespace/base-uri/default collation
- FLWOR: for (multiple, positional), let, where, order by (stable,
  multiple keys, asc/desc), return — compiled to XPATH_OP_FOR/LET
  chains with a new ORDER wrapper; group by/windowing in 12
- path expressions, constructors (direct element), if/typeswitch
  (reject), function calls, doc()/collection()
- CLI: leptris xquery -q file.xq [-s doc.xml]; C API
  leptris_xquery_eval / leptris_xquery_parse
- Saxon-HE net.sf.saxon.Query is the oracle AND perf target
Gate: qt3tests XQuery 1.0 subset adopted + Saxon side-by-side.

## Update 2026-09-03: slice A SHIPPED (v1.9.64 / PR #780)

- New src/leptris/xquery/ orchestration layer (SSOT — no second
  evaluator): prolog (declare variable / namespace / function
  local:*; unsupported declarations error explicitly) + FLWOR
  (nested for domains, per-tuple lets, where, stable multi-key
  order by asc/desc with numeric-when-both-parse keys, return as
  the synthetic-text sequence). Public API
  leptris_xquery_parse/eval/free; results reuse the XPath result
  model. Scanner: nestable (: :) comments, QName names, clause
  keywords only at depth-0 word boundaries.
- Banked scanner bug class: scan_word does NOT advance the cursor
  (callers must move s->p themselves) — the clause-boundary check
  must look at w+wl, not the un-advanced copy.
- LSan class: xpath_context_cleanup treats variable_set as
  caller-borrowed — a scratch set must be freed by its creator.
- Spec: test/xquery/test_xquery.cpp (8, Saxon oracle
  /tmp/probe9/xq). Drive-by fixes: \x03N marker leaked through
  leptris_xpath_result_node_value into CLI output (v1.9.61
  regression); CLI -Wswitch gaps for LEPTRIS_XPATH_FUNCTION.

Remaining for lane 11: direct+computed element constructors,
doc()/collection(), qt3tests 1.0 subset adoption, CLI `xquery`
command, then lane 12 (group by/window, try/catch, typeswitch).
