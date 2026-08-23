# 03 — Mixed-nodeset batch accessor

User report (#477 residual): `leptris_xpath_result_get_nodes` copies
2-of-4 on a mixed nodeset — the elements-only contract is real but
undocumented, forcing bindings into per-index fallbacks.

- Document `leptris_xpath_result_get_nodes` as ELEMENTS-ONLY (header
  doc block).
- New API: `leptris_xpath_result_get_nodes_ex(result, out_nodes,
  out_kinds, max_count)` — copies EVERY entry plus its
  LeptrisXPathNodeKind; returns the copied count.

DONE 2026-08-23: both landed; spec asserts 4-of-4 with kinds on the
mixed nodeset that previously truncated.
