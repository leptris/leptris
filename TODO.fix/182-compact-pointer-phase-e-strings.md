# TODO 182 — Compact pointer Phase E (compact_string for names)

**Priority**: P1 (final structural compaction; stretch goal)
**Status**: **PARSE THESIS DEAD (2026-08-16 upper-bound probe).**
The 32 B split-stream was probed at its theoretical maximum — a
parser-internal hack writing views-only at 32 B stride with no
ctrl fields, no next_cp wiring, and no finalize pass (strictly
less work than any real split-stream could do; benchmark never
reads attrs, so the probe is valid). Interleaved 6-run A/B vs the
shipped 48 B:

| K | 48 B shipped | 32 B upper bound |
|---|--------------|------------------|
| 5 | 58.5 µs | 57.9 µs |
| 20 | **239.4 µs** | 273.5 µs (+14%) |
| 50 | 650.3 µs | 650.0 µs |
| 100 | 1326.3 µs | 1239.6 µs (−6.5%) |

Even the theoretical maximum LOSES 14% at K=20 (where 48 B wins
19%) and recovers only a third of the K=100 penalty at 64 B
levels (1239 vs 1013). A real implementation — with ctrl writes,
wiring, finalize support, and the public handle→ctrl mapping this
design never solved — would be strictly slower than the probe.
**No split-stream layout can beat the shipped 48 B. Closed.**

The string-interning half of this TODO (dedupe common tag names
via a per-document pool, pointer-equality name compare) remains
orthogonal and unmeasured; it was never the parse lever.

## CONSTRAINT (measured, TODO 184 round 4 + TODO 185; superseded by TODO 186)

sizeof(attr) = 64 today and the natural 48 B form REGRESSED 22% at
K=100 — sub-64-B node structs straddle cache lines unless the
allocation is aligned to the struct size. Any layout this TODO
produces must deliver attrs at 2-per-64B-line (32 B, pugixml
density) or keep 64 B. A 32 B attr fits name_view + value_view
exactly, leaving NO room for next_cp/has_entities/name_hash —
those need either a parallel side-array in the attr block or
bit-packing into the views' length fields. This is the >5% lever
the TODO 185 endgame identifies; everything cheaper is measured
dead.

## Goal

Migrate element/attr `name`, `prefix`, `namespace_uri` from `char*`
to `compact_string_2byte` — 2-byte offsets into a per-document
string pool. Eliminates the last 8-byte pointers from element and
attribute structs.

## Why

After [[180-compact-pointer-phase-c-element]] and
[[181-compact-pointer-phase-d-attributes]], element is ~24 B and
attr is ~24 B. The remaining size is dominated by `name_view` /
`value_view` (16 B each — StringView struct) and the raw `char*`
fields.

`compact_string_2byte` replaces the `char*` with a 2-byte offset.
Combined with a StringView computed lazily on access, the struct
drops another 6–12 B per field. Final layout may approach 16 B
per node — pugixml parity.

Side benefit: **automatic string interning**. Common tag names
(`<chapter>`, `<p>`, `<div>`) are stored once per document,
deduplicated by the pool. Pointer-equality compare replaces
strlen for repeated names.

## Migration steps (one PR)

### Step 1 — Per-document string pool

`TaurusDocument` gains `struct taurus_string_pool* strings`.
Hash-table-backed pool: insert returns 2-byte offset.

### Step 2 — Migrate parser to intern names

`direct_parse.c` writes parsed names to the string pool and stores
`compact_string_2byte` instead of `char*`.

Legacy parser (`parser_new.c`) does the same.

### Step 3 — Migrate accessors

`taurus_element_get_name(elem)` becomes:
```c
return string_pool_get(elem->document->strings, elem->name_cs);
```

Add `_view` variants that return StringView (length stored
alongside the pool entry).

### Step 4 — Update serializer, XPath, DTD validator

All name reads go through the accessors — should be mechanical
once the accessors are correctly typed.

### Step 5 — Drop the `name_view` cached field

Once accessors derive it on demand from the pool entry, the cached
StringView fields can be removed from the structs (saves 16 B per
element, 16 B per attr).

## Estimated impact

5–10% additional on top of C+D. Bigger wins on docs with many
repeated tag names (interning makes name-compare O(1)).

## Risk

Medium-high. Memory-ownership changes: pool must outlive any
outstanding StringView (currently StringView borrows from the
parser's text buffer; pool interning adds a copy step).

Risk areas:
- Mutation API (`taurus_element_set_name`) — must intern new value.
- `taurus_element_copy` — copies must share or re-intern.
- XPath name-compare hot path — verify interning actually helps.

## References

- Depends on: [[180-compact-pointer-phase-c-element]], [[181-compact-pointer-phase-d-attributes]]
- Background: [[169-compact-1-byte-in-page-pointers]]
