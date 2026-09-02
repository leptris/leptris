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

## json-to-xml / xml-to-json ground truth (2026-09-03, next slice)

Saxon-HE 12.7 (/tmp/probe9/jx8.xsl): json-to-xml('{"b":"beta","n":2}')
=> <map xmlns="http://www.w3.org/2005/xpath-functions">
     <string key="b">beta</string><number key="n">2</number></map>
Canonical fn vocabulary: map/array/string/number/boolean/null,
@key on members, arrays positional. Implementation note: these
return a DOCUMENT node through the nodeset channel — the result
nodeset does not own documents, so build the fragment in a scratch
doc and LEAK-SAFELY by reusing the capture-content ownership
discipline (xslt_capture_content pattern) or attaching to the exec.
xml-to-json is the reverse walk over that vocabulary.

## json-to-xml slice — precise handoff (2026-09-03, reverted clean)

First implementation attempt returned an EMPTY nodeset and its
internal leptris_parse_string was never reached (lldb: only the
stylesheet + source parses fire) — so fn_json_to_xml exits early:
either json_root returns NULL on this exact input (though the same
root logic passes the parse-json specs — verify the escape/arg
path: re_str_arg on a &quot;-laden select) or the registration is
not reached. Bisect from a clean main with a one-line probe:
printf the re_str_arg result + json_root outcome inside a scratch
fn. Spec (Saxon ground truth, already written once):
  copy-of json-to-xml('{"b": "beta", "n": 2}') =>
  <o><map xmlns="http://www.w3.org/2005/xpath-functions">
    <string key="b">beta</string><number key="n">2</number></map></o>
and the xml-to-json round trip map:get(...,'b')='beta'. Design
kept: build canonical XML TEXT (escape &<>" + classify
number/boolean/null by lexical form) → parse into a scratch doc →
return the ROOT ELEMENT in the nodeset; xml-to-json walks the
fn: vocabulary back into the shared map representation.
