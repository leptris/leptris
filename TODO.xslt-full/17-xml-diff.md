# 17 — Native XML diff (canon consumer)

Opened 2026-09-09 (user directive: "implement xml diff, see
lutaml/canon"). Canon is the semantic-comparison gem that already
rides libleptris (via moxml) for XML parse + C14N with
Nokogiri-fallback parity; today its diff layer runs in Ruby over
fully-materialized trees. A native engine-side diff gives canon
the same moxml-style speedup C14N got (23x binding-armed).

## Engine design

- **Prefilter**: the #869 Merkle digest already hashes subtrees
  (element name + sorted attrs + child content digests) — equal
  digests prune whole subtrees in O(1); only diverging regions
  descend.
- **Diff kernel**: Zhang-Shasha-style ordered tree edit distance
  over the pruned regions (children lists), producing an edit
  script: insert-node / delete-node / update-text / update-attr /
  (v2: move). Bounded matrix; docs are small after pruning.
- **Result model**: ordered op list, each op carrying a path
  (root-relative index path, stable across edits), the op kind,
  and before/after payloads (text or attr name+values).
- **Public surface**: `leptris_diff(docA, docB, options, status)`
  -> opaque `LeptrisDiff` (op list accessor +
  `leptris_diff_serialize` unified-text form for the CLI);
  options: { include-whitespace-text = 0 }. ABI: new opaque type,
  no existing entries touched (lane-15 gate applies).

## Phases

1. Digest-pruned equivalence fast path + op model (insert/delete/
   update on text/attrs) — unit specs, RED first; diff(a,a) is
   empty; single-node edits each kind.
2. Ordered children alignment (edit distance on child lists,
   recursion into matched pairs) — reorder + deep-change specs.
3. Serializer (unified line form) + CLI `leptris diff a.xml b.xml`.
4. Bindings (leptris-ruby Diff class; PRs only) + canon
   integration PR (native fast path, Ruby fallback kept, parity
   spec-gated like its C14N path).

## Bars

- diff(a,a) O(n) via digest (no descent).
- Realistic edit fixtures >= 2x canon's Ruby layer (engine side
  measured on the C API; binding via moxml-style FFI).
- No false ops: diff output applied to A must yield B (round-trip
  spec).

## Out of scope (v1)

Moves/renames across parents; JSON/YAML (canon handles those
outside libleptris); three-way merge.
