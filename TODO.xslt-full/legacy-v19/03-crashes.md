# 03 — Crash cases (10): depth accounting + cycle hardening

bug-6-, bug-70, bug-100, bug-111, bug-130, bug-140, bug-142,
bug-147, bug-155, bug-157 crash the process.

Work:
- XsltExec depth counter (not a stack-canary gamble): increment in
  xslt_exec_instrs AND xslt_invoke_template; at XSLT_MAX_DEPTH
  return a recoverable error (per §14 terminations), never SIGSEGV.
- Include/import cycle guard exists; add document() cycle guard
  (same chain table) — bug-130 imports four .imp files deep.
- Each crasher gets its own root-cause line in the PR (10 lines);
  if a crash is an XPath-layer bug (bug-100), file it separately
  and cross-link.
Perf gate: the counter is one increment/return — zero measurable.
