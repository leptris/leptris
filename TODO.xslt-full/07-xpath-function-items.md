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

## Update 2026-09-03: FR dispatch + lane 07B SHIPPED (v1.9.59 / PR #768, 07B PR #770)

- **FR dispatch root cause (v1.9.59, PR #768)**: FN_REF nodes carry
  NO children, so evaluate_operator's opening `child_count < 1`
  guard rejected them before any op check — the "fails upstream"
  mystery closed. FR construction now runs BEFORE the guard; the
  shadowed duplicate block deleted. Plus XPath 3.1 one-arg
  string-join (separator '') — `string-join#1` lands in it.
- **Lane 07B (PR #770)**: fn:function-lookup (string name, fn:
  prefix stripped, registry arity-range validated; returns an FR
  item or the empty sequence), fn:function-name (fn:name / empty),
  fn:function-arity (#N / closure param count); fn:for-each,
  fn:filter, fn:fold-left, fn:fold-right via the new
  xpath_call_function_item seam (FR = synthesized call over string
  literal arg nodes; FN = params bound from string args);
  `?` partial application DESUGARED AT PARSE TIME to an inline fn
  (`concat('x',?,'z')` → `function($%1){concat('x',$%1,'z')}`).
  Banked invariant: hole names use `%N` — NCNames exclude '%', and
  `\x01` inside a hole name COLLIDES with the params separator
  (first attempt bound param "1" while the body referenced "\x011"
  — the doubled ifn/dc eval trace was the giveaway).
- Spec: Xslt30.NamedFunctionReferenceDispatch + 11-case
  Xslt30.FunctionItemMetadataAndHofs (190 total in test_xslt).
  Oracle: /tmp/probe9/l07b/{t1,t2}.xsl.

Remaining for lane 07 closure: PUBLIC XPathResultType change +
FFI mirrors (leptris-ruby/py — PRs only, never release);
fn:for-each-pair and the map:/array: HOF family if the combiner
seam makes them cheap; then lane 11/12 (XQuery).

## Update 2026-09-03 (later): LANE 07 CLOSED (v1.9.63 / PR #777)

- Public type: LEPTRIS_XPATH_FUNCTION (appended to
  LeptrisXPathResultType; ABI numbers hold) — the type accessor
  classifies the one-member synthetic carrier, GATED on the
  synthetic-text tag (casting a real single-element nodeset as
  XPathTextNode segfaulted the CLI predicate spec — caught by the
  full ctest gate).
- Combininers through xpath_call_function_item: for-each-pair
  (zip), apply($f, array) (positional members as the arg list),
  map:for-each (entry order), array:for-each/filter/fold-left/
  fold-right. En-route bug class: map_entries_free ZEROES e.n —
  capture the count before the compaction loop.
- FFI mirrors shipped as PRs (never release, per the standing
  rule): leptris-ruby#127 (constant + explicit XPathError wrap
  branch), leptris-py#71 (same + _leptrisaccel.c finish_result
  hands rtype 4 to the engine path — a bare NULL there surfaced as
  SystemError; also un-pinned #744's element() pin).
- Specs: Xslt30.HofPairsApplyAndArrayCombinators (8),
  XPathResultTypes.FunctionItemsReportFunctionType; oracle
  /tmp/probe9/l07b/t5.xsl. Remaining from the lane brief (fn:sort
  with key/ Collation arity, fn:for-each-pair over uneven inputs
  past the zip): fold into the #691 catalog tail, not blockers.
