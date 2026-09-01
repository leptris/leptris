# 09 — XSLT 2.0/3.0 instruction fill-ins (#690 + #685)

Every silent no-op becomes implemented or a loud XTSE compile
error (never silent). Items:
- xsl:sequence (select passthrough — with 01 done)
- xsl:perform-sort (select + xsl:sort children; reuses xslt_sort)
- xsl:next-match (rank+1 selection like apply-imports)
- xsl:merge (two-source merge by keys; iterative walk)
- xsl:fork (non-streaming = sequential execution of both arms)
- xsl:result-document (href + format: serialize a second tree to a
  file via the serializer; principal result unchanged)
- @default (xsl:catch default error-code binding), @start-at
  (xsl:number), on-completion (xsl:iterate post-body)
- xsl:copy/@select (copy-of semantics inside xsl:copy), xsl:map
  (needs 08), xsl:namespace (namespace-node construction),
  xsl:document (inline doc construction — with 08's fragments)
- composite keys (key use="a b" token list), xsl:next-iteration
  param chaining re-check, xsl:evaluate with-params
- xsl:where-populated / xsl:on-non-empty
Gate: Xslt30.* spec per item (Saxon-probed) + suite green.
