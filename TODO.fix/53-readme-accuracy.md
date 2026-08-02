# TODO 53: README accuracy pass

**Priority**: P3 (documentation)
**Status**: Planned
**Effort**: S

## Problem

`README.adoc` (~2700 lines) makes several claims that may not match
the current state of the codebase:

- "100% W3C XPath 1.0 conformance (438/438 tests passing)" — the test
  suite isn't in the tree; this is unverifiable.
- "Element size: 96 bytes (compact mode)" — verify against actual
  sizeof(TaurusElement) after the vtable field addition.
- Various function counts that may have drifted.
- "Production-ready" claim — subjective but should be defended.

Outdated docs erode trust.

## Fix

Walk README.adoc section by section:

1. **Verify every numeric claim** by running the actual command.
2. **Update test counts** to match `ctest --test-dir build` output.
3. **Update element size** to match the current struct layout.
4. **Remove or qualify unverifiable claims** (e.g., "100% XPath
   conformance" → "passes common XPath queries; full W3C conformance
   suite is tracked separately").
5. **Add a "Validation" section** pointing at TODO.fix/README.md so
   readers can see the audit history.

## Tests

No behavioral change.

## Architecture notes

Documentation drift is a real maintainability cost.  The fix is not
just updating README once — it's putting in place a process that
keeps it updated.  Options:

- **CI check**: a script that greps README for "sizeof(X)" patterns
  and asserts against the actual sizeof.  Catches drift at PR time.
- **Pre-release checklist**: explicit "update README" step.
- **Single source of truth**: where possible, derive numbers from
  source.  E.g., the test count comes from `ctest --print-tests |
  wc -l` — could be a script in CI that rewrites README.

## Verification

```bash
# Spot-check: every numeric claim in README matches reality.
grep -E "[0-9]+ bytes|[0-9]+/[0-9]+ tests|[0-9]+% conformance" README.adoc
# Each value is verifiable against the codebase.
```
