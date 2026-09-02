# 07 — XPath function items + HOF (#692-B)

The public-surface item: XPathResultType/FFI mirrors gain
XPATH_RESULT_FUNCTION (coordinate ext/ mirrors in leptris-ruby +
leptris-py — PRs ok, never release). Then:
- named function references name#arity (function items as values)
- inline functions function($x){ body } (closure over the varset
  snapshot; body = compiled expr evaluated on call)
- partial application f(?, ...) — captures arg placeholders
- dynamic invocation f(a,b) where f is a function item
- fn:for-each/filter/fold-left/fold-right/apply/sort/function-
  lookup/function-name/function-arity/function-arity
- xsl:function (see 09) reuses this machinery

Gate: Xslt30.FnItems spec (Saxon-probed) + mirrors synced + suite.

## Status 2026-09-03 — first slice attempted, reverted clean; bisect notes

Ground truth banked (Saxon-HE 12.7, /tmp/probe9/fi7.xsl fi7b.xsl):
  function($x){$x*2} via let → 42; immediate call fn(41) → 42;
  string-join#1(('a','b')) → 'ab'; 2-param fn → 42.

Design (kept, compiles clean — all parser/lexer/enum wiring was
reverted with the slice): TOK_HASH token; XPATH_OP_INLINE_FN /
FN_REF / DYN_CALL (all VM AST-fallback); parser hooks beside the
`map` branch (function+LPAREN; NCNAME+HASH) and a TOK_LPAREN arm
in parse_postfix_ops (dynamic call). Closure repr: synthetic node
"\x03FN\x02" params-'\x01'-joined "\x02" + raw body-AST pointer
bytes (borrowed, transform-lifetime); FN_REF = "\x03FR"+name#arity;
DYN_CALL dispatches FR via a stack XPATH_AST_FUNCTION_CALL over
borrowed arg ASTs, FN binds params (FOR-discipline variable sets).

Bisect point (traces on the gtest surface): `function($x){$x+1}(41)`
compiles but the transform errors. [dyn] enter fires (twice —
why twice?), the callee-content branches are reached but the
body-eval trace NEVER prints → the flow exits between the content
check and the FN branch: prime suspect = the closure content never
starts "\x03FN" (check what INLINE_FN actually produced — maybe
evaluate_expr on the INLINE_FN AST never dispatched to
evaluate_operator, so the callee is something else), or the
strchr('\x02') guard rejected. Trace the callee content bytes hex-
first at DYN_CALL entry.

Remaining after this slice: fn:function-lookup/name/arity, partial
application, HOF combiners, PUBLIC XPathResultType change + FFI
mirrors (leptris-ruby/py: PRs only, never release).

## Update later 2026-09-03: slice GREEN locally, Linux-LSan leak

The full slice (inline fns + dynamic calls, multi-param, hex
closure encoding; both bug fixes below) was GREEN: test_xslt
188/188, ctest 1203/1203, macOS ASAN clean — but Linux LSan
flagged 2 leaked 48-byte synth nodes from the DYN_CALL param
binding (evaluator_operators.c:488 → xpath_synth_text). macOS
ASAN cannot run LSan (the known CI gap). Suspect: the unbind
remove-by-name on the exec's cached varset (non-scratch path) —
trace which of the 4 dyn calls leaks (2 objects) and whether
xpath_variable_set_remove actually frees the nodeset when the set
is the XSLT-exec mirror. Branch was reverted; the commit
(feat/xpath-function-items2, PR #765) has the full implementation
to re-apply verbatim, plus the two fixed bugs: (1) C hex-escape
greed "\x03FN" → 0x3F+'N' (F is a hex digit; \x03MAP only worked
because M isn't); (2) raw pointer bytes carry NULs and the let
deep-copy truncates — closure body pointer is 16-hex-char encoded.
Also open: name#arity (TOK_HASH + FN_REF) parses but its dispatch
(f synthesized FUNCTION_CALL over borrowed arg ASTs) fails
upstream of the call — case d of the spec, trimmed out.
