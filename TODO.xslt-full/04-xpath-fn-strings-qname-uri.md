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
