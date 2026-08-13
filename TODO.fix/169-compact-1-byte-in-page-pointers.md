# TODO 169 — Compact 1-byte in-page pointers

## Status

**Deferred (multi-week refactor).** This is THE biggest remaining
lever for closing the structural gap to pugixml — element struct
64B → ~16B, attribute ~80B → ~16B. Cache locality wins cascade
through every tree walk and attr scan.

## Why

pugixml's `xml_node_struct` is 20-24 bytes:
- `compact_header` (2B): page offset + node-type flags
- `compact_pointer_parent` (1-2B)
- `compact_pointer_child` (1B)
- `compact_pointer_sibling` (1B)
- `compact_pointer_attribute` (1B)
- `compact_string` name (2B)
- `compact_string` value (2B)

taurus's `struct taurus_element` is 64 bytes (already one cache line,
but 3× pugixml's size). The extra space goes to:
- 4-byte int32 offsets (parent_off, first_child_off, first_attr_off,
  ns_head_off) instead of 1-byte
- 8-byte pointers for name/prefix instead of 2-byte compact_string
- 16-bit name_hash (which pugixml doesn't have)
- 4-byte line + child_count fields

Every tree walk processes one cache line per node visited. pugixml
fits 4 nodes per cache line; taurus fits 1. On deep traversals or
wide attribute lists, this is the dominant cost.

## Scope of the migration

This is genuinely multi-week work. The migration touches:

### Phase A — Encoding primitives (~1 week)

- Define `compact_pointer_dx` template-equivalent (C macro):
  1-byte offset from base, divides by alignment, sentinel for overflow.
- Define `compact_string`: 2-byte offset from page string-base.
- Hash table fallback for offsets that overflow 1 byte.

### Phase B — DOM struct migration (~2 weeks)

- Change every `int32_t *_off` field to `compact_pointer_*`.
- Change `name`/`prefix` char* to `compact_string`.
- Update accessor helpers in `dom/element.h`, `text.h`, `comment.h`,
  `cdata.h`, `pi.h`.
- Every read of these fields goes through the new accessor.

### Phase C — Parser migration (~1 week)

- `direct_parse.c`: write 1-byte offsets at parse time.
- Pool allocator: must guarantee 64-byte alignment so 1-byte offset
  covers 256 bytes of true reach (64 positions × 4-byte alignment).
- For docs whose first-element-to-last-node distance exceeds 256
  bytes (almost all real docs), fall back to overflow table.

### Phase D — XPath + serializer migration (~1 week)

- `vm.c`: tree-walk handlers use new accessors.
- `evaluator_axes.c`: descendant walk uses new accessors.
- `serialize.c`: tree walk for output.

### Phase E — Test + benchmark (~1 week)

- All 464 tests must pass with new encoding.
- Benchmark: expect 1.5-2× on cache-bound workloads (many-attrs
  benchmark, deep traversal).

## Estimated impact

- `bench_dom_taurus` Tree Traversal: 1.5-2× (more nodes per cache line).
- `benchmark_many_attrs` K=50: 2-3× (attribute structs shrink from
  ~80B to ~16B; 5× more attrs per cache line).
- `bench_xpath_taurus`: 1.2-1.5× (VM tree walks).

Combined with TODO 167 (already shipped 3-4×) and TODO 170
(amalgamation), this would close most of the remaining gap to pugixml.

## Risk

- **Correctness.** Overflow handling must be bulletproof. pugixml
  has had bugs in this area. Test coverage must include adversarial
  inputs that stress the overflow table.
- **ABI break.** Public structs that currently expose `int32_t *_off`
  fields would change layout. Need to verify no public API exposes
  these (they're internal-only per `types.h`).
- **Memory ownership.** Switching `name` from `char*` to
  `compact_string` (2-byte offset) changes lifetime semantics — the
  page base must outlive any node reference.

## Why deferred

- Genuine multi-week effort. Cannot be done in one PR.
- TODO 167 already delivered 3-4× speedup via build flags, reducing
  the urgency.
- Risk of correctness bugs in the overflow path is high without a
  dedicated test investment.

## Recommended next step

If pursuing, start with Phase A only — define the encoding
primitives alongside the existing int32 path, no migration. Land
that as one PR. Then Phase B in a second PR (one node type at a
time: text first, then comment, then cdata, then pi, then element).
Then Phase C/D/E.

Each phase independently testable and revertable.
