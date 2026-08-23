# TODO.bindings/05-per-parse-options — Per-parse options struct

From issue #510 (Tier 2/3 binding asks).

Replace global leptris_set_strict_mode (global mutable state is thread-hostile): entity resolution mode, depth/size/entity-expansion caps, whitespace policy, XInclude fetch policy. The XInclude no_network flag matters — external fetching is an SSRF/filesystem hazard that deserves an explicit opt-in.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
