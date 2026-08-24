# TODO.transform — XSLT 1.0 engine (flagship)

Design contract (mirrors TODO.bindings/engine discipline):
- COMPILE ONCE, EXECUTE MANY: stylesheets parse to an instruction
  tree with function-pointer dispatch (the node_vtable pattern) —
  adding an instruction/EXSLT element = a new handler + one
  registration, no engine edits (OCP).
- The XPath layer is the moat: patterns and selects go through the
  existing parser/VM/evaluator. XSLT adds: pattern matcher (§5.2),
  template selection (§5.4/5.5 priority + import precedence),
  keys, numbering, and a context-function bridge (current(), key(),
  document(), format-number(), generate-id()) injected through the
  per-eval registry copy — the same path as custom functions (SSOT).
- Output is built as a DOM into a result document via the mutation
  API, serialized by the mixed-content-correct serializer (DRY);
  result-tree-fragments are scratch documents surfaced as top-level
  node lists (no fake document node needed).

Phases (one PR per phase group, full specs each):
- 01-pattern-matcher: XSLT pattern semantics over the XPath AST
  (ancestor-chain matching, computed default priorities, unions)
- 02-stylesheet-compiler: stylesheet parse → instruction tree;
  include/import precedence; strip-space; output settings;
  attribute-sets; decimal-format
- 03-template-engine: apply-templates/mode/priority selection,
  literal result elements, value-of/text/copy/copy-of,
  if/choose/for-each, variables & params (scope frames),
  call-template/with-param, element/attribute/comment/PI,
  message(terminate), xml|text output methods
- 04-sort-number-keys: xsl:sort (text|number, asc|desc, stable),
  xsl:number (level single|multiple|any, count/from, format with
  1/a/A/i/I + grouping-separator/size), xsl:key + key() with lazy
  per-document indexes, format-number + decimal-format
- 05-exslt-and-document: document(), exslt:node-set(), regexp
  (match/replace/test via POSIX ERE), date core, generate-id(),
  current(), system-property()
- 06-conformance-suite: in-tree W3C-behavior suite covering §5
  patterns/priorities, §7 replication, §8 variables, §9 numbering,
  §11 keys, §12 output, §13 fallback, + EXSLT behaviors
