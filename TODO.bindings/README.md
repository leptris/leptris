# TODO.bindings — binding-driven engine asks (issue #510)

Tracked asks from the lxml-mirror Python binding (leptris-py) and the
Nokogiri-mirror Ruby binding, ranked by binding value ÷ effort.

## Shipped (Tier 1 / Tier 3, v1.3.0)

- `leptris_version()` / `leptris_version_components()` — runtime
  version detection at load time.
- Parse-error positions: `leptris_last_error_position(line, column)`
  next to `leptris_last_error()`.
- `leptris_node_line(node)` — 1-based source line, on demand.
- Batch children fetch: `leptris_element_children` (issue #509);
  result batch: `leptris_xpath_result_get_nodes(_ex)`.
- Documented threading contract: README "Threading model"
  (one-document-per-thread; TLS error channel; `leptris_thread_cleanup`).
- Header/binary drift gate: export-surface CI check (issue #508).

## Open (one file per item)

- 01 — mutation/construction API (read-only → write path)
- 02 — incremental tree building / true iterparse
- 03 — compiled XPath expressions (hot loops)
- 04 — pull (StAX-style) API over the SAX core
- 05 — per-parse options struct (replace global strict-mode setter;
  entity/depth caps, XInclude no_network flag)
- 06 — serialize-with-encoding round-trip guarantees (iconv matrix)

## Non-goals (keep leptris small)

XSLT, XSD/RelaxNG/DTD validation, HTML parsing, XPath 2.0+.
