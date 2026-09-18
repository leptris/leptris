# Lever 2: compile-don't-interpret the tokenizer dispatch

Status: TODO. Precedents: XGrammar-2 JIT + tag-dispatched switching
(arXiv:2601.04426), tree-sitter grammar-to-C, CPython's generated
PEG. Our html_parse.c start/end-tag ladder is a long strcmp chain
per token; replace with a precomputed first-char dispatch table
(built once per process) + name-hash switch, like the XPath VM's
BYTE_DISPATCH lane. Same pattern applies to the end-tag scope walk.
