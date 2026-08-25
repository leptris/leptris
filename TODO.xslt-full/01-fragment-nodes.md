# 01 — Fragment-level PIs, comments, and text order (84 cases)

The largest cluster: literal PIs/comments in template content and
fragment-level output misorder or drop. libxslt keeps exact
document order of top-level result nodes (elements, PIs, comments,
text interleaved).

Work:
- The result FRAGMENT is a node sequence, not "elements + two text
  buffers". Replace top_text/tail_text/frag_nodes with one ordered
  fragment chain on XsltExec (MECE: exactly one representation).
- out_append_* routes every node kind through it; serialization
  walks it once (DRY with the serializer's doc-order walk).
- xsl:processing-instruction / xsl:comment / xsl:text at fragment
  level then serialize in order (bug-107, bug-100 family, bug-11-).

Verify: xsltproc on each case first; then the suite delta.
Perf gate: the fragment chain replaces THREE buffers — allocation
count must drop on benchmarks/xslt/fragment.
