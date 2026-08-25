# 00 — Triage board (updated 2026-08-25, suite 67→66/205 tracked)

Live census of test/xslt/open_cases.txt after the root-semantics,
namespace-axis, attr-set-chain, and serializer batches:
- ~92 DIFF (mixed: number formatting, sort, document(), strip-space
  corners, AVT quirks, stale-fixture bug-124)
- ~17 NUMBER, ~9 SORT, ~8 KEY, ~7 DOCUMENT, ~6 EMPTY/crash
- Windows-only divergences gated on the list: bug-93, bug-102
  (xsl:import resolution on Win32), bug-118 (copy-of select='/'
  document copy). Fix the Win32 path/join in the import resolver and
  the document-copy path, then un-gate.

# 00 — Triage harness (standing tooling)

The open list is living state, not a snapshot. Ship the triage as a
script (scripts/xslt_triage.sh) so every PR regenerates it:
- Runs each open case via the fork-isolated runner.
- Buckets by signature: EMPTY / CRASH / NS-OUT / CHARREF / PI-OUT /
  DIFF with one-line diffs.
- Rewrites test/xslt/open_cases.txt; CI fails if a case in the list
  passes or a case outside it fails (already enforced by the suite
  runner — this script keeps the list honest).

Done: the ad-hoc python used so far; productize it (SSOT: one
script, used by CI + devs).
