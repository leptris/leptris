# TODO.bindings/05-per-parse-options — Per-parse options struct

From issue #510 (Tier 2/3 binding asks).

Replace global leptris_set_strict_mode (global mutable state is thread-hostile): entity resolution mode, depth/size/entity-expansion caps, whitespace policy, XInclude fetch policy. The XInclude no_network flag matters — external fetching is an SSRF/filesystem hazard that deserves an explicit opt-in.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
DONE 2026-08-23: LeptrisParseOptions {flags, strict_mode, max_depth}
+ leptris_parse_string_ex — save/restore of the thread-local
defaults around one call (documented not-reentrant). The XInclude
no_network flag was dropped from v1: the resolver never fetches over
the network (file/local only), so the hazard it guards does not
exist today — revisit if/when remote XInclude lands. Global setters
remain for compatibility. Specs: ParseOptions.* in test_parser.cpp
(depth cap + restore, defaults == plain parse, flags passthrough).
LeptrisParseFlags moved to types.h (single canonical types source).
