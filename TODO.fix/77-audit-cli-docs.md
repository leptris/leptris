# TODO 77: Audit CLI docs

**Priority**: P3 (docs)
**Status**: Planned
**Effort**: S

## Problem

`cli/` contains two design docs:

- `cli/CLI_ARCHITECTURE.md` — 21 KB design document.
- `cli/ARCHITECTURE_REVIEW.md` — 14 KB review notes.

These were written before the validation passes and may describe
behavior that has since changed:

- The CLI's option parser may have shifted.
- The MECE option model may have been refined.
- The command interface may have new requirements.

## Fix

Walk each doc and verify claims against the current code:

1. Does each documented command still exist?
2. Do the option descriptions match `cli/options.h`?
3. Are the design principles still accurate?
4. Update or remove outdated sections.

If a doc is more historical than current, move to `archive/`.

## Verification

```bash
# Spot-check each documented command against the current code.
grep -rn "taurus_parse\|taurus_xpath" cli/commands/   # match docs
```
