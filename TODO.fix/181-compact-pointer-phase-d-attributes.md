# TODO 181 — Compact pointer Phase D (attribute list migration)

**Priority**: P0 (closes pugixml gap on K=100 attr-heavy benchmark)
**Status**: scoped

## Goal

Migrate `struct leptris_attribute`'s `next` pointer and the
element's `first_attribute` pointer to `compact_pointer_1byte`.
Attribute struct shrinks 72 → ~24 bytes.

## Why

K=100 many-attrs benchmark is the worst-case for leptris vs pugixml
(current gap 3.06×). Each attr struct is 72 B; pugixml's is ~16 B
in compact mode. Five attrs per cache line vs one — exactly the
shape of the gap.

After [[180-compact-pointer-phase-c-element]] lands, attribute
list is the last remaining large struct in the tree. This phase
closes the structural gap to pugixml.

## Migration steps (one PR)

### Step 1 — Migrate `struct leptris_attribute.next` to `compact_pointer_1byte next_cp`

Rename field, update all reads/writes via accessors. Hot sites:

- `dp_add_attr_inline` (parser writes during attr-list construction)
- `leptris_element_get_attribute_by_name` (DOM query walks the list)
- `leptris_attribute_foreach` (public iteration macro)
- XPath VM `PRED_ATTR_EXISTS` / `PRED_ATTR_EQ_STRING` opcodes
- Serializer attr-walk

### Step 2 — Migrate element's `first_attribute_cp` to compact_pointer

Already migrated in Step 5 of [[180-compact-pointer-phase-c-element]]
if that's done first. If this PR runs first, do it here.

### Step 3 — Wire overflow table for attribute lists

Attributes are pool-allocated. Per-doc overflow table from Step 1
of [[180-compact-pointer-phase-c-element]] handles this — same
table, just attribute field-offset entries.

### Step 4 — Tighten `_Static_assert(sizeof(leptris_attribute) == 24)`

(Depending on remaining fields: `name_view`, `value_view`,
`name_hash`, `has_entities`, `ns_cache`. Some of these may also
compress in [[182-compact-pointer-phase-e-strings]].)

### Step 5 — Benchmark K=100 many-attrs

Should hit ~1.5× gap to pugixml (down from 3.06×). If not, profile
to find remaining hot spot.

## Estimated impact

2–3× on K=100 attr benchmark. Combined with [[180-compact-pointer-phase-c-element]],
cumulative 3–4× on tree-walk heavy workloads.

## Risk

Medium. Attribute writes are concentrated in `dp_add_attr_inline`
(one site, well-tested). Reads are spread across DOM/XPath/serializer
but each is straightforward.

ASAN + 464 tests must pass. Overflow stress test (1000 attrs in one
list spanning multiple pool pages) must work.

## References

- Depends on: [[178-compact-pointer-phase-a]], [[180-compact-pointer-phase-c-element]]
- Next: [[182-compact-pointer-phase-e-strings]]
- Benchmark: `benchmarks/benchmark_many_attrs.c`
