# TODO 177 — Multi-byte literal matchers

**Priority**: P2 (sub-task of [[175-aot-simd-intrinsics]])
**Status**: scoped

## Goal

Detect 3- and 4-byte literal terminators (`-->`, `]]>`, `?>`, `</x`)
using a single aligned 4-byte read + masked compare, instead of
byte-at-a-time scan.

## Why

Current end-detection loops in CDATA / comment / PI parsers:

```c
while (p < end) {
    if (p[0] == '-' && p[1] == '-' && p[2] == '>') break;
    p++;
}
```

is N byte-comparisons per scan. The equivalent 4-byte aligned read
+ XOR + mask is one compare per 4 bytes — 4× fewer ops on long
runs. simdjson's structural-character finder uses this throughout.

## Phases — one PR each

### Phase 1 — Matcher primitives (`common/literal_match.h`)

- `swar_contains3(s, len, c0, c1, c2)` → offset of first match or -1
- `swar_contains4(s, len, c0, c1, c2, c3)` → offset or -1
- All operate on 4-byte aligned reads when possible, scalar tail.
- SIMD-implemented under [[175-aot-simd-intrinsics]] framework.

### Phase 2 — Migrate `dp_parse_comment` end scan

Replace `-->` byte loop with `swar_contains3`.

### Phase 3 — Migrate `dp_parse_cdata` end scan

Replace `]]>` byte loop.

### Phase 4 — Migrate `dp_parse_pi` end scan

Replace `?>` byte loop. (`dp_parse_doctype` also — already cold path
but easy to migrate for consistency.)

### Phase 5 — Migrate close-tag detection `</name`

4-byte pattern with `name[0..2]` filled from the open tag name.

## Estimated impact

3–5% on docs with many comments/CDATA/PIs. Lower on text-heavy docs.

## Risk

Low. Matchers are stateless. Each migration is revertable.

## References

- Source: [[175-aot-simd-intrinsics]]
- Related: [[176-swar-byte-classification]]
