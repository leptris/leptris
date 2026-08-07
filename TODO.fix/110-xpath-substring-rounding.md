# TODO 110 — XPath substring() rounding edge cases diverge from spec

**Priority**: P3 (correctness — XPath 1.0 edge cases)
**Status**: open
**Effort**: S

## Problem

`substring()` in taurus treats `start` and `length` as plain integers
(truncating toward zero). The XPath 1.0 spec defines them with
rounding to the nearest integer, and uses half-open intervals that
produce subtly different results for fractional and negative inputs.

## Examples (per W3C XPath 1.0 spec section 4.2)

| Expression                              | taurus | Spec  |
|---|---|---|
| `substring('12345', 1.5, 2.6)`          | ?      | '234' |
| `substring('12345', 0, 2)`              | '12'   | '1'   |
| `substring('12345', 0, 3)`              | '123'  | '12'  |
| `substring('12345', -1, 5)`             | error  | '1234'|
| `substring('12345', -1 div 0, 5)`       | error  | ''    |

## Cause

`src/taurus/xpath/functions.c:xpath_func_substring` computes
`start_int = (long)start` and `length_int = (long)length` directly
(truncation). The spec uses `round(start)` and `round(start+length)`
with `<=` / `<` comparison on positions.

## Fix

Replace the truncation with the spec's rounding + position-range
formula:

```
rounded_start = round(start);
rounded_end   = round(start + length);
for pos in 1..string-length:
    if rounded_start <= pos < rounded_end:
        emit char at pos
```

Handle NaN/Inf edge cases per the spec table.

## Verification

After fix, restore the omitted edge-case expectations in
`test/xpath/test_xpath_conformance.cpp` (SubstringIsOneIndexed
spec has a TODO comment marking their absence).
