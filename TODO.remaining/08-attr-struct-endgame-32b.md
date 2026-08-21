# 08 — Attribute struct endgame: 32 B (2 per cache line)

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
