# TODO.bindings/06-serialize-encoding-guarantees — Serialize-with-encoding round-trip guarantees

From issue #510 (Tier 2/3 binding asks).

Document what output encodings are promised across the iconv-optional build matrix (LEPTRIS_ENABLE_ICONV on/off × input encodings), and what serialize does when the target encoding is unavailable. Bindings need the contract, not per-platform probing.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
DONE 2026-08-23: contract fixed and documented. Output is ALWAYS
UTF-8 (iconv is input-side only); the XML declaration never lies —
docs parsed as ISO-8859-1 (or explicit non-UTF-8 serialize requests)
now declare UTF-8 instead of the pre-fix mismatch (declaration said
ISO-8859-1 while the body was UTF-8). serialize(parse(serialize(x)))
byte-stable, spec'd. README "Serialization encoding guarantees".
