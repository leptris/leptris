# TODO.bindings/02-incremental-iterparse — Incremental tree building / true iterparse

From issue #510 (Tier 2/3 binding asks).

Builder + push-parse + subtree prune/free on the arena from 01. Bounded-memory processing of huge files — a top lxml selling point. Subtree free must return nodes to the pool (or a dead-list) without invalidating siblings.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
