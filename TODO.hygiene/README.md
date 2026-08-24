# TODO.hygiene — conformance completeness

Phases:
- 01-document-node: XPath-level synthetic document node — //node(),
  /, count(/), and the generic descendant expansion select/see the
  document as a node without a DOM overhaul (nodesets carry a
  sentinel; accessors handle it)
- 02-dtd-phase8: ENTITY/ENTITIES attribute resolution, parameter
  entities in the internal subset, choice-model backtracking in
  content validation (TODO 91 closeout)
- 03-ns-fixup-mutation: namespace fixup on adopt/insert across
  parents (undeclared prefixes repaired by declaration synthesis,
  per DOM Level 3 semantics)
- 04-spec-sweep: every new behavior falsifiable — violation cases
  required per spec group (project rule)
