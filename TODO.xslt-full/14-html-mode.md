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



## Status 2026-09-05 (v1.9.83-85): slice 2 shipped

- PI-ish bogus constructs (v1.9.83): `<?target data?>` = PI node,
  data keeps trailing `?`, leading ws trimmed (libxml2 shape).
- Head-content lift (v1.9.84): contiguous leading title/meta/link/
  base run -> synthesized `<head>` before `<body>`; sibling chain
  severed at the boundary.
- Characterization (v1.9.85): `<template>` = ordinary element
  (libxml2 predates WHATWG inert-fragment — NO special handling is
  correct for parity); misnesting = close-at-outer-end-tag +
  stray-drop (our natural-nesting model). Both locked as gate
  specs. RE-SCOPE: adoption-agency is needed ONLY for the html5lib
  corpus gate (different tree shape), NOT for Nokogiri parity.

Remaining: html5lib tokenizer/tree corpus adoption (needs the
adoption-agency tree shape + a JSON test harness), Nokogiri parity
corpus on a crawl sample, bindings exposure of
leptris_parse_html_string.

## Design 2026-09-05 — html5lib corpus harness (the #659 slice)

Corpus: html5lib/html5lib-tests tree-construction .dat files
(Nokogiri vendors them as a git submodule; reference
~/src/external/nokogiri). Pin a snapshot in-repo under
test/html/html5lib-tests/ (vendored copy, not a submodule —
CI must not need network; keep only tree-construction + a
README crediting upstream + the pinned commit).

Harness shape (test/html/test_html5lib.cpp):
1. .dat scanner: split on blank-line-separated blocks; fields
   |#data| |#errors| |#document| |#script-on| (skip script-on
   variants initially). Multiple #data sections per case are
   fragments-after-document pairs (stage 2; skip initially).
2. Expected-tree parser: read the "| " indented lines into a
   node tree spec (element/name-ns, text, comment, DOCTYPE,
   template content). Namespaced names carry the URI in braces
   (svg/mathml foreign content).
3. Runner: leptris_parse_html_string per case; walk our tree in
   the same order; compare node kind, name, namespace URI (not
   prefix), attributes (order-insensitive), text content.
4. Mode-gate: the corpus REQUIRES adoption-agency for the
   formatting-element cases (~15% of cases) — mode-gated OFF in
   slice 1; the mode flips on when AA lands, keeping the
   Nokogiri-parity characterization specs green meanwhile.
5. Report: pass/skip counts by category; a red list file
   (like open_cases.txt) naming the failing cases that AA +
   later slices must close, so progress is falsifiable.

Estimated slices: (a) scanner+expected parser+runner skeleton
with pass-count spec ~ 1 PR; (b) fix fallout outside AA
(misnesting corners the characterization gate already shapes);
(c) AA implementation gated ON + red-list burn-down.

## Status 2026-09-05 (later) — html5lib harness SHIPPED (slice 1)

Vendored corpus (c67f90ea — Nokogiri's pin, the last upstream
revision with tree-construction/; upstream moved to WPT after) +
test_html5lib.cpp: .dat scanner (blank-line case separation — a
#data after #document with NO blank line above is html5lib's
fragment-pair continuation, skip), expected-tree parser (attr
continuation lines like `id="foo"` attach to the nearest open
element; html5lib's template `content` marker flattens — our
template is an ordinary element), comparator (strict on names/ns/
attrs/text/comments/doctype; documented divergences: expected
EMPTY <head> optional, adjacent text coalesced).

Baseline: total=1753, PASS=186, fail=1369, skip=198 (fragment
mode 190 + script-on 8). Floor pinned at 186 — falsifiable.
Red-list snapshot: test/html/html5lib-redlist.txt. Failure shape:
666 child-count under text-bearing elems + 199 under html
(implied-head/foster/misnesting class), 163 element-name, 69 text,
30 parse-failed, 13 kind. NEXT slices by category, NOT adoption
agency first: the child-count/html cluster is the implied-head +
foster-parenting model — the biggest bucket.

## Follow-up (same day): empty-shape inputs are documents (+7)

The harness's 30 parse-failed cases exposed that inputs appending
NOTHING (stray end tag only, doctype-only, second doctype, empty
string) hard-failed at the done-gate. Nothing-parsed is not an
error in lenient HTML: the wrapper synthesis handles the empty
chain (html>body empty). Floor 186 -> 193; spec
EmptyShapeInputsAreDocuments pins it. Remaining parse-fails (23)
are deeper tokenizer edges - next slice.
