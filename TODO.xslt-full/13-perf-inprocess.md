# 13 — Performance: in-process parity + superiority (#682)

#682: dispatch ~2.6x behind in-process libxslt (the v1.9.31/32
comparison used xsltproc WALL time — in-process libx2 xsltApplyStylesheet
is the honest reference). Work:
- twin bench: bench_xslt_transform_libxml2 (in-process libxslt via
  libxsltApplyStylesheet; the libxslt checkout + brew lib are
  available) for s1/s2/s3 + XSLTMark corpus
- profile s2 (13.5ms) against libxslt (~5ms): per-invoke template
  overhead (frames, current_* save/restore), result-tree element
  creation (out_append_elem path), attribute copies
- levers: frame-free invoke fast path when no params/ns; pooled
  result nodes reuse across iterations; sort index reuse
- gates: fresh Release dirs, min-of-8 interleaved (benchmark-
  discipline memory); PerfRegression budget updates; the harness
  becomes the release scorecard (site G2/G3)
Gate: s1/s2/s3 >= libxslt in-process on this machine, documented.
