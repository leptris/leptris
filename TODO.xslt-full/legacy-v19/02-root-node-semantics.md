# 02 — Document-node context for match='/' — DONE (2026-08-25, a524706)

# 02 — Document-node context for match='/' (part of EMPTY, ~12 cases)

match='/' is the DOCUMENT node: child::*/node() from it select the
root element; count(node()) = 1 even with comments/PIs before it.
Our engine conflates "/" with the root element.

Work (SSOT — fix at the model, not per-expression):
- Give the evaluator a real document-node context entry point: a
  context flag + child/descendant axis handlers that treat the doc
  root as the child of the virtual document node (ONE site in
  evaluate_step's axis dispatch — not expression-name intercepts;
  the intercept attempt crashed and was reverted).
- Root invocation sets the doc context; apply-templates default
  enumerates [root element]; built-in rules unchanged.
- Covers bug-104 (copy-of select='*'), bug-119, bug-124 family.

Verify: name(/*), count(node()), select='*' from "/" all agree
with xsltproc; 45/45 XsltFull stays green (match='/r' is ELEMENT
context — distinct semantics, one test each way).

---
## DONE — implemented in a524706

- DOM: LEPTRIS_NODE_TYPE_DOCUMENT singleton (dom/document_node.{h,c}).
- Evaluator: doc-node child/descendant axes (VM + AST paths); self/parent
  from non-element nodes.
- XSLT: entry + built-in root rule run on the document node; "/" matches
  only it; apply-templates/copy/copy-of take the node-typed accessor.
- Identity transforms (bug-107 family) verified end-to-end.
- Remaining known open: bug-119 (libxslt emits the literal body of a
  source-side xsl:template matched via an attribute pattern — a
  recorded-libxslt-quirk case; needs a quirk-equivalence decision).
