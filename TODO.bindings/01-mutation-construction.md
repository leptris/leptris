# TODO.bindings/01-mutation-construction — Mutation/construction API

From issue #510 (Tier 2/3 binding asks).

leptris_document_new (alias of leptris_document_create), create element/text/comment/CDATA/PI, append/insert/remove/reparent, set text/attribute, subtree clone. Requires arena growth POST-PARSE (the pool is sized at parse time — growth is the core engineering risk). Unlocks the entire write path: lxml SubElement/append, Nokogiri builders, edit-then-serialize. Both bindings are read-only today only because this is missing. Rides into 02 (incremental building).

Status: open. Design first (see the board README); one PR per item,
full phases, like TODO.concurrency.
DONE 2026-08-23: audit showed the full surface already shipped across
earlier TODOs — create: leptris_document_create, element_create,
text/comment/cdata/pi_node_create; wire: element_append_child,
insert_before/after, remove_child/children, document_adopt_child,
node_unlink; set: element_set_text, set_attribute (+int/uint/double/
float/bool variants), remove_attribute(+all); clone: element_copy,
document_copy, element_append/prepend_copy. The issue text predates
this. What was missing was proof: DomBuilder round-trip spec (build
from scratch -> serialize -> reparse -> verify, incl. all node types
+ batch children check) + deep-copy spec now in test_dom.cpp.
