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

## Update 2026-09-03 (later): slice B SHIPPED (v1.9.65 / PR #783)

- Computed constructors (element/attribute/text) as XPath ops —
  value-level serialized-XML strings; empty elements self-close
  (Saxon). Comma-separated ctor bodies attach children directly
  (SEQUENCE wrappers also flattened).
- Direct constructors translate to the computed form in the xquery
  scanner (balanced-tag text scans; AVTs → concat pieces, single
  pieces stay bare — concat rejects arity 1; text runs → text{}
  literals). XQuery-scoped; XSLT keeps literal result elements.
- fn:doc(): document anchors on XPathContext.owned_docs, freed by
  xpath_context_cleanup — the borrowed root outlives the result.
- CLI: leptris xquery [-s FILE] (-q FILE | -e EXPR). Banked: CLI
  command structs MUST be static (cli_registry_free never frees
  them — a malloc'd one leaked 48B and aborted every CLI test
  under Linux ASAN abort_on_error); Windows cmd.exe ignores the
  test harness's single-quote wrapping and has no /tmp — CliXquery
  specs ride a cwd-relative query FILE and are !defined(_WIN32)
  gated (the LIBRARY tests are green on Windows; the harness-side
  plumbing is the TODO follow-up).

Remaining for lane 11: qt3tests XQuery 1.0 subset adoption,
collection(), computed-document/value constructors, positional
FOR ($x at $i). Then lane 12 (group by, windowing, try/catch —
the #692 silent-wrong case, typeswitch).

## Update 2026-09-03 (final): slice C SHIPPED (v1.9.66 / PR #786)

- Positional for ($x at $i): 1-based position bound through the
  tuple snapshot/rebind cycle. Banked: snapshot arrays size 2n
  (var + pos var) — ASAN caught the n-sized overflow macOS ctest
  missed; and the scan_word-doesn't-advance trap bit AGAIN in the
  `at` keyword check (advance via s.p = w + wl, never s = scan
  copy).
- document { content }: no-wrapper serialization (Saxon). value {}
  stays OUT — Saxon-HE rejects it (XPST0003).
- try/catch (lane 12 opener, #692's silent-wrong FIXED): XPATH_OP_
  TRY — failed try body runs the first catch whose name-test
  matches; catch * catches all ($err:code/description/value bound
  from ctx->error_msg, cleared on catch); named tests never match
  (no error-code model — pinned: the error propagates). The XSLT
  rejection pin flipped: try/catch is XPath 3.0.

Lane 11 remainder (qt3tests subset, collection(), Windows CLI
harness) is tracked in the line above; lane 12 continues with
group by / windowing / typeswitch / error-code model.
