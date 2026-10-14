# Agent working agreement (leptris engine + bindings)

## Binding version sync (law)

`leptris-ruby`, `leptris-py`, and `leptris-ts` pin the engine by
version. The pins move in the same wave as the engine release they
need: engine fix → release X.Y.Z → every binding pin bumps to X.Y.Z
in the same session. A binding pin never lags main by more than one
engine release.

## Session ownership

The engine session OWNS the binding repos' engineering work
(leptris-ruby, leptris-py, leptris-ts): branches, CI, PRs, merges.
Publication (rubygems/PyPI/npm) stays owner-gated — the owner runs
releases or hands over the exact version.

## Standing rules (see CLAUDE.md for the full contract)

- Every fix: branch → red-first specs → full gates → PR → CI green →
  rebase-merge → release ride.
- Never commit to main, never push tags, never add AI attribution.
- Binding publications (gem/PyPI/npm) are the owner's decision and
  the owner's command.
