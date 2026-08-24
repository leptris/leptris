# TODO.validate — Relax NG + RNC + Schematron

Design contract:
- RNG core is a DERIVATIVE matcher: patterns (empty, text, data,
  value, choice, group, interleave, oneOrMore, list, element,
  attribute, ref/parentRef, grammar/start/define) derive against
  the document event stream; null-ability checks termination.
  One algorithm, no hedge-automaton build step — compiles fast,
  validates linear, easy to audit.
- Modular: rng_model.c (model + RNG-XML parser) / rng_deriv.c
  (the algebra) / rng_validate.c (the walk) / rnc_parse.c (compact
  front-end targets the SAME model — SSOT) / schematron.c (small
  interpreter riding XPath 1.0 + our registry functions).
- Diagnostics: element-path + expected-vs-got messages through the
  public LeptrisStatus/last_error channel.

Phases:
- 01-rng-model-parser: pattern model, RNG-in-XML parsing, grammar
  tables, ref resolution, datatype-library hooks (string-only v1
  + built-in "string"/"token"; XSD types later behind the hook)
- 02-rng-derivative: nullable/deriv/textDeriv/attDeriv/listDeriv
  algebra + attribute-set collection; complexity guards
- 03-rng-validate: document walk (elements/attrs/text/whitespace
  policy), error reporting, conformance spec suite (OASIS/Jing
  behaviors: interleave, oneOrMore, list, data/value, defines,
  start=choice, combine)
- 04-rnc-frontend: full compact syntax (decorators, groups,
  nesting, |, &, ,, *, ?, -, annotations, datatypes, namespaces,
  . parent, root) → the SAME model; round-trip spec vs RNG
- 05-schematron: ISO 19757-3 XPath-1 edition — patterns/rules/
  contexts/assert/report/diagnostics/phases; result document via
  the DOM mutation API; svrl-style output
