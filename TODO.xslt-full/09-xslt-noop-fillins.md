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

## Status 2026-09-03 — CLOSED (re-audit after v1.9.75)

Every listed item ships with an Xslt30 spec (Saxon-probed):
sequence/perform-sort/next-match/merge/fork/result-document/
where-populated/on-non-empty (batches B-E, v1.9.2x), @default/
copy-@select/xsl:namespace/xsl:document/on-completion (batch C,
#729-#732 family), @start-at, xsl:map+map-entry (08B), composite
keys (#720), next-iteration chaining + evaluate with-params (batch
D), xsl:assert, shadow attributes, accumulator. The 2026-09-03
re-audit added xsl:global-context-item acceptance (no-op by
construction — the transform always carries the source doc).
Remaining OUT (tracked elsewhere): xsl:use-package/packaging
(roadmap v3.1 packages, non-blocking), streaming (explicit
non-goal). Issues #690/#685 close on this evidence.
