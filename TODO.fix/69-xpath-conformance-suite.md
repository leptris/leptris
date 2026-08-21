# TODO 69: XPath conformance suite

**Priority**: P1 (correctness — README claims 438/438 but suite isn't in tree)
**Status**: Planned
**Effort**: L

## Problem

The README claimed "100% W3C XPath 1.0 conformance (438/438 tests
passing)" — but the conformance suite itself was never committed.
The current 79-spec suite covers common queries but doesn't
guarantee spec compliance.

## Fix

### Phase 1: source the suite

The W3C XPath 1.0 conformance suite is at
`https://github.com/w3c/qtspecs` or via the XML Query Test Suite.
Download and commit (or git-submodule) the XPath subset.

### Phase 2: test runner

Write a harness that:
1. Reads each XML + XPath query + expected result from the suite.
2. Runs `leptris_xpath_eval(doc, ctx, expr)`.
3. Asserts result matches expected.

### Phase 3: gaps

Any failures become individual TODOs.

## Tests

The conformance runner is the deliverable.

## Architecture notes

Conformance testing is high-value but slow to set up.  The suite is
large (thousands of test cases); a single failure means a real bug.

If integration is too heavy for now, at minimum add specs that
exercise each XPath function with multiple inputs — narrower but
catches regressions in the most-used paths.

## Verification

```bash
ctest --test-dir build --output-on-failure -R XPathConformance
# All conformance tests pass.
```
