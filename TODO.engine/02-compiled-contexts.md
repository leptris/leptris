# TODO.engine/02-compiled-contexts — Compiled XPath with ns and variable contexts

leptris_xpath_compiled_eval_ns / _eval_vars: the compiled handle on the context-carrying paths. The ns/vars routes run the direct evaluator (the VM fast paths skip prefixed name tests), so these variants evaluate the pinned AST with ns_set / variable_set installed on the context — same semantics as leptris_xpath_eval_ns / eval_with_vars_context, minus the re-parse.

DONE 2026-08-24: leptris_xpath_compiled_eval_ns / _eval_vars via a shared
compiled_eval_context helper — the pinned AST evaluated directly
(VM fast paths skip prefixed name tests, matching the plain-path
design) with ns_set / variable_set installed on the context; doc-slot
error snapshotting preserved. Specs: CompiledXPath.EvalWithNamespaceBindings,
EvalWithVariables. README documents the variants.
