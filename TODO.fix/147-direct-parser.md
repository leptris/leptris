# TODO 147 — Direct parser: single-pass parse into TaurusElement tree

## Why

The flat_parse → FlatDoc → promote pipeline has THREE passes over
the input:
1. flat_parse: scan XML, build FlatDoc (28B records)
2. flat_promote: walk FlatDoc, build TaurusElement tree (96B records)
3. freeze: walk tree, mark frozen

pugixml does ONE pass: scan XML, write directly into the tree.

## Design

Replace the three-pass pipeline with a single-pass `direct_parse`:

```
XML input
    ↓ (single pass)
  direct_parse
    ↓
  TaurusElement tree (bulk-allocated, zero-copy names, direct edges)
```

### Key techniques (from pugixml architecture study)

1. **Upfront bulk allocation**: estimate node count from input size
   (len / 80), allocate the entire element block in one malloc. If
   estimate is wrong, grow via realloc.

2. **Zero-copy names via in-place NUL termination**: copy XML input,
   write NUL at each name/value boundary. Element names point
   directly into the buffer copy. No pool_strdup, no interning.

3. **Direct edge offsets**: since all elements are in one contiguous
   block, edge offsets are simple pointer subtraction
   `(char*)child - (char*)parent`. No compact-pointer encode
   function call, no overflow table check.

4. **Goto-based dispatch**: the main parse loop uses a switch on
   the character after `<` to dispatch to element/comment/CDATA/PI/
   DOCTYPE handlers. Each handler is inline (no function call).

## Expected performance

For 5 KB plain XML (~50 elements):
- Current (flat_parse + promote): 60 µs
- Direct parse target: ~20 µs
- pugixml: ~5 µs

The remaining gap vs pugixml comes from:
- Their parser is C++ with PCH (more aggressive inlining)
- Their name scanning uses lookup tables (branchless)
- Their state machine uses computed goto

These are Phase 2+ optimizations on top of the direct parser.

## Implementation plan

### Phase A — Core direct parser (this PR)

New file `src/taurus/flat/direct_parse.c`:
- `direct_parse(xml, len) → TaurusDocument`
- Single pass producing a complete tree
- Reuses flat_parser's tokenizer logic but writes TaurusElement
  records instead of FlatNode records

### Phase B — Lookup table name scanning

Replace per-byte branch checks with a 256-byte lookup table.
Eliminates 6 comparisons per name byte → 1 lookup.

### Phase C — Computed goto dispatch

Replace the switch-based `<` dispatch with computed goto (GCC
labels-as-values extension). Eliminates branch misprediction.

Phases B and C are incremental on top of Phase A.
