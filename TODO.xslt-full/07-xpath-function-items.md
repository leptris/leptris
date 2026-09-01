# 07 — XPath function items + HOF (#692-B)

The public-surface item: XPathResultType/FFI mirrors gain
XPATH_RESULT_FUNCTION (coordinate ext/ mirrors in leptris-ruby +
leptris-py — PRs ok, never release). Then:
- named function references name#arity (function items as values)
- inline functions function($x){ body } (closure over the varset
  snapshot; body = compiled expr evaluated on call)
- partial application f(?, ...) — captures arg placeholders
- dynamic invocation f(a,b) where f is a function item
- fn:for-each/filter/fold-left/fold-right/apply/sort/function-
  lookup/function-name/function-arity/function-arity
- xsl:function (see 09) reuses this machinery

Gate: Xslt30.FnItems spec (Saxon-probed) + mirrors synced + suite.
