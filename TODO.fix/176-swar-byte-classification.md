# TODO 176 — SWAR byte-classification (LUT-free)

**Priority**: P2 (sub-task of [[175-aot-simd-intrinsics]])
**Status**: scoped

## Goal

Replace 256-byte chartype LUTs in hot paths with SWAR (SIMD Within A
Register) arithmetic tricks. Classify 8 bytes per operation, no
cache-line miss.

## Why

Each LUT lookup is one cache-line fill on first hit. For short
scans (5–15 bytes — typical attr names), the LUT setup cost can
exceed the scan itself. SWAR does the same classification in
registers using arithmetic identities:

```c
// "is ASCII alpha" via SWAR — 8 bytes per op
static inline uint64_t swar_alpha_mask(uint64_t bytes) {
    uint64_t shifted = (bytes | 0x20) - 'a';   // fold to lowercase
    return (shifted + ('z' - 'a' + 1)) & ~shifted & 0x8080808080808080ULL;
}
```

simdjson uses this throughout. Parabix classifies via parallel bit
streams (more general but JIT-bound).

## Phases — one PR each

### Phase 1 — SWAR primitives (`common/swar.h`)

- `swar_is_alpha_mask(u64)`, `swar_is_digit_mask(u64)`,
  `swar_is_ws_mask(u64)`, `swar_is_namechar_mask(u64)`.
- All operate on little-endian-packed 8 bytes.
- Static unit tests for each.

### Phase 2 — Migrate `dp_name_char_lut` scan

In `flat/direct_parse.c`'s `dp_scan_name`, replace the byte-by-byte
LUT loop with a SWAR loop processing 8 bytes at a time, scalar tail
for the remainder.

### Phase 3 — Migrate `dp_ws_lut` whitespace scan

Same pattern for whitespace runs between attributes.

### Phase 4 — Migrate `dp_name_start_lut`

Same pattern for the first-char-of-name check.

### Phase 5 — Benchmark + regress fallback

If SWAR regresses on short names (likely under 8 bytes), keep LUT
for `len < 8`, SWAR for longer. Threshold tuning per benchmark.

## Estimated impact

5–10% on long-name workloads. May regress on very short names —
benchmark before committing to threshold.

## Risk

Low on correctness (arithmetic identities are well-known). Medium
on perf — must benchmark each migration; SWAR has setup cost too.

## References

- Source: [[175-aot-simd-intrinsics]]
- Related: [[177-multibyte-literal-matchers]]
