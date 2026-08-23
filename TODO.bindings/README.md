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

## Executed (v1.4.0) — see DONE notes in each file

- 01 — mutation/construction API: audit found the full surface
  already shipped; added the builder round-trip + deep-copy specs
- 02 — incremental tree building / true iterparse:
  leptris_iterparse_* (per-subtree pools, bounded memory)
- 03 — compiled XPath expressions: leptris_xpath_compile /
  _compiled_eval / _compiled_free (pinned cache entry)
- 04 — pull (StAX-style) API: leptris_pull_* over the streaming
  SAX core (no C->host callbacks)
- 05 — per-parse options: LeptrisParseOptions +
  leptris_parse_string_ex (scoped strict/depth/flags)
- 06 — serialize-encoding guarantees: UTF-8 output always,
  truthful declarations, byte-stable double-serialize

## Non-goals (keep leptris small)

XSLT, XSD/RelaxNG/DTD validation, HTML parsing, XPath 2.0+.
