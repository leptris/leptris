# 08 — Attribute struct endgame: 32 B (2 per cache line)

CLOSED (2026-08-22): measured dead. TODO 185 round 6 ran the
upper-bound probe of the whole split-stream family — a views-only
32 B stride with NO ctrl array, NO wiring, NO finalize, i.e. a
parser strictly faster than any realizable split-stream — against
the then-shipped 48 B attrs: K=20 +14% WORSE, K=100 only −6.5%,
and at K=100 still 22% behind the 64 B point (1239.6 vs 1013.4 µs).
A real design adds the ctrl-array stores on top, so the family's
ceiling is below the shipped points at every K that matters. The
attr-size axis is exhaustively measured: {32-split-ceiling <
40 = SHIPPED > 48, 56 dead, 64 old law}.

Reopen only if a real user shows up with K=100-class documents AND
a measurement contradicting round 6 — the gate (parse all-K,
serialize byte-diff, set_attribute scaling + duplicate spec,
mutation specs) stays as written below.

---

Original text:

The attr-size axis is measured: {32 upper-bound probe = dead,
40 = shipped (v0.26.2), 48, 56 = dead, 64 = legacy}. The remaining
>5% parse lever (README Planned; docs/173-attr-struct-shrink.md) is
the TODO 182 split-stream: 32 B of hot views + a parallel control
array, giving 2 attrs per cache line. Requires the 40 B layout's
has_entities/name_hash packing to move into the side array.

Gate hard: rounds 19-22 taught that attr layout changes ripple into
the attr index (duplicate-attr regression of v0.26.2 — see the
ledger) and into serialize. The full gate: parse all-K, serialize
byte-diff, set_attribute scaling + duplicate spec, mutation specs.
Only worth it if K=100-class documents matter to a user.
