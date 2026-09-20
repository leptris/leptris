# The node-layout fork — pugixml-class allocation for the DOM

Status: DESIGN (2026-09-20). Owner green-light received. This is
the durable fix for #1222 (2-5.9x vs pugixml large docs), #1179's
residual node-creation cost, and the parse-side constant factor of
#1238. Measured basis: scaling is LINEAR on serialbench fixtures
(352-467 MB/s across 1.5-13MB) — the gap is a per-node constant
cost, not complexity.

## Why the gap exists (measured)

Every element today costs: pool_alloc (block chain + freelist
recycler bookkeeping) + compact-edge encoding (cp16 + overflow
table on out-of-range pairs) + root-doc registration for detached
nodes + name materialization through the namebp/inline paths.
pugixml pays: one bump-pointer increment inside ONE document-sized
block, with node "pointers" as intra-block offsets and names
interned into a shared char arena. Constant-factor difference per
node, multiplied by millions of nodes.

## Target layout (pugixml-class, adapted)

- ONE primary allocation per document (grown geometrically,
  never per-node): node array with intra-block integer offsets for
  parent/first-child/next-sibling/first-attr edges. No cp16
  overflow table (offsets are block-relative and always
  representable), no root-doc map for parse-created nodes (block
  base implies the document).
- String arena beside the node array: element/attr names
  interned by (ptr,len) hash into the block; parse buffers stay
  immutable (#1125) and zero-copy views remain the primary
  representation — interning only replaces the per-node copies.
- MUTATION-CREATED nodes keep today's paths initially (mut blocks
  + root-doc map) behind the same public API; the internal
  get_document walk resolves by block base first. Migration of
  mutation paths is a later slice, gated by the same benches.
- ABI unchanged: every public handle is already opaque; the
  compact-pointer exports and element-size check (VALIDATION.md)
  get new expected values.

## Slices (one PR each, RED bench first)

1. **Document block allocator**: the growable block + offset-edge
   node struct, PARALLEL to the pool (parse path only, behind a
   flag). Gate: no test changes; element-size check documents both.
2. **Parser wiring**: flat/direct_parse + html_parse build into
   block storage. Gate: full ctest + html5lib corpus 1169 floor;
   RED bench: serialbench shapes to >=350 MB/s at 13MB (close the
   libxml2 gap, halve the pugixml gap).
3. **Name interning**: shared arena replaces per-node name copies.
   Gate: attr/name read benches (many-attrs, first-access) no
   regression; memory RSS down on name-heavy docs.
4. **Compact-edge retirement for parse trees**: drop overflow-table
   traffic on the block path; mutation keeps the old encode until
   slice 5.
5. **Mutation unification** (optional, last): programmatic
   create/append onto the block. Gate: append <= pugixml (the
   lane-18 P1 row).

## Non-goals / invariants

- No public API or ABI change (opaque handles).
- Digest (#869), diff, c14n correctness rides the same node
  accessors — all read through the inline edge helpers, which
  become offset arithmetic.
- The address-keyed TLS caches (root-doc map/memo) survive for
  MUTATION nodes only; their invalidation contracts (#1242/#1038)
  must be re-gated, not assumed.

Effort: medium-large. Every slice independently shippable.
