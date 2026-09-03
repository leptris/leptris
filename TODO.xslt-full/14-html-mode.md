# 14 — HTML parsing mode (#659)

HTML4/5-tolerant parse into the standard DOM: implied end tags
(p/li/td/tr/...), attribute minimization, <script>/<style> raw
text, entities (named table), DOCTYPE legacy strings, optional
case-insensitive element names. API: LEPTRIS_PARSE_HTML flag +
leptris_parse_html_string; serializer method=html already ships.
Reference: libxml2 HTMLparser behaviors + the html5lib tree-
construction subset. Gate: html5lib tokenizer/tree tests subset +
Nokogiri parity corpus.

## Status 2026-09-03 — slice 1 SHIPPED (v1.9.75, PR #809)

`leptris_parse_html_string` (src/leptris/html/html_parse.c):
tokenizer + open-stack tree builder over the STANDARD DOM
(same nodes/pool/serializer/XPath as XML). Covered: implied ends
(p on blocks, li, dt/dd, td/th, tr, thead/tbody/tfoot,
option/optgroup), voids, raw script/style, lowercased names,
minimized + unquoted attrs, 2032-entry WHATWG entity table +
numeric in text and values, stray-end-tag pops, EOF closing,
never-failing tokenizer, Nokogiri document shape (synthesize
<html><head/><body> when absent; explicit <html> honored; no
implied tbody). 13 specs in test/html/ (watched RED first).

Remaining slices:
- head-content placement: <title>/<meta>/<link>/<base> before
  body content lift into <head> (libxml2 moves them); <template>
  contents as inert
- CDATA-section passthrough; processing-instruction-ish bogus
  comments (<?...> → comment)
- adoption-agency-shaped misnesting (current: natural nesting +
  stray-pop, libxml2-like)
- html5lib tokenizer/tree-construction corpus adoption (the gate)
- Nokogiri parity corpus: diff against nokogiri on a crawl sample
- bindings: expose the entry in leptris-ruby/py (their entries
  tracked on #683)

