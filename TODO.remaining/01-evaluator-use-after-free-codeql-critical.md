# 01 — Fix the use-after-free in xpath/evaluator.c (CodeQL CRITICAL, alert #32)

Open alert `cpp/use-after-free` at `src/leptris/xpath/evaluator.c:645`
(severity: critical; open since 2026-08-13).

CodeQL is the only thing flagging it — the ASAN suite passes 585/585,
so it is either a real latent bug reachable only through a path the
tests miss, or a false positive the analyzer cannot discharge.

Plan:
1. Read the flagged data flow (alert #32's path trace in the code-
   scanning UI).
2. If real: fix + regression spec in test/xpath.
3. If false positive: restructure so the analyzer can prove safety
   (avoid the aliased-lifetime pattern it dislikes), or annotate with
   a narrowly-scoped justification comment.

Acceptance: alert #32 closed; CodeQL check green on main.
