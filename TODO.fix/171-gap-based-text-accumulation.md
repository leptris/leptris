# TODO 171 — Gap-based text accumulation

## Status

**Deferred.** Investigated; for leptris's typical workload (text +
sparse CDATA), the marginal win doesn't justify the implementation
complexity. pugixml's gap buffer is high-leverage for *mixed*
text/CDATA/entity streams (e.g. SOAP with embedded payloads) but
leptris's docs are mostly one or the other.

## Why

pugixml uses an in-place gap buffer to merge adjacent text segments
(plain text + CDATA + entity-expanded text) into a single text node
without per-segment pool_alloc. The savings:
- Fewer pool_allocs (1 vs N for N adjacent text segments)
- Less tree-walk overhead for consumers that concatenate text content

## Why deferred for leptris

1. **Most text in real-world XML is plain.** CDATA and entity-
   expanded segments are rare outside SOAP/XHTML. Per-node pool_alloc
   is already cheap (bump pointer).
2. **Borrowed text is the fast path.** `leptris_text_create_borrowed`
   stores a pointer into the parse buffer — zero-copy. pugixml's gap
   buffer requires mutable storage (it writes the merged text into a
   side buffer). For borrowed text, we'd lose zero-copy.
3. **Implementation requires either:**
   - Tracking a per-element "current text accumulator" with delayed
     materialization on close-tag, OR
     - Merging adjacent text nodes post-parse via a tree walk.
   Both add complexity to the parse hot path.
4. **The actual hot path is per-attribute, not per-text.** TODO 165
   benchmark shows leptris at ~30 ns/attr vs pugixml at ~8 ns/attr.
   Text-node alloc is ~5 ns/op — small by comparison.

## Plan (when revisited)

If a future profile shows text-node alloc as a hot spot:

1. Add a "merge with previous sibling if it's a text node" branch
   in `dp_wire_child` for `LeptrisTextNode`.
2. Provide `leptris_text_append(LeptrisTextNode*, const char*, size_t)`
   that pool-alloc's a new buffer if the existing one doesn't fit.
3. Switch from borrowed to owned when an append happens.

## Risk

- Borrowed → owned transition is a memory-ownership change that
  ripples through `leptris_text_free` and `leptris_text_get_content`.
- Edge cases: text + comment + text should NOT merge.

## Expected impact

<2% on bench_dom_leptris (text-heavy docs would see more). Not the
next leverage point.

## Status

Deferred. Revisit if profiling shows text-node alloc as a hot spot
on real production workloads.
