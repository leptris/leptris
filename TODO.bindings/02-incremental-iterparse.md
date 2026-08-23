# TODO.bindings/02-incremental-iterparse — Incremental tree building / true iterparse

From issue #510 (Tier 2/3 binding asks).

Builder + push-parse + subtree prune/free on the arena from 01. Bounded-memory processing of huge files — a top lxml selling point. Subtree free must return nodes to the pool (or a dead-list) without invalidating siblings.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
DONE 2026-08-23: leptris_iterparse_new/_next/_free over the pull API.
Each top-level child of the root materializes in its OWN document
(pool); the next _next() releases the previous subtree document —
memory bounded by the largest subtree, not the document. Text runs
accumulate across pull text events before node creation. v1
limitation documented in the header: QNames as written, prefixes not
re-resolved. Specs: test/sax/test_pull.cpp (Iterparse.*).
