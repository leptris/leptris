# QT3 pin dispositions

Every case in an adopted QT3 test set that does NOT run-and-agree is a
pin. This directory records a disposition for every pin (board item A2:
"zero unexplained pins"). Regenerate the inventory with:

    QT3_REPORT=1 ./build/test/test_qt3 2> pins.txt

(one `QT3PIN <set> <case> <reason>` line per pin, one `QT3SETSUM` line
per set; `run=` is the adopted count that the in-file pin carries).

Fresh re-pin: 2026-10-22, RelWithDebInfo, utf8proc+iconv+DUCET ON,
62/62 tests green, zero reds (`EXPECT_EQ(agree, run)` holds on every
set). No-utf8proc leg verified separately (59/59).

## Totals

| disposition file | pins |
|---|---|
| error-channel.md | 685 |
| assert-type.md | 257 |
| query-exclude.md | 337 |
| assert-deep-eq.md | 156 |
| env-unsupported.md | 118 |
| named-exclude.md | 68 |
| env-inline-no-params.md | 32 |
| dep-gates.md | 41 |
| assert-kinds-misc.md | 26 |
| empty-query.md | 3 |
| **total** | **1723** |

## Fixes shipped with this re-pin

- fn:codepoint-equal empty-operand propagation (F&O 7.6.2): the empty
  sequence is returned when either argument is empty — adopts
  fn-codepoint-equal-2/2a/2b (spec: fn-compare precedent).
- Driver: assert-count + assert-empty checkers, set-level role="."
  context sources, and the QT3_REPORT inventory channel itself.

## Adopted counts moved by the re-pin (DUCET leg)

72 of 106 sets re-pinned from one verified run, including fn/avg
182→184, fn/sum 210→211, fn/codepoint-equal 24→27,
fn/function-name 4→7, prod/OrderByClause 129→130, fn/subsequence
69→73, fn/deep-equal COLLPIN(223, 221) unchanged. The no-utf8proc
COLLPIN members were re-verified in their own config: substring-before
12→13, substring-after 16→18; the other four unchanged.
