# TODO.bindings/04-pull-stax-api — Pull (StAX-style) API over the SAX core

From issue #510 (Tier 2/3 binding asks).

next_event instead of callbacks. C→host callbacks cost ~µs each through FFI and eat the streaming advantage; a host-driven pull loop keeps it. Reuses sax/parser.c states; add an event struct + cursor handle.

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
