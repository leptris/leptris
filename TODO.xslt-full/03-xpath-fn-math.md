# 03 — XPath fn: catalog: math: namespace (#691-C)

math:sqrt, math:pow, math:exp, math:exp10, math:log, math:log10,
math:sin/cos/tan/asin/acos/atan, math:atan2, math:pi, plus core
fn:abs, fn:ceiling/floor/round/round-half-to-even (round-half-to-
even with precision arg is new). All trivial over libm; registered
under the math: prefix with bare-name aliases where Saxon accepts
them.

Gate: Xslt30.FnMath spec (Saxon-probed) + suite green.
