# 04 — XInclude remaining phases (TODO 92 remainder)

Shipped: parse="text" with xi:fallback, parse="xml" end-to-end,
xpointer="xpath(...)" fragment selection, depth-limit cycle guard
(Phase 5).

Remaining:
- xpointer shorthand + element() / xpointer() scheme forms
  (currently only xpath() scheme).
- xpointer on parse="text".
- Accept/reject charset negotiation for parse="text".
- href fragment identifiers stripped per spec §4: verify against
  the W3C XInclude test suite.
