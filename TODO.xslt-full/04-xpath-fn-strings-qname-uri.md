# 04 — XPath fn: catalog: strings / QNames / URIs (#691-D)

Strings: codepoint-equal, normalize-space (exists), normalize-
unicode (NFC via utf8proc when built, else identity+doc), upper/
lower-case (exist), translate (exists), string-join (exists;
sequence arity), starts-with/ends-with (exist), substring-before/
after (exist; empty-match 3.1 semantics check vs Saxon),
concat (exists), iri-to-uri, encode-for-uri, decode-from-uri,
escape-html-uri, contains-token, format-integer (complete pattern
set: 9/A/a/I/i/〇 and pattern separators), adjust-string.
QNames: QName(prefix,local), resolve-QName, QName-from-string,
local-name-from-QName, prefix-from-QName, namespace-uri-from-QName,
node-name (element/attribute/PI), expanded-QName-to-string.

Gate: Xslt30.FnStrings spec (Saxon-probed) + suite green.


## Status 2026-09-04 — string/QName/URI tail shipped (v1.9.77+)

Shipped in the #691 scalar-tail wave: compare, codepoint-equal,
normalize-unicode (utf8proc forms; absent when built without
utf8proc), resolve-QName (prefix against in-scope namespaces via
the QName TLS channel), environment-variable,
available-environment-variables, unparsed-text / -lines /
-available (raw file reads, CWD-relative), uri-collection (empty
catalog). Remaining in this lane: analyze-string (needs the
fn:match/fn:non-match element model — XSLT xsl:analyze-string
exists; the fn form returns nodes), fn:format-number as a plain
XPath fn (the formatting core lives XSLT-side — layering decision
needed), fn:snapshot (needs detached-copy result trees).


## Status addendum 2026-09-04 (v1.9.79 candidate)

random-number-generator SHIPPED (PR #834): seeded xorshift64*
deterministic per seed, map carrier with 'number' in [0,1).
Remaining in this lane:
- fn:analyze-string — needs the fn:match/fn:non-match element
  model; build on a fresh anchored document (xq_anchor_owned_doc
  chain) + POSIX regex iteration; xsl:analyze-string exists
  XSLT-side as the semantic reference.
- fn:format-number as plain XPath — move the self-contained core
  (parse_pattern/PatternInfo/format_value, ~200 lines) from
  xslt_functions.c into a shared common/ module; XSLT keeps the
  decimal-format lookup and passes separator chars. Libxslt-suite
  format-number corners gate the move.
- fn:snapshot — detached deep copies via copy_subtree_detached,
  anchored on a fresh document through the owned-docs chain.
- RNG 'next'/'permute' members — need function-item closure state.
