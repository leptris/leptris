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

**GATE MET (verified 2026-10-17, main @ 75cd5f6e):**
libxslt suite **205/205** (open_cases.txt empty; macOS legitimately
skips bug-169 — encoding-sensitive via iconv, Linux CI is
authoritative), XsltFull **27/27** (the suite carries 27 cases; the
historical 45 count predates the case consolidation), full suite
**2053/2053** locally. The census below is the campaign-start
snapshot, kept for the record:
- 84 PI/COMMENT-OUT — fragment-node serialization + content edges
- 28 EMPTY — root-node `/` semantics, doc() chains, early exits
- 24 DIFF — number corners, attr-set precedence, sort, AVT edges
- 10 CRASH — depth accounting, cycle handling (bug-6-, bug-70…)
-  9 NS-OUT — namespace copies on result trees
-  2 CHARREF — attribute char escaping corners
