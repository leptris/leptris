# TODO.bindings/03-compiled-xpath — Compiled XPath expressions

From issue #510 (Tier 2/3 binding asks).

leptris_xpath_compile/eval/free with a stated thread contract (the AST/bc cache is process-global + mutex/pin-guarded today — a compiled handle pins its entry for its lifetime). Hot-loop win on top of the batch accessors (#509): today every leptris_xpath_eval re-checks the cache by string hash.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
DONE 2026-08-23: leptris_xpath_compile / _compiled_eval /
_compiled_free wrapping the pinned AST cache — one parse + one pin
for the handle's lifetime, bytecode cached on first eval; skips the
per-call hash + cache probe of leptris_xpath_eval. Thread contract:
immutable handle, concurrent eval safe (pinned entry), free after
last eval; failures snapshot into the document error slot. Specs:
test/xpath/test_compiled.cpp incl. a 4-thread one-handle stress.
