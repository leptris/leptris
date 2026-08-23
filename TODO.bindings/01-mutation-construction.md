# TODO.bindings/01-mutation-construction — Mutation/construction API

From issue #510 (Tier 2/3 binding asks).

leptris_document_new (alias of leptris_document_create), create element/text/comment/CDATA/PI, append/insert/remove/reparent, set text/attribute, subtree clone. Requires arena growth POST-PARSE (the pool is sized at parse time — growth is the core engineering risk). Unlocks the entire write path: lxml SubElement/append, Nokogiri builders, edit-then-serialize. Both bindings are read-only today only because this is missing. Rides into 02 (incremental building).

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
