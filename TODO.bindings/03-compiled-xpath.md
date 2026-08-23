# TODO.bindings/03-compiled-xpath — Compiled XPath expressions

From issue #510 (Tier 2/3 binding asks).

leptris_xpath_compile/eval/free with a stated thread contract (the AST/bc cache is process-global + mutex/pin-guarded today — a compiled handle pins its entry for its lifetime). Hot-loop win on top of the batch accessors (#509): today every leptris_xpath_eval re-checks the cache by string hash.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
