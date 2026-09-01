# 11 — XQuery 1.0 core (#684-A)

New subsystem src/leptris/xquery/ reusing the XPath engine (SSOT):
- prolog: declare variable/function (compiled to the existing var/
  ufn machinery), declare namespace/base-uri/default collation
- FLWOR: for (multiple, positional), let, where, order by (stable,
  multiple keys, asc/desc), return — compiled to XPATH_OP_FOR/LET
  chains with a new ORDER wrapper; group by/windowing in 12
- path expressions, constructors (direct element), if/typeswitch
  (reject), function calls, doc()/collection()
- CLI: leptris xquery -q file.xq [-s doc.xml]; C API
  leptris_xquery_eval / leptris_xquery_parse
- Saxon-HE net.sf.saxon.Query is the oracle AND perf target
Gate: qt3tests XQuery 1.0 subset adopted + Saxon side-by-side.
