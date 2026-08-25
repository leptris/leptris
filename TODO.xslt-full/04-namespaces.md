# 04 — Namespace output corners (9 NS-OUT + parts of PI-OUT)

Namespaced results lose declarations or drop them on the floor.

Work:
- xmlns="" (unset inherited default) — build_ns_copy must carry an
  "empty default" state and emit it (bug-103).
- Prefixed xsl:attribute namespace= → declaration lands on the
  PARENT element (bug-122, bug-104).
- copy-of of namespaced trees copies in-scope decls onto each copy
  (§7.5) — copy_node_deep walks the SOURCE element's decl list.
- xsl:namespace-alias + prefixed LRE output (bug-129).
- xml:base/xml:lang/xml:space should pass through (bug-12- cluster).
Verify against xsltproc; suite delta.
