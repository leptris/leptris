# TODO 159 — XPath engine: close the 5-13× gap vs pugixml

## Why

Benchmark `bench_xpath_pugixml` (1000-element XML):

| Query                            | Taurus | pugixml | Ratio |
|----------------------------------|--------|---------|-------|
| `count(//book)`                  | 44 µs  | 5.6 µs  | 7.9×  |
| `/library/section/book`          | 27 µs  | 4.8 µs  | 5.6×  |
| `//book[price > 30]`             | 68 µs  | 6.5 µs  | 10.5× |
| `//book[@cat='fiction']/title`   | 58 µs  | 5.7 µs  | 10.2× |
| `count(//*)`                     | 47 µs  | 5.3 µs  | 8.9×  |

We've shipped the bytecode VM (TODO 120), element index (TODO 132),
attribute index (TODO 133), and fused opcodes (TODO 134-137). Still
5-13× slower than pugixml.

## Diagnosis

pugixml's XPath is **monomorphic, arena-allocated, and tight**.
Ours has indirection through bytecode + nodeset_add_fast + the
element/attribute index lookups. Profiling the slowest case
(`//book[price > 30]`):

| Section               | Time  | Notes                                  |
|-----------------------|-------|----------------------------------------|
| bytecode dispatch     | 8 µs  | VM loop overhead                       |
| descendant walk       | 20 µs | calls first_child + next_sibling       |
| predicate eval (text) | 15 µs | nodeset_add_fast per match             |
| predicate comparison  | 5 µs  | string compare for `> 30`              |
| other                 | 20 µs | nodeset lifecycle, cleanup             |

## Plan

### Phase A — Monomorphize the VM dispatch

The VM uses a switch dispatch. Replace with computed-goto (GCC
extension, MSVC falls back to switch). Each opcode handler is a
label. Eliminates branch mispredicts (~5 ns per op).

### Phase B — Arena-allocate nodesets

`xpath_nodeset_add_fast` allocates from a per-result pool. The pool
has malloc-on-first-alloc overhead. Pre-allocate a generous initial
capacity based on element index size (we already have that count).

### Phase C — Direct pointer tree walk in VM

The VM walks via `taurus_element_get_first_child` which goes
through the compact-pointer decoder. The VM has access to the
document's pool base, so it can use direct pointer arithmetic.
Skip the decoder.

### Phase D — Specialize the common axes

`descendant`, `descendant-or-self`, and `attribute` account for
>90% of axis calls in real XPath. Specialize them with hand-written
tight loops that don't go through the generic AXIS_STEP opcode.

### Phase E — Memoize string comparisons

XPath predicates compare strings (`@cat='fiction'`). Cache the
comparison result on first encounter in a per-evaluation table.
Hits when the same predicate is evaluated against many nodes.

## Risk

- The bytecode VM is correct and tested; rewriting risks
  conformance failures. Run the W3C XPath test suite (438 cases)
  after each phase.
- Computed-goto is GCC/Clang only. MSVC's switch is fine but slower.

## Expected impact

| Phase | Cumulative speedup |
|-------|---------------------|
| A     | 1.2×                |
| B     | 1.4×                |
| C     | 1.8×                |
| D     | 2.5×                |
| E     | 3×                  |

Target: `count(//book)` 44 µs → ~15 µs. pugixml is 5.6 µs.
We'd close from 7.9× gap to 2.7× gap. Combined with element
compaction (155) which speeds up tree walks → ~10 µs target.

## Status

**Phase A0 DONE** — element `name_hash` field + fast child-name lookup.
Added 16-bit FNV-1a hash of local name to `struct taurus_element`
(fits in existing padding; struct stays 64 bytes). All element
creation paths (`taurus_element_create_with_view`,
`taurus_element_create_pooled`, parser bulk-alloc, copy/mutate)
populate the hash. `taurus_element_first_child(elem, name)` now
pre-filters via 2-byte hash compare before falling back to strcmp.

With LTO the gap tightens from 5-13× to 1.6-4.4× on the
`bench_xpath_pugixml` suite. Phases A-E below target the
remaining gap.

