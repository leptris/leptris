# The interleaved-parse rewrite (the pugixml-class lane)

Status: PLAN (2026-09-20, owner directive). Supersedes the fork's
remaining slices for the XML parse path. Target: close the #1222
large-document gap (pugixml 2-5.9x ahead on CI) at the SOURCE.

## Why the current lane can't get there

The no-LTO dissection (2026-09-20) shows direct_parse is at its
representation optimum: fused 48B probe + SIMD value scan, packed
attr tail, O(1) wiring, offset edges, root-only registration. The
remaining per-item cost is the representation itself: per-attr
40B struct stores inline in the parse loop, the scratch
NUL-terminate/restore dance, per-element ns/line bookkeeping, and
the O(K) dup-detection walk.

## The pugixml model (what we're adopting)

One immutable input (already ours, #1125) + ONE node region + ONE
stream region. The parse loop ONLY appends to streams; nothing is
finalized inline:

1. **SOA attr streams** — per element, attrs land in parallel
   arrays: name_off[4B] / value_off[4B] / len8[len pairs] into the
   string stream. ~12-16B per attr, sequential stores, no structs
   built during parse.
2. **Length-based views everywhere** — names and values are
   (offset, len) into the pristine buffer. NO NUL-termination, NO
   scratch mutation, NO restore pass (the current lane's biggest
   hidden cost).
3. **Element records as pure data** — a 32-48B record per element
   (name view, first_attr index, parent/first/next indices, line)
   bump-carved from the node region (doc_block, slice 1 landed).
4. **Deferred passes** — dup detection, ns stamping, hash
   sentinel: all become ONE linear post-pass sweep over the
   streams (cache-sequential), building the public
   leptris_attribute structs. The public API, DOM semantics, and
   every consumer see IDENTICAL trees.

## Slices (one PR each, CI-benchmark-gated)

0. **A/B harness**: bench_parse_ab in benchmarks/ — the attr/text
   shapes at 1.5/5/13MB, leptris vs pugixml vs libxml2, run by the
   CI Benchmark legs on QUIET runners (the local Mac under load
   343 produced 46-85 MB/s phantom numbers — measurements gate on
   CI only). PR with this plan.
1. **The dp_il lane skeleton** (flag off): streams + records +
   post-pass building the public tree for well-formed, DTD-free
   input. Gate: parity specs byte-identical to the current lane;
   CI A/B numbers recorded.
2. **Tokenizer port**: the SIMD scan helpers onto the stream
   appends. Gate: attr shape ≤1.5x pugixml, text ≤1.25x on CI.
3. **Feature coverage**: DTD/entity/namespace/CDATA/PI routes
   through the lane (fall back to the classic lane per-feature,
   never wrong). Gate: full ctest + html5lib-style parity for the
   XML suite (W3C + libxml2 corpus green).
4. **Default flip**: LEPTRIS_PARSE_INTERLEAVED=1 default, classic
   lane behind an env escape hatch. Gate: all 24 CI legs + a
   release cut with the CI A/B table in the notes.

## Invariants (non-negotiable)

- Public API/ABI unchanged; every existing spec passes unmodified.
- The classic lane stays until slice 4 ships AND one full release
  runs clean.
- Zero-copy views remain primary; the pristine buffer is never
  mutated by the interleaved lane.
