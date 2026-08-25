# TODO.xslt-full — XSLT 1.0 conformance completion (the v1.10.0 board)

The single release gate for v1.10.0: **libxslt suite 205/205**
(test/xslt/open_cases.txt must be EMPTY), 45/45 XsltFull, full CI
green (ASAN/TSAN/Windows/leaks). No release before the gate passes
(user directive 2026-08-25).

Rules (per repo conventions):
- One PR per task; the ENTIRE task in that PR (no phase-splitting).
- Every fix verifies against xsltproc ground truth first.
- Architecture: OCP for instruction handlers (new handler +
  registration, no engine edits), SSOT for semantics (the pattern
  matcher / ns-context / serializer each own exactly one copy),
  MECE by construct, DRY via shared helpers — never fork a rule.
- Performance gates: benchmarks must stay ahead of libxml2/xsltproc
  (benchmarks/xslt/*) — conformance may not cost the lead. A fix
  that regresses a benchmark by >3% needs an algorithmic answer.

Live census (157 open, by failure signature; refresh with the
triage script in 00):
- 84 PI/COMMENT-OUT — fragment-node serialization + content edges
- 28 EMPTY — root-node `/` semantics, doc() chains, early exits
- 24 DIFF — number corners, attr-set precedence, sort, AVT edges
- 10 CRASH — depth accounting, cycle handling (bug-6-, bug-70…)
-  9 NS-OUT — namespace copies on result trees
-  2 CHARREF — attribute char escaping corners
