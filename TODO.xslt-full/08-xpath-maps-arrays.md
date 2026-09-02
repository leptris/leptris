# 08 — XPath maps + arrays + JSON (#691-F / #692-C)

Internal representation: a map/array is a synthetic document-
fragment node set (map = element node with entry children; array =
element with positional children) so values flow through the
existing NODESET channel without an ABI break; the FFI-level map
view is a later binding ask (#683).
- map { k: v, ... } / map constructors via function forms
- map:get/put/remove/contains/keys/size/merge/for-each/entry
- array { ... } / [ ... ], array:get/size/put/append/subarray/
  remove/insert-before/reverse/join/for-each/filter/fold-left/
  fold-right; lookup '?' on maps/arrays; '[' ']' postfix
- parse-json / json-to-xml / xml-to-json / fn:serialize json
  method (JSON escapes via the serializer's json writer)

Gate: Xslt30.FnMaps spec (Saxon-probed) + suite green.

## Status 2026-09-03 — A/B/C/D-core shipped, tail remaining

DONE (v1.9.51–53): map constructor + get/size/keys/contains (A);
put/remove/merge + xsl:map → 09 closed (B); square array ctor +
array:size/get/append/put (C); parse-json (D-core). Representation
authority: MapEntries + xpath_map_value/builder in
functions_ext31.c; arrays = map with positional keys "1".."n".

REMAINING (08 tail):
- ?lookup (map?key / array?n chains): needs a '?' lexer token
  (TOK_QUESTION does not exist) + postfix parse in parse_filter_expr.
- json-to-xml / xml-to-json / fn:serialize json method (serializer
  json writer exists per the TODO header — wire it).
- map:for-each/entry, array:subarray/remove/insert-before/reverse/
  join/fold-left/fold-right — thin MapEntries loops once HOF
  machinery (lane 07) exists for the callback forms.
Then lane 07 → 12 (see 06's status block for the Saxon verdicts).
