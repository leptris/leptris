# TODO 180 — Compact pointer Phase C (element tree migration)

**Priority**: P0 (biggest single lever; closes 1.5–2× of the gap)
**Status**: scoped

## Goal

Migrate `struct taurus_element`'s tree pointers (`parent_off`,
`first_child_off`, `last_child_off`, `next_sibling_off`,
`first_attribute_off`) from `int32_t` self-relative offsets to
`compact_pointer_1byte` with overflow side-table. Element struct
shrinks 64 → ~24 bytes (3 cache lines saved per walk step).

## Why

Element struct is the dominant cost in every tree walk, attr scan,
and serialization. Currently 64 B (one full cache line). pugixml's
`xml_node_struct` is 20–24 B (compact mode 8–12 B). Three nodes per
cache line vs one — that's where the 3× gap lives.

Per [[149-pugixml-architecture-study]], 60 µs of the 75 µs gap on
the K=100 benchmark is structural cache pressure, not algorithmic.

## Migration steps (one PR per step)

### Step 1 — Wire overflow table into `TaurusDocument`

Document gains a `struct taurus_pointer_overflow* ptr_overflow`
field. Lazy-allocated on first overflow. Freed in
`taurus_document_free`.

### Step 2 — Migrate `parent_off` to `compact_pointer_1byte parent_cp`

All writes go through `cp1_set(elem, parent, offsetof(taurus_element, parent_cp), &doc->ptr_overflow)`.
All reads go through `cp1_get(elem, parent_cp, doc->ptr_overflow)`.

Every direct access site is found by renaming the field (compiler
enumerates errors).

### Step 3 — Migrate `first_child_off`, `last_child_off`

Pair migration — they're often written together. Risk: any missing
site breaks tree walking.

### Step 4 — Migrate `next_sibling_off` (element variant)

Distinct from [[179-compact-pointer-phase-b-nodes]] which handles
text/comment/cdata/pi sibling pointers.

### Step 5 — Migrate `first_attribute_off`

Last tree-edge migration. Attribute list itself migrates in
[[181-compact-pointer-phase-d-attributes]].

### Step 6 — Tighten `_Static_assert(sizeof(taurus_element) == 24)`

(Or whatever the new layout settles at — exact size depends on
remaining 4-byte fields like `name_hash`, `child_count`, `line`.)

### Step 7 — Stress-test overflow path

Adversarial inputs:
- 100,000 elements spanning > 1024 bytes apart (forces overflow).
- Same document built via mutation API (forces overflow on insert).
- Same document after `taurus_element_copy` (forces overflow on copy).

These MUST pass before merge.

## Estimated impact

1.5–2× on tree-walk benchmarks (`bench_dom_taurus` traversal).
Likely 1.3–1.5× on K=100 attr benchmark (attr struct still 72 B
until [[181-compact-pointer-phase-d-attributes]]).

## Risk

**HIGH**. Overflow path correctness must be bulletproof. pugixml
has had bugs in this area. Mitigations:

1. Adversarial test inputs (Step 7) must be exhaustive.
2. ASAN + UBSAN clean across all tests.
3. Memory-leak check on macOS (`leaks --atExit`) and Linux (valgrind).
4. Fuzz test (existing `test/fuzz/`) for 10M iterations before merge.

If any of these fail, revert and debug — do NOT ship partial.

## References

- Depends on: [[178-compact-pointer-phase-a]], [[179-compact-pointer-phase-b-nodes]]
- Next: [[181-compact-pointer-phase-d-attributes]]
- Background: [[149-pugixml-architecture-study]], [[169-compact-1-byte-in-page-pointers]]
