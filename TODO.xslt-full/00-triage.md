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
