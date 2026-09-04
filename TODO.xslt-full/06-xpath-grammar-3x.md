# 06 — XPath 3.x grammar completion (#692-A)

Parser surface (lexer + parser + evaluator, BC_FALLBACK_EVAL in
the compiler — no VM opcodes yet):
- switch ($v) { case A return B ... default return D }
- string constructors `` `...{expr}...` `` (3.1)
- typeswitch — XQuery-only: REJECT loudly (Saxon XPST0003 parity)
- xs:* constructor functions (cast forms for the atomic set in 05/
  04 — string/integer/double/decimal/boolean/anyURI/date family)
- instance of / castable as / cast as / treat as over the 1.0 type
  set (node(), item(), xs:atomic set) — value-level only
- 'otherwise' operator and '||' (exists) — 4.0 previews Saxon 13
  defaults: probe Saxon-HE 13 behavior first

Gate: Xslt30.Grammar3x spec (Saxon-probed) + suite green.

## Status 2026-09-02 (post v1.9.48) — remaining, in order

1. xs: constructor functions FIRST (pure registrations, no parser
   work): xs:string/integer/double/decimal/boolean/anyURI in
   functions_ext31.c via the extension-namespace path (evaluator.c
   resolves prefixed fns through k_extension_ns_uris — the XSD
   namespace http://www.w3.org/2001/XMLSchema must be in that list;
   the date family already registers via fn_passthrough_ctor).
   Saxon-HE 12.7 ground truth (banked, /tmp/probe9/g6.xsl):
   xs:integer('42')+1=43 · xs:double('1.5')*2=3 ·
   xs:boolean('true')=true · xs:string(7)="7".
2. instance of / castable as / cast as / treat as: parser
   productions (multi-word operators) + evaluator type tests over
   node()/item()/xs:atomic set. Ground truth: 'x' instance of
   xs:string=true · //i instance of node()+=true · '3' castable as
   xs:integer=true · '42' cast as xs:integer=42 · 1.9 cast as
   xs:integer=1 (truncate) · 'x' castable as xs:integer=false.
3. switch expressions + 3.1 string constructors; typeswitch stays
   a loud XPST0003 rejection (Saxon parity, already specced).
4. 'otherwise' / '||': probe Saxon 13 defaults first (not 12.7).

Gate: Xslt30.Grammar3x* spec (Saxon-probed) + full suite green +
1.0 438/438 unchanged. NOTE (from #732): multi-top-level result
nodes are SUPPORTED (libxslt parity) — never "fix" to Saxon's error.

## Status 2026-09-02 (v1.9.49 + v1.9.50 shipped)

DONE: item 1 (xs: constructors — v1.9.49, PR #737) and item 2
(instance of / castable as / cast as / treat as — v1.9.50, PR #740;
lexer gotcha: `node` is TOK_NODE not NCNAME; the four ops are VM
AST-fallback). REMAINING: item 3 (switch expressions — note
XPATH_OP_SWITCH already exists in the enum + compiler fallback list;
verify what's implemented and spec the gaps) and item 4 (3.1 string
constructors `` `...{expr}...` ``; 'otherwise'/'||' → probe Saxon 13
first). Then lane 08.

## Status 2026-09-02 (v1.9.50) — LANE CLOSED

Items 3-4 resolved by oracle verdicts (pinned in
RejectsXQueryOnlySyntaxInExpressions):
- switch: Saxon-HE 12.7 XPST0003 "switch is not allowed in XPath"
  (XQuery-only). Our loud rejection = parity. XPATH_OP_SWITCH stays
  for the XQuery lane (11/12).
- string constructors + otherwise/||: XPath 4.0 — Saxon 12.7 rejects
  (XPST0003 4.0 syntax). Deferred to the Saxon-13 probe (item 4's
  own directive); rejecting now is parity.
Lane 06 COMPLETE (items 1-2 shipped v1.9.49/v1.9.50). Next: lane 08.


## Status 2026-09-03 (post v1.9.76 + PR #825) — grammar surface COMPLETE

Shipped through the wave: xs:* constructors + cast/castable/
instance of/treat (v1.9.49-50), `||` concat, `=>`, `!`, `let`,
`for ... return`, if/then/else, function items (`function($x){}`,
name#arity, partial application `?`), inline HOFs, dynamic calls
($f(21)), maps/arrays (both constructor forms incl. `array { E }`),
typeswitch, try/catch expressions with the error-code model,
quantified some/every, is/<</>>, intersect/except, `()`, deep-
equal, ends-with, the XQuery 3.0 braceless switch, and
parse-xml/parse-fragment returning document nodes. The #if 0'd
braced switch form stays rejected (Saxon XPST0003 parity).

Remaining (tracked, not blocking):
- string constructors `` `lit{expr}lit` `` (3.1)
- fn-as-path-step (`X/string()` — XQuery allows PostfixExpr steps)
- VM opcodes for the fallback-eval'd 3.x ops (perf, lane 13)
- packaging (xsl:use-package — roadmap v3.1), streaming (non-goal)
