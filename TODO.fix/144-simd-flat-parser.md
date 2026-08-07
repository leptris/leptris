# TODO 144 — SIMD-accelerated flat parser

Phase D of TODO 140.

## Problem

The flat parser scans element names one byte at a time in a tight
ASCII loop. pugixml uses SIMD (SSE4.2 / NEON) to scan 16 bytes per
cycle for name-end characters.

## Fix

Vectorize the name-scan loop in flat_parser.c using the existing
`src/taurus/simd_helpers.h` already used by the legacy parser.
The ASCII tight loop becomes a 16-byte vectorized scan.

## Expected impact

Parse cost drops 4-8×. For a 5 KB doc:
- Before: 47 µs parse-only
- After:  ~10-15 µs parse-only

Combined with Phase A wire_child inlining:
- Parse+promote: ~30 µs at 5 KB (was 78 µs)
- Within 2× of pugixml's 40 µs at 10 KB

## Risk

Low. SIMD helpers are already proven in the legacy parser. The
flat parser's loops are simpler (no encoding conversion, no
namespace splitting) so the integration is mechanical.

## Test plan

- 30 existing FlatParse specs verify correctness.
- New spec: SIMD path produces identical trees to scalar path
  on Unicode-heavy input.
