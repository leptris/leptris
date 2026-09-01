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
