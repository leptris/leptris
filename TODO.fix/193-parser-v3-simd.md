# TODO 193: Parser v3 — two-pass SIMD scan-index

**Priority**: P0 (the only remaining path to beating pugixml on parse)
**Status**: Design (post round-11; all single-parser levers measured dead)
**Effort**: XL

## Why this is the only option left

Eleven measured failures (TODO 185 rounds 2-11) establish that the
current single-pass parser is at a compiler-global optimum: loop-
shape edits, work-removal, layout variants, and even a genuine
out-of-line function split all regress mid-K. The fresh K=50
profile (767 samples): 89% in the attr-scan region, 8% in the
fused copy+count, <3% everything else. There is no slack inside
the current architecture.

The shared cost with pugixml is the byte-at-a-time classification
(char load + chartype-table load per byte). pugixml pays it with
scalar 4x-unrolled loops. We cannot beat that with better C on the
same shape — we must stop classifying bytes one at a time.

## Design

**Pass 1 — SIMD structural scan.** One NEON/AVX2 pass over the
whole input buffer classifies bytes into a tiny event set and
records SPANS, never branching per byte:

- `<`, `>`, `/`, `=`, `'`, `"`, `&`, whitespace
- Using the existing simd_text framework: per 32-byte chunk, produce
  a bitmask per class (movemask/vmovemaskq tricks already proven in
  count3/find3), then compress to span records:
  `{tag_start, name_end, kind}` for tags,
  `{name_span, value_span, quote}` for attributes.
- Pass 1 has NO tree state, NO branches on content — pure vector
  throughput + branchless span extraction. Expected ~2-4x the
  scalar classification rate.

**Pass 2 — materialize.** Walk the span index (a few bytes per
event) and build the element/attr records exactly as today
(bulk blocks, compact wiring, in-place NUL termination of the
copied buffer). Every existing correctness path (namespaces,
entities, DTD fallback) is preserved because pass 2 consumes the
same logical events the current scanner emits.

**Fallback ladder**: keep direct_parse as-is; route to v3 only for
documents whose pass-1 scan shows the simple grammar (no DTD
internal subset, no unexpected constructs), exactly like today's
flat_parse fallback. Ship v3 behind a build flag first, flip the
default after gates.

## Gate discipline (unchanged)

8-run interleaved fresh-dir Release A/B, min per section, ALL four
K sections must not regress; per-phase gates inside the build:
span-scanner unit-tested against the scalar scanner on the fuzz
corpus before wiring; correctness = 553 tests + conformance +
ASAN + fuzzing.

## Expected outcome

Scan classification is the dominant shared cost; SIMD pass 1 at
even 2x scalar rate converts the 1.5-1.8x gap to parity-or-better
at mid/high K, where the gap is largest (1.84x/1.83x). K=5 stays
wiring-bound (already 1.54x).
