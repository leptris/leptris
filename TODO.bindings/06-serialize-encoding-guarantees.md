# TODO.bindings/06-serialize-encoding-guarantees — Serialize-with-encoding round-trip guarantees

From issue #510 (Tier 2/3 binding asks).

Document what output encodings are promised across the iconv-optional build matrix (LEPTRIS_ENABLE_ICONV on/off × input encodings), and what serialize does when the target encoding is unavailable. Bindings need the contract, not per-platform probing.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
