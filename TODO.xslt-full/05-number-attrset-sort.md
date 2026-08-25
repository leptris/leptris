# 05 — xsl:number, attribute-set precedence, sort/AVT corners (24 DIFF)

- xsl:number: `from` stopping semantics at depth, level=any across
  comment/PI interleaving, format="&#x2474;"-style single-token
  corners, lang (bug-25-, bug-63, bug-90 family).
- xsl:attribute-set: LAST declaration wins across imports (our
  lookup takes first); use-attribute-sets chains resolve at USE
  time with import rank (bug-93, bug-102 family).
- xsl:sort: data-type="number" with leading/trailing whitespace
  keys; lang collation falls back to codepoint ONLY when lang
  unsupported (bug-120 family).
- AVT: `{{`/`}}` escaping corners; `{$x` literal (bug-126).
All SSOT: one comparator, one priority resolver, one AVT scanner.
