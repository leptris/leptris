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
