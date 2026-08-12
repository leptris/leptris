# TODO 157 — SIMD-accelerated parse hot loops

## Status

**Investigated and DEFERRED.** Integrated `simd_skip_whitespace` and
`simd_scan_name` into `direct_parse.c`. Result: **SLOWER**, not faster.

## Why SIMD doesn't help for XML parsing

XML tokens (element names, attr names, whitespace runs) are typically
**3-15 bytes**. The SIMD prologue overhead (loading constants, computing
masks, `_mm_movemask_epi8` + `__builtin_ctz`) costs ~5-10 ns per call.
For a 5-byte name, the scalar LUT loop costs ~5 ns total. SIMD adds
overhead without benefit.

pugixml's speed comes from **compact storage** (not SIMD) and tight
inner loops. We've already achieved both: element is now 64 bytes
(one cache line), and the parse loop uses direct offset arithmetic.

SIMD would only help for **long runs** of whitespace (pretty-printed
XML with deep indentation) or very long element names (rare). The
common case doesn't benefit.

## When to revisit

If profiling on a specific workload (e.g., deeply-indented
configuration files with >1KB of whitespace per element) shows
whitespace or name scanning as the dominant cost, revisit SIMD
with a **threshold guard** — only use SIMD when the remaining
input is >32 bytes. For shorter runs, stay on the scalar path.


with SSE2 scaffolding for `skip_whitespace`, `find_char` and a few
helpers — but they're not called from `direct_parse`.

## Plan

### Phase A — Whitespace skip with SSE2

`dp_skip_ws` currently loops byte-by-byte. Replace with
`simd_skip_whitespace(p->pos, p->end)` from simd_helpers.h. The
SSE2 version processes 16 bytes per instruction.

Hot path: every element-open, every attr-value-pair, every text node
calls `dp_skip_ws`. Called ~5× per element on average.

### Phase B — Name scan with SSE2

`IS_NAME_CHAR` and `IS_NAME_START` use a 256-byte lookup table. Each
call is 1 indexed load + 1 mask test. The bottleneck is the
per-byte loop overhead, not the lookup.

Replace the name-scan loop with a SSE2 version:
- Load 16 bytes
- Use lookup table OR pcmpgtb to test name-char-ness for all 16 at once
- Find first non-name-char via `_mm_movemask_epi8` + `__builtin_ctz`

### Phase C — Attribute value memchr

The current code already uses `memchr` for attribute value scanning.
memchr is libc-vectorized. No change needed unless profiling shows
a hot libc memchr — then drop to SIMD directly.

### Phase D — Tag-name match in close tag

When `</foo>` is encountered, we compare `foo` against the open
element's name. Replace the `memcmp` with a length-prefixed SIMD
compare for short names (≤ 16 bytes).

## Risk

- **Portability**: SSE2 is x86_64-only. NEON for ARM64 already
  scaffolded in simd_helpers.h. MSVC needs `_BitScanForward` (done).
  Falls back to scalar otherwise.
- **Correctness**: SIMD name scan must exactly match the scalar
  `IS_NAME_CHAR` table. Need test cases for all byte values
  0x00-0xFF to verify.

## Expected impact

Medium doc (24 KB): 132 µs → ~50 µs (3× speedup).

Combined with TODO 155 (element compaction): ~25 µs target —
**within 1.25× of pugixml's 20 µs**.

## Status

Pending. Phase A is the first PR (smallest, safest, highest impact).
